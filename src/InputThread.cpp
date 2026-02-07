#include "../include/InputThread.h"
#include <avrt.h>
#include <mmsystem.h>
#include <strsafe.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")

InputThread::InputThread() {
    h_instance_ = GetModuleHandleW(nullptr);
}

InputThread::~InputThread() {
    Stop();
}

InputThread::Config InputThread::GetConfig() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

bool InputThread::Start(const Config& config) {
    desired_running_ = true;
    if (running_) return false;

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
    }

    should_exit_ = false;
    running_ = true;

    thread_ = std::thread(&InputThread::ThreadProc, this);
    thread_handle_ = reinterpret_cast<HANDLE>(thread_.native_handle());
    return true;
}

void InputThread::Stop() {
    desired_running_ = false;
    if (!running_) return;

    should_exit_ = true;

    if (thread_id_ != 0) {
        PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    }

    if (thread_.joinable()) thread_.join();

    thread_handle_ = nullptr;
    thread_id_ = 0;
    running_ = false;

    RestoreSystemSettings();
}

void InputThread::UpdateConfig(const Config& newConfig) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = newConfig;
    }

    if (running_) {
        ApplyAffinity();
        ApplyThreadPriority();
        ApplyProcessPriority();
        ApplyTimerResolution();
    }
}

std::wstring InputThread::GetStatus() const {
    Config cfg = GetConfig();

    wchar_t buf[640]{};
    StringCchPrintfW(buf, _countof(buf),
        L"Input Thread: %s\r\n"
        L"Timer Boost: %s (%u ms)\r\n"
        L"Process Priority: %s\r\n"
        L"Thread Priority: %s\r\n"
        L"Affinity: %s\r\n"
        L"Samples: %zu",
        running_ ? L"Running" : L"Stopped",
        cfg.enableTimerBoost ? L"Enabled" : L"Disabled",
        cfg.enableTimerBoost ? cfg.timerResolutionMs : 0,
        cfg.enableProcessPriority ? L"High" : L"Normal",
        cfg.enableThreadPriority ? L"High" : L"Normal",
        (cfg.enableAffinity && cfg.affinityMask) ? L"Pinned" : L"Default",
        measurer_.GetSampleCount());

    return std::wstring(buf);
}

LRESULT CALLBACK InputThread::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InputThread::CreateHiddenWindow() {
    const wchar_t* cls = L"ILO_InputMsgWnd";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = h_instance_;
    wc.lpszClassName = cls;

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, cls, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, h_instance_, nullptr);

    return hwnd_ != nullptr;
}

void InputThread::DestroyHiddenWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool InputThread::InitializeRawInput(HWND hwnd) {
    RAWINPUTDEVICE rid[2]{};

    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06; // keyboard
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = hwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02; // mouse
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = hwnd;

    return RegisterRawInputDevices(rid, 2, sizeof(rid[0])) != FALSE;
}

void InputThread::ThreadProc() {
    thread_id_ = GetCurrentThreadId();

    MSG dummy{};
    PeekMessageW(&dummy, nullptr, 0, 0, PM_NOREMOVE);

    Config cfg = GetConfig();

    // MMCSS only when user intent is Medium/Max (enableThreadPriority)
    if (cfg.enableThreadPriority) {
        hMmcss_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_task_index_);
    }

    ApplyAffinity();
    ApplyThreadPriority();
    ApplyProcessPriority();
    ApplyTimerResolution();

    if (!CreateHiddenWindow()) {
        running_ = false;
        Cleanup();
        return;
    }

    if (!InitializeRawInput(hwnd_)) {
        running_ = false;
        DestroyHiddenWindow();
        Cleanup();
        return;
    }

    static thread_local std::vector<BYTE> buf;

    MSG msg{};
    while (!should_exit_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_INPUT) {
            measurer_.StartMeasurement();

            UINT size = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT,
                nullptr, &size, sizeof(RAWINPUTHEADER));

            if (size > 0) {
                if (buf.size() < size) buf.resize(size);
                UINT got = size;
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT,
                    buf.data(), &got, sizeof(RAWINPUTHEADER)) > 0) {
                    // Timing only.
                }
            }

            measurer_.EndMeasurement();
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyHiddenWindow();
    Cleanup();
}

void InputThread::Cleanup() {
    if (hMmcss_) {
        AvRevertMmThreadCharacteristics(hMmcss_);
        hMmcss_ = nullptr;
    }

    RestoreSystemSettings();
}

void InputThread::ApplyAffinity() {
    if (!thread_handle_) return;

    Config cfg = GetConfig();

    if (cfg.enableAffinity && cfg.affinityMask) {
        DWORD_PTR prev = SetThreadAffinityMask(thread_handle_, cfg.affinityMask);
        if (prev != 0) {
            if (original_affinity_mask_ == 0) original_affinity_mask_ = prev;
            applied_affinity_mask_ = cfg.affinityMask;
        }
    } else {
        if (original_affinity_mask_ != 0 && applied_affinity_mask_ != 0) {
            SetThreadAffinityMask(thread_handle_, original_affinity_mask_);
            applied_affinity_mask_ = 0;
        }
    }
}

void InputThread::ApplyThreadPriority() {
    if (!thread_handle_) return;

    Config cfg = GetConfig();

    if (cfg.enableThreadPriority) {
        SetThreadPriorityBoost(thread_handle_, TRUE); // disable dynamic boosting for determinism
        SetThreadPriority(thread_handle_, cfg.threadPriority);
    } else {
        SetThreadPriorityBoost(thread_handle_, FALSE);
        SetThreadPriority(thread_handle_, THREAD_PRIORITY_NORMAL);
    }
}

void InputThread::ApplyProcessPriority() {
    Config cfg = GetConfig();

    HANDLE hProc = GetCurrentProcess();
    if (cfg.enableProcessPriority) {
        original_process_priority_ = GetPriorityClass(hProc);
        SetPriorityClass(hProc, cfg.processPriority);
    } else {
        SetPriorityClass(hProc, NORMAL_PRIORITY_CLASS);
    }
}

void InputThread::ApplyTimerResolution() {
    Config cfg = GetConfig();

    if (cfg.enableTimerBoost) {
        timeBeginPeriod(cfg.timerResolutionMs);
        applied_timer_resolution_ms_ = cfg.timerResolutionMs;
    } else {
        if (applied_timer_resolution_ms_ != 0) {
            timeEndPeriod(applied_timer_resolution_ms_);
            applied_timer_resolution_ms_ = 0;
        }
    }
}

void InputThread::RestoreSystemSettings() {
    HANDLE hProc = GetCurrentProcess();
    SetPriorityClass(hProc, original_process_priority_);

    if (applied_timer_resolution_ms_ != 0) {
        timeEndPeriod(applied_timer_resolution_ms_);
        applied_timer_resolution_ms_ = 0;
    }

    if (original_affinity_mask_ != 0 && applied_affinity_mask_ != 0 && thread_handle_) {
        SetThreadAffinityMask(thread_handle_, original_affinity_mask_);
        applied_affinity_mask_ = 0;
    }

    original_affinity_mask_ = 0;
}

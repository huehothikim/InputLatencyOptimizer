#include "../include/SettingsDialog.h"
#include "../include/Resource.h"
#include "../include/DeviceTuner.h"
#include "../include/ConfigStore.h"
#include <commctrl.h>
#include <strsafe.h>

static const wchar_t* ModeToText(SettingsDialog::Mode m) {
    switch (m) {
    case SettingsDialog::Mode::Recommend: return L"Recommend";
    case SettingsDialog::Mode::Light:  return L"Nhẹ";
    case SettingsDialog::Mode::Medium: return L"Trung Bình";
    case SettingsDialog::Mode::Max:    return L"Tối đa";
    default: return L"Recommend";
    }
}

static int ModeToPos(SettingsDialog::Mode m) {
    switch (m) {
    case SettingsDialog::Mode::Recommend: return 0;
    case SettingsDialog::Mode::Light: return 1;
    case SettingsDialog::Mode::Medium: return 2;
    case SettingsDialog::Mode::Max: return 3;
    default: return 0;
    }
}

static SettingsDialog::Mode PosToMode(int pos) {
    if (pos <= 0) return SettingsDialog::Mode::Recommend;
    if (pos == 1) return SettingsDialog::Mode::Light;
    if (pos == 2) return SettingsDialog::Mode::Medium;
    return SettingsDialog::Mode::Max;
}

SettingsDialog::SettingsDialog(HINSTANCE hInstance, InputThread& inputThread, AutoStartManager& autoStartManager)
    : h_instance_(hInstance),
      input_thread_(inputThread),
      auto_start_manager_(autoStartManager) {

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    app_path_ = path;

    size_t pos = app_path_.find_last_of(L"\\/");
    if (pos != std::wstring::npos) app_name_ = app_path_.substr(pos + 1);
    else app_name_ = L"InputLatencyOptimizer.exe";
}

SettingsDialog::~SettingsDialog() {
    if (h_dialog_) DestroyWindow(h_dialog_);
}

bool SettingsDialog::Create() {
    h_dialog_ = CreateDialogParamW(h_instance_,
        MAKEINTRESOURCE(IDD_SETTINGS_DIALOG),
        nullptr,
        DialogProc,
        reinterpret_cast<LPARAM>(this));

    if (!h_dialog_) return false;

    LoadSettings();
    UpdateModeUi();
    return true;
}

void SettingsDialog::Show(bool show) {
    if (!h_dialog_) return;
    is_visible_ = show;
    ShowWindow(h_dialog_, show ? SW_SHOW : SW_HIDE);
    if (show) SetForegroundWindow(h_dialog_);
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
        SettingsDialog* pThis = reinterpret_cast<SettingsDialog*>(lParam);
        pThis->h_dialog_ = hwnd;
        return TRUE;
    }

    auto* pThis = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (pThis) return pThis->HandleMessage(msg, wParam, lParam);

    return FALSE;
}

void SettingsDialog::UpdateStatus(const std::wstring& status) {
    // Minimal UI: use window title as status
    SetWindowTextW(h_dialog_, status.c_str());
}

void SettingsDialog::UpdateModeUi() {
    if (!h_dialog_) return;

    HWND hSlider = GetDlgItem(h_dialog_, IDC_MODE_SLIDER);
    if (hSlider) {
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 3));
        SendMessageW(hSlider, TBM_SETTICFREQ, 1, 0);
        SendMessageW(hSlider, TBM_SETPOS, TRUE, ModeToPos(selected_mode_));
    }

    SetDlgItemTextW(h_dialog_, IDC_MODE_LABEL, ModeToText(selected_mode_));
}

InputThread::Config SettingsDialog::ConfigForMode(Mode mode) {
    if (mode == Mode::Recommend) {
        const auto p = DeviceTuner::CollectProfile();
        const ULONGLONG ramGB = p.ramBytes / (1024ull * 1024ull * 1024ull);
        const bool strongCPU = (p.logicalProcessors >= 8) || (p.cpuMHz >= 3200);

        Mode resolved = Mode::Light;
        if (!p.onBattery) {
            if (strongCPU && ramGB >= 16) resolved = Mode::Max;
            else if (p.logicalProcessors >= 4 && ramGB >= 8) resolved = Mode::Medium;
            else resolved = Mode::Light;
        } else {
            resolved = Mode::Light;
        }

        return DeviceTuner::ComputeConfigCached(resolved);
    }

    return DeviceTuner::ComputeConfigCached(mode);
}

SettingsDialog::Mode SettingsDialog::RecommendModeForDevice() const {
    const auto p = DeviceTuner::CollectProfile();
    const ULONGLONG ramGB = p.ramBytes / (1024ull * 1024ull * 1024ull);
    const bool strongCPU = (p.logicalProcessors >= 8) || (p.cpuMHz >= 3200);

    if (p.onBattery) return Mode::Light;
    if (strongCPU && ramGB >= 16) return Mode::Max;
    if (p.logicalProcessors >= 4 && ramGB >= 8) return Mode::Medium;
    return Mode::Light;
}

void SettingsDialog::ApplyMode(Mode mode) {
    InputThread::Config cfg = ConfigForMode(mode);

    if (!input_thread_.IsRunning()) input_thread_.Start(cfg);
    else input_thread_.UpdateConfig(cfg);

    applied_mode_ = mode;
    input_thread_.GetMeasurer().Reset();

    ConfigStore::SaveApplied(static_cast<DWORD>(mode), cfg, true);

    const wchar_t* modeText = ModeToText(mode);
    wchar_t shown[96]{};
    if (mode == Mode::Recommend) {
        Mode resolved = RecommendModeForDevice();
        StringCchPrintfW(shown, _countof(shown), L"%s -> %s", modeText, ModeToText(resolved));
        modeText = shown;
    }

    wchar_t s[256]{};
    StringCchPrintfW(s, _countof(s),
        L"Applied: %s | Boost:%s | Aff:%s | Proc:%s | Thr:%s",
        modeText,
        cfg.enableTimerBoost ? L"ON" : L"OFF",
        (cfg.enableAffinity && cfg.affinityMask) ? L"ON" : L"OFF",
        cfg.enableProcessPriority ? L"ON" : L"OFF",
        cfg.enableThreadPriority ? L"ON" : L"OFF");

    UpdateStatus(s);
}

void SettingsDialog::LoadSettings() {
    StoredConfig s{};
    bool ok = ConfigStore::Load(s);

    if (ok) selected_mode_ = static_cast<Mode>(s.mode);
    else selected_mode_ = Mode::Recommend;

    if (ok && s.hasAppliedConfig) applied_mode_ = static_cast<Mode>(s.appliedMode);
    else applied_mode_ = selected_mode_;

    if (ok && s.enabled) {
        InputThread::Config cfg{};
        if (s.hasAppliedConfig) cfg = s.appliedConfig;
        else cfg = ConfigForMode(applied_mode_);

        if (!input_thread_.IsRunning()) input_thread_.Start(cfg);
        else input_thread_.UpdateConfig(cfg);

        input_thread_.GetMeasurer().Reset();
        UpdateStatus(L"Running (loaded).");
    } else {
        UpdateStatus(L"Idle.");
    }
}

void SettingsDialog::SaveSettings() {
    ConfigStore::SaveEnabled(true);
}

INT_PTR SettingsDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        UpdateModeUi();
        return TRUE;

    case WM_HSCROLL: {
        HWND hSlider = GetDlgItem(h_dialog_, IDC_MODE_SLIDER);
        if ((HWND)lParam == hSlider) {
            int pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            selected_mode_ = PosToMode(pos);
            SetDlgItemTextW(h_dialog_, IDC_MODE_LABEL, ModeToText(selected_mode_));
            ConfigStore::SaveSelectedMode(static_cast<DWORD>(selected_mode_));
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RESET_BUTTON:
            if (HIWORD(wParam) == BN_CLICKED) {
                StoredConfig s{};
                if (ConfigStore::Load(s) && s.hasAppliedConfig) {
                    selected_mode_ = static_cast<Mode>(s.appliedMode);
                    applied_mode_ = static_cast<Mode>(s.appliedMode);
                } else {
                    selected_mode_ = Mode::Recommend;
                }
                UpdateModeUi();
                UpdateStatus(L"Reset selection.");
            }
            break;

        case IDC_APPLY_BUTTON:
            if (HIWORD(wParam) == BN_CLICKED) {
                ApplyMode(selected_mode_);
                SaveSettings();
            }
            break;

        case IDOK:
        case IDCANCEL:
            Show(false);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        Show(false);
        return TRUE;
    }

    return FALSE;
}

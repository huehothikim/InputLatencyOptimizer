#include "../include/Resource.h"
#include "../include/InputThread.h"
#include "../include/AutoStartManager.h"
#include "../include/SettingsDialog.h"
#include "../include/TrayIcon.h"
#include "../include/ConfigStore.h"
#include "../include/DeviceTuner.h"

#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

static InputThread g_inputThread;
static AutoStartManager g_autoStartManager;
static SettingsDialog* g_settingsDialog = nullptr;
static TrayIcon* g_trayIcon = nullptr;

static std::atomic<bool> g_running{true};
static std::mutex g_mutex;
static std::condition_variable g_cv;

static HWND g_hwndMain = nullptr;

static bool InitializeApplication(HINSTANCE hInstance);
static void CleanupApplication();
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void EnsureSettingsDialog(HINSTANCE hInstance) {
    if (g_settingsDialog) return;
    g_settingsDialog = new SettingsDialog(hInstance, g_inputThread, g_autoStartManager);
    (void)g_settingsDialog->Create();
}

static void StartIfEnabledFromStore() {
    StoredConfig s{};
    if (!ConfigStore::Load(s) || !s.enabled) return;

    InputThread::Config cfg{};
    if (s.hasAppliedConfig) cfg = s.appliedConfig;
    else {
        auto m = static_cast<SettingsDialog::Mode>(s.appliedMode);
        cfg = SettingsDialog::ConfigForMode(m);
    }

    // Start exactly as user last applied (no automatic downshift).
    if (!g_inputThread.IsRunning()) g_inputThread.Start(cfg);
    else g_inputThread.UpdateConfig(cfg);
}

static void WatchdogThread() {
    while (g_running) {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cv.wait_for(lock, std::chrono::seconds(5));
        if (!g_running) break;

        if (g_inputThread.ShouldBeRunning() && !g_inputThread.IsRunning()) {
            g_inputThread.Start(g_inputThread.GetConfig());
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"InputLatencyOptimizer_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    if (!InitializeApplication(hInstance)) return 1;

    // Start optimizer without showing UI if previously enabled
    StartIfEnabledFromStore();

    std::thread watchdog(WatchdogThread);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running = false;
    g_cv.notify_all();
    if (watchdog.joinable()) watchdog.join();

    CleanupApplication();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}

static bool InitializeApplication(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"InputLatencyOptimizerMain";
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) return false;

    g_hwndMain = CreateWindowExW(0, wc.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hwndMain) return false;

    g_trayIcon = new TrayIcon(hInstance, g_hwndMain);
    if (!g_trayIcon->Create()) return false;

    return true;
}

static void CleanupApplication() {
    g_inputThread.Stop();

    if (g_trayIcon) {
        g_trayIcon->Destroy();
        delete g_trayIcon;
        g_trayIcon = nullptr;
    }

    if (g_settingsDialog) {
        delete g_settingsDialog;
        g_settingsDialog = nullptr;
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_OPEN_SETTINGS:
            EnsureSettingsDialog((HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            if (g_settingsDialog) g_settingsDialog->Show(true);
            break;

        case IDM_VIEW_STATUS:
            EnsureSettingsDialog((HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            if (g_settingsDialog) {
                g_settingsDialog->Show(true);
                g_settingsDialog->UpdateStatus(g_inputThread.GetStatus());
            }
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case TrayIcon::WM_TRAYICON:
        if (g_trayIcon) {
            if (lParam == WM_RBUTTONUP) g_trayIcon->ShowContextMenu();
            else if (lParam == WM_LBUTTONDBLCLK) {
                EnsureSettingsDialog((HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
                if (g_settingsDialog) g_settingsDialog->Show(true);
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
}

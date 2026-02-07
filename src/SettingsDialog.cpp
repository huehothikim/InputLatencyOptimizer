#include "../include/SettingsDialog.h"
#include "../include/Resource.h"
#include "../include/DeviceTuner.h"
#include "../include/ConfigStore.h"
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
    UpdateModeButtonText();
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

INT_PTR SettingsDialog::HandleMessage(UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MODE_BUTTON:
            if (HIWORD(wParam) == BN_CLICKED) ShowModeMenu();
            break;

        case IDC_RESET_BUTTON:
            if (HIWORD(wParam) == BN_CLICKED) {
                // Reset = revert UI selection back to last applied (backup), NOT uninstall.
                StoredConfig s{};
                if (ConfigStore::Load(s) && s.hasAppliedConfig) {
                    selected_mode_ = static_cast<Mode>(s.appliedMode);
                    applied_mode_ = static_cast<Mode>(s.appliedMode);
                    UpdateModeButtonText();
                    UpdateStatus(L"Reverted to last applied.");
                } else {
                    // No backup yet => go to Recommend
                    selected_mode_ = Mode::Recommend;
                    UpdateModeButtonText();
                    UpdateStatus(L"Reverted to recommended.");
                }
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

void SettingsDialog::UpdateStatus(const std::wstring& status) {
    SetWindowTextW(h_dialog_, status.c_str());
}

void SettingsDialog::UpdateModeButtonText() {
    wchar_t text[128]{};
    StringCchPrintfW(text, _countof(text), L"Chế độ: %s", ModeToText(selected_mode_));
    SetDlgItemTextW(h_dialog_, IDC_MODE_BUTTON, text);
}

void SettingsDialog::ShowModeMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_MODE_RECOMMEND, L"Recommend");
    AppendMenuW(hMenu, MF_STRING, IDM_MODE_LIGHT,  L"Nhẹ");
    AppendMenuW(hMenu, MF_STRING, IDM_MODE_MEDIUM, L"Trung Bình");
    AppendMenuW(hMenu, MF_STRING, IDM_MODE_MAX,    L"Tối đa");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(h_dialog_);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h_dialog_, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_MODE_RECOMMEND: selected_mode_ = Mode::Recommend; break;
    case IDM_MODE_LIGHT:  selected_mode_ = Mode::Light; break;
    case IDM_MODE_MEDIUM: selected_mode_ = Mode::Medium; break;
    case IDM_MODE_MAX:    selected_mode_ = Mode::Max; break;
    default: return;
    }

    // Save selection immediately (not applied yet)
    ConfigStore::SaveSelectedMode(static_cast<DWORD>(selected_mode_));
    UpdateModeButtonText();
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
    const auto& p = DeviceTuner::CollectProfile();
    const ULONGLONG ramGB = p.ramBytes / (1024ull * 1024ull * 1024ull);
    const bool strongCPU = (p.logicalProcessors >= 8) || (p.cpuMHz >= 3200);

    if (p.onBattery) return Mode::Light;
    if (strongCPU && ramGB >= 16) return Mode::Max;
    if (p.logicalProcessors >= 4 && ramGB >= 8) return Mode::Medium;
    return Mode::Light;
}

void SettingsDialog::ApplyMode(Mode mode) {
    // Performance-first tuned config (no auto-disable after user Apply).
    InputThread::Config cfg = ConfigForMode(mode);

    if (!input_thread_.IsRunning()) {
        input_thread_.Start(cfg);
    } else {
        input_thread_.UpdateConfig(cfg);
    }

    applied_mode_ = mode;
    input_thread_.GetMeasurer().Reset();

    // Persist backup of applied tuning
    ConfigStore::SaveApplied(static_cast<DWORD>(mode), cfg, true);

    const wchar_t* modeText = ModeToText(mode);
    wchar_t shown[64]{};
    if (mode == Mode::Recommend) {
        // Show resolved tier for clarity
        const auto p = DeviceTuner::CollectProfile();
        const ULONGLONG ramGB = p.ramBytes / (1024ull * 1024ull * 1024ull);
        const bool strongCPU = (p.logicalProcessors >= 8) || (p.cpuMHz >= 3200);
        const wchar_t* resolved = L"Nhẹ";
        if (!p.onBattery) {
            if (strongCPU && ramGB >= 16) resolved = L"Tối đa";
            else if (p.logicalProcessors >= 4 && ramGB >= 8) resolved = L"Trung Bình";
            else resolved = L"Nhẹ";
        }
        StringCchPrintfW(shown, _countof(shown), L"%s -> %s", modeText, resolved);
        modeText = shown;
    }

    wchar_t s[320]{};
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

    // Selection shown in UI: last selected if available else recommended
    if (ok) selected_mode_ = static_cast<Mode>(s.mode);
    else selected_mode_ = Mode::Recommend;

    // Applied mode: from backup if exists else selection
    if (ok && s.hasAppliedConfig) applied_mode_ = static_cast<Mode>(s.appliedMode);
    else applied_mode_ = selected_mode_;

    // Do NOT show dialog on startup.
    // If enabled, start optimizer with backed-up config (normalized).
    if (ok && s.enabled) {
        InputThread::Config cfg{};
        if (s.hasAppliedConfig) cfg = s.appliedConfig;
        else cfg = ConfigForMode(applied_mode_);

        // Start exactly as user last applied (no automatic downshift).
        if (!input_thread_.IsRunning()) input_thread_.Start(cfg);
        else input_thread_.UpdateConfig(cfg);

        input_thread_.GetMeasurer().Reset();
        UpdateStatus(L"Running (loaded).");
    } else {
        UpdateStatus(L"Idle (not applied).");
    }
}

void SettingsDialog::SaveSettings() {
    // Selection already saved on change; here we only ensure Enabled flag is 1 if applied.
    ConfigStore::SaveEnabled(true);
}

#pragma once
#include <windows.h>
#include <string>
#include "InputThread.h"
#include "AutoStartManager.h"

class SettingsDialog {
public:
    enum class Mode : DWORD { Light = 0, Medium = 1, Max = 2, Recommend = 3 };

    SettingsDialog(HINSTANCE hInstance, InputThread& inputThread, AutoStartManager& autoStartManager);
    ~SettingsDialog();

    bool Create();
    void Show(bool show = true);
    bool IsVisible() const { return is_visible_; }

    void UpdateStatus(const std::wstring& status);

    const std::wstring& AppName() const { return app_name_; }
    const std::wstring& AppPath() const { return app_path_; }

    static InputThread::Config ConfigForMode(Mode mode);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void LoadSettings();
    void SaveSettings();

    void ApplyMode(Mode mode);
    Mode RecommendModeForDevice() const;
    void UpdateModeUi();
HINSTANCE h_instance_;
    HWND h_dialog_ = nullptr;
    bool is_visible_ = false;

    InputThread& input_thread_;
    AutoStartManager& auto_start_manager_;

    std::wstring app_name_;
    std::wstring app_path_;

    Mode selected_mode_ = Mode::Light;
    Mode applied_mode_ = Mode::Light;
};

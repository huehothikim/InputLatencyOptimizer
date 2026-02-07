#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

class TrayIcon {
public:
    static constexpr UINT WM_TRAYICON = WM_APP + 1;

    TrayIcon(HINSTANCE hInstance, HWND hwnd);
    ~TrayIcon();

    bool Create();
    void Destroy();

    void UpdateIcon(bool is_active);
    void UpdateTooltip(const std::wstring& text);

    void ShowContextMenu();

private:
    bool AddIcon();
    bool ModifyIcon();
    bool DeleteIcon();

    HINSTANCE h_instance_;
    HWND hwnd_;
    NOTIFYICONDATAW nid_{};
    bool created_ = false;
};

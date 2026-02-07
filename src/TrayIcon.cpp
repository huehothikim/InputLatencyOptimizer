#include "../include/TrayIcon.h"
#include "../include/Resource.h"
#include <shellapi.h>

TrayIcon::TrayIcon(HINSTANCE hInstance, HWND hwnd)
    : h_instance_(hInstance), hwnd_(hwnd) {

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    // If app.ico isn't present at runtime, Windows still loads resource icon.
    nid_.hIcon = static_cast<HICON>(LoadImageW(h_instance_, MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

    if (!nid_.hIcon) nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    wcscpy_s(nid_.szTip, L"Input Latency Optimizer");
}

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create() {
    if (created_) return true;
    if (!AddIcon()) return false;
    created_ = true;
    return true;
}

void TrayIcon::Destroy() {
    if (!created_) return;
    DeleteIcon();
    created_ = false;
}

void TrayIcon::UpdateIcon(bool is_active) {
    if (!created_) return;

    // For now we use the same icon in both states.
    nid_.hIcon = static_cast<HICON>(LoadImageW(h_instance_, MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!nid_.hIcon) nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    (void)is_active;
    ModifyIcon();
}

void TrayIcon::UpdateTooltip(const std::wstring& text) {
    if (!created_) return;

    wcsncpy_s(nid_.szTip, text.c_str(), _countof(nid_.szTip) - 1);
    nid_.szTip[_countof(nid_.szTip) - 1] = L'\0';

    ModifyIcon();
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_SETTINGS, L"&Settings");
    AppendMenuW(hMenu, MF_STRING, IDM_VIEW_STATUS, L"&View Status");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"E&xit");

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd_);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

bool TrayIcon::AddIcon() {
    return Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

bool TrayIcon::ModifyIcon() {
    return Shell_NotifyIconW(NIM_MODIFY, &nid_) != FALSE;
}

bool TrayIcon::DeleteIcon() {
    return Shell_NotifyIconW(NIM_DELETE, &nid_) != FALSE;
}

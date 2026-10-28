#include "ui/TrayIcon.h"
#include <cstring>

#pragma comment(lib, "shell32.lib")

namespace clipback {

TrayIcon::~TrayIcon() { Remove(); }

bool TrayIcon::Add(HWND owner, HICON icon, const std::wstring& tooltip)
{
    Remove();
    memset(&data_, 0, sizeof(data_));
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &data_)) return false;
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
    added_ = true;
    return true;
}

void TrayIcon::Remove()
{
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

void TrayIcon::SetTooltip(const std::wstring& tooltip)
{
    if (!added_) return;
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::SetIcon(HICON icon)
{
    if (!added_) return;
    data_.uFlags = NIF_ICON;
    data_.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, DWORD infoFlags)
{
    if (!added_) return;
    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = infoFlags | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(data_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data_.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

} // namespace clipback

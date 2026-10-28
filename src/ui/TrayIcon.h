// System tray icon + balloon/toast notifications (Shell_NotifyIcon).
#pragma once
#include "common/Common.h"
#include <shellapi.h>
#include <string>

namespace clipback {

class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 1;

    TrayIcon() = default;
    ~TrayIcon();

    bool Add(HWND owner, HICON icon, const std::wstring& tooltip);
    void Remove();
    void SetTooltip(const std::wstring& tooltip);
    void SetIcon(HICON icon);
    // NIIF_INFO / NIIF_WARNING / NIIF_ERROR
    void Notify(const std::wstring& title, const std::wstring& text, DWORD infoFlags = NIIF_INFO);
    bool IsAdded() const { return added_; }

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

} // namespace clipback

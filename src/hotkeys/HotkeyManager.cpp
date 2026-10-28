#include "hotkeys/HotkeyManager.h"
#include "logging/Logger.h"
#include <commctrl.h>

namespace clipback {

HotkeyManager::~HotkeyManager() { Unregister(); }

bool HotkeyManager::Register(HWND window, const Hotkey& hotkey, std::wstring& error)
{
    Unregister();
    window_ = window;
    hotkey_ = hotkey;
    if (hotkey.vk == 0) {
        error = L"No key selected";
        return false;
    }
    // MOD_NOREPEAT: holding the key fires once (Windows 7+).
    if (!RegisterHotKey(window, kSaveReplayId, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk)) {
        const DWORD err = GetLastError();
        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            error = L"Hotkey " + hotkey.ToString() + L" is already used by another application";
        } else {
            error = L"RegisterHotKey failed: " + logging::HResultToString(HRESULT_FROM_WIN32(err));
        }
        return false;
    }
    registered_ = true;
    LOG_INFO(L"Hotkey registered: %s", hotkey.ToString().c_str());
    return true;
}

void HotkeyManager::Unregister()
{
    if (registered_ && window_) {
        UnregisterHotKey(window_, kSaveReplayId);
    }
    registered_ = false;
}

Hotkey HotkeyManager::FromControlValue(WORD value)
{
    Hotkey hk;
    hk.vk = LOBYTE(value);
    const BYTE flags = HIBYTE(value);
    hk.modifiers = 0;
    if (flags & HOTKEYF_ALT) hk.modifiers |= MOD_ALT;
    if (flags & HOTKEYF_CONTROL) hk.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_SHIFT) hk.modifiers |= MOD_SHIFT;
    return hk;
}

WORD HotkeyManager::ToControlValue(const Hotkey& hotkey)
{
    BYTE flags = 0;
    if (hotkey.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    if (hotkey.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (hotkey.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    return MAKEWORD(static_cast<BYTE>(hotkey.vk), flags);
}

} // namespace clipback

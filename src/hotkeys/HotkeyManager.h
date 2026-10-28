// Global hotkey via RegisterHotKey (no low-level keyboard hook: zero per-keystroke cost, works when a
// fullscreen game has focus, and looks nothing like input sniffing to anti-cheat).
#pragma once
#include "common/Common.h"
#include "settings/Settings.h"

namespace clipback {

class HotkeyManager {
public:
    static constexpr int kSaveReplayId = 1;

    HotkeyManager() = default;
    ~HotkeyManager();

    bool Register(HWND window, const Hotkey& hotkey, std::wstring& error);
    void Unregister();
    bool IsRegistered() const { return registered_; }
    Hotkey Current() const { return hotkey_; }

    // Converts a WC_HOTKEY control value (HKM_GETHOTKEY) to/from our representation.
    static Hotkey FromControlValue(WORD value);
    static WORD ToControlValue(const Hotkey& hotkey);

private:
    HWND window_ = nullptr;
    Hotkey hotkey_;
    bool registered_ = false;
};

} // namespace clipback

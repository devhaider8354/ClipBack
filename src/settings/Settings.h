// User configuration persisted to config.json.
#pragma once
#include "common/Common.h"
#include "ui/Strings.h"
#include <string>

namespace clipback {

enum class ResolutionMode { SameAsGame = 0, R1080p = 1, R720p = 2 };
enum class EncoderPreference { Auto = 0, Nvenc = 1, Amf = 2, Qsv = 3, Cpu = 4 };

struct Hotkey {
    UINT modifiers = 0;      // MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN
    UINT vk = VK_F10;        // virtual key

    std::wstring ToString() const;   // "Ctrl+F10"
    bool operator==(const Hotkey& o) const { return modifiers == o.modifiers && vk == o.vk; }
    bool operator!=(const Hotkey& o) const { return !(*this == o); }
};

struct Settings {
    int replayDurationSec = 60;                          // 30 / 60 / 120
    ResolutionMode resolution = ResolutionMode::SameAsGame;
    int fps = 60;                                        // 30 / 60
    int bitrateKbps = 12000;                             // 8000 / 12000 / 16000 / 20000 / custom
    EncoderPreference encoder = EncoderPreference::Auto;
    Hotkey hotkey;
    bool microphoneEnabled = false;
    std::wstring microphoneDeviceId;                     // empty = default capture device
    std::wstring outputDeviceId;                         // empty = default render device
    std::wstring saveDirectory;                          // empty = Videos\ClipBack
    bool startWithWindows = false;
    bool autoStartReplay = true;                         // begin buffering when the app launches
    bool startMinimized = true;                          // launch straight into the tray
    bool captureCursor = true;
    int monitorIndex = 0;                                // which display to capture (0 = primary)
    bool excludeWindowFromCapture = true;                // hide this window from the recording while replay runs
    Language language = DetectSystemLanguage();          // UI language (English / Arabic)

    std::wstring EffectiveSaveDirectory() const;

    static const wchar_t* ResolutionToString(ResolutionMode m);
    static ResolutionMode ResolutionFromString(const std::string& s);
    static const wchar_t* EncoderToString(EncoderPreference e);
    static EncoderPreference EncoderFromString(const std::string& s);
};

// Returns false if the file could not be read (defaults are used); parse errors are logged.
bool LoadSettings(const std::wstring& path, Settings& out);
bool SaveSettings(const std::wstring& path, const Settings& s, std::wstring& error);

} // namespace clipback

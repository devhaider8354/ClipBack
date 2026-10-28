#include "settings/Settings.h"
#include "settings/Json.h"
#include "system/Paths.h"
#include "common/StringUtils.h"
#include "logging/Logger.h"
#include <fstream>
#include <sstream>

namespace clipback {

// ---------------------------------------------------------------------------
// Hotkey
// ---------------------------------------------------------------------------

static std::wstring VkName(UINT vk)
{
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, static_cast<wchar_t>(vk));
    switch (vk) {
    case VK_SPACE: return L"Space";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_PAUSE: return L"Pause";
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_SCROLL: return L"ScrollLock";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Num" + std::to_wstring(vk - VK_NUMPAD0);
    // Fall back to the keyboard layout name.
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64] = {};
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0) return name;
    return L"Key" + std::to_wstring(vk);
}

std::wstring Hotkey::ToString() const
{
    std::wstring s;
    if (modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (modifiers & MOD_ALT) s += L"Alt+";
    if (modifiers & MOD_SHIFT) s += L"Shift+";
    if (modifiers & MOD_WIN) s += L"Win+";
    s += VkName(vk);
    return s;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

std::wstring Settings::EffectiveSaveDirectory() const
{
    if (saveDirectory.empty()) return paths::DefaultSaveDirectory();
    return paths::Expand(saveDirectory);
}

const wchar_t* Settings::ResolutionToString(ResolutionMode m)
{
    switch (m) {
    case ResolutionMode::R1080p: return L"1920x1080";
    case ResolutionMode::R720p: return L"1280x720";
    default: return L"Same as game";
    }
}

ResolutionMode Settings::ResolutionFromString(const std::string& s)
{
    if (s == "1080p" || s == "1920x1080") return ResolutionMode::R1080p;
    if (s == "720p" || s == "1280x720") return ResolutionMode::R720p;
    return ResolutionMode::SameAsGame;
}

const wchar_t* Settings::EncoderToString(EncoderPreference e)
{
    switch (e) {
    case EncoderPreference::Nvenc: return L"NVIDIA NVENC";
    case EncoderPreference::Amf: return L"AMD AMF";
    case EncoderPreference::Qsv: return L"Intel Quick Sync";
    case EncoderPreference::Cpu: return L"CPU (software)";
    default: return L"Auto";
    }
}

EncoderPreference Settings::EncoderFromString(const std::string& s)
{
    if (s == "nvenc") return EncoderPreference::Nvenc;
    if (s == "amf") return EncoderPreference::Amf;
    if (s == "qsv") return EncoderPreference::Qsv;
    if (s == "cpu") return EncoderPreference::Cpu;
    return EncoderPreference::Auto;
}

static std::string ResolutionKey(ResolutionMode m)
{
    switch (m) {
    case ResolutionMode::R1080p: return "1080p";
    case ResolutionMode::R720p: return "720p";
    default: return "same";
    }
}

static std::string EncoderKey(EncoderPreference e)
{
    switch (e) {
    case EncoderPreference::Nvenc: return "nvenc";
    case EncoderPreference::Amf: return "amf";
    case EncoderPreference::Qsv: return "qsv";
    case EncoderPreference::Cpu: return "cpu";
    default: return "auto";
    }
}

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool LoadSettings(const std::wstring& path, Settings& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();

    json::Value root;
    std::string err;
    if (!json::Parse(ss.str(), root, err) || !root.isObject()) {
        LOG_ERROR(L"config.json parse error: %s - using defaults", str::Utf8ToWide(err).c_str());
        return false;
    }

    auto getInt = [&](const char* key, int def) { const json::Value* v = root.find(key); return v ? v->asInt(def) : def; };
    auto getBool = [&](const char* key, bool def) { const json::Value* v = root.find(key); return v ? v->asBool(def) : def; };
    auto getStr = [&](const char* key, const std::wstring& def) { const json::Value* v = root.find(key); return v && v->isString() ? str::Utf8ToWide(v->asString()) : def; };

    out.replayDurationSec = ClampInt(getInt("replay_duration", 60), 5, 600);
    if (const json::Value* v = root.find("resolution"); v && v->isString()) out.resolution = Settings::ResolutionFromString(v->asString());
    out.fps = getInt("fps", 60) >= 45 ? 60 : 30;
    out.bitrateKbps = ClampInt(getInt("bitrate", 12000), 1000, 200000);
    if (const json::Value* v = root.find("encoder"); v && v->isString()) out.encoder = Settings::EncoderFromString(v->asString());
    if (const json::Value* hk = root.find("hotkey"); hk && hk->isObject()) {
        const json::Value* m = hk->find("modifiers");
        const json::Value* k = hk->find("vk");
        out.hotkey.modifiers = m ? static_cast<UINT>(m->asInt(0)) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN) : 0;
        out.hotkey.vk = k ? static_cast<UINT>(ClampInt(k->asInt(VK_F10), 1, 254)) : VK_F10;
    }
    out.microphoneEnabled = getBool("microphone_enabled", false);
    out.microphoneDeviceId = getStr("microphone_device", L"");
    out.outputDeviceId = getStr("output_device", L"");
    out.saveDirectory = getStr("save_directory", L"");
    out.startWithWindows = getBool("start_with_windows", false);
    out.autoStartReplay = getBool("auto_start_replay", true);
    out.startMinimized = getBool("start_minimized", true);
    out.captureCursor = getBool("capture_cursor", true);
    out.monitorIndex = ClampInt(getInt("monitor_index", 0), 0, 15);
    out.excludeWindowFromCapture = getBool("exclude_window_from_capture", true);
    if (const json::Value* v = root.find("language"); v && v->isString()) out.language = LanguageFromString(v->asString());
    return true;
}

bool SaveSettings(const std::wstring& path, const Settings& s, std::wstring& error)
{
    json::Value root = json::Value::Object();
    root.set("replay_duration", s.replayDurationSec);
    root.set("resolution", ResolutionKey(s.resolution));
    root.set("fps", s.fps);
    root.set("bitrate", s.bitrateKbps);
    root.set("encoder", EncoderKey(s.encoder));
    json::Value hk = json::Value::Object();
    hk.set("modifiers", static_cast<int>(s.hotkey.modifiers));
    hk.set("vk", static_cast<int>(s.hotkey.vk));
    hk.set("display", str::WideToUtf8(s.hotkey.ToString()));
    root.set("hotkey", hk);
    root.set("microphone_enabled", s.microphoneEnabled);
    root.set("microphone_device", str::WideToUtf8(s.microphoneDeviceId));
    root.set("output_device", str::WideToUtf8(s.outputDeviceId));
    root.set("save_directory", str::WideToUtf8(s.saveDirectory));
    root.set("start_with_windows", s.startWithWindows);
    root.set("auto_start_replay", s.autoStartReplay);
    root.set("start_minimized", s.startMinimized);
    root.set("capture_cursor", s.captureCursor);
    root.set("monitor_index", s.monitorIndex);
    root.set("exclude_window_from_capture", s.excludeWindowFromCapture);
    root.set("language", LanguageToString(s.language));

    const std::string text = root.serialize(2);

    // Write to a temp file and swap in, so a crash mid-write never corrupts the config.
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream outFile(tmp, std::ios::binary | std::ios::trunc);
        if (!outFile) {
            error = L"Cannot write " + tmp;
            return false;
        }
        outFile.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!outFile) {
            error = L"Write failed for " + tmp;
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"Cannot replace " + path + L": " + logging::HResultToString(HRESULT_FROM_WIN32(GetLastError()));
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace clipback

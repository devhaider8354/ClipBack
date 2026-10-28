#include "system/Startup.h"
#include "system/Paths.h"
#include "logging/Logger.h"

namespace clipback::startup {

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kValueName = L"ClipBack";

bool IsEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_SZ && size > 0;
}

bool SetEnabled(bool enabled, std::wstring& error)
{
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        error = L"Cannot open Run registry key: " + logging::HResultToString(HRESULT_FROM_WIN32(rc));
        return false;
    }
    if (enabled) {
        const std::wstring cmd = L"\"" + paths::ExePath() + L"\" --tray";
        rc = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kValueName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        error = L"Cannot update startup entry: " + logging::HResultToString(HRESULT_FROM_WIN32(rc));
        return false;
    }
    LOG_INFO(L"Start with Windows: %s", enabled ? L"enabled" : L"disabled");
    return true;
}

} // namespace clipback::startup

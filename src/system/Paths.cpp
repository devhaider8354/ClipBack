#include "system/Paths.h"
#include <shlobj.h>
#include <knownfolders.h>

namespace clipback::paths {

std::wstring ExePath()
{
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return buf;
}

std::wstring ExeDirectory()
{
    std::wstring p = ExePath();
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path)) && path) {
        result = path;
    }
    if (path) CoTaskMemFree(path);
    return result;
}

std::wstring AppDataDirectory()
{
    // Portable mode: a config.json next to the executable wins.
    const std::wstring portable = Join(ExeDirectory(), L"config.json");
    if (FileExists(portable)) return ExeDirectory();

    std::wstring base = KnownFolder(FOLDERID_LocalAppData);
    if (base.empty()) base = ExeDirectory();
    const std::wstring dir = Join(base, L"ClipBack");
    EnsureDirectory(dir);
    return dir;
}

std::wstring ConfigFilePath()
{
    return Join(AppDataDirectory(), L"config.json");
}

std::wstring LogDirectory()
{
    const std::wstring dir = Join(AppDataDirectory(), L"logs");
    EnsureDirectory(dir);
    return dir;
}

std::wstring DefaultSaveDirectory()
{
    std::wstring videos = KnownFolder(FOLDERID_Videos);
    if (videos.empty()) {
        wchar_t profile[MAX_PATH] = {};
        GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        videos = Join(profile, L"Videos");
    }
    return Join(videos, L"ClipBack");
}

bool EnsureDirectory(const std::wstring& dir)
{
    if (dir.empty()) return false;
    if (DirectoryExists(dir)) return true;
    const int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}

bool FileExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Join(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring Expand(const std::wstring& path)
{
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = ExpandEnvironmentStringsW(path.c_str(), buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n > std::size(buf)) return path;
    return buf;
}

} // namespace clipback::paths

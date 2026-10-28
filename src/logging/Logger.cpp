#include "logging/Logger.h"
#include "common/StringUtils.h"
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <share.h>

namespace clipback::logging {

namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_dir;
std::wstring g_path;
size_t g_maxBytes = 2 * 1024 * 1024;
int g_maxBackups = 3;
Level g_minLevel = Level::Debug;

const wchar_t* LevelName(Level l)
{
    switch (l) {
    case Level::Debug: return L"DEBUG";
    case Level::Info:  return L"INFO ";
    case Level::Warn:  return L"WARN ";
    case Level::Error: return L"ERROR";
    }
    return L"?????";
}

std::wstring BackupPath(int index)
{
    return g_dir + L"\\clipback." + std::to_wstring(index) + L".log";
}

void OpenFile()
{
    // _wfsopen with _SH_DENYNO: the log stays readable (Notepad, "Open log file" button) while we write it.
    // (_wfopen_s would open it exclusively.)
    g_file = _wfsopen(g_path.c_str(), L"a, ccs=UTF-8", _SH_DENYNO);
}

// Called with g_mutex held. Shifts clipback.log -> .1.log -> .2.log ... and reopens a fresh file.
void RotateIfNeeded()
{
    if (!g_file) return;
    const long size = ftell(g_file);
    if (size < 0 || static_cast<size_t>(size) < g_maxBytes) return;

    fclose(g_file);
    g_file = nullptr;

    DeleteFileW(BackupPath(g_maxBackups).c_str());
    for (int i = g_maxBackups - 1; i >= 1; --i) {
        MoveFileExW(BackupPath(i).c_str(), BackupPath(i + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    MoveFileExW(g_path.c_str(), BackupPath(1).c_str(), MOVEFILE_REPLACE_EXISTING);
    OpenFile();
}

} // namespace

bool Initialize(const std::wstring& logDirectory, size_t maxFileBytes, int maxBackups)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = logDirectory;
    g_path = logDirectory + L"\\clipback.log";
    g_maxBytes = maxFileBytes;
    g_maxBackups = maxBackups < 1 ? 1 : maxBackups;
    CreateDirectoryW(logDirectory.c_str(), nullptr);
    if (g_file) { fclose(g_file); g_file = nullptr; }
    OpenFile();
    return g_file != nullptr;
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}

void SetMinimumLevel(Level level)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minLevel = level;
}

std::wstring LogFilePath()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

void Log(Level level, const wchar_t* format, ...)
{
    if (static_cast<int>(level) < static_cast<int>(g_minLevel)) return;

    va_list args;
    va_start(args, format);
    std::wstring message = str::FormatV(format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring line = str::Format(L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] [%5lu] %s\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        LevelName(level), GetCurrentThreadId(), message.c_str());

    std::lock_guard<std::mutex> lock(g_mutex);
    OutputDebugStringW(line.c_str());
    if (g_file) {
        fputws(line.c_str(), g_file);
        fflush(g_file);
        RotateIfNeeded();
    }
}

std::wstring HResultToString(HRESULT hr)
{
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text;
    if (n && buf) {
        text = str::Trim(buf);
        LocalFree(buf);
    } else {
        text = L"Unknown error";
    }
    return str::Format(L"0x%08X (%s)", static_cast<unsigned>(hr), text.c_str());
}

} // namespace clipback::logging

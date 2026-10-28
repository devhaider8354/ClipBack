#include "common/Common.h"
#include "common/StringUtils.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>

namespace clipback::str {

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring FormatV(const wchar_t* fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int needed = _vscwprintf(fmt, copy);
    va_end(copy);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed) + 1, L'\0');
    _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, fmt, args);
    out.resize(static_cast<size_t>(needed));
    return out;
}

std::wstring Format(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::wstring s = FormatV(fmt, args);
    va_end(args);
    return s;
}

std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring Trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring BytesToHuman(uint64_t bytes)
{
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return Format(L"%.1f %s", v, units[u]);
}

} // namespace clipback::str

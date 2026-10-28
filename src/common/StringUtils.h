#pragma once
#include <string>
#include <cstdarg>

namespace clipback::str {

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
std::wstring Format(const wchar_t* fmt, ...);
std::wstring FormatV(const wchar_t* fmt, va_list args);
std::wstring ToLower(std::wstring s);
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);
std::wstring Trim(const std::wstring& s);
std::wstring BytesToHuman(uint64_t bytes);

} // namespace clipback::str

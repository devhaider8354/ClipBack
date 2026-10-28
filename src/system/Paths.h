#pragma once
#include "common/Common.h"
#include <string>

namespace clipback::paths {

std::wstring ExePath();
std::wstring ExeDirectory();
// %LOCALAPPDATA%\ClipBack (or the exe directory in portable mode = when config.json sits next to the exe).
std::wstring AppDataDirectory();
std::wstring ConfigFilePath();
std::wstring LogDirectory();
// %USERPROFILE%\Videos\ClipBack
std::wstring DefaultSaveDirectory();
bool EnsureDirectory(const std::wstring& dir);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
std::wstring Join(const std::wstring& a, const std::wstring& b);
// Expands %ENV% variables inside a path.
std::wstring Expand(const std::wstring& path);

} // namespace clipback::paths

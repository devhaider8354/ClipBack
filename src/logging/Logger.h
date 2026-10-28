// Thread-safe file logger with size-based rotation (logs\clipback.log, .1, .2 ...).
#pragma once
#include "common/Common.h"
#include <string>

namespace clipback::logging {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Creates the directory if needed and opens logDirectory\clipback.log.
bool Initialize(const std::wstring& logDirectory, size_t maxFileBytes = 2 * 1024 * 1024, int maxBackups = 3);
void Shutdown();
void SetMinimumLevel(Level level);
void Log(Level level, const wchar_t* format, ...);
std::wstring LogFilePath();

// "0x8007000E (Not enough memory resources are available to complete this operation.)"
std::wstring HResultToString(HRESULT hr);

} // namespace clipback::logging

#define LOG_DEBUG(...) ::clipback::logging::Log(::clipback::logging::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::clipback::logging::Log(::clipback::logging::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::clipback::logging::Log(::clipback::logging::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::clipback::logging::Log(::clipback::logging::Level::Error, __VA_ARGS__)
#define LOG_HR(msg, hr) ::clipback::logging::Log(::clipback::logging::Level::Error, L"%s: %s", msg, ::clipback::logging::HResultToString(hr).c_str())

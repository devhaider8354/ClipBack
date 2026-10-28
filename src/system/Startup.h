// "Start ClipBack with Windows" via HKCU\...\Run (no admin rights required).
#pragma once
#include "common/Common.h"
#include <string>

namespace clipback::startup {

bool IsEnabled();
bool SetEnabled(bool enabled, std::wstring& error);

} // namespace clipback::startup

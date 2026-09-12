#pragma once
#include "application.h"
#include <windows.h>
namespace poetoolbox::ui {
[[nodiscard]] Result<std::string> ShowAddShortcutDialog(HWND owner, ApplicationServices &services);
}

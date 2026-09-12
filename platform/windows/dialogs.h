#pragma once
#include <filesystem>
#include <poetoolbox/result.h>
#include <string_view>
#include <windows.h>
namespace poetoolbox {
[[nodiscard]] Result<std::filesystem::path> PickExecutable(HWND owner, std::wstring_view title);
[[nodiscard]] Result<std::filesystem::path> PickShortcut(HWND owner, std::wstring_view title);
[[nodiscard]] Result<std::filesystem::path> PickDirectory(HWND owner, std::wstring_view title,
                                                          const std::filesystem::path &current = {});
} // namespace poetoolbox

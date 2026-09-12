#pragma once
#include <string>
#include <string_view>
namespace poetoolbox {
[[nodiscard]] std::string Utf8(std::wstring_view text);
[[nodiscard]] std::wstring Utf16(std::string_view text);
} // namespace poetoolbox

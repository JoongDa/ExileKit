#include "utf.h"
#include <limits>
#include <windows.h>
namespace poetoolbox {
std::string Utf8(std::wstring_view text) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return {};
    const auto length = static_cast<int>(text.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (!size)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), size, nullptr, nullptr))
        return {};
    return result;
}
std::wstring Utf16(std::string_view text) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return {};
    const auto length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (!size)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), size))
        return {};
    return result;
}
} // namespace poetoolbox

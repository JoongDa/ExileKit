#include "poetoolbox/tool.h"
#include <algorithm>
#include <array>

namespace poetoolbox {
std::string_view CategoryKey(ToolCategory category) {
    constexpr std::array keys{"official",   "market",      "craft", "database", "regex", "build",  "hideout",
                              "calculator", "price-check", "trade", "filter",   "wiki",  "utility"};
    const auto index = static_cast<std::size_t>(category);
    return index < keys.size() ? keys[index] : "utility";
}
std::string_view GameFilterKey(GameFilter filter) {
    switch (filter) {
    case GameFilter::POE1:
        return "poe1";
    case GameFilter::POE2:
        return "poe2";
    default:
        return "all";
    }
}
std::string Localize(const LocalizedText &text, std::string_view language, std::string_view fallback) {
    if (const auto it = text.find(language); it != text.end() && !it->second.empty())
        return it->second;
    if (const auto it = text.find("en-US"); it != text.end() && !it->second.empty())
        return it->second;
    return std::string(fallback);
}
bool IsValidToolId(std::string_view id) {
    if (id.empty() || id.size() > 64 || id.front() == '-' || id.back() == '-')
        return false;
    if (!std::all_of(id.begin(), id.end(),
                     [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }))
        return false;
    // Stable ids also become Windows directory names; reject reserved devices.
    constexpr std::array reserved{"con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
                                  "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
                                  "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return std::find(reserved.begin(), reserved.end(), id) == reserved.end();
}
bool IsSafeWebUrl(std::string_view url) {
    if (url.size() > 8192 || !url.starts_with("https://"))
        return false;
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) {
            return c <= 0x20 || c == 0x7f || c == '\\' || c == '"' || c == '<' || c == '>';
        }))
        return false;
    const auto start = url.find("://") + 3;
    const auto authority = url.substr(start, url.find_first_of("/?#", start) - start);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find('%') != std::string_view::npos)
        return false;
    std::string_view host = authority, port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || close < 3)
            return false;
        host = authority.substr(1, close - 1);
        if (host.find(':') == std::string_view::npos || !std::all_of(host.begin(), host.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' ||
                       c == '.';
            }))
            return false;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':')
                return false;
            port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos)
            port = authority.substr(colon + 1);
        if (host.empty() || host.front() == '.' || host.front() == '-' || host.back() == '-' ||
            host.find("..") != std::string_view::npos || !std::all_of(host.begin(), host.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                       c == '.';
            }))
            return false;
    }
    if (authority.back() == ':')
        return false;
    if (!port.empty()) {
        unsigned int value = 0;
        if (port.size() > 5)
            return false;
        for (const auto c : port) {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + static_cast<unsigned int>(c - '0');
        }
        if (value == 0 || value > 65535)
            return false;
    }
    return true;
}
} // namespace poetoolbox

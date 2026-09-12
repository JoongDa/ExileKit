#pragma once
#include "poetoolbox/result.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace poetoolbox::detail {
inline constexpr std::size_t MaxJsonBytes = 256 * 1024;
inline Result<std::string> ReadJsonFile(const std::filesystem::path &file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec)
        return std::unexpected(
            Error{ErrorCode::IoError, "Cannot read JSON file.", static_cast<std::uint32_t>(ec.value())});
    if (size > MaxJsonBytes)
        return std::unexpected(Error{ErrorCode::IoError, "JSON file exceeds 256 KiB limit."});
    std::ifstream input(file, std::ios::binary);
    if (!input)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot open JSON file."});
    std::string data(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !input.read(data.data(), static_cast<std::streamsize>(data.size())))
        return std::unexpected(Error{ErrorCode::IoError, "Cannot read complete JSON file."});
    return data;
}
inline nlohmann::json ParseJson(std::string_view data, ErrorCode code) {
    if (data.size() > MaxJsonBytes)
        throw Error{code, "JSON input exceeds 256 KiB limit."};
    return nlohmann::json::parse(data, [code](int depth, nlohmann::json::parse_event_t, nlohmann::json &) {
        if (depth > 64)
            throw Error{code, "JSON nesting exceeds 64 levels."};
        return true;
    });
}
} // namespace poetoolbox::detail

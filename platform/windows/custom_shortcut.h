#pragma once
#include <filesystem>
#include <poetoolbox/custom_tool.h>
#include <poetoolbox/result.h>
#include <string>
#include <string_view>
#include <vector>

namespace poetoolbox {
struct ShortcutDetails {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::filesystem::path iconPath;
    int iconIndex = 0;
};

// Read only: neither API executes a file, resolves a moved link, nor searches for an application.
[[nodiscard]] Result<ShortcutDetails> InspectWindowsShortcut(const std::filesystem::path &path);
[[nodiscard]] Result<CustomTool> ParseCustomShortcut(std::string_view input, std::string id);
} // namespace poetoolbox

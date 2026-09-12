#pragma once
#include <string>
namespace poetoolbox {
enum class ShortcutKind { Url, Executable, WindowsShortcut };
struct CustomTool {
    std::string id;
    std::string name;
    ShortcutKind kind = ShortcutKind::Url;
    // Original user input, normalized URL or absolute UTF-8 file path. No shell command.
    std::string target;
};
} // namespace poetoolbox

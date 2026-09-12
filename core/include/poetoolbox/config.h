#pragma once
#include "custom_tool.h"
#include "tool.h"
#include <cstdint>
#include <set>
namespace poetoolbox {
struct RecentTool {
    std::int64_t lastLaunchTime = 0;
    std::uint64_t launchCount = 0;
};
struct HomeEntry {
    bool added = false;
    bool pinned = false;
    bool hiddenFromHome = false;
    std::int64_t addedAt = 0;
};
struct UserConfig {
    std::string language;
    GameFilter selectedGame = GameFilter::All;
    std::filesystem::path managedToolsDirectory;
    std::map<std::string, std::filesystem::path, std::less<>> executablePaths;
    std::set<std::string, std::less<>> favorites;
    std::map<std::string, RecentTool, std::less<>> recentTools;
    std::map<std::string, HomeEntry, std::less<>> home;
    int homeAutoRemoveDays = 30; // 0 means Never; other supported values are 7, 30, and 90.
    std::map<std::string, CustomTool, std::less<>> customTools;
};
class ConfigManager {
  public:
    explicit ConfigManager(std::filesystem::path file) : file_(std::move(file)) {}
    // In-memory validation uses exactly the same encoding and byte limit as Save; no filesystem access.
    [[nodiscard]] static Status Validate(const UserConfig &config);
    [[nodiscard]] Result<UserConfig> Load() const;
    [[nodiscard]] Status Save(const UserConfig &config) const;

  private:
    std::filesystem::path file_;
};
} // namespace poetoolbox

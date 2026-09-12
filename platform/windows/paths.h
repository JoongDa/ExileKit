#pragma once
#include <filesystem>
#include <poetoolbox/result.h>
#include <string>
namespace poetoolbox {
class PathManager final {
  public:
    [[nodiscard]] static Result<PathManager> Create(std::filesystem::path dataRoot = {});
    [[nodiscard]] const std::filesystem::path &AppDataDirectory() const { return root_; }
    [[nodiscard]] std::filesystem::path ConfigDirectory() const { return root_ / L"Config"; }
    [[nodiscard]] std::filesystem::path LogsDirectory() const { return root_ / L"Logs"; }
    [[nodiscard]] std::filesystem::path DownloadsDirectory() const { return root_ / L"Downloads"; }
    [[nodiscard]] std::filesystem::path IconCacheDirectory() const { return root_ / L"Cache" / L"Icons"; }
    [[nodiscard]] std::filesystem::path DefaultManagedToolsDirectory() const { return root_ / L"Tools"; }
    [[nodiscard]] std::filesystem::path LegacyConfigFile() const {
        return legacyConfig_;
    }
    [[nodiscard]] Status EnsureDirectories() const;
    [[nodiscard]] static Status ValidateManagedDirectory(const std::filesystem::path &directory);

  private:
    explicit PathManager(std::filesystem::path root) : root_(std::move(root)) {}
    std::filesystem::path root_;
    std::filesystem::path legacyConfig_;
};
[[nodiscard]] std::string SystemDefaultLanguage();
} // namespace poetoolbox

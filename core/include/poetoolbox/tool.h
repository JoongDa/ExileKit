#pragma once
#include "result.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace poetoolbox {
enum class ToolType { Web, Application, Builtin, ExternalLink };
enum class DistributionType { None, External, Managed, Bundled };
enum class GameType { POE1, POE2 };
enum class RiskLevel { Normal, Elevated, GameModifying };
enum class InstallState { NotConfigured, Installed, ManagedUnavailable };
enum class ToolCategory {
    Official,
    Market,
    Craft,
    Database,
    Regex,
    Build,
    Hideout,
    Calculator,
    PriceCheck,
    Trade,
    Filter,
    Wiki,
    Utility
};
enum class GameFilter { All, POE1, POE2 };
struct VersionInfo {
    std::string installed;
    std::string available;
};
using LocalizedText = std::map<std::string, std::string, std::less<>>;
struct LaunchSpec {
    std::string url;
    std::vector<std::string> executableNames;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::map<std::string, std::string, std::less<>> environment;
};
struct ToolManifest {
    int schemaVersion = 1;
    std::string id, name;
    LocalizedText description;
    ToolCategory category = ToolCategory::Utility;
    std::vector<std::string> tags;
    std::map<std::string, std::vector<std::string>, std::less<>> aliases;
    std::vector<GameType> games;
    bool gameSupportVerified = true;
    ToolType type = ToolType::Web;
    DistributionType distribution = DistributionType::None;
    RiskLevel riskLevel = RiskLevel::Normal;
    bool official = false;
    std::string author, homepage, repository, icon, license, licenseUrl, downloadPage;
    std::string provider, expectedSha256;
    bool outbound = false, inbound = false;
    LaunchSpec launch;
};
struct Tool {
    ToolManifest manifest;
    std::string searchText;
};
[[nodiscard]] std::string_view CategoryKey(ToolCategory category);
[[nodiscard]] std::string_view GameFilterKey(GameFilter filter);
[[nodiscard]] std::string Localize(const LocalizedText &text, std::string_view language,
                                   std::string_view fallback = {});
[[nodiscard]] bool IsValidToolId(std::string_view id);
[[nodiscard]] bool IsSafeWebUrl(std::string_view url);
} // namespace poetoolbox

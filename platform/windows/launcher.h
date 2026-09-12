#pragma once
#include <filesystem>
#include <poetoolbox/custom_tool.h>
#include <poetoolbox/tool.h>
#include <string>
#include <vector>
namespace poetoolbox {
struct LaunchRequest {
    std::filesystem::path executable;
    std::filesystem::path workingDirectory;
    std::wstring commandLine;
    std::vector<wchar_t> environment;
};
[[nodiscard]] std::wstring QuoteWindowsArgument(std::wstring_view argument);
// System-call boundary: unit tests substitute a recorder without opening a browser.
class LaunchSystem {
  public:
    virtual ~LaunchSystem() = default;
    [[nodiscard]] virtual Status OpenUrl(std::wstring_view url) = 0;
    [[nodiscard]] virtual Status StartProcess(const LaunchRequest &request) = 0;
};
class ToolLauncher final {
  public:
    explicit ToolLauncher(LaunchSystem *system = nullptr) : system_(system) {}
    [[nodiscard]] Status Launch(const ToolManifest &tool, const std::filesystem::path &configuredExe = {},
                                bool riskAccepted = false) const;
    [[nodiscard]] Status LaunchCustom(const CustomTool &tool) const;
    [[nodiscard]] static Result<LaunchRequest> PrepareApplication(const ToolManifest &tool,
                                                                  const std::filesystem::path &configuredExe);

  private:
    LaunchSystem *system_;
};
} // namespace poetoolbox

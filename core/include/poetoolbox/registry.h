#pragma once
#include "tool.h"
#include <optional>

namespace poetoolbox {
class ToolRegistry {
  public:
    // An unavailable directory fails; malformed individual files are skipped.
    [[nodiscard]] Status Load(const std::filesystem::path &directory);
    [[nodiscard]] const std::vector<Tool> &GetTools() const noexcept { return tools_; }
    [[nodiscard]] const Tool *FindTool(std::string_view id) const;
    [[nodiscard]] std::vector<std::size_t> Search(std::string_view query, GameFilter game = GameFilter::All,
                                                  std::optional<ToolCategory> category = std::nullopt,
                                                  std::string_view language = "en-US") const;
    [[nodiscard]] const std::vector<Error> &Diagnostics() const noexcept { return diagnostics_; }

  private:
    std::vector<Tool> tools_;
    std::vector<Error> diagnostics_;
    std::map<std::string, std::size_t, std::less<>> byId_;
    std::map<ToolCategory, std::vector<std::size_t>> byCategory_;
};
} // namespace poetoolbox

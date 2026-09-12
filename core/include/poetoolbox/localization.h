#pragma once
#include "result.h"
#include <array>
#include <filesystem>
#include <map>
#include <string_view>

namespace poetoolbox {
class LocalizationManager {
  public:
    [[nodiscard]] Status Load(const std::filesystem::path &directory);
    [[nodiscard]] bool SetLanguage(std::string_view language);
    [[nodiscard]] const std::string &GetCurrentLanguage() const noexcept { return language_; }
    [[nodiscard]] static constexpr std::array<std::string_view, 2> GetAvailableLanguages() noexcept {
        return {"en-US", "zh-CN"};
    }
    [[nodiscard]] std::string GetString(std::string_view key, std::string_view defaultText = {}) const;

  private:
    using Strings = std::map<std::string, std::string, std::less<>>;
    std::map<std::string, Strings, std::less<>> translations_;
    std::string language_ = "en-US";
};
} // namespace poetoolbox

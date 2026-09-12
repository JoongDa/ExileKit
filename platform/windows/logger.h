#pragma once
#include <filesystem>
#include <string_view>

namespace poetoolbox {
enum class LogLevel { Debug, Info, Warning, Error };

// Owned by the application; no singleton, no persistent file handle or worker.
class Logger final {
  public:
    void Initialize(std::filesystem::path directory) noexcept;
    void Write(LogLevel level, std::wstring_view message) const noexcept;

  private:
    std::filesystem::path path_;
};
} // namespace poetoolbox

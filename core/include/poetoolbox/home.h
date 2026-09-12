#pragma once
#include "config.h"
#include <string_view>
#include <vector>

namespace poetoolbox {
// All times are Unix seconds supplied by the caller; no clock, timer, or file I/O.
class HomeService {
  public:
    static void RecordSuccessfulLaunch(UserConfig &config, std::string_view id, std::int64_t now);
    static void Add(UserConfig &config, std::string_view id, std::int64_t now);
    static void Pin(UserConfig &config, std::string_view id, std::int64_t now);
    static void Unpin(UserConfig &config, std::string_view id);
    static void Remove(UserConfig &config, std::string_view id);
    // Expiry changes only Home membership. History and custom tools remain intact.
    static void Evaluate(UserConfig &config, std::int64_t now);
    [[nodiscard]] static std::vector<std::string> Visible(const UserConfig &config, std::int64_t now);
};
} // namespace poetoolbox

#include "poetoolbox/home.h"
#include <algorithm>
#include <limits>

namespace poetoolbox {
namespace {
constexpr std::int64_t SecondsPerDay = 24 * 60 * 60;
std::int64_t Window(const UserConfig &config) {
    return static_cast<std::int64_t>(config.homeAutoRemoveDays == 0 ? 30 : config.homeAutoRemoveDays) * SecondsPerDay;
}
RecentTool Recent(const UserConfig &config, std::string_view id) {
    const auto found = config.recentTools.find(id);
    return found == config.recentTools.end() ? RecentTool{} : found->second;
}
bool Expired(const UserConfig &config, const HomeEntry &entry, std::string_view id, std::int64_t now) {
    if (entry.pinned || config.homeAutoRemoveDays == 0)
        return false;
    const auto used = std::max(entry.addedAt, Recent(config, id).lastLaunchTime);
    // Treat a future timestamp conservatively, without unsigned arithmetic underflow.
    return now > used && now - used >= Window(config);
}
} // namespace

void HomeService::RecordSuccessfulLaunch(UserConfig &config, std::string_view id, std::int64_t now) {
    now = std::max<std::int64_t>(0, now);
    // Expire using the previous usage, before this launch can refresh its timestamp.
    Evaluate(config, now);
    auto &usage = config.recentTools[std::string(id)];
    const bool frequent =
        usage.launchCount > 0 && now >= usage.lastLaunchTime && now - usage.lastLaunchTime < Window(config);
    usage.lastLaunchTime = now;
    if (usage.launchCount != std::numeric_limits<std::uint64_t>::max())
        ++usage.launchCount;
    auto &entry = config.home[std::string(id)];
    if (frequent && !entry.hiddenFromHome && !entry.added) {
        entry.added = true;
        entry.addedAt = now;
    }
}
void HomeService::Add(UserConfig &config, std::string_view id, std::int64_t now) {
    auto &entry = config.home[std::string(id)];
    entry.added = true;
    entry.hiddenFromHome = false;
    if (!entry.pinned)
        entry.addedAt = std::max<std::int64_t>(0, now);
}
void HomeService::Pin(UserConfig &config, std::string_view id, std::int64_t now) {
    auto &entry = config.home[std::string(id)];
    // Repeating Pin does not reorder an already pinned item.
    if (!entry.pinned)
        entry.addedAt = std::max<std::int64_t>(0, now);
    entry.added = true;
    entry.hiddenFromHome = false;
    entry.pinned = true;
}
void HomeService::Unpin(UserConfig &config, std::string_view id) {
    if (auto found = config.home.find(id); found != config.home.end())
        found->second.pinned = false;
}
void HomeService::Remove(UserConfig &config, std::string_view id) {
    auto &entry = config.home[std::string(id)];
    entry.added = false;
    entry.pinned = false;
    entry.hiddenFromHome = true;
}
void HomeService::Evaluate(UserConfig &config, std::int64_t now) {
    now = std::max<std::int64_t>(0, now);
    // M2.5 has no Home membership. Import only existing recent, repeated usage.
    for (const auto &[id, recent] : config.recentTools) {
        if (!config.home.contains(id) && recent.launchCount >= 2 && now >= recent.lastLaunchTime &&
            now - recent.lastLaunchTime < Window(config)) {
            auto &entry = config.home[id];
            entry.added = true;
            entry.addedAt = recent.lastLaunchTime;
        }
    }
    for (auto &[id, entry] : config.home) {
        if (entry.added && Expired(config, entry, id, now))
            entry.added = false;
    }
}
std::vector<std::string> HomeService::Visible(const UserConfig &config, std::int64_t now) {
    now = std::max<std::int64_t>(0, now);
    std::vector<std::string> ids;
    for (const auto &[id, entry] : config.home) {
        if (entry.added && !entry.hiddenFromHome && !Expired(config, entry, id, now))
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [&](const auto &left, const auto &right) {
        const auto &a = config.home.at(left), &b = config.home.at(right);
        if (a.pinned != b.pinned)
            return a.pinned;
        if (a.pinned) {
            if (a.addedAt != b.addedAt)
                return a.addedAt < b.addedAt;
        } else {
            const auto ar = Recent(config, left), br = Recent(config, right);
            if (ar.lastLaunchTime != br.lastLaunchTime)
                return ar.lastLaunchTime > br.lastLaunchTime;
            if (ar.launchCount != br.launchCount)
                return ar.launchCount > br.launchCount;
            if (a.addedAt != b.addedAt)
                return a.addedAt > b.addedAt;
        }
        return left < right;
    });
    return ids;
}
} // namespace poetoolbox

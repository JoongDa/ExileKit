#include "poetoolbox/home.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace poetoolbox;
using Json = nlohmann::json;
namespace {
constexpr std::int64_t Day = 24 * 60 * 60;
constexpr std::int64_t Start = 1700000000;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool Contains(const UserConfig &config, std::string_view id, std::int64_t now) {
    const auto visible = HomeService::Visible(config, now);
    return std::find(visible.begin(), visible.end(), id) != visible.end();
}
void Write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}
std::string Read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 1;
    try {
        UserConfig c;
        Check(HomeService::Visible(c, Start).empty(), "New profile Home must be empty");
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start);
        Check(c.recentTools.at("poe-ninja").launchCount == 1 && c.recentTools.at("poe-ninja").lastLaunchTime == Start &&
                  !Contains(c, "poe-ninja", Start),
              "First success statistics or qualification incorrect");
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start + 1);
        Check(c.recentTools.at("poe-ninja").launchCount == 2 && Contains(c, "poe-ninja", Start + 1) &&
                  !c.home.at("poe-ninja").pinned,
              "Repeated success did not auto-add an ordinary item");
        HomeService::Remove(c, "poe-ninja");
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start + 2);
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start + 3);
        Check(!Contains(c, "poe-ninja", Start + 3) && c.recentTools.at("poe-ninja").launchCount == 4,
              "Manual removal was overridden by automatic qualification");
        HomeService::Add(c, "poe-ninja", Start + 4);
        Check(Contains(c, "poe-ninja", Start + 4) && !c.home.at("poe-ninja").pinned &&
                  !c.home.at("poe-ninja").hiddenFromHome,
              "Add to Home must restore visibility without pinning");
        HomeService::Pin(c, "poe-ninja", Start + 5);
        HomeService::Evaluate(c, Start + 400 * Day);
        Check(Contains(c, "poe-ninja", Start + 400 * Day), "Pinned item expired");
        HomeService::Unpin(c, "poe-ninja");
        HomeService::Evaluate(c, Start + 400 * Day);
        Check(!Contains(c, "poe-ninja", Start + 400 * Day) && c.recentTools.contains("poe-ninja"),
              "Unpinned stale item did not resume expiry or lost history");
        // A single launch after a stale gap must not immediately reuse an old high cumulative count.
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start + 400 * Day + 1);
        Check(!Contains(c, "poe-ninja", Start + 400 * Day + 1), "Stale usage falsely qualified as frequent");
        HomeService::RecordSuccessfulLaunch(c, "poe-ninja", Start + 400 * Day + 2);
        Check(Contains(c, "poe-ninja", Start + 400 * Day + 2), "New repeated usage did not qualify");

        for (const int policy : {7, 30, 90}) {
            UserConfig expiry;
            expiry.homeAutoRemoveDays = policy;
            HomeService::Add(expiry, "ordinary", Start);
            HomeService::Pin(expiry, "pinned", Start);
            const auto boundary = Start + static_cast<std::int64_t>(policy) * Day;
            Check(Contains(expiry, "ordinary", boundary - 1), "Non-expired item disappeared");
            HomeService::Evaluate(expiry, boundary);
            Check(!Contains(expiry, "ordinary", boundary) && Contains(expiry, "pinned", boundary),
                  "Exact policy boundary incorrect");
            Check(expiry.home.contains("ordinary"), "Auto removal deleted Home state");
        }
        UserConfig never;
        never.homeAutoRemoveDays = 0;
        HomeService::Add(never, "ordinary", Start);
        HomeService::Evaluate(never, Start + 10000 * Day);
        Check(Contains(never, "ordinary", Start + 10000 * Day), "Never policy expired an item");
        HomeService::Remove(never, "ordinary");
        Check(!Contains(never, "ordinary", Start), "Never policy ignored explicit removal");

        UserConfig ordering;
        for (int i = 0; i < 100; ++i)
            HomeService::Add(ordering, "tool-" + std::to_string(i), Start);
        Check(HomeService::Visible(ordering, Start).size() == 100, "Home has a fixed item cap");
        HomeService::Pin(ordering, "tool-2", Start + 1);
        HomeService::Pin(ordering, "tool-1", Start + 2);
        HomeService::Pin(ordering, "tool-2", Start + 3);
        HomeService::RecordSuccessfulLaunch(ordering, "tool-3", Start + 4);
        HomeService::RecordSuccessfulLaunch(ordering, "tool-1", Start + 5);
        const auto sorted = HomeService::Visible(ordering, Start + 5);
        Check(sorted[0] == "tool-2" && sorted[1] == "tool-1" && sorted[2] == "tool-3",
              "Pinned ordering changed on usage or ordinary recency ordering failed");
        ordering.recentTools["tool-4"] = {Start + 4, 10};
        Check(HomeService::Visible(ordering, Start + 5)[2] == "tool-4", "Usage frequency tie-breaker failed");
        ordering.recentTools["tool-5"] = {Start, std::numeric_limits<std::uint64_t>::max()};
        HomeService::RecordSuccessfulLaunch(ordering, "tool-5", Start + 6);
        Check(ordering.recentTools["tool-5"].launchCount == std::numeric_limits<std::uint64_t>::max(),
              "Usage count overflowed");
        Check(Contains(ordering, "tool-10", Start - Day), "Backward clock adjustment prematurely expired an item");

        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        const auto file = output / L"settings.json";
        ConfigManager config(file);
        UserConfig saved;
        saved.language = "zh-CN";
        saved.homeAutoRemoveDays = 90;
        saved.customTools["custom-site"] = {"custom-site", "网站 名称", ShortcutKind::Url, "https://example.com/a?q=b"};
        saved.customTools["custom-exe"] = {"custom-exe", "程序", ShortcutKind::Executable, "C:\\用户 文件\\程序.EXE"};
        saved.customTools["custom-link"] = {"custom-link", "快捷方式", ShortcutKind::WindowsShortcut,
                                            "C:\\用户 文件\\程序.lnk"};
        saved.executablePaths["installed-tool"] = output / L"installed.exe";
        Write(saved.executablePaths.at("installed-tool"), "fixture stays on disk");
        HomeService::Add(saved, "custom-site", Start);
        HomeService::Pin(saved, "custom-exe", Start);
        HomeService::Remove(saved, "custom-link");
        HomeService::RecordSuccessfulLaunch(saved, "installed-tool", Start);
        HomeService::RecordSuccessfulLaunch(saved, "installed-tool", Start + 1);
        HomeService::Remove(saved, "installed-tool");
        Check(saved.executablePaths.contains("installed-tool") &&
                  std::filesystem::exists(saved.executablePaths.at("installed-tool")),
              "Remove from Home changed an executable mapping or file");
        Check(config.Save(saved).has_value(), "Custom and Home config save failed");
        auto loaded = config.Load();
        Check(loaded && loaded->language == "zh-CN" && loaded->homeAutoRemoveDays == 90 &&
                  loaded->customTools.size() == 3 && loaded->customTools.at("custom-site").name == "网站 名称" &&
                  loaded->customTools.at("custom-exe").target == saved.customTools.at("custom-exe").target &&
                  loaded->customTools.at("custom-link").kind == ShortcutKind::WindowsShortcut &&
                  loaded->home.at("custom-exe").pinned && loaded->home.at("custom-link").hiddenFromHome &&
                  loaded->home.at("custom-site").addedAt == Start &&
                  loaded->recentTools.at("installed-tool").launchCount == 2,
              "Custom and Home config roundtrip failed");
        HomeService::Evaluate(*loaded, Start + 1000 * Day);
        Check(!Contains(*loaded, "custom-site", Start + 1000 * Day) && loaded->customTools.size() == 3 &&
                  Contains(*loaded, "custom-exe", Start + 1000 * Day),
              "Custom expiry/pinning did not preserve underlying user tools");
        const auto original = Read(file);
        auto invalid = saved;
        invalid.homeAutoRemoveDays = 8;
        Check(!config.Save(invalid) && Read(file) == original, "Invalid policy destroyed saved config");
        invalid = saved;
        invalid.customTools.at("custom-site").target = "javascript:alert(1)";
        Check(!config.Save(invalid) && Read(file) == original, "Unsafe custom target accepted or destroyed config");
        invalid = saved;
        invalid.customTools.at("custom-exe").target = "relative.exe";
        Check(!config.Save(invalid), "Relative custom file path accepted");
        invalid = saved;
        invalid.customTools.at("custom-link").kind = ShortcutKind::Executable;
        Check(!config.Save(invalid), "Custom file kind/extension mismatch accepted");
        invalid = saved;
        invalid.customTools.at("custom-site").id = "other-id";
        Check(!config.Save(invalid), "Mismatched custom id accepted");

        const auto badFile = output / L"invalid.json";
        auto bad = Json::parse(original);
        bad["home"]["custom-exe"]["pinned"] = "true";
        Write(badFile, bad.dump());
        const auto badText = Read(badFile);
        Check(!ConfigManager(badFile).Load() && Read(badFile) == badText, "Invalid config load modified user data");
        bad = Json::parse(original);
        bad["homeAutoRemoveDays"] = std::numeric_limits<std::uint64_t>::max();
        Write(badFile, bad.dump());
        Check(!ConfigManager(badFile).Load(), "Oversized policy integer accepted");

        const auto legacyFile = output / L"legacy.json";
        Write(
            legacyFile,
            R"({"schemaVersion":1,"language":"en-US","selectedGame":"poe2","favorites":["poe-ninja"],"recentTools":{"poe-ninja":{"lastLaunchTime":1700000000,"launchCount":3}},"unknownFutureField":true})");
        auto legacy = ConfigManager(legacyFile).Load();
        Check(legacy && legacy->home.empty() && legacy->customTools.empty() && legacy->homeAutoRemoveDays == 30 &&
                  legacy->favorites.contains("poe-ninja") && legacy->selectedGame == GameFilter::POE2,
              "M2.5 config compatibility failed");
        HomeService::Evaluate(*legacy, Start + Day);
        Check(Contains(*legacy, "poe-ninja", Start + Day), "Recent M2.5 usage did not migrate into Home");
        Check(ConfigManager(legacyFile).Save(*legacy).has_value() && ConfigManager(legacyFile).Load().has_value(),
              "Migrated config did not persist");
        std::cout << "Home fake-clock, ordering, user intent, custom persistence, and legacy config checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

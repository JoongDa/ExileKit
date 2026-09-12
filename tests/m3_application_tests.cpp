#include "application.h"
#include "web_metadata.h"
#include "utf.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <thread>
using namespace poetoolbox;
namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate> void Wait(ApplicationServices &app, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        app.Drain();
        if (predicate())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Application event timed out.");
}
struct FakeWeb final : HttpClient {
    std::atomic<int> calls{0};
    std::vector<uint8_t> png;
    Result<HttpResponse> Get(const HttpRequest &request) override {
        ++calls;
        if (request.stop.stop_requested())
            return std::unexpected(Error{ErrorCode::Cancelled, "Cancelled."});
        if (request.url.ends_with(".ico") || request.url.ends_with(".png"))
            return HttpResponse{200, {}, "image/png", png};
        const std::string page =
            "<html><head><title>Shortcut &amp; Test</title><link rel='icon' href='/favicon.ico'></head></html>";
        return HttpResponse{200, {}, "text/html", {page.begin(), page.end()}};
    }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 1;
    try {
        const std::filesystem::path source(argv[1]), output = std::filesystem::absolute(argv[2]), fixture(argv[3]);
        const auto profile = output / L"profile";
        Check(ConfigManager(profile / L"Config/settings.json").Save(UserConfig{}).has_value(),
              "Cannot seed isolated profile.");
        FakeWeb web;
        {
            std::ifstream file(source / L"resources/icons/generic-web.png", std::ios::binary);
            web.png.assign(std::istreambuf_iterator<char>(file), {});
        }
        ApplicationServices app(source, profile, &web);
        app.Start([] {});
        Wait(app, [&] { return app.Data() != nullptr; });
        Check(app.HomeIds().empty() && web.calls == 0, "Startup populated Home or accessed the network.");
        app.AddToHome("poe-ninja");
        Check(app.HomeIds().size() == 1 && !app.Data()->config.home.at("poe-ninja").pinned, "Add unexpectedly pinned.");
        app.PinToHome("poe-ninja");
        app.RemoveFromHome("poe-ninja");
        Check(app.HomeIds().empty() && app.Data()->registry.FindTool("poe-ninja"), "Remove deleted library data.");
        app.AddToHome("poe-ninja");
        Check(!app.Data()->config.home.at("poe-ninja").hiddenFromHome, "Explicit add did not restore.");
        app.AddCustomShortcut("not a valid shortcut");
        Wait(app, [&] { return app.LastError().has_value(); });
        Check(app.Data()->config.customTools.empty(), "Invalid input created custom data.");
        app.AddCustomShortcut("https://example.com/");
        Wait(app, [&] { return app.Data()->config.customTools.size() == 1; });
        const auto urlId = app.Data()->config.customTools.begin()->first;
        Check(!app.Data()->config.home.at(urlId).pinned, "New custom shortcut was pinned.");
        Wait(app, [&] {
            return app.Data()->config.customTools.at(urlId).name == "Shortcut & Test" &&
                   app.Data()->icons.contains(urlId);
        });
        Check(app.Data()->config.recentTools.find(urlId) == app.Data()->config.recentTools.end(),
              "Adding custom URL counted as launch.");
        const int calls = web.calls;
        app.RequestIcon(urlId, true);
        app.RemoveFromHome(urlId);
        app.AddCustomShortcut("https://example.com/");
        Wait(app, [&] { return !app.Data()->config.home.at(urlId).hiddenFromHome; });
        Check(app.Data()->config.customTools.size() == 1 && web.calls == calls,
              "Duplicate input made duplicate tool or redundant request.");
        const auto dir = output / L"中文 程序";
        std::filesystem::create_directories(dir);
        const auto exe = dir / L"fixture.exe";
        std::filesystem::copy_file(fixture, exe, std::filesystem::copy_options::overwrite_existing);
        app.AddCustomShortcut(Utf8(exe.native()));
        Wait(app, [&] { return app.Data()->config.customTools.size() == 2; });
        std::string exeId;
        for (const auto &[id, tool] : app.Data()->config.customTools)
            if (tool.kind == ShortcutKind::Executable)
                exeId = id;
        Check(!exeId.empty(), "EXE was not recognized.");
        app.Launch(exeId, false);
        Wait(app, [&] { return app.Data()->config.recentTools.contains(exeId); });
        Check(app.Data()->config.recentTools.at(exeId).launchCount == 1, "Successful EXE launch not counted once.");
        app.RemoveFromHome(exeId);
        app.Launch(exeId, false);
        Wait(app, [&] { return app.Data()->config.recentTools.at(exeId).launchCount == 2; });
        Check(app.Data()->config.home.at(exeId).hiddenFromHome, "Launch resurrected manually removed custom shortcut.");
        app.Launch("poecharm2", false);
        Wait(app, [&] { return app.LastError().has_value(); });
        Check(!app.Data()->config.recentTools.contains("poecharm2"), "Failed launch counted as successful use.");
        app.SetHomeAutoRemoval(7);
        app.SetLanguage("zh-CN");
        Check(app.Tr("home.pin") == "固定到 Home" && app.Tr("shortcut.add") == "添加快捷方式",
              "New UI translation absent.");
        app.Stop();
        const auto saved = ConfigManager(profile / L"Config/settings.json").Load();
        Check(saved && saved->customTools.size() == 2 && saved->homeAutoRemoveDays == 7 &&
                  saved->home.at(exeId).hiddenFromHome && saved->recentTools.at(exeId).launchCount == 2,
              "Custom/Home state did not persist at shutdown.");
        Check(std::filesystem::exists(exe), "Removing Home entry deleted actual executable.");
        const auto corruptProfile = output / L"corrupt-profile";
        std::filesystem::create_directories(corruptProfile / L"Config");
        {
            std::ofstream bad(corruptProfile / L"Config/settings.json");
            bad << "{broken";
        }
        ApplicationServices recovered(source, corruptProfile, &web);
        recovered.Start([] {});
        Wait(recovered, [&] { return recovered.Data() != nullptr; });
        recovered.SetLanguage("en-US");
        recovered.Stop();
        bool backup = false;
        for (const auto &entry : std::filesystem::directory_iterator(corruptProfile / L"Config"))
            if (entry.path().filename().native().find(L".recovery-") != std::wstring::npos)
                backup = true;
        Check(backup && ConfigManager(corruptProfile / L"Config/settings.json").Load().has_value(),
              "Corrupt configuration not preserved before recovery.");
        const auto capacityProfile = output / L"capacity-profile";
        UserConfig full;
        full.language = "en-US";
        full.managedToolsDirectory = capacityProfile / L"Tools";
        std::string rejectedUrl;
        for (int i = 0; i < 128; ++i) {
            auto candidate = full;
            const auto id = "user-capacity-" + std::to_string(i);
            const auto url = "https://capacity.example/" + std::string(8000, 'a') + std::to_string(i);
            candidate.customTools[id] = {id, "Capacity", ShortcutKind::Url, url};
            if (!ConfigManager::Validate(candidate)) {
                rejectedUrl = url;
                break;
            }
            full = std::move(candidate);
        }
        Check(!rejectedUrl.empty() && ConfigManager(capacityProfile / L"Config/settings.json").Save(full).has_value(),
              "Cannot seed configuration capacity boundary.");
        const int callsBeforeCapacity = web.calls;
        ApplicationServices bounded(source, capacityProfile, &web);
        bounded.Start([] {});
        Wait(bounded, [&] { return bounded.Data() != nullptr; });
        bounded.AddCustomShortcut(rejectedUrl);
        bounded.SetLanguage("zh-CN"); // Changes the snapshot while the worker validates the pending addition.
        Wait(bounded, [&] { return bounded.LastError().has_value(); });
        Check(bounded.Data()->config.customTools.size() == full.customTools.size() && web.calls == callsBeforeCapacity,
              "Rejected oversized addition damaged live configuration or started HTTP.");
        bounded.SetLanguage("en-US");
        bounded.Stop();
        const auto kept = ConfigManager(capacityProfile / L"Config/settings.json").Load();
        Check(kept && kept->customTools.size() == full.customTools.size() && kept->language == "en-US",
              "An oversized shortcut prevented subsequent valid settings from being saved.");
        std::cout << "M3 application: empty startup/no network, Home intent, custom async metadata, cache dedup, "
                     "actual EXE launch, failed launch accounting and recovery passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}

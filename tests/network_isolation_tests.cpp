#include "application.h"
#include "web_metadata.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace poetoolbox;
namespace {
using Clock = std::chrono::steady_clock;
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate>
void WaitUntil(ApplicationServices &app, Clock::time_point deadline, Predicate predicate, const char *message) {
    do {
        app.Drain();
        if (predicate())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    throw std::runtime_error(message);
}
double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// This fake deliberately does not produce an HTTP response. Cancellation must wake it;
// a separate safety deadline turns a missing stop signal into a failure, not a hung test.
struct BlockingWeb final : HttpClient {
    std::atomic<int> calls{0};
    std::atomic<int> active{0};
    std::atomic<int> cancellations{0};
    std::mutex mutex;
    std::condition_variable_any condition;

    Result<HttpResponse> Get(const HttpRequest &request) override {
        ++calls;
        ++active;
        std::unique_lock lock(mutex);
        condition.wait_until(lock, request.stop, Clock::now() + std::chrono::seconds(15), [] { return false; });
        --active;
        if (request.stop.stop_requested()) {
            ++cancellations;
            return std::unexpected(Error{ErrorCode::Cancelled, "Blocked test request cancelled."});
        }
        return std::unexpected(Error{ErrorCode::IoError, "Blocked test request reached its safety deadline."});
    }
};
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 1;
    try {
        const std::filesystem::path source(argv[1]), output = std::filesystem::absolute(argv[2]), fixture(argv[3]);
        // Each run has no icon or failure cache left over from an earlier test.
        const auto run = output / (L"run-" + std::to_wstring(Clock::now().time_since_epoch().count()));
        const auto profile = run / L"profile";
        const auto file = profile / L"Config" / L"settings.json";
        UserConfig seed;
        seed.customTools["user-seeded-site"] = {"user-seeded-site", "Saved website", ShortcutKind::Url,
                                                "https://example.org/"};
        HomeService::Pin(seed, "user-seeded-site", 1);
        Check(ConfigManager(file).Save(seed).has_value(), "Cannot seed isolated URL Home profile.");

        const auto executableDirectory = run / L"中文 程序";
        std::filesystem::create_directories(executableDirectory);
        const auto exe = executableDirectory / L"fixture.exe";
        const auto report = executableDirectory / L"launch-fixture.json";
        std::filesystem::copy_file(fixture, exe);

        BlockingWeb web;
        ApplicationServices app(source, profile, &web);
        app.Start([] {});
        WaitUntil(
            app, Clock::now() + std::chrono::seconds(10), [&] { return app.Data() != nullptr; },
            "Startup did not finish.");
        Check(!app.LastError() && app.HomeIds().size() == 1 && app.HomeIds().front() == "user-seeded-site",
              "Saved URL Home was not restored.");
        Check(web.calls == 0, "Restoring Home started HTTP requests before any explicit remote icon request.");

        app.RequestIcon("user-seeded-site", false);
        const auto initialManaged = run / L"initial-managed";
        app.SetManagedDirectory(initialManaged);
        WaitUntil(
            app, Clock::now() + std::chrono::seconds(3),
            [&] { return app.Data()->config.managedToolsDirectory == initialManaged; },
            "Startup local work did not complete.");
        Check(web.calls == 0, "Local-only startup icon lookup accessed HTTP.");

        app.AddCustomShortcut("https://example.com/");
        WaitUntil(
            app, Clock::now() + std::chrono::seconds(3),
            [&] { return app.Data()->config.customTools.size() == 2 && web.active == 1; },
            "Adding a URL did not create a shortcut and begin asynchronous metadata work.");
        std::string blockedId;
        for (const auto &[id, tool] : app.Data()->config.customTools)
            if (tool.target == "https://example.com/")
                blockedId = id;
        Check(!blockedId.empty() && !app.Data()->config.customTools.at(blockedId).name.empty() &&
                  app.Data()->config.home.at(blockedId).added && !app.Data()->config.home.at(blockedId).pinned,
              "A pending website request prevented a usable ordinary Home shortcut.");

        const auto localStart = Clock::now();
        const auto localDeadline = localStart + std::chrono::seconds(3);
        app.SetLanguage("zh-CN");
        app.SetGame(GameFilter::POE2);
        app.SetHomeAutoRemoval(7);
        app.AddToHome("poe-ninja");
        app.PinToHome("poe-ninja");
        app.RemoveFromHome("poe-ninja");
        app.AddToHome("poe-ninja");
        app.PinToHome("user-seeded-site");
        app.RemoveFromHome(blockedId);
        Check(app.Tr("nav.settings") == "设置" && app.Data()->config.selectedGame == GameFilter::POE2 &&
                  app.Data()->config.homeAutoRemoveDays == 7 && app.Data()->config.home.at("poe-ninja").added &&
                  !app.Data()->config.home.at("poe-ninja").pinned &&
                  app.Data()->config.home.at(blockedId).hiddenFromHome,
              "Blocked HTTP prevented immediate localization/configuration/Home actions.");

        // This queues behind the blocked request. It must still be added locally, and
        // shutdown must discard its pending metadata job without starting another GET.
        app.AddCustomShortcut("https://example.net/");
        app.ConfigureExecutable("poecharm2", exe);
        WaitUntil(
            app, localDeadline,
            [&] { return app.Data()->config.customTools.size() == 3 && app.Data()->installed.contains("poecharm2"); },
            "Blocked HTTP stalled shortcut creation or local executable configuration.");
        Check(!std::filesystem::exists(report), "Configuring the executable unexpectedly launched it.");
        app.Launch("poecharm2", false);
        WaitUntil(
            app, localDeadline,
            [&] {
                std::error_code error;
                const auto size = std::filesystem::file_size(report, error);
                return app.Data()->config.recentTools.contains("poecharm2") && !error && size > 0;
            },
            "Blocked HTTP stalled actual local executable launch or successful usage recording.");
        Check(!app.LastError() && app.Data()->config.recentTools.at("poecharm2").launchCount == 1,
              "The real local launch failed or was counted incorrectly.");
        const auto localMs = Milliseconds(localStart);
        Check(localMs < 3000 && web.calls == 1 && web.active == 1 && web.cancellations == 0,
              "Local work waited for HTTP or the blocked request ended before isolation was exercised.");

        const auto stopStart = Clock::now();
        app.Stop();
        const auto stopMs = Milliseconds(stopStart);
        Check(stopMs < 2000 && web.active == 0 && web.cancellations == 1 && web.calls == 1,
              "Shutdown did not promptly cancel active HTTP and discard queued metadata requests.");
        const auto saved = ConfigManager(file).Load();
        Check(saved && saved->language == "zh-CN" && saved->selectedGame == GameFilter::POE2 &&
                  saved->homeAutoRemoveDays == 7 && saved->customTools.size() == 3 &&
                  saved->home.at(blockedId).hiddenFromHome && saved->home.at("user-seeded-site").pinned &&
                  saved->home.at("poe-ninja").added && !saved->home.at("poe-ninja").pinned &&
                  saved->executablePaths.at("poecharm2") == exe && saved->recentTools.at("poecharm2").launchCount == 1,
              "Cancelling metadata lost independent configuration, Home, shortcut, or launch state.");
        Check(std::filesystem::exists(exe) && std::filesystem::exists(report), "Home changes removed a local file.");
        std::cout << "Network isolation: restored Home/local icons made zero requests; blocked HTTP allowed local "
                     "Home/settings/shortcut/real EXE operations; shutdown cancelled active work and flushed state.\n"
                  << "LocalOperationsMs=" << localMs << " CancelAndFlushMs=" << stopMs << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "application.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace poetoolbox;
namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate> void Wait(ApplicationServices &service, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        service.Drain();
        if (done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Application service did not complete before deadline.");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc < 4)
        return 1;
    try {
        const std::filesystem::path root(argv[1]), output = std::filesystem::absolute(argv[2]), fixture(argv[3]);
        ApplicationServices app(root, output / L"profile");
        app.Start([] {});
        Wait(app, [&] { return app.Data() != nullptr; });
        Check(!app.LastError(), "Startup failed");
        app.SetLanguage("zh-CN");
        Check(app.Tr("nav.settings") == "设置", "Chinese did not refresh immediately");
        app.SetLanguage("en-US");
        Check(app.Tr("nav.settings") == "Settings", "English did not refresh immediately");
        app.SetGame(GameFilter::POE2);
        Check(app.Data()->config.selectedGame == GameFilter::POE2, "Game state failed");
        const bool wasFavorite = app.Data()->config.favorites.contains("poecharm2");
        app.ToggleFavorite("poecharm2");
        Check(app.Data()->config.favorites.contains("poecharm2") != wasFavorite, "Star toggle failed");
        const auto dir = output / L"配置 中文 空格";
        std::filesystem::create_directories(dir);
        const auto exe = dir / L"fixture.exe";
        std::filesystem::copy_file(fixture, exe, std::filesystem::copy_options::overwrite_existing);
        const auto report = dir / L"launch-fixture.json";
        std::error_code ec;
        std::filesystem::remove(report, ec);
        app.ConfigureExecutable("poecharm2", exe);
        Wait(app, [&] { return app.Data()->installed.contains("poecharm2"); });
        Check(!std::filesystem::exists(report), "Locate unexpectedly autoexecuted the file");
        app.Launch("poecharm2", false);
        Wait(app, [&] { return app.StatusKey() == "status.opened"; });
        Wait(app, [&] { return std::filesystem::exists(report); });
        Check(app.Data()->config.recentTools.at("poecharm2").launchCount >= 1,
              "Successful launch missing recent state");
        const auto original = app.Data()->config.managedToolsDirectory;
        app.SetManagedDirectory("relative/bad");
        Wait(app, [&] { return app.LastError().has_value(); });
        Check(app.Data()->config.managedToolsDirectory == original, "Invalid install directory changed config");
        app.SetManagedDirectory(output / L"future installs");
        Wait(app, [&] { return !app.LastError(); });
        Check(std::filesystem::exists(exe), "Changing install folder moved existing external tool");
        app.RequestIcon("path-of-exile");
        Wait(app, [&] { return app.Data()->icons.contains("path-of-exile"); });
        if (argc > 4 && std::wstring_view(argv[4]) == L"--live-web") {
            app.Launch("poe-ninja", false);
            Wait(app, [&] { return app.StatusKey() == "status.opened" || app.LastError().has_value(); });
            Check(!app.LastError(), "Live ShellExecuteExW failed");
            std::cout << "LIVE WEB: Windows accepted poe.ninja via default browser; inspect browser page separately.\n";
        }
        const auto file = app.Data()->paths->ConfigDirectory() / L"settings.json";
        app.Stop();
        const auto saved = ConfigManager(file).Load();
        Check(saved && saved->language == "en-US" && saved->selectedGame == GameFilter::POE2 &&
                  saved->executablePaths.at("poecharm2") == exe && saved->recentTools.at("poecharm2").launchCount >= 1,
              "Shutdown lost config writes");
        std::cout << "Application flow: load -> language -> filter -> favorite -> locate/save -> actual launch -> "
                     "recent -> directory validation -> icon -> clean flush passed.\n";
        std::cout << "ManifestMs=" << app.Data()->manifestMs << " LocalizationMs=" << app.Data()->localizationMs
                  << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}

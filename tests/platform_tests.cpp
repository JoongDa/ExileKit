#include "icon_provider.h"
#include "launcher.h"
#include "paths.h"
#include "poetoolbox/config.h"
#include "utf.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <objbase.h>
#include <thread>
#include <windows.h>
using namespace poetoolbox;
namespace {
void Check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
struct Recorder : LaunchSystem {
    int web = 0, exe = 0;
    Status OpenUrl(std::wstring_view) override {
        ++web;
        return {};
    }
    Status StartProcess(const LaunchRequest &) override {
        ++exe;
        return {};
    }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 1;
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int exit = 0;
    try {
        const std::filesystem::path source(argv[1]), output = std::filesystem::absolute(argv[2]), fixture(argv[3]);
        std::filesystem::create_directories(output);
        auto paths = PathManager::Create(output / L"profile");
        Check(paths.has_value() && paths->EnsureDirectories().has_value(), "Data path creation failed");
        Check(std::filesystem::is_directory(paths->DefaultManagedToolsDirectory()), "Default Tools absent");
        const auto managed = output / L"中文 安装目录";
        Check(PathManager::ValidateManagedDirectory(managed).has_value(), "Unicode writable directory rejected");
        const auto blocker = output / L"not-a-directory";
        {
            std::ofstream f(blocker);
            f << "x";
        }
        Check(!PathManager::ValidateManagedDirectory(blocker) && !PathManager::ValidateManagedDirectory("relative"),
              "Invalid managed folder accepted");
        ConfigManager config(paths->ConfigDirectory() / L"settings.json");
        UserConfig c;
        c.language = "zh-CN";
        c.selectedGame = GameFilter::POE2;
        c.managedToolsDirectory = managed;
        c.executablePaths["test-tool"] = output / L"中文 路径" / L"程序.exe";
        c.favorites.insert("test-tool");
        c.recentTools["test-tool"] = {123456, 7};
        Check(config.Save(c).has_value(), "Config save failed");
        auto loaded = config.Load();
        Check(loaded && loaded->language == c.language && loaded->selectedGame == c.selectedGame &&
                  loaded->managedToolsDirectory == managed && loaded->executablePaths == c.executablePaths &&
                  loaded->favorites == c.favorites && loaded->recentTools.at("test-tool").launchCount == 7,
              "Config roundtrip failed");
        auto invalid = c;
        invalid.managedToolsDirectory = "relative";
        Check(!config.Save(invalid) && config.Load()->managedToolsDirectory == managed,
              "Invalid config destroyed previous settings");
        {
            std::ofstream f(paths->ConfigDirectory() / L"corrupt.json");
            f << "[broken";
        }
        Check(!ConfigManager(paths->ConfigDirectory() / L"corrupt.json").Load(), "Corrupt config accepted");
        Check(QuoteWindowsArgument(L"") == L"\"\"", "Empty argument quote failed");
        Check(QuoteWindowsArgument(L"a\\") == L"\"a\\\\\"", "Trailing slash quoting failed");
        Recorder recorder;
        ToolLauncher mocked(&recorder);
        ToolManifest web;
        web.id = "test-web";
        web.launch.url = "https://example.com/";
        Check(mocked.Launch(web).has_value() && recorder.web == 1, "Web boundary failed");
        web.launch.url = "file:///C:/bad.exe";
        Check(!mocked.Launch(web) && recorder.web == 1, "Unsafe URL reached system call");
        const auto dir = output / L"中文 空格";
        std::filesystem::create_directories(dir);
        const auto exe = dir / L"测试 程序.exe";
        std::filesystem::copy_file(fixture, exe, std::filesystem::copy_options::overwrite_existing);
        const auto report = dir / L"report.json";
        std::error_code ec;
        std::filesystem::remove(report, ec);
        ToolManifest app;
        app.id = "test-app";
        app.type = ToolType::Application;
        app.distribution = DistributionType::External;
        app.launch.workingDirectory = ".";
        const std::vector<std::string> arguments = {"中文 空格", "", "quote\"inside", "trailing\\", "a\\\\\"b"};
        app.launch.arguments = {Utf8(report.native())};
        app.launch.arguments.insert(app.launch.arguments.end(), arguments.begin(), arguments.end());
        app.launch.environment["POE_TOOLBOX_TEST"] = "测试 value";
        auto prepared = ToolLauncher::PrepareApplication(app, exe);
        Check(prepared && std::filesystem::equivalent(prepared->workingDirectory, dir),
              "Working directory preparation failed");
        app.riskLevel = RiskLevel::GameModifying;
        Check(!mocked.Launch(app, exe) && recorder.exe == 0, "Risk bypass reached syscall");
        Check(mocked.Launch(app, exe, true).has_value() && recorder.exe == 1, "Risk confirmation not respected");
        app.riskLevel = RiskLevel::Normal;
        Check(ToolLauncher().Launch(app, exe).has_value(), "Real CreateProcessW failed");
        for (int i = 0; i < 500 && !std::filesystem::exists(report); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        nlohmann::json child;
        {
            std::ifstream f(report);
            f >> child;
        }
        Check(child.at("arguments").get<std::vector<std::string>>() == arguments,
              "Actual CRT argv differs after quoting");
        Check(child.at("environment") == "测试 value", "Unicode environment lost");
        Check(std::filesystem::path(Utf16(child.at("workingDirectory").get<std::string>())).lexically_normal() ==
                  dir.lexically_normal(),
              "Actual working directory wrong");
        Check(!ToolLauncher().Launch(app, dir / L"missing.exe"), "Missing executable launch succeeded");
        IconProvider icons(paths->IconCacheDirectory());
        web.launch.url = "https://example.com/";
        web.icon = "resources/icons/generic-web.png";
        auto icon = icons.Load(web, {}, source);
        Check(icon && icon->width <= 64 && icon->height <= 64, "Bundled icon failed");
        std::filesystem::copy_file(source / L"resources/icons/generic-web.png",
                                   paths->IconCacheDirectory() / L"test-web.png",
                                   std::filesystem::copy_options::overwrite_existing);
        web.icon = "missing.png";
        Check(icons.Load(web, {}, source).has_value(), "Cache fallback after bad bundled icon failed");
        web.id = "missing-icon";
        Check(!icons.Load(web, {}, source), "Missing icon should request placeholder");
        std::cout << "Config/path protection, mock web launch, risk guard, real Unicode EXE/argv/env/cwd, "
                     "bundled/cache/failed icon passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        exit = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return exit;
}

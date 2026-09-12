#include "custom_shortcut.h"
#include "icon_provider.h"
#include "launcher.h"
#include "utf.h"
#include "poetoolbox/config.h"
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace poetoolbox;
namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Recorder : LaunchSystem {
    int websites = 0, applications = 0;
    std::wstring openedUrl;
    Status OpenUrl(std::wstring_view url) override {
        openedUrl = url;
        ++websites;
        return {};
    }
    Status StartProcess(const LaunchRequest &) override {
        ++applications;
        return {};
    }
};
void WriteShortcut(const std::filesystem::path &linkPath, const std::filesystem::path &executable,
                   const std::filesystem::path &directory, const std::vector<std::string> &arguments,
                   const std::filesystem::path &icon = {}, int iconIndex = 0) {
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))),
          "Cannot create fixture shortcut");
    Check(SUCCEEDED(link->SetPath(executable.c_str())), "Cannot set shortcut target");
    Check(SUCCEEDED(link->SetWorkingDirectory(directory.c_str())), "Cannot set shortcut working directory");
    std::wstring command;
    for (const auto &argument : arguments) {
        if (!command.empty())
            command += L' ';
        command += QuoteWindowsArgument(Utf16(argument));
    }
    Check(SUCCEEDED(link->SetArguments(command.c_str())), "Cannot set shortcut arguments");
    if (!icon.empty())
        Check(SUCCEEDED(link->SetIconLocation(icon.c_str(), iconIndex)), "Cannot set shortcut icon");
    Microsoft::WRL::ComPtr<IPersistFile> file;
    Check(SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(linkPath.c_str(), TRUE)), "Cannot save fixture shortcut");
}
nlohmann::json WaitForReport(const std::filesystem::path &report) {
    // The child creates and writes separately; wait for complete JSON, not just file existence.
    for (int i = 0; i < 500; ++i) {
        std::ifstream input(report);
        if (input) {
            const auto document = nlohmann::json::parse(input, nullptr, false);
            if (!document.is_discarded())
                return document;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Launched shortcut did not produce a report");
}
void CheckConfigBounds(const std::filesystem::path &output, const std::filesystem::path &executable,
                       const std::filesystem::path &directory, const std::vector<std::string> &arguments) {
    const std::string longDomain = std::string(540, 'a') + ".example";
    const auto longUrl = ParseCustomShortcut("https://" + longDomain, "custom-long-name");
    Check(longUrl && longUrl->name.size() == 512 && longUrl->target == "https://" + longDomain,
          "Long automatic URL name exceeded config limit or changed its target");
    UserConfig original;
    original.customTools[longUrl->id] = *longUrl;
    const auto longLink = output / (std::wstring(180, L'测') + L".lnk");
    if (longLink.native().size() < MAX_PATH) {
        WriteShortcut(longLink, executable, directory, arguments);
        const auto parsed = ParseCustomShortcut(Utf8(longLink.native()), "custom-long-link");
        Check(parsed && parsed->name == Utf8(std::wstring(170, L'测')) && !Utf16(parsed->name).empty(),
              "Chinese shortcut name was not truncated on a valid UTF-8 boundary");
        original.customTools[parsed->id] = *parsed;
    } else {
        std::cout << "Long Chinese LNK filename check skipped: test output path exceeds MAX_PATH.\n";
    }
    const ConfigManager store(output / L"bounded-names.json");
    Check(ConfigManager::Validate(original).has_value() && store.Save(original).has_value(),
          "Parsed automatic names prevented memory-only validation or config saving");
    const auto saved = store.Load();
    Check(saved && saved->customTools.size() == original.customTools.size() &&
              saved->customTools.at(longUrl->id).name == longUrl->name,
          "Bounded automatic names did not survive config roundtrip");
    auto oversized = original;
    for (int index = 0; index < 40; ++index) {
        const auto id = "large-" + std::to_string(index);
        oversized.customTools[id] = {id, "Website", ShortcutKind::Url,
                                     "https://example.com/?q=" + std::string(8000, 'a')};
    }
    const auto rejected = ConfigManager::Validate(oversized);
    const auto rejectedSave = store.Save(oversized);
    Check(!rejected && rejected.error().code == ErrorCode::InvalidConfig && !rejectedSave &&
              rejected.error().code == rejectedSave.error().code &&
              rejected.error().message == rejectedSave.error().message,
          "Oversized preflight and Save did not reject consistently");
    const auto retained = store.Load();
    Check(retained && retained->customTools.size() == original.customTools.size() && store.Save(original).has_value(),
          "Rejected candidate damaged the previous configuration or prevented later saves");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 1;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int exitCode = 0;
    try {
        Check(SUCCEEDED(com), "Cannot initialize fixture COM");
        const std::filesystem::path source(argv[1]), output = std::filesystem::absolute(argv[2]), fixture(argv[3]);
        const auto directory = output / L"中文 快捷方式";
        const auto workingDirectory = directory / L"单独 工作目录";
        std::filesystem::create_directories(workingDirectory);
        const auto executable = directory / L"我的 程序.exe";
        std::filesystem::copy_file(fixture, executable, std::filesystem::copy_options::overwrite_existing);

        const auto url = ParseCustomShortcut("  HTTPS://example.com/path?q=test#anchor \r\n", "custom-url");
        Check(url && url->kind == ShortcutKind::Url && url->target == "https://example.com/path?q=test#anchor" &&
                  url->name == "example.com", "URL normalization or domain fallback failed");
        const auto bare = ParseCustomShortcut("example.com/path", "custom-bare");
        Check(bare && bare->target == "https://example.com/path", "Bare domain was not recognized");
        for (const auto *bad : {"", "not a website", "http://example.com", "javascript:alert(1)",
                               "file:///C:/tool.exe", "https://user@example.com", "C:\\missing.exe",
                               "C:\\tool.exe --argument", "relative\\tool.exe", "https://example.com:99999/"}) {
            const auto result = ParseCustomShortcut(bad, "custom-invalid");
            Check(!result && result.error().code == ErrorCode::InvalidShortcut, "Invalid input returned no shortcut error");
        }
        Check(!ParseCustomShortcut(std::string("https://example.com\0bad", 23), "custom-invalid"), "NUL accepted");
        Check(!ParseCustomShortcut(std::string("https://example.com/\xff", 21), "custom-invalid"), "Invalid UTF-8 accepted");
        Check(!ParseCustomShortcut("https://example.com/", "../bad-id"), "Unsafe identifier accepted");
        Recorder recorder;
        ToolLauncher mocked(&recorder);
        Check(mocked.LaunchCustom(*url).has_value() && recorder.websites == 1 &&
                  recorder.openedUrl == L"https://example.com/path?q=test#anchor", "Custom URL launch boundary failed");
        auto invalidUrl = *url;
        invalidUrl.target = "file:///C:/tool.exe";
        Check(!mocked.LaunchCustom(invalidUrl) && recorder.websites == 1, "Malformed stored URL reached Windows");

        const auto directReport = directory / L"launch-fixture.json";
        std::error_code error;
        std::filesystem::remove(directReport, error);
        const auto application = ParseCustomShortcut("\"" + Utf8(executable.native()) + "\"", "custom-exe");
        Check(application && application->kind == ShortcutKind::Executable &&
                  application->name == "ExileKit Shortcut Fixture", "EXE ProductName priority or recognition failed");
        const auto noResources = directory / L"无版本信息.exe";
        { std::ofstream file(noResources); file << "No version resource"; }
        const auto fallback = ParseCustomShortcut(Utf8(noResources.native()), "custom-fallback");
        Check(fallback && fallback->name == "无版本信息", "Missing version metadata lost filename fallback");
        Check(!std::filesystem::exists(directReport), "Parsing executed an application");
        Check(ToolLauncher().LaunchCustom(*application).has_value(), "Custom EXE CreateProcessW failed");
        const auto direct = WaitForReport(directReport);
        Check(direct.at("arguments").empty() &&
                  std::filesystem::equivalent(Utf16(direct.at("workingDirectory").get<std::string>()), directory),
              "Custom EXE working directory or arguments changed");

        const auto linkReport = workingDirectory / L"快捷方式 结果.json";
        std::filesystem::remove(linkReport, error);
        const std::vector<std::string> payload = {"中文 空格", "", "quote\"inside", "trailing\\", "a\\\\\"b"};
        std::vector<std::string> arguments{Utf8(linkReport.native())};
        arguments.insert(arguments.end(), payload.begin(), payload.end());
        const auto linkPath = directory / L"用户自定义 名称.lnk";
        const auto iconPath = source / L"resources/icons/generic-web.png";
        WriteShortcut(linkPath, executable, workingDirectory, arguments, iconPath, -7);
        const auto shortcut = ParseCustomShortcut(Utf8(linkPath.native()), "custom-lnk");
        const auto details = InspectWindowsShortcut(linkPath);
        Check(shortcut && shortcut->kind == ShortcutKind::WindowsShortcut && shortcut->name == "用户自定义 名称",
              "LNK display name or type was lost");
        Check(details && std::filesystem::equivalent(details->executable, executable) &&
                  std::filesystem::equivalent(details->workingDirectory, workingDirectory) &&
                  std::filesystem::equivalent(details->iconPath, iconPath) && details->iconIndex == -7 &&
                  details->arguments == arguments, "LNK target, arguments, working directory, or icon metadata was lost");
        Check(!std::filesystem::exists(linkReport), "Inspecting a shortcut executed its target");
        const auto ownIcon = IconProvider(output / L"icons").LoadCustom(*shortcut);
        const auto expectedIcon = IconProvider::DecodeFile(iconPath);
        Check(ownIcon && expectedIcon && ownIcon->bgra == expectedIcon->bgra,
              "LNK did not prefer its own icon over the target executable");
        Check(!IconProvider(output / L"icons").LoadCustom(*application),
              "Icon-free fixture should use the UI placeholder");
        const auto noIconLink = directory / L"no-icon.lnk";
        WriteShortcut(noIconLink, executable, workingDirectory, {}, directory / L"missing.ico");
        const auto noIconShortcut = ParseCustomShortcut(Utf8(noIconLink.native()), "custom-no-icon");
        Check(noIconShortcut && !IconProvider(output / L"icons").LoadCustom(*noIconShortcut),
              "Missing shortcut and target icons should use the UI placeholder");
        Check(!std::filesystem::exists(linkReport), "Reading shortcut icons executed the target");
        CheckConfigBounds(output, executable, workingDirectory, arguments);
        Check(ToolLauncher().LaunchCustom(*shortcut).has_value(), "Real LNK launch failed");
        const auto child = WaitForReport(linkReport);
        Check(child.at("arguments").get<std::vector<std::string>>() == payload &&
                  std::filesystem::equivalent(Utf16(child.at("workingDirectory").get<std::string>()), workingDirectory),
              "Real LNK launch lost Unicode, quoted, empty arguments or working directory");

        const auto broken = directory / L"broken.lnk";
        WriteShortcut(broken, directory / L"absent.exe", workingDirectory, {});
        Check(!ParseCustomShortcut(Utf8(broken.native()), "custom-broken"), "Broken shortcut accepted");
        const auto invalid = directory / L"invalid.lnk";
        { std::ofstream file(invalid); file << "not a Windows shortcut"; }
        Check(!InspectWindowsShortcut(invalid), "Invalid shortcut data accepted");
        auto missing = *application;
        missing.target = Utf8((directory / L"absent.exe").native());
        Check(!mocked.LaunchCustom(missing) && recorder.applications == 0, "Missing application reached process API");
        std::cout << "Custom URL/EXE/LNK recognition, real Unicode/argv/cwd launch, bounded names/config preflight, "
                     "no execution during parse, and malformed input passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        exitCode = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return exitCode;
}

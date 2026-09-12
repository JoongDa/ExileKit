#include "utf.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    nlohmann::json report;
    report["arguments"] = nlohmann::json::array();
    for (int i = 2; i < argc; ++i)
        report["arguments"].push_back(poetoolbox::Utf8(argv[i]));
    wchar_t cwd[32768]{};
    GetCurrentDirectoryW(32768, cwd);
    report["workingDirectory"] = poetoolbox::Utf8(cwd);
    wchar_t env[1024]{};
    GetEnvironmentVariableW(L"POE_TOOLBOX_TEST", env, 1024);
    report["environment"] = poetoolbox::Utf8(env);
    std::ofstream file(argc < 2 ? std::filesystem::path(L"launch-fixture.json") : std::filesystem::path(argv[1]),
                       std::ios::binary);
    file << report.dump();
    file.close();
    LocalFree(argv);
    return file ? 0 : 1;
}

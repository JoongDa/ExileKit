#include "application.h"
#include "shell/main_window.h"
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // PerMonitorV2 is declared in the embedded manifest, before any HWND exists.
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        CoUninitialize();
        return 1;
    }
    int result = 1;
    {
        poetoolbox::Logger logger;
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) {
            CoUninitialize();
            return 1;
        }
        executable.resize(length);
        std::filesystem::path dataRoot;
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i + 1 < argc; ++i)
                if (std::wstring_view(argv[i]) == L"--data-dir")
                    dataRoot = argv[++i];
            LocalFree(argv);
        }
        poetoolbox::ApplicationServices services(std::filesystem::path(executable).parent_path(), dataRoot);
        poetoolbox::ui::MainWindow window(logger, services);
        result = window.Run(instance, show, IDI_EXILEKIT);
    }
    CoUninitialize();
    return result;
}

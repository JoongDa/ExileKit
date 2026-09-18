// Exercises this application's real HWNDs, tooltip and popup menus in an isolated profile.
#include "application.h"
#include "resource.h"
#include "shell/main_window.h"
#include <commctrl.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace poetoolbox;
namespace {
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate> void Wait(Predicate predicate, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
HWND Find(DWORD thread, const wchar_t *name) {
    struct Query {
        const wchar_t *name;
        HWND found = nullptr;
    } query{name};
    EnumThreadWindows(
        thread,
        [](HWND window, LPARAM data) -> BOOL {
            auto &q = *reinterpret_cast<Query *>(data);
            wchar_t name[100]{};
            GetClassNameW(window, name, 100);
            if (std::wstring_view(name) == q.name) {
                q.found = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&query));
    return query.found;
}
struct Popup {
    HWND window;
    HMENU menu;
    HWND owner;
};
void VerifyIcon(HICON icon, int width, int height) {
    Require(icon != nullptr, "Window icon is missing.");
    ICONINFO info{};
    Require(GetIconInfo(icon, &info), "Cannot inspect window icon.");
    BITMAP bitmap{};
    GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    BITMAPINFO format{};
    format.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    const auto dc = CreateCompatibleDC(nullptr);
    const auto lines = GetDIBits(dc, info.hbmColor, 0, height, pixels.data(), &format, DIB_RGB_COLORS);
    DeleteDC(dc);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    Require(bitmap.bmWidth == width && bitmap.bmHeight == height, "Window icon loaded at the wrong size.");
    Require(lines == height, "Cannot inspect window icon alpha.");
    const auto alpha = [&](int x, int y) { return pixels[(static_cast<size_t>(y) * width + x) * 4 + 3]; };
    Require(alpha(0, 0) == 0 && alpha(width - 1, height - 1) == 0 && alpha(width / 2, height / 4) == 0,
            "Window icon still contains an opaque background.");
    bool opaque = false, feathered = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        opaque |= pixels[i] == 255;
        feathered |= pixels[i] > 0 && pixels[i] < 255;
    }
    Require(opaque && feathered, "Window icon lost its artwork or antialiased edges.");
}
void VerifyWindowIcons(HWND window) {
    const auto systemDpi = GetDpiForSystem();
    VerifyIcon(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON)),
               GetSystemMetricsForDpi(SM_CXICON, systemDpi), GetSystemMetricsForDpi(SM_CYICON, systemDpi));
    VerifyIcon(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM)),
               GetSystemMetricsForDpi(SM_CXSMICON, systemDpi), GetSystemMetricsForDpi(SM_CYSMICON, systemDpi));
    const auto verify = [&](UINT dpi) {
        const auto bigIcon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_BIG, 0));
        const auto smallIcon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
        Require(bigIcon != smallIcon, "Small and big window icons share a handle.");
        VerifyIcon(bigIcon, GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi));
        VerifyIcon(smallIcon, GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi));
    };
    const auto originalDpi = GetDpiForWindow(window);
    verify(originalDpi);
    RECT rect{};
    GetWindowRect(window, &rect);
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u, 288u}) {
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        verify(dpi);
    }
    SendMessageW(window, WM_DPICHANGED, MAKELONG(originalDpi, originalDpi), reinterpret_cast<LPARAM>(&rect));
}
Popup OpenMenu(HWND window, DWORD thread) {
    POINT point{MulDiv(300, GetDpiForWindow(window), 96), MulDiv(180, GetDpiForWindow(window), 96)};
    ClientToScreen(window, &point);
    PostMessageW(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window), MAKELPARAM(point.x, point.y));
    HWND popup = nullptr;
    Wait(
        [&] {
            popup = Find(thread, L"#32768");
            return popup != nullptr;
        },
        "Context menu did not open.");
    MENUBARINFO info{sizeof(info)};
    Require(GetMenuBarInfo(popup, OBJID_CLIENT, 0, &info) && info.hMenu, "Cannot inspect native context menu.");
    return {popup, info.hMenu, window};
}
int MenuIndex(Popup popup, const wchar_t *name) {
    for (int i = 0; i < GetMenuItemCount(popup.menu); ++i) {
        wchar_t text[256]{};
        GetMenuStringW(popup.menu, i, text, 256, MF_BYPOSITION);
        if (std::wstring_view(text) == name)
            return i;
    }
    return -1;
}
void Choose(Popup popup, const wchar_t *label) {
    const auto index = MenuIndex(popup, label);
    Require(index >= 0, "Required menu action is missing.");
    RECT rect{};
    Require(GetMenuItemRect(nullptr, popup.menu, index, &rect), "Cannot locate native menu item.");
    POINT point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    SetCursorPos(point.x, point.y);
    Require(WindowFromPoint(point) == popup.window, "Test menu is occluded; refusing to click another window.");
    INPUT clicks[2]{};
    clicks[0].type = clicks[1].type = INPUT_MOUSE;
    clicks[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    clicks[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    Require(SendInput(2, clicks, sizeof(INPUT)) == 2, "Cannot click own test menu.");
    Wait([&] { return !IsWindow(popup.window); }, "Menu selection did not close popup.");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 3 && argc != 4)
        return 1;
    const bool iconsOnly = argc == 4 && std::wstring_view(argv[3]) == L"--icons-only";
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 1;
    try {
        const std::filesystem::path root(argv[1]), profile = std::filesystem::absolute(argv[2]);
        UserConfig config;
        config.language = "en-US";
        HomeService::Add(
            config, "poe-ninja",
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        Require(ConfigManager(profile / L"Config/settings.json").Save(config).has_value(), "Cannot seed profile.");
        Logger logger;
        ApplicationServices services(root, profile);
        ui::MainWindow main(logger, services);
        const auto thread = GetCurrentThreadId();
        std::string failure;
        std::jthread driver([&] {
            HWND window = nullptr;
            try {
                Wait(
                    [&] {
                        window = Find(thread, L"POEToolbox.MainWindow");
                        return window && (iconsOnly ? GetDlgItem(window, 1001) != nullptr
                                                    : GetPropW(window, L"POEToolbox.RegistryReady") != nullptr);
                    },
                    "Application did not become ready.");
                VerifyWindowIcons(window);
                if (iconsOnly) {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    return;
                }
                Require(!GetDlgItem(window, 1002), "Toolbar Add button must be removed.");
                const UINT dpi = GetDpiForWindow(window);
                const auto point = MAKELPARAM(MulDiv(300, dpi, 96), MulDiv(180, dpi, 96));
                POINT cursor{};
                GetCursorPos(&cursor);
                struct RestoreCursor {
                    POINT point;
                    ~RestoreCursor() { SetCursorPos(point.x, point.y); }
                } restore{cursor};
                POINT hover{MulDiv(300, dpi, 96), MulDiv(180, dpi, 96)};
                ClientToScreen(window, &hover);
                SetForegroundWindow(window);
                SetCursorPos(hover.x, hover.y);
                SendMessageW(window, WM_MOUSEMOVE, 0, point);
                const auto tooltip = Find(thread, TOOLTIPS_CLASSW);
                Require(tooltip != nullptr, "Native tooltip missing.");
                wchar_t text[2048]{};
                TOOLINFOW tip{sizeof(tip)};
                tip.hwnd = window;
                tip.uId = 1;
                tip.lpszText = text;
                SendMessageW(tooltip, TTM_GETTEXTW, std::size(text), reinterpret_cast<LPARAM>(&tip));
                Require(std::wstring_view(text).find(L"Economy, prices and character builds.") != std::wstring::npos,
                        "Hover tooltip does not contain description.");
                Wait(
                    [&] {
                        SendMessageW(window, WM_MOUSEMOVE, 0, point);
                        SendMessageW(tooltip, TTM_POPUP, 0, 0);
                        return IsWindowVisible(tooltip);
                    },
                    "Native hover popup was not shown.");
                SendMessageW(window, WM_MOUSELEAVE, 0, 0);
                Require(!IsWindowVisible(tooltip), "Tooltip survived mouse leave.");
                auto popup = OpenMenu(window, thread);
                Require(MenuIndex(popup, L"Open") >= 0 && MenuIndex(popup, L"Remove from Home") >= 0 &&
                            MenuIndex(popup, L"Locate") < 0,
                        "Web menu has incorrect actions.");
                Choose(popup, L"Pin to Home");
                Wait(
                    [&] {
                        auto saved = ConfigManager(profile / L"Config/settings.json").Load();
                        return saved && saved->home.at("poe-ninja").pinned;
                    },
                    "Pin menu action was not saved.");
                popup = OpenMenu(window, thread);
                Require(MenuIndex(popup, L"Pin to Home") < 0, "Pinned tool still offers Pin.");
                Choose(popup, L"Unpin");
                Wait(
                    [&] {
                        auto saved = ConfigManager(profile / L"Config/settings.json").Load();
                        return saved && !saved->home.at("poe-ninja").pinned;
                    },
                    "Unpin menu action was not saved.");
                popup = OpenMenu(window, thread);
                Require(MenuIndex(popup, L"Pin to Home") >= 0, "Unpin did not update menu.");
                Choose(popup, L"Remove from Home");
                Wait(
                    [&] {
                        auto saved = ConfigManager(profile / L"Config/settings.json").Load();
                        return saved && saved->home.at("poe-ninja").hiddenFromHome;
                    },
                    "Remove menu action was not saved.");
                // Removing the last tool returns to Welcome; the grid Add item must open the real dialog.
                PostMessageW(window, WM_LBUTTONDOWN, 0, MAKELPARAM(MulDiv(300, dpi, 96), MulDiv(280, dpi, 96)));
                HWND dialog = nullptr;
                Wait(
                    [&] {
                        dialog = Find(thread, L"#32770");
                        return dialog != nullptr;
                    },
                    "Home Add item did not open dialog.");
                Wait(
                    [&] { return IsWindowVisible(dialog) && GetDlgItem(dialog, IDCANCEL) && GetDlgItem(dialog, 2101); },
                    "Shortcut dialog did not finish initialization.");
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                Wait([&] { return !IsWindow(dialog); }, "Shortcut dialog did not close.");
            } catch (const std::exception &error) {
                failure = error.what();
            }
            if (window) {
                SendMessageW(window, WM_CANCELMODE, 0, 0);
                PostMessageW(window, WM_CLOSE, 0, 0);
            } else {
                PostThreadMessageW(thread, WM_QUIT, 1, 0);
            }
        });
        const auto exit = main.Run(GetModuleHandleW(nullptr), iconsOnly ? SW_HIDE : SW_SHOWNORMAL, IDI_EXILEKIT);
        driver.join();
        Require(failure.empty(), failure.c_str());
        Require(exit == 0, "Window did not close normally.");
        std::cout << (iconsOnly ? "Class/window icon sizes, transparency and DPI changes passed.\n"
                               : "Native hover popup, dynamic Pin/Unpin/Remove menu and Home Add dialog passed.\n");
        result = 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    CoUninitialize();
    return result;
}

#include "application.h"
#include "pages/library_page.h"
#include "renderer/renderer.h"
#include "utf.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void Check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("D2D/WIC failed: " + std::to_string(hr));
}
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return 1;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return 1;
    int result = 0;
    try {
        const std::filesystem::path output(argv[1]), root(argv[2]);
        std::filesystem::create_directories(output);
        const auto profile = std::filesystem::absolute(output / L"test-profile-m3");
        if (!poetoolbox::ConfigManager(profile / L"Config/settings.json").Save(poetoolbox::UserConfig{}))
            throw std::runtime_error("Cannot seed preview profile.");
        poetoolbox::ApplicationServices services(root, profile);
        services.Start([] {});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!services.Data() && std::chrono::steady_clock::now() < deadline) {
            services.Drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!services.Data() || services.Data()->registry.GetTools().size() != 15)
            throw std::runtime_error("Registry did not load real tools.");
        services.RequestIcon("path-of-exile");
        while (!services.Data()->icons.contains("path-of-exile") && std::chrono::steady_clock::now() < deadline) {
            services.Drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!services.Data()->icons.contains("path-of-exile"))
            throw std::runtime_error("Bundled tool icon did not load.");
        poetoolbox::ui::Renderer renderer;
        Check(renderer.Initialize());
        poetoolbox::ui::LibraryPage page(services);
        for (const auto *language : {"en-US", "zh-CN"}) {
            services.SetLanguage(language);
            page.SetView(poetoolbox::ui::LibraryView::Home);
            page.Refresh(true);
            page.Resize(1200, 760);
            if (!services.HomeIds().empty())
                throw std::runtime_error("Fresh Home is not empty.");
            Require(page.Click({300, 280}).kind == poetoolbox::ui::PageActionKind::AddShortcut,
                    "Empty Home must expose Add Shortcut inside the grid.");
            Require(page.ContextTool({300, 280}).empty() && page.VisibleToolIds().empty(),
                    "Add Shortcut must not be treated as a launchable tool or request an icon.");
            const auto addTip = page.Tooltip({300, 280});
            Require(addTip && addTip->text.starts_with(poetoolbox::Utf16(services.Tr("shortcut.add"))),
                    "Add Shortcut tooltip must use the current language.");
            page.MouseMove({-1, -1});
            Check(renderer.BeginOffscreen(1200, 760, 96));
            page.Draw(renderer);
            Check(renderer.End());
            Check(renderer.SavePng((output / (std::string(language) + "-welcome.png")).c_str()));
        }
        services.AddToHome("path-of-exile");
        services.PinToHome("poe-ninja");
        using poetoolbox::ui::LibraryView;
        using poetoolbox::ui::PageActionKind;
        for (const auto view : {LibraryView::Home, LibraryView::POE, LibraryView::POE2}) {
            page.SetView(view);
            page.Resize(1200, 760);
            page.SetSearch("poe.ninja character");
            Require(page.ToolCount() == 1, "Search must filter the shared icon grid.");
            const auto open = page.Click({300, 180});
            Require(open.kind == PageActionKind::Open && !open.id.empty(), "Grid click must open a web tool.");
            Require(page.ContextTool({300, 180}) == open.id, "Right-click target must match the icon item.");
            const auto tip = page.Tooltip({300, 180});
            const auto *tool = services.Data()->registry.FindTool(open.id);
            Require(tip && tool &&
                        tip->text.find(poetoolbox::Utf16(poetoolbox::Localize(
                            tool->manifest.description, services.Data()->locale.GetCurrentLanguage()))) !=
                            std::wstring::npos,
                    "Hover must expose the tool description.");
            Require(!page.Tooltip({300, 40}), "Search/header must not expose a tool tooltip.");
            Require(page.Click({430, 180}).kind ==
                        (view == LibraryView::Home ? PageActionKind::AddShortcut : PageActionKind::None),
                    "Only Home may append Add Shortcut to search results.");
            page.SetSearch("no-such-tool-404");
            Require(page.VisibleToolIds().empty() && page.ToolCount() == 0, "No-match search must hide tools.");
            Require(page.Click({300, 230}).kind ==
                        (view == LibraryView::Home ? PageActionKind::AddShortcut : PageActionKind::None),
                    "Home must retain Add Shortcut even when no tools match search.");
            page.SetSearch("");
        }
        page.SetView(LibraryView::POE);
        page.SetSearch("PoE Overlay");
        Require(page.Click({300, 180}).kind == PageActionKind::Locate,
                "Unconfigured applications must retain the existing Locate action.");
        page.SetSearch("");
        services.RemoveFromHome("path-of-exile");
        page.SetView(LibraryView::Home);
        Require(page.ToolCount() == 1 && page.ContextTool({300, 180}) == "poe-ninja", "Remove must update Home grid.");
        services.Unpin("poe-ninja");
        page.Refresh();
        Require(!services.Data()->config.home.at("poe-ninja").pinned, "Unpin must retain the existing Home model.");
        services.AddToHome("path-of-exile");
        services.PinToHome("poe-ninja");
        for (const auto *language : {"en-US", "zh-CN"}) {
            services.SetLanguage(language);
            for (const auto view : {poetoolbox::ui::LibraryView::Home, poetoolbox::ui::LibraryView::POE,
                                    poetoolbox::ui::LibraryView::POE2, poetoolbox::ui::LibraryView::Settings}) {
                page.SetView(view);
                for (const UINT dpi : {96u, 120u, 144u, 168u, 192u})
                    for (const UINT width : {720u, 1200u}) {
                        page.ScrollTo(0);
                        page.Resize(static_cast<float>(width), 760);
                        Require(!page.Tooltip({219, 180}), "Sidebar must never hit a grid item.");
                        page.MouseMove({-1, -1});
                        Check(renderer.BeginOffscreen(width * dpi / 96, 760 * dpi / 96, static_cast<float>(dpi)));
                        page.Draw(renderer);
                        Check(renderer.End());
                        auto file = output / (std::string(language) + "-view" + std::to_string(static_cast<int>(view)) +
                                              "-" + std::to_string(width) + "-" + std::to_string(dpi) + ".png");
                        Check(renderer.SavePng(file.c_str()));
                    }
            }
        }
        std::cout << "Welcome and all four M3 views rendered in two languages at five DPI scales.\n";
        std::cout << "ManifestMs=" << services.Data()->manifestMs
                  << " LocalizationMs=" << services.Data()->localizationMs << '\n';
        services.Stop();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}

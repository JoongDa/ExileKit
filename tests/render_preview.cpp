#include "application.h"
#include "pages/library_page.h"
#include "renderer/renderer.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void Check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("D2D/WIC failed: " + std::to_string(hr));
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
            Check(renderer.BeginOffscreen(1200, 760, 96));
            page.Draw(renderer);
            Check(renderer.End());
            Check(renderer.SavePng((output / (std::string(language) + "-welcome.png")).c_str()));
        }
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

#include "icon_provider.h"
#include "custom_shortcut.h"
#include "web_metadata.h"
#include "application.h"
#include "renderer/renderer.h"
#include "utf.h"
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstring>
#include <stdexcept>
using namespace poetoolbox;
namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Hr(HRESULT hr) {
    Check(SUCCEEDED(hr), "WIC / D2D failed");
}
std::vector<uint8_t> Bytes(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return {(std::istreambuf_iterator<char>(input)), {}};
}
void Write(const std::filesystem::path &file, const std::vector<uint8_t> &bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    Check(bool(out), "Cannot write ICO fixture");
}
uint32_t U32(const uint8_t *p) {
    uint32_t value;
    memcpy(&value, p, 4);
    return value;
}
void Put32(uint8_t *p, uint32_t value) {
    memcpy(p, &value, 4);
}
void FilterIco(const std::filesystem::path &source, const std::filesystem::path &output, bool smallOnly) {
    auto bytes = Bytes(source);
    Check(bytes.size() >= 6 && bytes[2] == 1, "Missing real ICO");
    std::vector<size_t> entries;
    for (size_t i = 0; i < bytes[4]; ++i) {
        const size_t offset = 6 + i * 16;
        const auto size = bytes[offset] ? bytes[offset] : 256;
        if (size == 16 || (!smallOnly && (size == 32 || size == 48 || size == 256)))
            entries.push_back(offset);
    }
    Check(entries.size() == (smallOnly ? 1u : 4u), "ICO must contain 16/32/48/256 frames");
    std::vector<uint8_t> result(6 + entries.size() * 16);
    result[2] = 1;
    result[4] = static_cast<uint8_t>(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto offset = entries[i];
        const auto length = U32(bytes.data() + offset + 8), start = U32(bytes.data() + offset + 12);
        Check(start + length <= bytes.size(), "Invalid ICO fixture");
        memcpy(result.data() + 6 + i * 16, bytes.data() + offset, 16);
        Put32(result.data() + 6 + i * 16 + 12, static_cast<uint32_t>(result.size()));
        result.insert(result.end(), bytes.begin() + start, bytes.begin() + start + length);
    }
    Write(output, result);
}
void VerifyAppIco(const std::filesystem::path &source) {
    const auto bytes = Bytes(source);
    const std::vector<unsigned> sizes{16, 20, 24, 28, 32, 40, 48, 56, 64, 96, 128, 256};
    Check(bytes.size() >= 6 + sizes.size() * 16 && bytes[4] == sizes.size() && bytes[5] == 0,
          "Application ICO is missing native DPI sizes");
    for (size_t i = 0; i < sizes.size(); ++i) {
        const auto entry = bytes.data() + 6 + i * 16;
        const auto size = sizes[i];
        Check((entry[0] ? entry[0] : 256u) == size && entry[0] == entry[1] && entry[6] == 32,
              "Application ICO frame has the wrong dimensions/depth");
        const auto length = U32(entry + 8), offset = U32(entry + 12);
        Check(static_cast<size_t>(offset) + length <= bytes.size(), "Invalid application ICO frame");
        const auto image = IconProvider::DecodeBytes(std::span(bytes).subspan(offset, length), size);
        Check(image && image->width == size && image->height == size, "Application ICO frame cannot be decoded");
        const auto alpha = [&](unsigned x, unsigned y) { return image->bgra[(y * size + x) * 4 + 3]; };
        Check(alpha(0, 0) == 0 && alpha(size - 1, size - 1) == 0 && alpha(size / 2, size / 4) == 0,
              "Application ICO has an opaque background");
    }
}
void Report(std::string_view name, const IconPixels &icon, float dpi) {
    std::cout << name << " dpi=" << dpi << " requested=" << icon.requestedPx
              << " shell-request=" << IconProvider::SourcePixels(icon.requestedPx) << " source=" << icon.source << ' '
              << icon.sourceWidth << 'x' << icon.sourceHeight << " cache=" << icon.width << 'x' << icon.height << '\n';
}
// Reproduce the old ExtractIconEx -> DrawIconEx(64) pipeline for a measured visual baseline.
IconPixels Legacy(const std::filesystem::path &file, int index) {
    HICON icon = nullptr;
    Check(ExtractIconExW(file.c_str(), index, &icon, nullptr, 1) == 1 && icon, "Legacy extraction failed");
    ICONINFO details{};
    Check(GetIconInfo(icon, &details), "Cannot measure old HICON");
    BITMAP bitmapInfo{};
    GetObjectW(details.hbmColor ? details.hbmColor : details.hbmMask, sizeof(bitmapInfo), &bitmapInfo);
    const auto width = static_cast<uint32_t>(bitmapInfo.bmWidth);
    const auto height = static_cast<uint32_t>(details.hbmColor ? bitmapInfo.bmHeight : bitmapInfo.bmHeight / 2);
    if (details.hbmColor)
        DeleteObject(details.hbmColor);
    if (details.hbmMask)
        DeleteObject(details.hbmMask);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 64, -64, 1, 32, BI_RGB};
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Check(dc && bitmap, "Cannot allocate legacy bitmap");
    auto previous = SelectObject(dc, bitmap);
    ZeroMemory(bits, 64 * 64 * 4);
    Check(DrawIconEx(dc, 0, 0, icon, 64, 64, 0, nullptr, DI_NORMAL), "Cannot draw legacy icon");
    IconPixels pixels{64, 64, std::vector<uint8_t>(64 * 64 * 4)};
    memcpy(pixels.bgra.data(), bits, pixels.bgra.size());
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyIcon(icon);
    pixels.sourceWidth = width;
    pixels.sourceHeight = height;
    pixels.source = "old ExtractIconEx";
    return pixels;
}
void SavePreview(const std::filesystem::path &output, float dpi, const IconPixels &old, const IconPixels &current) {
    ui::Renderer renderer;
    Hr(renderer.Initialize());
    Hr(renderer.BeginOffscreen(static_cast<UINT>(440 * dpi / 96), static_cast<UINT>(150 * dpi / 96), dpi));
    renderer.Text(L"Old 32px source", {12, 8, 210, 38}, 0xffffff);
    renderer.Text(L"Shell / WIC", {230, 8, 438, 38}, 0xffffff);
    // The old renderer enlarged its 64px bitmap to 72 DIP. Resample only in this baseline harness.
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    Hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    Hr(factory->CreateBitmapFromMemory(old.width, old.height, GUID_WICPixelFormat32bppPBGRA, old.width * 4,
                                       static_cast<UINT>(old.bgra.size()), const_cast<BYTE *>(old.bgra.data()),
                                       &bitmap));
    Hr(factory->CreateBitmapScaler(&scaler));
    const auto px = IconProvider::TargetPixels(72, dpi);
    Hr(scaler->Initialize(bitmap.Get(), px, px, WICBitmapInterpolationModeLinear));
    auto baseline = std::make_shared<IconPixels>(IconPixels{px, px, std::vector<uint8_t>(px * px * 4)});
    Hr(scaler->CopyPixels(nullptr, px * 4, static_cast<UINT>(baseline->bgra.size()), baseline->bgra.data()));
    Check(renderer.Image(baseline, {60, 48, 132, 120}), "Old render failed");
    Check(renderer.Image(std::make_shared<IconPixels>(current), {278, 48, 350, 120}), "New render failed");
    Hr(renderer.End());
    Hr(renderer.SavePng(output.c_str()));
}
void VerifyNoRenderUpscale(const std::filesystem::path &output) {
    auto pixels = std::make_shared<IconPixels>(IconPixels{16, 8, std::vector<uint8_t>(16 * 8 * 4)});
    for (size_t i = 0; i < pixels->bgra.size(); i += 4) {
        pixels->bgra[i + 2] = 255;
        pixels->bgra[i + 3] = 255;
    }
    ui::Renderer renderer;
    Hr(renderer.Initialize());
    Hr(renderer.BeginOffscreen(144, 144, 192));
    Check(renderer.Image(pixels, {0, 0, 72, 72}), "Small source rendering failed");
    Hr(renderer.End());
    const auto file = output / L"no-upscale-200.png";
    Hr(renderer.SavePng(file.c_str()));
    const auto image = IconProvider::DecodeFile(file, 144);
    Check(image && image->width == 144, "Cannot inspect final render pixels");
    unsigned red = 0;
    for (size_t i = 0; i < image->bgra.size(); i += 4)
        if (image->bgra[i] == 0 && image->bgra[i + 1] == 0 && image->bgra[i + 2] == 255)
            ++red;
    Check(red == 16 * 8, "Direct2D enlarged or distorted a small source at 200% DPI");
}
template <class F> void Wait(ApplicationServices &app, F ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        app.Drain();
        if (ready())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("DPI cache completion timed out");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 4 && argc != 6)
        return 2;
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int status = 0;
    try {
        const std::filesystem::path root(argv[1]), output = std::filesystem::absolute(argv[2]), exe(argv[3]);
        std::filesystem::create_directories(output);
        VerifyAppIco(root / L"apps/toolbox/exilekit.ico");
        const auto sparse = output / L"16-32-48-256.ico", tinyFile = output / L"16-only.ico";
        FilterIco(root / L"apps/toolbox/exilekit.ico", sparse, false);
        FilterIco(root / L"apps/toolbox/exilekit.ico", tinyFile, true);
        CustomTool tool{"real-exe", "ExileKit", ShortcutKind::Executable, Utf8(exe.native())};
        auto oldExe = Legacy(exe, 0);
        Report("EXE baseline", oldExe, 96);
        for (float dpi : {96.0f, 144.0f, 192.0f}) {
            const auto px = IconProvider::TargetPixels(72, dpi);
            Check(px == (dpi == 96 ? 72u : dpi == 144 ? 108u : 144u), "DIP to pixels incorrect");
            auto ico = IconProvider::DecodeFile(sparse, px);
            Check(ico && ico->sourceWidth == 256 && ico->width == px && ico->height == px,
                  "Wrong multi-frame ICO selection");
            Report("ICO", *ico, dpi);
            const auto undersized = IconProvider::DecodeFile(tinyFile, px);
            Check(undersized && undersized->width == 16 && undersized->height == 16, "Small source was upscaled");
            const auto app = IconProvider(output).LoadCustom(tool, px);
            Check(app && app->source == "Shell" && app->sourceWidth >= px && app->width == px,
                  "High-res EXE Shell path failed");
            Report("EXE", *app, dpi);
            SavePreview(output / (L"exe-" + std::to_wstring(static_cast<int>(dpi)) + L".png"), dpi, oldExe, *app);
        }
        VerifyNoRenderUpscale(output);
        // Largest frame fallback when even 256 cannot fill the destination.
        auto largest = IconProvider::DecodeFile(sparse, 512);
        Check(largest && largest->width == 256 && largest->sourceWidth == 256, "Largest frame fallback was upscaled");
        Check(!IconProvider::DecodeBytes(std::vector<uint8_t>{1, 2, 3}, 144), "Invalid icon accepted");
        UserConfig config;
        config.customTools[tool.id] = tool;
        const auto profile = output / L"profile";
        Check(ConfigManager(profile / L"Config/settings.json").Save(config).has_value(), "Cannot seed DPI profile");
        ApplicationServices services(root, profile);
        services.Start([] {});
        Wait(services, [&] { return services.Data() != nullptr; });
        // Queue all three before draining to exercise stale worker completion rejection.
        for (float dpi : {96.0f, 144.0f, 192.0f}) {
            services.SetIconMetrics(72, dpi);
            services.RequestIcon(tool.id);
        }
        Wait(services, [&] { return services.Data()->icons.contains(tool.id); });
        Check(services.Data()->icons.at(tool.id)->width == 144, "Stale completion replaced current DPI cache");
        const auto retained = services.Data()->icons.at(tool.id);
        services.RequestIcon(tool.id);
        services.Stop();
        Check(services.Data()->icons.at(tool.id) == retained, "Identical size duplicated bitmap cache");
        if (argc == 6) {
            const std::filesystem::path link(argv[4]);
            const auto shortcut = InspectWindowsShortcut(link);
            Check(shortcut.has_value(), "Cannot inspect real shortcut");
            auto legacy =
                Legacy(shortcut->iconPath.empty() ? shortcut->executable : shortcut->iconPath, shortcut->iconIndex);
            Report("Hermes baseline", legacy, 96);
            CustomTool hermes{"hermes", "Hermes", ShortcutKind::WindowsShortcut, Utf8(link.native())};
            const auto cache = output / L"web";
            for (float dpi : {96.0f, 144.0f, 192.0f}) {
                const auto px = IconProvider::TargetPixels(72, dpi);
                const auto icon = IconProvider(output).LoadCustom(hermes, px);
                Check(icon && icon->source == "Shell", "Real shortcut Shell failed");
                Report("Hermes.lnk", *icon, dpi);
                SavePreview(output / (L"hermes-" + std::to_wstring(static_cast<int>(dpi)) + L".png"), dpi, legacy,
                            *icon);
                auto web = WebMetadataProvider(cache).Fetch("live-web", Utf8(argv[5]), false, {}, px);
                if (!web)
                    throw std::runtime_error(web.error().message + " Native=" + std::to_string(web.error().nativeCode));
                Check(web->icon.has_value(), "Live web icon unavailable");
                Report("Live Web", *web->icon, dpi);
                std::cout << "SVG metadata: " << web->svgIcon << '\n';
            }
        }
        std::cout << "Icon frame selection, no-upscale, Shell, DPI cache and stale completion checks passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return status;
}

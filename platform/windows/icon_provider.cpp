#include "icon_provider.h"
#include "custom_shortcut.h"
#include "utf.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace poetoolbox {
namespace {
Result<IconPixels> Decode(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 2 * 1024 * 1024)
        return std::unexpected(Error{ErrorCode::IoError, "Icon is empty or exceeds 2 MiB."});
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    auto hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size()));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr))
        hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0 || w > 4096 || h > 4096)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot decode bounded icon."});
    const float factor = std::min(64.0f / w, 64.0f / h);
    const UINT sw = std::max(1u, static_cast<UINT>(w * factor)), sh = std::max(1u, static_cast<UINT>(h * factor));
    hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr))
        hr = scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom);
    IconPixels pixels{sw, sh, std::vector<uint8_t>(sw * sh * 4)};
    if (SUCCEEDED(hr))
        hr = converter->CopyPixels(nullptr, sw * 4, static_cast<UINT>(pixels.bgra.size()), pixels.bgra.data());
    if (FAILED(hr))
        return std::unexpected(Error{ErrorCode::IoError, "Icon conversion failed.", static_cast<uint32_t>(hr)});
    return pixels;
}
Result<IconPixels> FromExe(const std::filesystem::path &file, int index = 0) {
    HICON icon = nullptr;
    if (ExtractIconExW(file.c_str(), index, &icon, nullptr, 1) != 1 || !icon)
        return std::unexpected(Error{ErrorCode::IoError, "EXE icon unavailable."});
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 64;
    info.bmiHeader.biHeight = -64;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        DestroyIcon(icon);
        return std::unexpected(Error{ErrorCode::IoError, "Cannot allocate icon surface."});
    }
    auto old = SelectObject(dc, bitmap);
    ZeroMemory(bits, 64 * 64 * 4);
    const auto drawn = DrawIconEx(dc, 0, 0, icon, 64, 64, 0, nullptr, DI_NORMAL);
    IconPixels pixels{64, 64, std::vector<uint8_t>(64 * 64 * 4)};
    if (drawn)
        memcpy(pixels.bgra.data(), bits, pixels.bgra.size());
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyIcon(icon);
    if (!drawn)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot draw executable icon."});
    // Legacy icons have no alpha channel; give their nonzero pixels opaque alpha.
    bool alpha = false;
    for (size_t i = 3; i < pixels.bgra.size(); i += 4)
        if (pixels.bgra[i]) {
            alpha = true;
            break;
        }
    if (!alpha)
        for (size_t i = 0; i < pixels.bgra.size(); i += 4)
            if (pixels.bgra[i] || pixels.bgra[i + 1] || pixels.bgra[i + 2])
                pixels.bgra[i + 3] = 255;
    return pixels;
}
} // namespace
Result<IconPixels> IconProvider::DecodeBytes(std::span<const uint8_t> bytes) {
    return Decode(bytes);
}
Result<IconPixels> IconProvider::DecodeFile(const std::filesystem::path &file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0 || size > 2 * 1024 * 1024)
        return std::unexpected(Error{ErrorCode::IoError, "Icon missing or exceeds 2 MiB."});
    std::ifstream stream(file, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected(Error{ErrorCode::IoError, "Cannot read icon."});
    return Decode(bytes);
}
Result<IconPixels> IconProvider::Load(const ToolManifest &tool, const std::filesystem::path &executable,
                                      const std::filesystem::path &root) const {
    if (!IsValidToolId(tool.id))
        return std::unexpected(Error{ErrorCode::InvalidManifest, "Invalid icon key."});
    if (!tool.icon.empty()) {
        std::error_code ec;
        const auto base = std::filesystem::weakly_canonical(root, ec);
        if (!ec) {
            const auto file = std::filesystem::weakly_canonical(root / Utf16(tool.icon), ec);
            if (!ec) {
                const auto relative = file.lexically_relative(base);
                bool contained = !relative.empty() && !relative.is_absolute();
                for (const auto &p : relative)
                    if (p == "..")
                        contained = false;
                if (contained) {
                    auto local = DecodeFile(file);
                    if (local)
                        return local;
                }
            }
        }
    }
    if (tool.type == ToolType::Application && !executable.empty()) {
        auto icon = FromExe(executable);
        if (icon)
            return icon;
    }
    if (tool.type == ToolType::Web || tool.type == ToolType::ExternalLink)
        for (const auto *ext : {L".png", L".ico", L".icon"}) {
            auto cached = DecodeFile(cache_ / (Utf16(tool.id) + ext));
            if (cached)
                return cached;
        }
    return std::unexpected(Error{ErrorCode::IoError, "No local icon; use placeholder."});
}
Result<IconPixels> IconProvider::LoadCustom(const CustomTool &tool) const {
    if (!IsValidToolId(tool.id))
        return std::unexpected(Error{ErrorCode::InvalidPath, "Invalid custom icon key."});
    if (tool.kind == ShortcutKind::Url) {
        ToolManifest manifest;
        manifest.id = tool.id;
        manifest.type = ToolType::Web;
        return Load(manifest, {}, {});
    }
    const std::filesystem::path path(Utf16(tool.target));
    if (!path.is_absolute())
        return std::unexpected(Error{ErrorCode::InvalidPath, "Custom icon target must be absolute."});
    if (tool.kind == ShortcutKind::Executable)
        return FromExe(path);
    auto shortcut = InspectWindowsShortcut(path);
    if (!shortcut)
        return std::unexpected(shortcut.error());
    if (!shortcut->iconPath.empty()) {
        auto own = FromExe(shortcut->iconPath, shortcut->iconIndex);
        if (own)
            return own;
        auto image = DecodeFile(shortcut->iconPath);
        if (image)
            return image;
    }
    return FromExe(shortcut->executable);
}
} // namespace poetoolbox

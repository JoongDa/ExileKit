#include "icon_provider.h"
#include "custom_shortcut.h"
#include "utf.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <cmath>
#include <wincodec.h>
#include <wrl/client.h>
namespace poetoolbox {
namespace {
using Microsoft::WRL::ComPtr;
Result<IconPixels> Convert(IWICImagingFactory *factory, IWICBitmapSource *source, uint32_t targetPx,
                           const char *origin) {
    UINT w = 0, h = 0;
    auto hr = source->GetSize(&w, &h);
    if (FAILED(hr) || !w || !h || w > 4096 || h > 4096)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot decode bounded icon."});
    targetPx = std::clamp(targetPx, 1u, 512u);
    // Never synthesize detail by upscaling a small source. Preserve aspect ratio.
    const double factor = std::min({1.0, double(targetPx) / w, double(targetPx) / h});
    const UINT sw = std::max(1u, static_cast<UINT>(std::lround(w * factor)));
    const UINT sh = std::max(1u, static_cast<UINT>(std::lround(h * factor)));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (sw != w || sh != h) {
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr))
            hr = scaler->Initialize(source, sw, sh, WICBitmapInterpolationModeFant);
        if (SUCCEEDED(hr))
            source = scaler.Get();
    }
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom);
    IconPixels pixels{sw, sh, std::vector<uint8_t>(sw * sh * 4)};
    if (SUCCEEDED(hr))
        hr = converter->CopyPixels(nullptr, sw * 4, static_cast<UINT>(pixels.bgra.size()), pixels.bgra.data());
    if (FAILED(hr))
        return std::unexpected(Error{ErrorCode::IoError, "Icon conversion failed.", static_cast<uint32_t>(hr)});
    pixels.sourceWidth = w;
    pixels.sourceHeight = h;
    pixels.requestedPx = targetPx;
    pixels.source = origin;
    return pixels;
}
Result<IconPixels> Decode(std::span<const uint8_t> bytes, uint32_t targetPx) {
    if (bytes.empty() || bytes.size() > 2 * 1024 * 1024)
        return std::unexpected(Error{ErrorCode::IoError, "Icon is empty or exceeds 2 MiB."});
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    auto hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size()));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    UINT count = 0;
    if (SUCCEEDED(hr))
        hr = decoder->GetFrameCount(&count);
    if (FAILED(hr) || !count || count > 256)
        return std::unexpected(Error{ErrorCode::IoError, "Cannot decode bounded icon frames."});
    ComPtr<IWICBitmapFrameDecode> best;
    UINT bestSize = 0;
    targetPx = std::clamp(targetPx, 1u, 512u);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        UINT w = 0, h = 0;
        if (FAILED(decoder->GetFrame(i, &frame)) || FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 4096 || h > 4096)
            continue;
        const auto size = std::min(w, h);
        if (!best || (size >= targetPx && (bestSize < targetPx || size < bestSize)) ||
            (size < targetPx && bestSize < targetPx && size > bestSize)) {
            best = frame;
            bestSize = size;
        }
    }
    if (!best)
        return std::unexpected(Error{ErrorCode::IoError, "No usable icon frame."});
    return Convert(factory.Get(), best.Get(), targetPx, "WIC");
}
Result<IconPixels> FromShell(const std::filesystem::path &file, uint32_t targetPx) {
    ComPtr<IShellItemImageFactory> item;
    auto hr =
        SHCreateItemFromParsingName(file.lexically_normal().make_preferred().c_str(), nullptr, IID_PPV_ARGS(&item));
    HBITMAP bitmap = nullptr;
    const auto requested = static_cast<LONG>(IconProvider::SourcePixels(targetPx));
    if (SUCCEEDED(hr))
        hr = item->GetImage({requested, requested}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> source;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &source);
    if (bitmap)
        DeleteObject(bitmap);
    if (FAILED(hr))
        return std::unexpected(Error{ErrorCode::IoError, "Shell icon unavailable.", static_cast<uint32_t>(hr)});
    return Convert(factory.Get(), source.Get(), targetPx, "Shell");
}
Result<IconPixels> FromExe(const std::filesystem::path &file, uint32_t targetPx, int index = 0) {
    HICON icon = nullptr;
    if (ExtractIconExW(file.c_str(), index, &icon, nullptr, 1) != 1 || !icon)
        return std::unexpected(Error{ErrorCode::IoError, "EXE icon unavailable."});
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> source;
    auto hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapFromHICON(icon, &source);
    DestroyIcon(icon);
    if (FAILED(hr))
        return std::unexpected(Error{ErrorCode::IoError, "Legacy icon conversion failed.", static_cast<uint32_t>(hr)});
    return Convert(factory.Get(), source.Get(), targetPx, "ExtractIconEx");
}
} // namespace
uint32_t IconProvider::TargetPixels(float dip, float dpi) {
    if (!std::isfinite(dip) || !std::isfinite(dpi) || dip <= 0 || dpi <= 0)
        return 64;
    return static_cast<uint32_t>(std::clamp(std::ceil(double(dip) * dpi / 96.0), 1.0, 512.0));
}
uint32_t IconProvider::SourcePixels(uint32_t targetPx) {
    if (targetPx < 128)
        return 128;
    if (targetPx < 256)
        return 256;
    return 512;
}
Result<IconPixels> IconProvider::DecodeBytes(std::span<const uint8_t> bytes, uint32_t targetPx) {
    return Decode(bytes, targetPx);
}
Result<IconPixels> IconProvider::DecodeFile(const std::filesystem::path &file, uint32_t targetPx) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0 || size > 2 * 1024 * 1024)
        return std::unexpected(Error{ErrorCode::IoError, "Icon missing or exceeds 2 MiB."});
    std::ifstream stream(file, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected(Error{ErrorCode::IoError, "Cannot read icon."});
    return Decode(bytes, targetPx);
}
Result<IconPixels> IconProvider::Load(const ToolManifest &tool, const std::filesystem::path &executable,
                                      const std::filesystem::path &root, uint32_t targetPx) const {
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
                    auto local = DecodeFile(file, targetPx);
                    if (local)
                        return local;
                }
            }
        }
    }
    if (tool.type == ToolType::Application && !executable.empty()) {
        auto icon = FromShell(executable, targetPx);
        if (!icon)
            icon = FromExe(executable, targetPx);
        if (icon)
            return icon;
    }
    if (tool.type == ToolType::Web || tool.type == ToolType::ExternalLink)
        for (const auto *ext : {L".icon", L".png", L".ico"}) {
            auto cached = DecodeFile(cache_ / (Utf16(tool.id) + ext), targetPx);
            if (cached)
                return cached;
        }
    return std::unexpected(Error{ErrorCode::IoError, "No local icon; use placeholder."});
}
Result<IconPixels> IconProvider::LoadCustom(const CustomTool &tool, uint32_t targetPx) const {
    if (!IsValidToolId(tool.id))
        return std::unexpected(Error{ErrorCode::InvalidPath, "Invalid custom icon key."});
    if (tool.kind == ShortcutKind::Url) {
        ToolManifest manifest;
        manifest.id = tool.id;
        manifest.type = ToolType::Web;
        return Load(manifest, {}, {}, targetPx);
    }
    const std::filesystem::path path(Utf16(tool.target));
    if (!path.is_absolute())
        return std::unexpected(Error{ErrorCode::InvalidPath, "Custom icon target must be absolute."});
    auto shell = FromShell(path, targetPx);
    if (shell)
        return shell;
    if (tool.kind == ShortcutKind::Executable)
        return FromExe(path, targetPx);
    auto shortcut = InspectWindowsShortcut(path);
    if (!shortcut)
        return std::unexpected(shortcut.error());
    if (!shortcut->iconPath.empty()) {
        auto image = DecodeFile(shortcut->iconPath, targetPx);
        if (image)
            return image;
        auto own = FromExe(shortcut->iconPath, targetPx, shortcut->iconIndex);
        if (own)
            return own;
    }
    auto target = FromShell(shortcut->executable, targetPx);
    if (target)
        return target;
    return FromExe(shortcut->executable, targetPx);
}
} // namespace poetoolbox

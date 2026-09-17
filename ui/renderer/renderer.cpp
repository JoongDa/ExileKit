#include "renderer.h"
#include <algorithm>
#include <cmath>

namespace poetoolbox::ui {
HRESULT Renderer::Initialize() {
    auto hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown **>(write_.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    hr = write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, 14, L"en-us", body_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, 22, L"en-us", heading_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, 14, L"en-us", iconLabel_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    iconLabel_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    hr = write_->CreateEllipsisTrimmingSign(iconLabel_.Get(), &ellipsis);
    if (FAILED(hr))
        return hr;
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    iconLabel_->SetTrimming(&trimming, ellipsis.Get());
    hr = write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, 28, L"en-us", monogram_.GetAddressOf());
    if (SUCCEEDED(hr)) {
        monogram_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        monogram_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return hr;
}
HRESULT Renderer::Begin(HWND window, float dpi) {
    if (!target_) {
        RECT rect{};
        GetClientRect(window, &rect);
        auto props = D2D1::RenderTargetProperties();
        props.dpiX = props.dpiY = dpi;
        auto hr = factory_->CreateHwndRenderTarget(
            props,
            D2D1::HwndRenderTargetProperties(
                window, D2D1::SizeU(static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom))),
            windowTarget_.GetAddressOf());
        if (FAILED(hr))
            return hr;
        target_ = windowTarget_;
        hr = target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), brush_.GetAddressOf());
        if (FAILED(hr)) {
            DiscardTarget();
            return hr;
        }
    }
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x101318));
    return S_OK;
}
HRESULT Renderer::End() {
    const auto hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardTarget();
    return hr;
}
void Renderer::DiscardTarget() {
    images_.clear();
    brush_.Reset();
    target_.Reset();
    windowTarget_.Reset();
    bitmap_.Reset();
}
void Renderer::Resize(UINT width, UINT height) {
    if (windowTarget_ && FAILED(windowTarget_->Resize(D2D1::SizeU(width, height))))
        DiscardTarget();
}
void Renderer::SetDpi(float dpi) {
    if (target_)
        target_->SetDpi(dpi, dpi);
}
void Renderer::Fill(D2D1_RECT_F rect, UINT32 color, float radius) {
    brush_->SetColor(D2D1::ColorF(color));
    if (radius > 0)
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
    else
        target_->FillRectangle(rect, brush_.Get());
}
void Renderer::Text(std::wstring_view text, D2D1_RECT_F rect, UINT32 color, bool heading) {
    brush_->SetColor(D2D1::ColorF(color));
    target_->DrawText(text.data(), static_cast<UINT32>(text.size()), heading ? heading_.Get() : body_.Get(), rect,
                      brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::PushClip(D2D1_RECT_F rect) {
    target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
}
void Renderer::IconLabel(std::wstring_view text, D2D1_RECT_F rect, UINT32 color, bool monogram) {
    brush_->SetColor(D2D1::ColorF(color));
    target_->DrawText(text.data(), static_cast<UINT32>(text.size()), monogram ? monogram_.Get() : iconLabel_.Get(),
                      rect, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::PopClip() {
    target_->PopAxisAlignedClip();
}
bool Renderer::Image(const std::shared_ptr<const IconPixels> &source, D2D1_RECT_F rect) {
    if (!source)
        return false;
    const auto &pixels = *source;
    if (!pixels.width || !pixels.height || pixels.bgra.size() != static_cast<size_t>(pixels.width) * pixels.height * 4)
        return false;
    auto found = images_.find(&pixels);
    if (found == images_.end()) {
        if (images_.size() >= 256)
            images_.clear();
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        const auto properties =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.bgra.data(), pixels.width * 4,
                                         properties, &bitmap)))
            return false;
        found = images_.emplace(&pixels, CachedImage{source, std::move(bitmap)}).first;
    }
    float dpiX = 96, dpiY = 96;
    target_->GetDpi(&dpiX, &dpiY);
    const float factor = std::min({1.0f, (rect.right - rect.left) * dpiX / (96 * pixels.width),
                                   (rect.bottom - rect.top) * dpiY / (96 * pixels.height)});
    const float width = pixels.width * factor * 96 / dpiX, height = pixels.height * factor * 96 / dpiY;
    const float left = std::round((rect.left + rect.right - width) * 0.5f * dpiX / 96) * 96 / dpiX;
    const float top = std::round((rect.top + rect.bottom - height) * 0.5f * dpiY / 96) * 96 / dpiY;
    target_->DrawBitmap(found->second.bitmap.Get(), D2D1::RectF(left, top, left + width, top + height));
    return true;
}
HRESULT Renderer::BeginOffscreen(UINT width, UINT height, float dpi) {
    DiscardTarget();
    HRESULT hr = S_OK;
    if (!imaging_)
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(imaging_.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    hr = imaging_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                bitmap_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    auto properties = D2D1::RenderTargetProperties();
    properties.dpiX = properties.dpiY = dpi;
    hr = factory_->CreateWicBitmapRenderTarget(bitmap_.Get(), properties, target_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = target_->CreateSolidColorBrush(D2D1::ColorF(0xffffff), brush_.GetAddressOf());
    if (FAILED(hr)) {
        DiscardTarget();
        return hr;
    }
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0x101318));
    return S_OK;
}
HRESULT Renderer::SavePng(const wchar_t *path) {
    if (!bitmap_)
        return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    auto hr = imaging_->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = imaging_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr))
        hr = frame->WriteSource(bitmap_.Get(), nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    return hr;
}
} // namespace poetoolbox::ui

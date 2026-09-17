#pragma once
#include "poetoolbox/image.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <map>
#include <memory>
#include <string_view>
#include <wincodec.h>
#include <wrl/client.h>

namespace poetoolbox::ui {
class Renderer final {
  public:
    HRESULT Initialize();
    HRESULT Begin(HWND window, float dpi);
    HRESULT End();
    // WIC is created only on explicit bitmap work, never on application startup.
    HRESULT BeginOffscreen(UINT width, UINT height, float dpi);
    HRESULT SavePng(const wchar_t *path);
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    void Fill(D2D1_RECT_F rect, UINT32 color, float radius = 0);
    void Text(std::wstring_view text, D2D1_RECT_F rect, UINT32 color, bool heading = false);
    void IconLabel(std::wstring_view text, D2D1_RECT_F rect, UINT32 color, bool monogram = false);
    void PushClip(D2D1_RECT_F rect);
    void PopClip();
    bool Image(const std::shared_ptr<const IconPixels> &pixels, D2D1_RECT_F rect);

  private:
    void DiscardTarget();
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> heading_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconLabel_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> monogram_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> windowTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> imaging_;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap_;
    struct CachedImage {
        std::shared_ptr<const IconPixels> pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    std::map<const IconPixels *, CachedImage> images_;
};
} // namespace poetoolbox::ui

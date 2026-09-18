#include "main_window.h"
#include "utf.h"
#include <cmath>
#include <commctrl.h>
#include <dwmapi.h>
#include <exception>
#include <windowsx.h>

namespace poetoolbox::ui {
MainWindow::~MainWindow() {
    services_.Stop();
    if (window_ && IsWindow(window_))
        DestroyWindow(window_);
    if (classInstance_)
        UnregisterClassW(L"POEToolbox.MainWindow", classInstance_);
    if (classBigIcon_)
        DestroyIcon(classBigIcon_);
    if (classSmallIcon_)
        DestroyIcon(classSmallIcon_);
    ReleaseWindowIcons();
    if (searchFont_)
        DeleteObject(searchFont_);
    if (searchBrush_)
        DeleteObject(searchBrush_);
}
int MainWindow::Run(HINSTANCE instance, int show, UINT iconResource) {
    iconResource_ = iconResource;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    const auto startupDpi = GetDpiForSystem();
    // LR_SHARED caches by resource name, not requested size. Own both class icons
    // so loading the big icon cannot make Windows reuse it as the small icon.
    if (iconResource_) {
        classBigIcon_ = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(iconResource_), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXICON, startupDpi),
            GetSystemMetricsForDpi(SM_CYICON, startupDpi), 0));
        classSmallIcon_ = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(iconResource_), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, startupDpi),
            GetSystemMetricsForDpi(SM_CYSMICON, startupDpi), 0));
    }
    wc.hIcon = classBigIcon_ ? classBigIcon_ : static_cast<HICON>(LoadImageW(
        nullptr, IDI_APPLICATION, IMAGE_ICON, GetSystemMetricsForDpi(SM_CXICON, startupDpi),
        GetSystemMetricsForDpi(SM_CYICON, startupDpi), LR_SHARED));
    wc.hIconSm = classSmallIcon_ ? classSmallIcon_ : static_cast<HICON>(LoadImageW(
        nullptr, IDI_APPLICATION, IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, startupDpi),
        GetSystemMetricsForDpi(SM_CYSMICON, startupDpi), LR_SHARED));
    wc.lpszClassName = L"POEToolbox.MainWindow";
    if (!RegisterClassExW(&wc))
        return 1;
    classInstance_ = instance;
    if (FAILED(renderer_.Initialize())) {
        logger_.Write(LogLevel::Error, L"Renderer initialization failed.");
        return 1;
    }
    window_ = CreateWindowExW(0, wc.lpszClassName, L"ExileKit", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1200, static_cast<int>(startupDpi), 96),
                              MulDiv(800, static_cast<int>(startupDpi), 96), nullptr, nullptr, instance, this);
    if (!window_) {
        logger_.Write(LogLevel::Error, L"Window creation failed.");
        return 1;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    ShowWindow(window_, show);
    UpdateWindow(window_);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == 'K' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SetFocus(search_);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
            SetFocus(GetFocus() == search_ || page_.Settings() ? window_ : search_);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == 'N' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            page_.View() == LibraryView::Home) {
            userEngaged_ = true;
            HandleAction({PageActionKind::AddShortcut, {}});
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto *self = reinterpret_cast<MainWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MainWindow *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, wparam, lparam);
    try {
        return self->HandleMessage(message, wparam, lparam);
    } catch (const std::exception &) {
        self->logger_.Write(LogLevel::Error, L"Unhandled window callback exception; closing application.");
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
}
LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        dpi_ = static_cast<float>(GetDpiForWindow(window_));
        UpdateWindowIcons();
        searchBrush_ = CreateSolidBrush(RGB(38, 46, 57));
        search_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                  window_, reinterpret_cast<HMENU>(1001), GetModuleHandleW(nullptr), nullptr);
        if (!search_ || !searchBrush_)
            return -1;
        SendMessageW(search_, EM_SETLIMITTEXT, 256, 0);
        SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"…"));
        SetWindowTextW(search_, L"");
        if (!SetWindowSubclass(search_, SearchProc, 1, reinterpret_cast<DWORD_PTR>(this)))
            return -1;
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   CW_USEDEFAULT, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (tooltip_) {
            TOOLINFOW tip{sizeof(tip)};
            tip.hwnd = window_;
            tip.uId = 1;
            tip.lpszText = const_cast<wchar_t *>(L"");
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
            SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(320 * dpi_ / 96));
            SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
            SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
        }
        page_.Refresh();
        UpdateSearchFont();
        Layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_APP + 41:
        services_.Start([window = window_] { PostMessageW(window, WM_APP + 42, 0, 0); });
        return 0;
    case WM_APP + 42:
        ApplyCompletions();
        return 0;
    case WM_COMMAND:
        if (controlsUpdating_)
            return 0;
        if (LOWORD(wparam) == 1001 && HIWORD(wparam) == EN_CHANGE) {
            if (services_.Data())
                userEngaged_ = true;
            const int length = GetWindowTextLengthW(search_);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(search_, text.data(), length + 1);
            text.resize(length);
            page_.SetSearch(Utf8(text));
            HideToolTooltip();
            UpdateScrollBar();
            RequestVisibleIcons();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            renderer_.Resize(LOWORD(lparam), HIWORD(lparam));
            Layout();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(wparam));
        renderer_.SetDpi(dpi_);
        services_.SetIconMetrics(ToolIconDip, dpi_);
        page_.Refresh();
        UpdateWindowIcons();
        UpdateSearchFont();
        const auto *rect = reinterpret_cast<RECT *>(lparam);
        SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(231, 235, 240));
        SetBkColor(dc, RGB(38, 46, 57));
        return reinterpret_cast<LRESULT>(searchBrush_);
    }
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, window_, 0};
            trackingMouse_ = TrackMouseEvent(&event) != FALSE;
        }
        const auto point = D2D1::Point2F(GET_X_LPARAM(lparam) * 96 / dpi_, GET_Y_LPARAM(lparam) * 96 / dpi_);
        if (page_.MouseMove(point))
            InvalidateRect(window_, nullptr, FALSE);
        UpdateToolTooltip(point);
        if (tooltip_) {
            MSG relay{window_, message, wparam, lparam, static_cast<DWORD>(GetMessageTime())};
            GetCursorPos(&relay.pt);
            SendMessageW(tooltip_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relay));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        HideToolTooltip();
        trackingMouse_ = false;
        if (page_.MouseMove(D2D1::Point2F(-1, -1)))
            InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        HideToolTooltip();
        userEngaged_ = true;
        SetFocus(window_);
        HandleAction(page_.Click(D2D1::Point2F(GET_X_LPARAM(lparam) * 96 / dpi_, GET_Y_LPARAM(lparam) * 96 / dpi_)));
        return 0;
    case WM_CONTEXTMENU: {
        HideToolTooltip();
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (point.x == -1 && point.y == -1) {
            point = {static_cast<LONG>((LibraryPage::sidebar + 50) * dpi_ / 96), static_cast<LONG>(165 * dpi_ / 96)};
            ClientToScreen(window_, &point);
        }
        auto client = point;
        ScreenToClient(window_, &client);
        const auto id = page_.ContextTool(D2D1::Point2F(client.x * 96 / dpi_, client.y * 96 / dpi_));
        if (!id.empty()) {
            userEngaged_ = true;
            ShowToolMenu(id, point);
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        HideToolTooltip();
        userEngaged_ = true;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        const float step = lines == WHEEL_PAGESCROLL ? page_.ViewportHeight() : static_cast<float>(lines) * 24;
        if (page_.Scroll(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * step)) {
            UpdateScrollBar();
            RequestVisibleIcons();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_VSCROLL: {
        HideToolTooltip();
        SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
        GetScrollInfo(window_, SB_VERT, &info);
        float offset = page_.ScrollOffset();
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            offset -= 24;
            break;
        case SB_LINEDOWN:
            offset += 24;
            break;
        case SB_PAGEUP:
            offset -= page_.ViewportHeight();
            break;
        case SB_PAGEDOWN:
            offset += page_.ViewportHeight();
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            offset = static_cast<float>(info.nTrackPos);
            break;
        case SB_TOP:
            offset = 0;
            break;
        case SB_BOTTOM:
            offset = page_.ScrollMax();
            break;
        default:
            return 0;
        }
        if (page_.ScrollTo(offset)) {
            UpdateScrollBar();
            RequestVisibleIcons();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            SetWindowTextW(search_, L"");
            return 0;
        }
        if (wparam == VK_NEXT || wparam == VK_PRIOR || wparam == VK_HOME || wparam == VK_END || wparam == VK_DOWN ||
            wparam == VK_UP) {
            const WORD command = wparam == VK_NEXT    ? SB_PAGEDOWN
                                 : wparam == VK_PRIOR ? SB_PAGEUP
                                 : wparam == VK_HOME  ? SB_TOP
                                 : wparam == VK_END   ? SB_BOTTOM
                                 : wparam == VK_DOWN  ? SB_LINEDOWN
                                                      : SB_LINEUP;
            SendMessageW(window_, WM_VSCROLL, command, 0);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
            HideToolTooltip();
        break;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lparam);
        const auto dpi = GetDpiForWindow(window_);
        info->ptMinTrackSize = {MulDiv(780, static_cast<int>(dpi ? dpi : 96), 96),
                                MulDiv(600, static_cast<int>(dpi ? dpi : 96), 96)};
        return 0;
    }
    case WM_DESTROY:
        services_.Stop();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY: {
        const auto handle = window_;
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        window_ = nullptr;
        search_ = nullptr;
        const auto result = DefWindowProcW(handle, message, wparam, lparam);
        ReleaseWindowIcons();
        return result;
    }
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}
void MainWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    auto hr = renderer_.Begin(window_, dpi_);
    if (SUCCEEDED(hr)) {
        page_.Draw(renderer_);
        hr = renderer_.End();
    }
    EndPaint(window_, &paint);
    if (SUCCEEDED(hr) && !firstFrame_) {
        firstFrame_ = true;
        // One-time readiness marker for the out-of-process smoke/performance harness.
        SetPropW(window_, L"POEToolbox.FirstFrame", reinterpret_cast<HANDLE>(1));
        PostMessageW(window_, WM_APP + 41, 0, 0);
    }
    if (hr == D2DERR_RECREATE_TARGET)
        InvalidateRect(window_, nullptr, FALSE);
    else if (FAILED(hr))
        logger_.Write(LogLevel::Error, L"Rendering failed.");
}
void MainWindow::Layout() {
    HideToolTooltip();
    RECT rect{};
    GetClientRect(window_, &rect);
    const float scale = dpi_ / 96;
    page_.Resize(static_cast<float>(rect.right) / scale, static_cast<float>(rect.bottom) / scale);
    const int x = static_cast<int>((LibraryPage::sidebar + 24) * scale);
    MoveWindow(search_, x, static_cast<int>(20 * scale),
               std::max(40, static_cast<int>(rect.right) - x - static_cast<int>(24 * scale)),
               static_cast<int>(28 * scale), TRUE);
    ShowWindow(search_, page_.Settings() ? SW_HIDE : SW_SHOWNA);
    UpdateScrollBar();
    RequestVisibleIcons();
}
void MainWindow::UpdateScrollBar() {
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    info.nMax = static_cast<int>(std::ceil(page_.ScrollMax() + page_.ViewportHeight())) - 1;
    info.nPage = static_cast<UINT>(std::max(0.0f, page_.ViewportHeight()));
    info.nPos = static_cast<int>(page_.ScrollOffset());
    SetScrollInfo(window_, SB_VERT, &info, TRUE);
}
void MainWindow::UpdateWindowIcons() {
    if (!iconResource_)
        return;
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE));
    const UINT dpi = static_cast<UINT>(dpi_);
    // Keep distinct, non-shared handles at the current monitor's native sizes.
    for (const auto kind : {ICON_BIG, ICON_SMALL}) {
        const auto icon =
            static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(iconResource_), IMAGE_ICON,
                                          GetSystemMetricsForDpi(kind == ICON_BIG ? SM_CXICON : SM_CXSMICON, dpi),
                                          GetSystemMetricsForDpi(kind == ICON_BIG ? SM_CYICON : SM_CYSMICON, dpi), 0));
        if (!icon)
            continue;
        auto &ownedIcon = kind == ICON_BIG ? windowBigIcon_ : windowSmallIcon_;
        SendMessageW(window_, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
        if (ownedIcon)
            DestroyIcon(ownedIcon);
        ownedIcon = icon;
    }
}
void MainWindow::ReleaseWindowIcons() {
    for (auto *icon : {&windowBigIcon_, &windowSmallIcon_}) {
        if (*icon)
            DestroyIcon(*icon);
        *icon = nullptr;
    }
}
void MainWindow::UpdateSearchFont() {
    const auto font =
        CreateFontW(-MulDiv(14, static_cast<int>(dpi_), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!font) {
        logger_.Write(LogLevel::Warning, L"Could not create search font.");
        return;
    }
    SendMessageW(search_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (searchFont_)
        DeleteObject(searchFont_);
    searchFont_ = font;
}
LRESULT CALLBACK MainWindow::SearchProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                        DWORD_PTR data) {
    const auto *self = reinterpret_cast<MainWindow *>(data);
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        SetWindowTextW(window, L"");
        SetFocus(self->window_);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, SearchProc, 1);
    return DefSubclassProc(window, message, wparam, lparam);
}
} // namespace poetoolbox::ui

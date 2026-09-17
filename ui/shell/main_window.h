#pragma once
#include "logger.h"
#include "pages/library_page.h"
#include "renderer/renderer.h"

namespace poetoolbox::ui {
class MainWindow final {
  public:
    MainWindow(Logger &logger, ApplicationServices &services) : logger_(logger), services_(services), page_(services) {}
    ~MainWindow();
    int Run(HINSTANCE instance, int show, UINT iconResource = 0);

  private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void Paint();
    void Layout();
    void UpdateScrollBar();
    void UpdateSearchFont();
    void UpdateWindowIcons();
    void ReleaseWindowIcons();
    void UpdateToolTooltip(D2D1_POINT_2F point);
    void HideToolTooltip();
    void RefreshContent(bool resetScroll = false);
    void RequestVisibleIcons();
    void HandleAction(PageAction action);
    void ApplyCompletions();
    void ShowToolMenu(const std::string &id, POINT screenPoint);
    static LRESULT CALLBACK SearchProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                       DWORD_PTR data);
    HWND window_ = nullptr;
    UINT iconResource_ = 0;
    HICON windowBigIcon_ = nullptr;
    HICON windowSmallIcon_ = nullptr;
    float dpi_ = 96;
    Renderer renderer_;
    Logger &logger_;
    ApplicationServices &services_;
    LibraryPage page_;
    HWND search_ = nullptr;
    HWND tooltip_ = nullptr;
    std::wstring toolTooltip_;
    RECT tooltipRect_{};
    HFONT searchFont_ = nullptr;
    HBRUSH searchBrush_ = nullptr;
    bool trackingMouse_ = false;
    bool firstFrame_ = false;
    bool controlsUpdating_ = false;
    bool loggerReady_ = false;
    bool userEngaged_ = false;
};
} // namespace poetoolbox::ui

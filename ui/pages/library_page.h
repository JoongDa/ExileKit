#pragma once
#include "application.h"
#include "controls/tool_icon_item.h"
#include "layout/grid.h"
namespace poetoolbox::ui {
enum class PageActionKind {
    None,
    Navigate,
    Open,
    Locate,
    Favorite,
    Download,
    LanguageEnglish,
    LanguageChinese,
    ChangeDirectory,
    ResetDirectory,
    AddShortcut,
    AddHome,
    PinHome,
    Unpin,
    RemoveHome,
    ContextMenu,
    AutoRemove7,
    AutoRemove30,
    AutoRemove90,
    AutoRemoveNever
};
struct PageAction {
    PageActionKind kind = PageActionKind::None;
    std::string id;
};
enum class LibraryView { Home, POE, POE2, Settings };
struct ToolTooltip {
    std::wstring text;
    D2D1_RECT_F rect;
};
class LibraryPage final {
  public:
    explicit LibraryPage(ApplicationServices &services) : services_(services) {}
    void Refresh(bool resetScroll = false);
    void SetSearch(std::string query);
    void SetView(LibraryView view);
    void Resize(float width, float height);
    void Draw(Renderer &renderer);
    bool MouseMove(D2D1_POINT_2F point);
    PageAction Click(D2D1_POINT_2F point);
    [[nodiscard]] std::string ContextTool(D2D1_POINT_2F point) const;
    [[nodiscard]] std::optional<ToolTooltip> Tooltip(D2D1_POINT_2F point) const;
    bool Scroll(float delta);
    bool ScrollTo(float offset);
    void UpdateIcon(std::string_view id);
    [[nodiscard]] std::vector<std::string> VisibleToolIds() const;
    [[nodiscard]] std::vector<D2D1_RECT_F> IconRects(std::string_view id) const;
    [[nodiscard]] float ScrollOffset() const { return scroll_.Offset(); }
    [[nodiscard]] float ScrollMax() const { return scroll_.Max(); }
    [[nodiscard]] float ViewportHeight() const { return std::max(0.0f, height_ - top - footer); }
    [[nodiscard]] LibraryView View() const { return view_; }
    [[nodiscard]] bool Settings() const { return view_ == LibraryView::Settings; }
    [[nodiscard]] std::wstring Title() const;
    [[nodiscard]] size_t ToolCount() const { return models_.size(); }
    static constexpr float sidebar = 220, top = 64, footer = 36;

  private:
    struct ItemPlacement {
        size_t model;
        D2D1_RECT_F rect;
    };
    void Layout();
    [[nodiscard]] int HitItem(D2D1_POINT_2F point) const;
    [[nodiscard]] std::wstring T(std::string_view key) const;
    ApplicationServices &services_;
    std::vector<ToolIconModel> models_;
    std::vector<ItemPlacement> items_;
    ScrollState scroll_;
    LibraryView view_ = LibraryView::Home;
    float width_ = 1200, height_ = 800;
    int hoverNav_ = -1, hoverItem_ = -1;
    std::string query_;
    bool homeEmpty_ = true;
};
} // namespace poetoolbox::ui

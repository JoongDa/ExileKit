#include "library_page.h"
#include "utf.h"
#include <algorithm>
#include <array>
namespace poetoolbox::ui {
namespace {
constexpr std::array<const char *, 4> Navigation = {"nav.home", "filter.poe1", "filter.poe2", "nav.settings"};
}
std::wstring LibraryPage::T(std::string_view key) const {
    return Utf16(services_.Tr(key));
}
std::wstring LibraryPage::Title() const {
    return T(Navigation[static_cast<size_t>(view_)]);
}
void LibraryPage::SetView(LibraryView view) {
    view_ = view;
    if (view == LibraryView::Home)
        services_.RefreshHome();
    else if (view == LibraryView::POE || view == LibraryView::POE2)
        services_.SetGame(view == LibraryView::POE ? GameFilter::POE1 : GameFilter::POE2);
    Refresh(true);
}
void LibraryPage::SetSearch(std::string query) {
    query_ = std::move(query);
    Refresh(true);
}
void LibraryPage::Refresh(bool resetScroll) {
    models_.clear();
    const auto *data = services_.Data();
    if (data && !Settings()) {
        std::vector<std::string> ids;
        if (view_ == LibraryView::Home) {
            ids = services_.HomeIds();
            homeEmpty_ = ids.empty();
        } else {
            for (auto index :
                 data->registry.Search(query_, view_ == LibraryView::POE ? GameFilter::POE1 : GameFilter::POE2, {},
                                       data->locale.GetCurrentLanguage()))
                ids.push_back(data->registry.GetTools()[index].manifest.id);
        }
        const auto matching = data->registry.Search(query_, GameFilter::All, {}, data->locale.GetCurrentLanguage());
        for (const auto &id : ids) {
            ToolIconModel card;
            card.id = id;
            if (const auto *tool = data->registry.FindTool(id)) {
                if (view_ == LibraryView::Home && !query_.empty() &&
                    std::none_of(matching.begin(), matching.end(),
                                 [&](size_t i) { return data->registry.GetTools()[i].manifest.id == id; }))
                    continue;
                const auto &m = tool->manifest;
                card.name = Utf16(m.name);
                card.description = Utf16(Localize(m.description, data->locale.GetCurrentLanguage()));
                card.application = m.type == ToolType::Application;
                card.installed = data->installed.contains(id);
                card.risk = m.riskLevel == RiskLevel::GameModifying ? T("risk.game_modifying")
                            : m.riskLevel == RiskLevel::Elevated    ? T("risk.elevated")
                                                                    : L"";
            } else {
                const auto custom = data->config.customTools.find(id);
                if (custom == data->config.customTools.end())
                    continue;
                const auto &c = custom->second;
                if (!query_.empty()) {
                    auto fold = [](std::string text) {
                        for (auto &ch : text)
                            if (ch >= 'A' && ch <= 'Z')
                                ch += 'a' - 'A';
                        return text;
                    };
                    if (fold(c.name).find(fold(query_)) == std::string::npos)
                        continue;
                }
                card.name = Utf16(c.name);
                card.description = L"";
                card.application = c.kind != ShortcutKind::Url;
                card.installed = true;
            }
            if (auto it = data->config.home.find(id); it != data->config.home.end())
                card.pinned = it->second.pinned;
            card.monogram = card.name.empty() ? L"?" : card.name.substr(0, 1);
            if (auto icon = data->icons.find(id); icon != data->icons.end())
                card.icon = icon->second;
            models_.push_back(std::move(card));
        }
    }
    if (resetScroll)
        scroll_.Set(0);
    hoverItem_ = -1;
    Layout();
}
void LibraryPage::Resize(float width, float height) {
    width_ = width;
    height_ = height;
    Layout();
}
void LibraryPage::Layout() {
    items_.clear();
    const ToolIconGrid grid(std::max(1.0f, width_ - sidebar - 48));
    const bool home = view_ == LibraryView::Home;
    const float start = home && homeEmpty_ ? 184.0f : home && models_.empty() ? 138.0f : 78.0f;
    const size_t count = Settings() ? 0 : models_.size() + (home ? 1 : 0);
    for (size_t i = 0; i < count; ++i) {
        const float x = sidebar + 24 + static_cast<float>(i % grid.columns) * (grid.itemWidth + ToolIconGrid::gap);
        const float y = start + static_cast<float>(i / grid.columns) * (ToolIconGrid::itemHeight + ToolIconGrid::gap);
        items_.push_back({i, D2D1::RectF(x, y, x + grid.itemWidth, y + ToolIconGrid::itemHeight)});
    }
    scroll_.SetExtent(Settings() ? 780 : start + grid.Height(count) + 28, ViewportHeight());
}
void LibraryPage::Draw(Renderer &r) {
    r.Fill(D2D1::RectF(0, 0, sidebar, height_), 0x171c24);
    r.Text(L"ExileKit", D2D1::RectF(22, 24, sidebar - 12, 59), 0xebd6a2, true);
    if (services_.Data()) {
        for (size_t i = 0; i < Navigation.size(); ++i) {
            const float y = 102 + static_cast<float>(i) * 44;
            if (static_cast<LibraryView>(i) == view_ || static_cast<int>(i) == hoverNav_)
                r.Fill(D2D1::RectF(12, y, sidebar - 12, y + 38),
                       static_cast<LibraryView>(i) == view_ ? 0x34342f : 0x252d38, 5);
            r.Text(T(Navigation[i]), D2D1::RectF(25, y + 9, sidebar - 12, y + 35),
                   static_cast<LibraryView>(i) == view_ ? 0xf2ddb0 : 0xa8b4c4);
        }
    }
    r.Text(L"v0.3.0", D2D1::RectF(22, height_ - 30, sidebar, height_), 0x78879b);
    r.Fill(D2D1::RectF(sidebar, 0, width_, top), 0x171c24);
    r.PushClip(D2D1::RectF(sidebar, top, width_, height_ - footer));
    const auto *data = services_.Data();
    const float x = sidebar + 24, y = top + 24 - scroll_.Offset();
    if (!data) {
        r.Text(L"…", D2D1::RectF(x, y, width_ - 24, y + 42), 0xf1f3f6, true);
    } else if (Settings()) {
        auto button = [&](std::wstring text, float bx, float by, float w, bool active = false) {
            r.Fill(D2D1::RectF(bx, by, bx + w, by + 34), active ? 0x57503c : 0x303a47, 6);
            r.Text(text, D2D1::RectF(bx + 10, by + 6, bx + w - 8, by + 31), 0xf1e6cb);
        };
        r.Text(Title(), D2D1::RectF(x, y, width_ - 24, y + 34), 0xf1f3f6, true);
        r.Text(T("settings.language"), D2D1::RectF(x, y + 52, width_ - 24, y + 78), 0xa8b4c4);
        button(L"English", x, y + 84, 116, data->locale.GetCurrentLanguage() == "en-US");
        button(L"简体中文", x + 128, y + 84, 116, data->locale.GetCurrentLanguage() == "zh-CN");
        r.Text(T("settings.homeRemoval"), D2D1::RectF(x, y + 143, width_ - 24, y + 172), 0xf1f3f6);
        const int days[] = {7, 30, 90, 0};
        const char *labels[] = {"home.days7", "home.days30", "home.days90", "home.never"};
        for (int i = 0; i < 4; ++i)
            button(T(labels[i]), x + i * 98, y + 180, 90, data->config.homeAutoRemoveDays == days[i]);
        r.Text(T("settings.managedDirectory"), D2D1::RectF(x, y + 244, width_ - 24, y + 288), 0xf1f3f6);
        r.Text(data->config.managedToolsDirectory.native(), D2D1::RectF(x, y + 294, width_ - 24, y + 344), 0xa8b4c4);
        button(T("action.change"), x, y + 352, 116);
        button(T("action.reset"), x + 128, y + 352, 156);
        r.Text(T("settings.directoryNote"), D2D1::RectF(x, y + 404, width_ - 24, y + 455), 0x8795a7);
        r.Text(T("settings.security"), D2D1::RectF(x, y + 483, width_ - 24, y + 514), 0xebd6a2);
        r.Text(T("settings.securityDescription"), D2D1::RectF(x, y + 524, width_ - 24, y + 610), 0xa8b4c4);
        r.Text(T("about.description"), D2D1::RectF(x, y + 630, width_ - 24, y + 681), 0xf1f3f6);
        r.Text(T("about.disclaimer"), D2D1::RectF(x, y + 686, width_ - 24, y + 749), 0x8795a7);
    } else {
        if (view_ == LibraryView::Home && homeEmpty_) {
            r.Text(T("home.welcome"), D2D1::RectF(x, y + 20, width_ - 30, y + 58), 0xf1f3f6, true);
            r.Text(T("home.welcomeBody"), D2D1::RectF(x, y + 70, width_ - 30, y + 142), 0xa2adbd);
        } else {
            r.Text(Title(), D2D1::RectF(x, y, width_ - 24, y + 36), 0xf1f3f6, true);
        }
        if (models_.empty() && !(view_ == LibraryView::Home && homeEmpty_))
            r.Text(T("app.empty"), D2D1::RectF(x, y + 76, width_ - 24, y + 135), 0xa8b4c4);
        for (size_t i = 0; i < items_.size(); ++i) {
            auto rect = items_[i].rect;
            rect.top += top - scroll_.Offset();
            rect.bottom += top - scroll_.Offset();
            if (rect.bottom < top || rect.top > height_ - footer)
                continue;
            if (items_[i].model < models_.size()) {
                DrawToolIconItem(r, models_[items_[i].model], rect, static_cast<int>(i) == hoverItem_);
            } else {
                ToolIconModel add;
                add.name = T("shortcut.add");
                DrawToolIconItem(r, add, rect, static_cast<int>(i) == hoverItem_, true);
            }
        }
    }
    r.PopClip();
    r.Fill(D2D1::RectF(sidebar, height_ - footer, width_, height_), 0x171c24);
    std::wstring status = data ? T(services_.StatusKey()) : L"…";
    if (const auto &error = services_.LastError(); error) {
        constexpr const char *keys[] = {"ioError",      "invalidManifest",       "unsupportedSchema",
                                        "duplicateId",  "invalidConfig",         "invalidPath",
                                        "toolNotFound", "executableNotFound",    "invalidURL",
                                        "accessDenied", "processCreationFailed", "unsupportedOperation",
                                        "cancelled",    "invalidShortcut"};
        const auto index = static_cast<size_t>(error->code);
        status = T(std::string("error.") + (index < std::size(keys) ? keys[index] : "ioError"));
        if (error->nativeCode && error->code != ErrorCode::InvalidShortcut)
            status += L" (" + std::to_wstring(error->nativeCode) + L")";
    }
    r.Text(status, D2D1::RectF(sidebar + 16, height_ - footer + 8, width_ - 8, height_), 0x98a6b8);
}
int LibraryPage::HitItem(D2D1_POINT_2F p) const {
    if (p.x < sidebar || p.y < top || p.y >= height_ - footer)
        return -1;
    p.y += scroll_.Offset() - top;
    for (size_t i = 0; i < items_.size(); ++i)
        if (Contains(items_[i].rect, p))
            return static_cast<int>(i);
    return -1;
}
bool LibraryPage::MouseMove(D2D1_POINT_2F p) {
    const auto oldNav = hoverNav_, oldCard = hoverItem_;
    hoverNav_ = -1;
    if (p.x >= 12 && p.x < sidebar - 12 && p.y >= 102 && p.y < 278)
        hoverNav_ = static_cast<int>((p.y - 102) / 44);
    hoverItem_ = HitItem(p);
    return oldNav != hoverNav_ || oldCard != hoverItem_;
}
PageAction LibraryPage::Click(D2D1_POINT_2F p) {
    MouseMove(p);
    if (hoverNav_ >= 0) {
        SetView(static_cast<LibraryView>(hoverNav_));
        return {PageActionKind::Navigate, {}};
    }
    if (Settings() && p.y >= top && p.y < height_ - footer) {
        const float x = p.x - sidebar - 24, y = p.y - top - 24 + scroll_.Offset();
        if (y >= 84 && y < 118) {
            if (x >= 0 && x < 116)
                return {PageActionKind::LanguageEnglish, {}};
            if (x >= 128 && x < 244)
                return {PageActionKind::LanguageChinese, {}};
        }
        if (y >= 180 && y < 214 && x >= 0 && x < 392) {
            const PageActionKind actions[] = {PageActionKind::AutoRemove7, PageActionKind::AutoRemove30,
                                              PageActionKind::AutoRemove90, PageActionKind::AutoRemoveNever};
            const int index = static_cast<int>(x / 98);
            if (x - index * 98 < 90)
                return {actions[index], {}};
        }
        if (y >= 352 && y < 386) {
            if (x >= 0 && x < 116)
                return {PageActionKind::ChangeDirectory, {}};
            if (x >= 128 && x < 284)
                return {PageActionKind::ResetDirectory, {}};
        }
        return {};
    }
    if (hoverItem_ < 0)
        return {};
    const auto &placement = items_[hoverItem_];
    if (placement.model == models_.size())
        return {PageActionKind::AddShortcut, {}};
    const auto &model = models_[placement.model];
    return {model.application && !model.installed ? PageActionKind::Locate : PageActionKind::Open, model.id};
}
std::string LibraryPage::ContextTool(D2D1_POINT_2F p) const {
    const auto i = HitItem(p);
    return i < 0 || items_[i].model >= models_.size() ? std::string{} : models_[items_[i].model].id;
}
std::optional<ToolTooltip> LibraryPage::Tooltip(D2D1_POINT_2F point) const {
    const auto index = HitItem(point);
    if (index < 0 || !services_.Data())
        return std::nullopt;
    const auto &item = items_[index];
    auto rect = item.rect;
    rect.top = std::max(top, rect.top + top - scroll_.Offset());
    rect.bottom = std::min(height_ - footer, rect.bottom + top - scroll_.Offset());
    if (item.model == models_.size())
        return ToolTooltip{T("shortcut.add") + L"\n" + T("shortcut.prompt"), rect};
    const auto &model = models_[item.model];
    std::wstring text = model.name;
    if (!model.description.empty())
        text += L"\n" + model.description;
    if (model.application && !model.installed)
        text += L"\n" + T("status.notConfigured");
    if (!model.risk.empty())
        text += L"\n" + model.risk;
    if (model.pinned)
        text += L"\n" + T("home.pinned");
    return ToolTooltip{std::move(text), rect};
}
bool LibraryPage::Scroll(float delta) {
    hoverItem_ = -1;
    return scroll_.Move(delta);
}
bool LibraryPage::ScrollTo(float offset) {
    hoverItem_ = -1;
    return scroll_.Set(offset);
}
void LibraryPage::UpdateIcon(std::string_view id) {
    if (const auto *d = services_.Data())
        for (auto &model : models_)
            if (model.id == id) {
                if (auto icon = d->icons.find(id); icon != d->icons.end())
                    model.icon = icon->second;
                if (auto custom = d->config.customTools.find(id); custom != d->config.customTools.end()) {
                    model.name = Utf16(custom->second.name);
                    model.monogram = model.name.empty() ? L"?" : model.name.substr(0, 1);
                }
            }
}
std::vector<std::string> LibraryPage::VisibleToolIds() const {
    std::vector<std::string> ids;
    for (const auto &card : items_)
        if (card.model < models_.size() && card.rect.bottom >= scroll_.Offset() &&
            card.rect.top <= scroll_.Offset() + ViewportHeight())
            ids.push_back(models_[card.model].id);
    return ids;
}
std::vector<D2D1_RECT_F> LibraryPage::IconRects(std::string_view id) const {
    std::vector<D2D1_RECT_F> rects;
    for (const auto &card : items_)
        if (card.model < models_.size() && models_[card.model].id == id) {
            auto r = card.rect;
            r.top += top - scroll_.Offset();
            r.bottom += top - scroll_.Offset();
            if (r.bottom > top && r.top < height_ - footer)
                rects.push_back(
                    D2D1::RectF(r.left, std::max(top, r.top), r.right, std::min(height_ - footer, r.bottom)));
        }
    return rects;
}
} // namespace poetoolbox::ui

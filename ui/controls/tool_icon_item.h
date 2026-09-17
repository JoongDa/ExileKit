#pragma once
#include "renderer/renderer.h"
#include <memory>
#include <string>
namespace poetoolbox::ui {
inline constexpr float ToolIconDip = 72;
struct ToolIconModel {
    std::string id;
    std::wstring name, description, monogram, risk;
    UINT32 color = 0x49665b;
    bool installed = false, application = false, pinned = false;
    std::shared_ptr<const IconPixels> icon;
};
inline bool Contains(D2D1_RECT_F r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
inline void DrawToolIconItem(Renderer &renderer, const ToolIconModel &model, D2D1_RECT_F r, bool hover,
                             bool addShortcut = false) {
    if (hover)
        renderer.Fill(r, 0x202731, 10);
    constexpr float iconSize = ToolIconDip;
    const float x = (r.left + r.right - iconSize) / 2, y = r.top + 8;
    const auto icon = D2D1::RectF(x, y, x + iconSize, y + iconSize);
    if (addShortcut) {
        renderer.Fill(icon, 0x282c30, 16);
        renderer.Fill(D2D1::RectF(x + 22, y + 34.5f, x + 50, y + 37.5f), 0xebd6a2, 1.5f);
        renderer.Fill(D2D1::RectF(x + 34.5f, y + 22, x + 37.5f, y + 50), 0xebd6a2, 1.5f);
    } else if (!model.icon || !renderer.Image(model.icon, icon)) {
        renderer.Fill(icon, model.color, 16);
        renderer.IconLabel(model.monogram, icon, 0xffffff, true);
    }
    renderer.IconLabel(model.name, D2D1::RectF(r.left + 4, y + iconSize + 10, r.right - 4, r.bottom - 4),
                       addShortcut ? 0xebd6a2 : 0xe7ebf0);
}
} // namespace poetoolbox::ui

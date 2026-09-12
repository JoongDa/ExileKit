#pragma once
#include "renderer/renderer.h"
#include <memory>
#include <string>
namespace poetoolbox::ui {
struct ToolCardModel {
    std::string id;
    std::wstring name, description, action, monogram, risk;
    UINT32 color = 0x49665b;
    bool favorite = false, installed = false, application = false, pinned = false;
    std::shared_ptr<const IconPixels> icon;
};
inline bool Contains(D2D1_RECT_F r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
inline void DrawToolCard(Renderer &renderer, const ToolCardModel &model, D2D1_RECT_F r, bool hover, bool selected) {
    renderer.Fill(r, selected ? 0x3b3930 : hover ? 0x2a303a : 0x1c222b, 10);
    const float x = r.left + 16, y = r.top + 16;
    if (!model.icon || !renderer.Image(model.icon, D2D1::RectF(x, y, x + 48, y + 48))) {
        renderer.Fill(D2D1::RectF(x, y, x + 48, y + 48), model.color, 8);
        renderer.Text(model.monogram, D2D1::RectF(x + 14, y + 8, x + 48, y + 40), 0xffffff, true);
    }
    renderer.Text(model.name, D2D1::RectF(x + 62, y + 1, r.right - 35, y + 49), 0xf1f3f6);
    renderer.Text(L"⋯", D2D1::RectF(r.right - 29, y - 2, r.right - 6, y + 30), 0xa8b4c4, true);
    renderer.Text(model.description, D2D1::RectF(x, y + 59, r.right - 16, y + 98), 0x98a5b7);
    renderer.Text(model.risk, D2D1::RectF(x, r.bottom - 28, r.right - 75, r.bottom - 5), 0xe0b373);
    if (model.pinned)
        renderer.Text(L"◆", D2D1::RectF(r.right - 29, r.bottom - 27, r.right - 8, r.bottom - 4), 0xebd6a2);
    else if (!model.installed && model.application)
        renderer.Text(model.action, D2D1::RectF(r.right - 102, r.bottom - 28, r.right - 12, r.bottom - 4), 0xcbb885);
}
} // namespace poetoolbox::ui

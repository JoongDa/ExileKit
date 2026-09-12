#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace poetoolbox::ui {
struct Grid final {
    static constexpr float gap = 16;
    static constexpr float cardHeight = 148;
    int columns;
    float cardWidth;
    explicit Grid(float width)
        : columns(std::max(1, static_cast<int>((width + gap) / (260 + gap)))),
          cardWidth(std::max(0.0f, (width - gap * (columns - 1)) / columns)) {}
    [[nodiscard]] float Height(size_t count) const {
        const auto rows = (count + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
        return rows ? static_cast<float>(rows) * (cardHeight + gap) - gap : 0;
    }
};
class ScrollState final {
  public:
    void SetExtent(float content, float viewport) {
        max_ = std::max(0.0f, content - viewport);
        offset_ = std::clamp(offset_, 0.0f, max_);
    }
    bool Move(float delta) { return Set(offset_ + delta); }
    bool Set(float value) {
        const auto previous = offset_;
        offset_ = std::clamp(value, 0.0f, max_);
        return previous != offset_;
    }
    [[nodiscard]] float Offset() const { return offset_; }
    [[nodiscard]] float Max() const { return max_; }

  private:
    float offset_ = 0;
    float max_ = 0;
};
} // namespace poetoolbox::ui

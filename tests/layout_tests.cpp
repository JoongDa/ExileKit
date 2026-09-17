#include "layout/grid.h"
#include <cstdlib>
#include <iostream>

using namespace poetoolbox::ui;
int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char *text) {
        if (!condition) {
            std::cerr << text << '\n';
            ++failures;
        }
    };
    // Physical client widths at every required DPI must translate into a bounded DIP grid.
    for (const float dpi : {96.0f, 120.0f, 144.0f, 168.0f, 192.0f}) {
        for (const float pixels : {720.0f, 1080.0f, 1440.0f, 1920.0f, 2560.0f, 3840.0f}) {
            const float width = std::max(1.0f, pixels * 96 / dpi - 268);
            const ToolIconGrid grid(width);
            check(grid.columns >= 1 && grid.itemWidth > 0, "ToolIconGrid must have a usable column");
            check(grid.itemWidth * grid.columns + ToolIconGrid::gap * (grid.columns - 1) <= width + 0.01f,
                  "Items overflow viewport");
            check(grid.columns == 1 || grid.itemWidth >= ToolIconGrid::minWidth,
                  "Multi-column items fall below minimum width");
            check(grid.Height(0) == 0 && grid.Height(1) == ToolIconGrid::itemHeight,
                  "Empty/single row extent incorrect");
            check(grid.Height(static_cast<size_t>(grid.columns) + 1) ==
                      2 * ToolIconGrid::itemHeight + ToolIconGrid::gap,
                  "Wrapped row extent incorrect");
        }
    }
    ScrollState scroll;
    scroll.SetExtent(1800, 500);
    check(scroll.Move(100000) && scroll.Offset() == 1300, "Scroll must clamp at bottom");
    check(!scroll.Move(1), "Unchanged scroll should not invalidate");
    scroll.SetExtent(700, 500);
    check(scroll.Offset() == 200, "Resize must clamp stale offset");
    scroll.SetExtent(100, 500);
    check(scroll.Max() == 0 && scroll.Offset() == 0, "Short content should not scroll");
    check(!scroll.Move(-1), "Scroll must clamp at top");
    if (!failures)
        std::cout << "Layout invariants passed at 100/125/150/175/200% DPI.\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

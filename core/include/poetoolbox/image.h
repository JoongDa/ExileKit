#pragma once
#include <cstdint>
#include <vector>
namespace poetoolbox {
struct IconPixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
};
} // namespace poetoolbox

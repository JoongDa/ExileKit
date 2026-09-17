#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace poetoolbox {
struct IconPixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
    // Measured before the single final downscale; retained for diagnostics.
    std::uint32_t sourceWidth = 0, sourceHeight = 0, requestedPx = 0;
    std::string source;
};
} // namespace poetoolbox

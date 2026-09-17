#pragma once
#include "poetoolbox/image.h"
#include "poetoolbox/tool.h"
#include "poetoolbox/custom_tool.h"
#include <span>
namespace poetoolbox {
class IconProvider final {
  public:
    explicit IconProvider(std::filesystem::path cache) : cache_(std::move(cache)) {}
    [[nodiscard]] Result<IconPixels> Load(const ToolManifest &tool, const std::filesystem::path &executable,
                                          const std::filesystem::path &bundledRoot, uint32_t targetPx = 64) const;
    [[nodiscard]] Result<IconPixels> LoadCustom(const CustomTool &tool, uint32_t targetPx = 64) const;
    [[nodiscard]] static Result<IconPixels> DecodeFile(const std::filesystem::path &file, uint32_t targetPx = 64);
    [[nodiscard]] static Result<IconPixels> DecodeBytes(std::span<const uint8_t> bytes, uint32_t targetPx = 64);
    [[nodiscard]] static uint32_t TargetPixels(float dip, float dpi);
    [[nodiscard]] static uint32_t SourcePixels(uint32_t targetPx);

  private:
    std::filesystem::path cache_;
};
} // namespace poetoolbox

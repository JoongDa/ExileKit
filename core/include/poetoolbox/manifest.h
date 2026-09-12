#pragma once
#include "tool.h"
#include <string_view>

namespace poetoolbox {
// UTF-8 JSON; errors contain diagnostic context, never UI or OS side effects.
[[nodiscard]] Result<ToolManifest> ParseManifest(std::string_view json);
} // namespace poetoolbox

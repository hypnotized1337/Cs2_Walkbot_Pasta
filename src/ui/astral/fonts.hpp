#pragma once

#include <imgui.h>

namespace ui::astral
{

// Returned fonts reference static embedded data; add them before building the ImFontAtlas.
[[nodiscard]] ImFont* add_inter_semibold(ImFontAtlas& atlas, float size_pixels);
[[nodiscard]] ImFont* add_inter_bold(ImFontAtlas& atlas, float size_pixels);

} // namespace ui::astral

#include <stdafx.hpp>

#include <ui/astral/fonts.hpp>

#include <ui/astral/fonts/inter.hpp>

#include <cstdio>

namespace ui::astral
{
namespace
{
[[nodiscard]] ImFont* add_inter_font(ImFontAtlas& atlas, unsigned char* data, int data_size, float size_pixels,
                                     const char* name)
{
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    font_config.PixelSnapH = true;
    std::snprintf(font_config.Name, sizeof(font_config.Name), "%s", name);
    return atlas.AddFontFromMemoryTTF(data, data_size, size_pixels, &font_config);
}
} // namespace

ImFont* add_inter_semibold(ImFontAtlas& atlas, float size_pixels)
{
    return add_inter_font(atlas, const_cast<unsigned char*>(fonts::inter_semibold),
                          static_cast<int>(fonts::inter_semibold_size), size_pixels, "Inter SemiBold");
}

ImFont* add_inter_bold(ImFontAtlas& atlas, float size_pixels)
{
    return add_inter_font(atlas, const_cast<unsigned char*>(fonts::inter_bold),
                          static_cast<int>(fonts::inter_bold_size), size_pixels, "Inter Bold");
}

} // namespace ui::astral

// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
#pragma once

#include <imgui.h>

#include <array>
#include <cstddef>
#include <vector>

#if IMGUI_VERSION_NUM < 19200
#error "icon_atlas requires Dear ImGui's 1.92 user-texture API"
#endif

namespace ui::astral
{

enum class icon_id : int
{
    mouse,
    crosshair,
    eye,
    palette,
    gear,
    list,
    code,
    search,
    gear_small,
    person,
    lightning,
    count
};

class icon_atlas
{
public:
    icon_atlas() = default;
    ~icon_atlas();
    icon_atlas(const icon_atlas&) = delete;
    icon_atlas& operator=(const icon_atlas&) = delete;

    // Rasterizes at pixel density while draw() and size() remain in logical design pixels.
    // Call outside NewFrame()/Render(); collect retired textures before NewFrame().
    void build(float density);
    void collect();

    // Call after renderer shutdown and before ImGui::DestroyContext().
    void release();

    [[nodiscard]] bool has(icon_id id) const noexcept;
    void draw(ImDrawList* draw_list, icon_id id, const ImVec2& center, ImU32 color) const;
    [[nodiscard]] ImVec2 size(icon_id id) const;

private:
    struct entry
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    std::array<entry, static_cast<std::size_t>(icon_id::count)> m_entries{};
    ImTextureData* m_texture = nullptr;
    std::vector<ImTextureData*> m_retired;
    float m_density = 0.0f;
};

} // namespace ui::astral

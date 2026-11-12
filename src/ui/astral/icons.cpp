#include <stdafx.hpp>
// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
#include <ui/astral/icons.hpp>

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui::astral
{

namespace
{

struct vec2 {
    float x, y;
};

[[nodiscard]] inline vec2 subtract(vec2 a, vec2 b) noexcept { return vec2{ a.x - b.x, a.y - b.y }; }
[[nodiscard]] inline float length(vec2 a) noexcept { return std::sqrt(a.x * a.x + a.y * a.y); }
[[nodiscard]] inline float dot(vec2 a, vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

[[nodiscard]] float circle(vec2 p, vec2 c, float r) noexcept { return length(subtract(p, c)) - r; }

[[nodiscard]] float ring(vec2 p, vec2 c, float r, float half_thickness) noexcept
{
    return std::fabs(length(subtract(p, c)) - r) - half_thickness;
}

[[nodiscard]] float box(vec2 p, vec2 c, vec2 half, float rounding) noexcept
{
    const float dx = std::fabs(p.x - c.x) - half.x + rounding;
    const float dy = std::fabs(p.y - c.y) - half.y + rounding;
    const float ox = std::max(dx, 0.0f);
    const float oy = std::max(dy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f) - rounding;
}

[[nodiscard]] float segment(vec2 p, vec2 a, vec2 b, float radius) noexcept
{
    const vec2 pa = subtract(p, a);
    const vec2 ba = subtract(b, a);
    const float h = std::clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
    return length(vec2{ pa.x - ba.x * h, pa.y - ba.y * h }) - radius;
}

// Lens of two circles (radius r, centres +-d on the x axis): tips on the y axis. Inigo Quilez' exact SDF.
[[nodiscard]] float vesica(vec2 p, float r, float d) noexcept
{
    p = vec2{ std::fabs(p.x), std::fabs(p.y) };
    const float b = std::sqrt(r * r - d * d);
    return ((p.y - b) * d > p.x * b) ? length(subtract(p, vec2{ 0.0f, b })) : length(subtract(p, vec2{ -d, 0.0f })) - r;
}

[[nodiscard]] inline float combine(float a, float b) noexcept { return std::min(a, b); }
[[nodiscard]] inline float cut(float a, float b) noexcept { return std::max(a, -b); }

struct design {
    float w, h;
};

constexpr design k_design[static_cast<std::size_t>(icon_id::count)] = {
    { 12.0f, 17.0f }, // mouse
    { 17.0f, 17.0f }, // crosshair
    { 20.0f, 15.0f }, // eye
    { 18.0f, 18.0f }, // palette
    { 18.0f, 18.0f }, // gear
    { 16.0f, 13.0f }, // list
    { 21.0f, 16.0f }, // code
    { 16.0f, 16.0f }, // search
    { 16.0f, 16.0f }, // gear_small
    { 16.0f, 18.0f }, // person
    { 16.0f, 18.0f }, // lightning
};

[[nodiscard]] float gear_sdf(vec2 p, float cx, float body, float tooth_center, vec2 tooth_half, float hole) noexcept
{
    const vec2 q = subtract(p, vec2{ cx, cx });
    const float r = length(q);
    constexpr float kSector = 6.28318530718f / 6.0f;
    const float a = std::atan2(q.y, q.x);
    const float local = a - kSector * std::round(a / kSector);
    const vec2 t{ r * std::cos(local), r * std::sin(local) };
    const float tooth = box(t, vec2{ tooth_center, 0.0f }, tooth_half, 0.7f);
    return cut(combine(r - body, tooth), r - hole);
}

// Distances in design pixels, origin at the top-left of the icon's design box.
[[nodiscard]] float sdf(icon_id icon, vec2 p) noexcept
{
    switch (icon) {
    case icon_id::mouse: {
        const float body = box(p, vec2{ 6.0f, 8.5f }, vec2{ 6.0f, 8.5f }, 5.6f);
        const float split = box(p, vec2{ 6.0f, 3.3f }, vec2{ 0.65f, 3.7f }, 0.0f);
        const float waist = box(p, vec2{ 6.0f, 7.0f }, vec2{ 6.5f, 0.65f }, 0.0f);
        return cut(body, combine(split, waist));
    }
    case icon_id::crosshair: {
        const vec2 c{ 8.5f, 8.5f };
        float d = ring(p, c, 5.75f, 1.15f);
        d = combine(d, segment(p, vec2{ 8.5f, 0.8f }, vec2{ 8.5f, 5.0f }, 1.0f));
        d = combine(d, segment(p, vec2{ 8.5f, 12.0f }, vec2{ 8.5f, 16.2f }, 1.0f));
        d = combine(d, segment(p, vec2{ 0.8f, 8.5f }, vec2{ 5.0f, 8.5f }, 1.0f));
        d = combine(d, segment(p, vec2{ 12.0f, 8.5f }, vec2{ 16.2f, 8.5f }, 1.0f));
        return combine(d, circle(p, c, 1.2f));
    }
    case icon_id::eye: {
        const vec2 c{ 10.0f, 7.5f };
        const float outer = vesica(vec2{ p.y - c.y, p.x - c.x }, 9.254f, 3.154f) - 0.9f;
        const float iris = cut(outer, circle(p, c, 4.6f));
        const float pupil = cut(circle(p, c, 3.1f), box(p, vec2{ c.x - 1.7f, c.y - 1.7f }, vec2{ 1.7f, 1.7f }, 0.0f));
        return combine(iris, pupil);
    }
    case icon_id::palette: {
        float holes = circle(p, vec2{ 13.4f, 13.1f }, 2.85f);
        holes = combine(holes, circle(p, vec2{ 4.8f, 9.6f }, 1.5f));
        holes = combine(holes, circle(p, vec2{ 5.5f, 5.4f }, 1.5f));
        holes = combine(holes, circle(p, vec2{ 9.4f, 3.6f }, 1.5f));
        holes = combine(holes, circle(p, vec2{ 13.2f, 5.4f }, 1.5f));
        return cut(circle(p, vec2{ 9.0f, 9.0f }, 8.7f), holes);
    }
    case icon_id::gear:
        return gear_sdf(p, 9.0f, 6.0f, 6.9f, vec2{ 2.05f, 2.45f }, 2.45f);
    case icon_id::gear_small:
        return gear_sdf(p, 8.0f, 5.3f, 6.1f, vec2{ 1.85f, 2.15f }, 2.15f);
    case icon_id::list: {
        float d = 1e9f;
        for (const float y : { 1.9f, 6.5f, 11.1f }) {
            d = combine(d, box(p, vec2{ 1.5f, y }, vec2{ 1.45f, 1.45f }, 0.45f));
            d = combine(d, box(p, vec2{ 10.3f, y }, vec2{ 5.7f, 1.05f }, 0.5f));
        }
        return d;
    }
    case icon_id::code: {
        float d = segment(p, vec2{ 6.2f, 3.0f }, vec2{ 1.5f, 8.0f }, 1.0f);
        d = combine(d, segment(p, vec2{ 1.5f, 8.0f }, vec2{ 6.2f, 13.0f }, 1.0f));
        d = combine(d, segment(p, vec2{ 14.8f, 3.0f }, vec2{ 19.5f, 8.0f }, 1.0f));
        d = combine(d, segment(p, vec2{ 19.5f, 8.0f }, vec2{ 14.8f, 13.0f }, 1.0f));
        return combine(d, segment(p, vec2{ 12.3f, 1.0f }, vec2{ 8.7f, 15.0f }, 0.95f));
    }
    case icon_id::search: {
        const float eye_ring = ring(p, vec2{ 9.6f, 6.4f }, 4.85f, 1.05f);
        return combine(eye_ring, segment(p, vec2{ 6.2f, 9.8f }, vec2{ 1.7f, 14.3f }, 1.25f));
    }
    case icon_id::person: {
        float d = ring(p, { 8.0f, 3.0f }, 2.0f, 0.8f);
        d = combine(d, segment(p, { 8.0f, 6.0f }, { 8.0f, 11.0f }, 0.85f));
        d = combine(d, segment(p, { 3.0f, 9.0f }, { 8.0f, 7.0f }, 0.85f));
        d = combine(d, segment(p, { 8.0f, 7.0f }, { 13.0f, 9.0f }, 0.85f));
        d = combine(d, segment(p, { 8.0f, 11.0f }, { 4.0f, 17.0f }, 0.85f));
        return combine(d, segment(p, { 8.0f, 11.0f }, { 12.0f, 17.0f }, 0.85f));
    }
    case icon_id::lightning: {
        float d = segment(p, { 10.5f, 1.0f }, { 3.0f, 10.0f }, 0.9f);
        d = combine(d, segment(p, { 3.0f, 10.0f }, { 9.0f, 10.0f }, 0.9f));
        d = combine(d, segment(p, { 9.0f, 10.0f }, { 5.5f, 17.0f }, 0.9f));
        d = combine(d, segment(p, { 5.5f, 17.0f }, { 13.0f, 7.0f }, 0.9f));
        d = combine(d, segment(p, { 13.0f, 7.0f }, { 7.0f, 7.0f }, 0.9f));
        return combine(d, segment(p, { 7.0f, 7.0f }, { 10.5f, 1.0f }, 0.9f));
    }
    case icon_id::count:
    default:
        return 1e9f;
    }
}

void rasterize(icon_id icon, float scale, unsigned char* dst, int pitch, int w, int h)
{
    const design& design = k_design[static_cast<std::size_t>(icon)];
    const float ox = (static_cast<float>(w) - design.w * scale) * 0.5f;
    const float oy = (static_cast<float>(h) - design.h * scale) * 0.5f;
    for (int y = 0; y < h; ++y) {
        unsigned char* row = dst + y * pitch;
        for (int x = 0; x < w; ++x) {
            const vec2 p{ (static_cast<float>(x) + 0.5f - ox) / scale, (static_cast<float>(y) + 0.5f - oy) / scale };
            const float coverage = std::clamp(0.5f - sdf(icon, p) * scale, 0.0f, 1.0f);
            row[x * 4 + 0] = 255;
            row[x * 4 + 1] = 255;
            row[x * 4 + 2] = 255;
            row[x * 4 + 3] = static_cast<unsigned char>(coverage * 255.0f + 0.5f);
        }
    }
}

} // namespace

icon_atlas::~icon_atlas()
{
    if (ImGui::GetCurrentContext() != nullptr) {
        release();
        return;
    }
    // Context already gone: nobody references the textures any more, only the CPU copies are left.
    if (m_texture != nullptr) IM_DELETE(m_texture);
    for (ImTextureData* t : m_retired) IM_DELETE(t);
    m_texture = nullptr;
    m_retired.clear();
}

void icon_atlas::build(float density)
{
    density = std::max(density, 0.35f);
    if (m_texture != nullptr && std::fabs(density - m_density) < 0.001f) return;
    m_density = density;

    constexpr int kPad = 1;
    const int atlas_w = density <= 2.0f ? 256 : 512;
    int x = 0, y = 0, row_h = 0;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const design& d = k_design[i];
        entry& e = m_entries[i];
        e.w = static_cast<int>(std::ceil(d.w * density)) + 2;
        e.h = static_cast<int>(std::ceil(d.h * density)) + 2;
        if (x + e.w + kPad > atlas_w) {
            x = 0;
            y += row_h + kPad;
            row_h = 0;
        }
        e.x = x + kPad;
        e.y = y + kPad;
        x += e.w + kPad;
        row_h = std::max(row_h, e.h);
    }
    int atlas_h = 32;
    while (atlas_h < y + row_h + kPad * 2) atlas_h *= 2;

    if (m_texture != nullptr && (m_texture->Width < atlas_w || m_texture->Height < atlas_h)) {
        m_texture->WantDestroyNextFrame = true;
        m_retired.push_back(m_texture);
        m_texture = nullptr;
    }

    const bool fresh = m_texture == nullptr;
    if (fresh) {
        m_texture = IM_NEW(ImTextureData)();
        m_texture->Create(ImTextureFormat_RGBA32, atlas_w, atlas_h);
    } else {
        std::memset(m_texture->GetPixels(), 0, static_cast<std::size_t>(m_texture->GetSizeInBytes()));
    }

    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const entry& e = m_entries[i];
        rasterize(static_cast<icon_id>(i), density, static_cast<unsigned char*>(m_texture->GetPixelsAt(e.x, e.y)), m_texture->GetPitch(), e.w, e.h);
    }

    const ImTextureRect all{ 0, 0, static_cast<unsigned short>(m_texture->Width), static_cast<unsigned short>(m_texture->Height) };
    m_texture->UsedRect = all;
    if (fresh) {
        ImGui::RegisterUserTexture(m_texture);
    } else if (m_texture->Status == ImTextureStatus_OK || m_texture->Status == ImTextureStatus_WantUpdates) {
        m_texture->Updates.push_back(all);
        m_texture->UpdateRect = all;
        m_texture->SetStatus(ImTextureStatus_WantUpdates);
    }
}

void icon_atlas::collect()
{
    for (std::size_t i = 0; i < m_retired.size();) {
        ImTextureData* t = m_retired[i];
        if (t->Status == ImTextureStatus_Destroyed) {
            ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
            m_retired.erase(m_retired.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void icon_atlas::release()
{
    if (m_texture != nullptr) {
        ImGui::UnregisterUserTexture(m_texture);
        IM_DELETE(m_texture);
        m_texture = nullptr;
    }
    for (ImTextureData* t : m_retired) {
        ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
    }
    m_retired.clear();
    m_density = 0.0f;
}

void icon_atlas::draw(ImDrawList* draw_list, icon_id id, const ImVec2& center, ImU32 color) const
{
    if (!has(id) || draw_list == nullptr) return;
    const entry& e = m_entries[static_cast<std::size_t>(id)];
    const float display_w = static_cast<float>(e.w) / m_density;
    const float display_h = static_cast<float>(e.h) / m_density;
    const ImVec2 p0(std::floor(center.x - display_w * 0.5f + 0.5f),
                    std::floor(center.y - display_h * 0.5f + 0.5f));
    const ImVec2 p1(p0.x + display_w, p0.y + display_h);
    const float iw = 1.0f / static_cast<float>(m_texture->Width);
    const float ih = 1.0f / static_cast<float>(m_texture->Height);
    draw_list->AddImage(m_texture->GetTexRef(), p0, p1, ImVec2(static_cast<float>(e.x) * iw, static_cast<float>(e.y) * ih),
                        ImVec2(static_cast<float>(e.x + e.w) * iw, static_cast<float>(e.y + e.h) * ih), color);
}

bool icon_atlas::has(icon_id id) const noexcept
{
    return m_texture != nullptr && id >= icon_id::mouse && id < icon_id::count;
}

ImVec2 icon_atlas::size(icon_id id) const
{
    if (id < icon_id::mouse || id >= icon_id::count) return ImVec2(0.0f, 0.0f);
    const entry& e = m_entries[static_cast<std::size_t>(id)];
    if (m_density <= 0.0f) return ImVec2(0.0f, 0.0f);
    return ImVec2(static_cast<float>(e.w) / m_density, static_cast<float>(e.h) / m_density);
}

} // namespace ui::astral

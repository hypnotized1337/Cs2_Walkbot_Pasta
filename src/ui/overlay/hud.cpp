#include <stdafx.hpp>
#include <cctype>
#include <foundation/identity.hpp>
#include <platform/windows/input.hpp>
#include <diagnostics/health.hpp>
#include <render/presentation.hpp>
#include <game/input/bindings.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <ui/presentation.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/misc/misc.hpp>
#include <features/visuals/event_log.hpp>
#include <features/visuals/visuals.hpp>
#include <render/text/localization.hpp>
#include <ui/overlay/hud.hpp>
#include <ui/overlay/overlay.hpp>
#include <ui/overlay/system_notices.hpp>
#include <imgui.h>
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>

namespace ui::hud
{
namespace trigger_debug = features::aimbot::trigger_debug;

namespace
{
constexpr ImU32 k_wm_outer = IM_COL32(3, 2, 2, 244);
constexpr ImU32 k_wm_bg = IM_COL32(13, 13, 13, 230);
constexpr ImU32 k_wm_border = IM_COL32(58, 58, 62, 255);
constexpr ImU32 k_wm_line = IM_COL32(180, 180, 185, 255);
// Notices must stay readable over any game scene, so their body is more opaque than the watermark's.
constexpr ImU32 k_wm_bg_solid = IM_COL32(13, 13, 13, 246);
constexpr ImU32 k_wm_text = IM_COL32(199, 199, 199, 255);
constexpr ImU32 k_wm_muted = IM_COL32(128, 128, 130, 255);
constexpr ImU32 k_wm_outline = IM_COL32(0, 0, 0, 220);

// The three readability panels share one spacing grid and one menu-like surface language. Keeping the values together
// prevents the watermark, spectators and active binds from drifting into three slightly different layouts.
constexpr float k_hud_pad_x = 10.0f;
constexpr float k_hud_pad_y = 3.0f;
constexpr float k_hud_title_gap = 6.0f;
constexpr float k_hud_row_gap = 3.0f;
constexpr float k_hud_rounding = 6.0f;
constexpr float k_hud_accent_width = 2.0f;

// Uppercase, matching the sidebar wordmark.
constexpr const char* k_wm_brand = "PASTA";

class cpu_sampler
{
public:
    float sample()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_next)
            return m_value;
        m_next = now + std::chrono::milliseconds(500);

        FILETIME create_ft{}, exit_ft{}, kernel_ft{}, user_ft{};
        if (!::GetProcessTimes(::GetCurrentProcess(), &create_ft, &exit_ft, &kernel_ft, &user_ft))
            return m_value;
        FILETIME wall_ft{};
        ::GetSystemTimeAsFileTime(&wall_ft);

        const auto cpu = to_ticks(kernel_ft) + to_ticks(user_ft);
        const auto wall = to_ticks(wall_ft);
        if (m_primed && wall > m_previous_wall)
        {
            const auto cpu_delta = static_cast<double>(cpu - m_previous_cpu);
            const auto wall_delta = static_cast<double>(wall - m_previous_wall);
            m_value = static_cast<float>(std::clamp(cpu_delta / wall_delta / m_cpu_count * 100.0, 0.0, 100.0));
        }
        m_previous_cpu = cpu;
        m_previous_wall = wall;
        m_primed = true;
        return m_value;
    }

private:
    static std::uint64_t to_ticks(const FILETIME& time) noexcept
    {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }

    static int logical_cpus() noexcept
    {
        SYSTEM_INFO info{};
        ::GetSystemInfo(&info);
        return std::max(1, static_cast<int>(info.dwNumberOfProcessors));
    }

    int m_cpu_count{logical_cpus()};
    std::uint64_t m_previous_cpu{};
    std::uint64_t m_previous_wall{};
    bool m_primed{};
    float m_value{};
    std::chrono::steady_clock::time_point m_next{};
};

// Scoreboard ping of the local controller, refreshed four times a second.
class ping_sampler
{
public:
    int sample()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_next)
            return m_value;
        m_next = now + std::chrono::milliseconds(250);

        const auto controller = game::local_player().controller();
        m_value = controller ? std::clamp(game::bound_memory().process.load<std::int32_t>(
                                              controller + SCHEMA("CCSPlayerController", "m_iPing"_id)),
                                          0, 999)
                             : 0;
        return m_value;
    }

private:
    int m_value{};
    std::chrono::steady_clock::time_point m_next{};
};

// Connection instability: the spread of recent ping samples relative to their mean, as 0..100.
class instability_sampler
{
public:
    float sample(int ping)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_next)
            return m_value;
        m_next = now + std::chrono::milliseconds(250);

        if (!m_primed)
        {
            m_samples.fill(ping);
            m_primed = true;
        }
        m_samples[m_head] = ping;
        m_head = (m_head + 1) % m_samples.size();

        auto mean = 0.0f;
        for (const auto sample : m_samples)
            mean += static_cast<float>(sample);
        mean /= static_cast<float>(m_samples.size());
        auto variance = 0.0f;
        for (const auto sample : m_samples)
        {
            const auto deviation = static_cast<float>(sample) - mean;
            variance += deviation * deviation;
        }
        variance /= static_cast<float>(m_samples.size());

        m_value = std::clamp(std::sqrt(variance) / mean * 140.0f, 0.0f, 100.0f);
        if (!std::isfinite(m_value))
            m_value = 0.0f;
        return m_value;
    }

private:
    std::array<int, 16> m_samples{};
    std::size_t m_head{};
    bool m_primed{};
    float m_value{};
    std::chrono::steady_clock::time_point m_next{};
};

// Frames drawn by the overlay per second, counted over half-second windows.
class frame_rate_counter
{
public:
    int tick()
    {
        ++m_frames;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_window_start >= std::chrono::milliseconds(500))
        {
            m_value = m_frames * 2;
            m_frames = 0;
            m_window_start = now;
        }
        return m_value;
    }

private:
    int m_frames{};
    int m_value{};
    std::chrono::steady_clock::time_point m_window_start{std::chrono::steady_clock::now()};
};

// Green below the low threshold, amber up to the high one, red beyond.
ImU32 threshold_color(float value, float low, float high)
{
    if (value <= low)
        return k_wm_text;
    if (value >= high)
        return IM_COL32(235, 70, 70, 255);
    const auto t = (value - low) / (high - low);
    if (t < 0.5f)
        return IM_COL32(235, 200, 90, 255);
    return IM_COL32(235, 130, 70, 255);
}

[[nodiscard]] float overlay_dpi_scale()
{
    return ui::bound_overlay().ui_dpi_scale();
}

[[nodiscard]] float readability_dpi_scale()
{
    const auto display = ImGui::GetIO().DisplaySize;
    return ui::menu::detail::current_menu_scale(display.x, display.y);
}

// The HUD reads the same live palette as the menu. Nothing here is cached, so changing an interface colour or the
// shared opacity updates the readability panels on the next frame without a restart.
[[nodiscard]] ImU32 hud_color(const foundation::rgba& color, const float opacity = 1.0f) noexcept
{
    const auto alpha =
        static_cast<int>(std::lround(static_cast<float>(color.a) * std::clamp(opacity, 0.0f, 1.0f)));
    return IM_COL32(static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), alpha);
}

[[nodiscard]] ImU32 hud_accent()
{
    return hud_color(config::general_settings.palette.accent);
}

[[nodiscard]] ImU32 hud_text()
{
    return hud_color(config::general_settings.palette.text);
}

[[nodiscard]] ImU32 hud_muted()
{
    return hud_color(config::general_settings.palette.muted_text);
}

[[nodiscard]] ImU32 hud_border()
{
    return hud_color(config::general_settings.palette.border);
}

// The menu's card headers use a light hairline, separate from the darker outer border.
[[nodiscard]] ImU32 hud_line()
{
    return hud_color(config::general_settings.palette.text, 0.90f);
}

[[nodiscard]] float hud_opacity()
{
    return std::clamp(config::general_settings.overlay_opacity, 0.25f, 1.0f);
}

[[nodiscard]] ImVec2 physical_mouse_position()
{
    POINT cursor{};
    const auto window = ui::bound_overlay().hwnd();
    if (window && ::GetCursorPos(&cursor) && ::ScreenToClient(window, &cursor))
    {
        return {static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
    }

    auto mouse = ImGui::GetIO().MousePos;
    const auto display = ImGui::GetIO().DisplaySize;
    ui::bound_menu().map_pointer_to_screen(mouse.x, mouse.y, display.x, display.y);
    return mouse;
}

class screen_ui_space final
{
public:
    screen_ui_space()
    {
        m_display = ImGui::GetIO().DisplaySize;
        const auto& overlay = ui::bound_overlay();
        if (!overlay.ui_uses_fullscreen_canvas() || overlay.ui_reference_width() == 0 ||
            overlay.ui_reference_height() == 0)
        {
            return;
        }
        m_position_scale = {m_display.x / std::max(1.0f, static_cast<float>(overlay.ui_reference_width())),
                            m_display.y / std::max(1.0f, static_cast<float>(overlay.ui_reference_height()))};
    }

    [[nodiscard]] ImVec2 display() const noexcept { return m_display; }
    [[nodiscard]] ImVec2 position_scale() const noexcept { return m_position_scale; }

private:
    ImVec2 m_display{};
    ImVec2 m_position_scale{1.0f, 1.0f};
};

[[nodiscard]] ImVec2 clamp_to_display(const ImVec2 position, const ImVec2 size, const ImVec2 display)
{
    return {std::clamp(position.x, 0.0f, std::max(0.0f, display.x - size.x)),
            std::clamp(position.y, 0.0f, std::max(0.0f, display.y - size.y))};
}

struct panel_bounds
{
    ImVec2 min{};
    ImVec2 max{};
    bool valid{};

    [[nodiscard]] bool overlaps(const panel_bounds& other) const noexcept
    {
        return valid && other.valid && min.x < other.max.x && max.x > other.min.x && min.y < other.max.y &&
               max.y > other.min.y;
    }
};

// Keep saved positions unless panels collide, including while dragging.
[[nodiscard]] ImVec2 avoid_panel_overlap(ImVec2 position, const ImVec2 size, const ImVec2 display,
                                         const panel_bounds* avoid, const std::size_t count, const float gap)
{
    position = clamp_to_display(position, size, display);
    const auto fits = [&](const ImVec2 candidate)
    {
        const panel_bounds bounds{{candidate.x - gap, candidate.y - gap},
                                   {candidate.x + size.x + gap, candidate.y + size.y + gap}, true};
        for (std::size_t index = 0; index < count; ++index)
            if (bounds.overlaps(avoid[index]))
                return false;
        return true;
    };
    if (fits(position))
        return position;

    // Consider both sides of every obstacle; pushing down alone fails at bottom-anchored panels.
    std::vector<float> xs{position.x, 0.0f, std::max(0.0f, display.x - size.x)};
    std::vector<float> ys{position.y, 0.0f, std::max(0.0f, display.y - size.y)};
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!avoid[index].valid)
            continue;
        xs.push_back(avoid[index].min.x - size.x - gap);
        xs.push_back(avoid[index].max.x + gap);
        ys.push_back(avoid[index].min.y - size.y - gap);
        ys.push_back(avoid[index].max.y + gap);
    }
    auto best = position;
    auto distance = FLT_MAX;
    for (const auto x : xs)
        for (const auto y : ys)
        {
            const auto candidate = clamp_to_display({x, y}, size, display);
            const auto dx = candidate.x - position.x, dy = candidate.y - position.y;
            const auto candidate_distance = dx * dx + dy * dy;
            if (candidate_distance < distance && fits(candidate))
            {
                best = candidate;
                distance = candidate_distance;
            }
        }
    return best;
}

void draw_outlined_text(ImDrawList* draw, ImFont* font, const float size, const ImVec2 position, const ImU32 color,
                        const char* text)
{
    if (!draw || !font || !text)
        return;
    const auto offset = std::max(1.0f, size * 0.045f);
    const std::array offsets{ImVec2{-offset, 0.0f}, ImVec2{offset, 0.0f}, ImVec2{0.0f, -offset},
                             ImVec2{0.0f, offset}};
    for (const auto& delta : offsets)
        draw->AddText(font, size, {position.x + delta.x, position.y + delta.y}, k_wm_outline, text);
    draw->AddText(font, size, position, color, text);
}

// Moves a HUD panel with the left button and, when given its scale, resizes it with the wheel. Only active while the
// menu is open and owns the mouse.
class panel_drag
{
public:
    // `origin` and `size` describe the panel as last laid out; returns the dragged origin, or `origin` when idle.
    ImVec2 update(const bool interactive, const ImVec2 origin, const ImVec2 size, float* scale = nullptr)
    {
        if (!interactive)
            return origin;
        const auto& io = ImGui::GetIO();
        const auto mouse = physical_mouse_position();
        const bool inside =
            mouse.x >= origin.x && mouse.x <= origin.x + size.x && mouse.y >= origin.y && mouse.y <= origin.y + size.y;
        if (scale && inside && !m_active && std::abs(io.MouseWheel) > 0.001f)
            *scale = std::clamp(*scale + io.MouseWheel * 0.08f, 0.55f, 2.0f);
        if (io.MouseDown[0] && !m_active && inside)
        {
            m_active = true;
            m_grab = {mouse.x - origin.x, mouse.y - origin.y};
        }
        if (!io.MouseDown[0])
            m_active = false;
        return m_active ? ImVec2{mouse.x - m_grab.x, mouse.y - m_grab.y} : origin;
    }

    [[nodiscard]] bool active() const noexcept { return m_active; }

private:
    bool m_active{};
    ImVec2 m_grab{};
};

// A font of the overlay atlas with its size, falling back to ImGui's default font.
struct hud_font
{
    ImFont* font{};
    float size{};
};

[[nodiscard]] hud_font resolve_font(const render::draw::font* wrap, const float scale)
{
    if (wrap && wrap->im_font)
        return {wrap->im_font, wrap->font_size * scale};
    return {ImGui::GetFont(), ImGui::GetFontSize() * scale};
}

// HUD text is sized in display pixels, not by a fraction of the font's atlas size. The atlas is rasterized large
// enough for the shared maximum HUD scale, so this avoids the old blurred magnification of the 18px menu font.
[[nodiscard]] hud_font resolve_hud_font(const render::draw::font* wrap, const float pixels)
{
    if (wrap && wrap->im_font)
        return {wrap->im_font, pixels};
    return {ImGui::GetFont(), pixels};
}

[[nodiscard]] ImU32 with_alpha(const ImU32 color, const float factor)
{
    const auto alpha =
        static_cast<ImU32>(std::lround(static_cast<float>(color >> 24) * std::clamp(factor, 0.0f, 1.0f)));
    return (color & 0x00ffffffu) | (alpha << 24);
}

// Soft drop shadow cast to the bottom right of a panel.
void draw_panel_shadow(ImDrawList* draw, const ImVec2 min, const ImVec2 max, const float rounding, const float step,
                       const float strength)
{
    for (int layer = 5; layer >= 1; --layer)
    {
        const auto offset = static_cast<float>(layer) * step;
        const auto alpha = static_cast<int>(strength * (1.0f - static_cast<float>(layer) / 6.0f));
        draw->AddRectFilled({min.x + offset, min.y + offset}, {max.x + offset, max.y + offset},
                            IM_COL32(0, 0, 0, alpha), rounding);
    }
}

// The menu's flat group-box frame: a near-black outer ring, a light hairline edge, then the dark surface. The HUD
// keeps a little translucency so the game remains readable underneath, but otherwise uses the same three layers.
void draw_panel_surface(ImDrawList* draw, const ImVec2 min, const ImVec2 max, const float rounding)
{
    draw->AddRectFilled(min, max, k_wm_outer, rounding);
    draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, k_wm_border,
                        std::max(0.0f, rounding - 1.0f));
    draw->AddRectFilled(min + ImVec2{2.0f, 2.0f}, max - ImVec2{2.0f, 2.0f}, k_wm_bg,
                        std::max(0.0f, rounding - 2.0f));
}

// The three readability panels use the menu's group-box frame: a dark outer ring, a hairline border, a translucent
// card fill, and one thin accent rule at the left edge. Other HUD panels keep their existing surfaces.
// Adapted from reference/menu/src/astral/{ui.cpp,menu.cpp}: rounded panel, shallow header and top accent.
void draw_readability_surface(ImDrawList* draw, const ImVec2 min, const ImVec2 max, const float metric,
                              const float header_height = 0.0f)
{
    const auto rounding = k_hud_rounding * metric;
    const auto opacity = hud_opacity();
    for (int layer = 4; layer > 0; --layer)
    {
        const auto spread = static_cast<float>(layer) * 3.5f * metric;
        draw->AddRectFilled({min.x - spread, min.y - spread + 4.0f * metric},
                            {max.x + spread, max.y + spread + 4.0f * metric},
                            IM_COL32(0, 0, 0, static_cast<int>(12.0f * opacity)), rounding + spread);
    }
    // Keep a readable floor even when the shared HUD opacity is low.
    const auto alpha = static_cast<int>(255.0f * std::clamp(opacity, 0.88f, 0.94f));
    draw->AddRectFilled(min, max, IM_COL32(0x25, 0x29, 0x35, alpha), rounding);
    if (header_height > 0.0f)
    {
        draw->AddRectFilled(min, {max.x, min.y + header_height}, IM_COL32(0x2f, 0x33, 0x41, alpha),
                            rounding, ImDrawFlags_RoundCornersTop);
    }
    else
    {
        draw->AddRectFilled(min, {max.x, min.y + k_hud_accent_width * metric}, hud_accent(), rounding,
                            ImDrawFlags_RoundCornersTop);
    }
    draw->AddRect(min, max, IM_COL32(0x3a, 0x3e, 0x4c, static_cast<int>(150.0f * opacity)), rounding, 0, metric);
}

constexpr ImU32 k_astral_hud_text = IM_COL32(201, 204, 212, 255);

void draw_readability_text(ImDrawList* draw, ImFont* font, const float size, const ImVec2 pos,
                           const ImU32 color, const char* text)
{
    draw->AddText(font, size, {std::round(pos.x), std::round(pos.y)}, color, text);
}

// Everything the HUD keeps between frames. All of it is touched on the render thread only.
struct hud_state
{
    frame_rate_counter frame_rate;
    cpu_sampler cpu;
    ping_sampler ping;
    instability_sampler instability;
    panel_drag watermark;
    ImVec2 watermark_size{};
    panel_drag spectators;
    panel_drag events;
    panel_drag keybinds;
    panel_drag bomb;
    // Panels drawn earlier this frame, used only to keep the larger readability overlays from covering each other.
    int panel_frame{-1};
    panel_bounds watermark_bounds{};
    panel_bounds spectator_bounds{};
    panel_bounds keybind_bounds{};
    ui::notices::dismissals closed_notices;
    std::uint64_t loading_serial{};
    std::chrono::steady_clock::time_point loading_since{};

    void begin_panel_frame()
    {
        const auto frame = ImGui::GetFrameCount();
        if (panel_frame == frame)
            return;
        panel_frame = frame;
        watermark_bounds = {};
        spectator_bounds = {};
        keybind_bounds = {};
    }
};

struct compact_panel_row
{
    std::string name{};
    std::string state{};
    bool active{};
};

[[nodiscard]] ImVec2 resolve_screen_layout(const config::general_profile::screen_layout& layout,
                                           const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
                                           const ImVec2 position_scale)
{
    const auto x_offset = std::max(0.0f, layout.offset_x) * dpi_scale * position_scale.x;
    const auto y_offset = std::max(0.0f, layout.offset_y) * dpi_scale * position_scale.y;
    float x = x_offset;
    float y = y_offset;
    using anchor = config::general_profile::screen_anchor;
    if (layout.anchor == anchor::top_right || layout.anchor == anchor::bottom_right)
    {
        x = display.x - panel_size.x - x_offset;
    }
    if (layout.anchor == anchor::bottom_left || layout.anchor == anchor::bottom_right)
    {
        y = display.y - panel_size.y - y_offset;
    }
    return {std::clamp(x, 0.0f, std::max(0.0f, display.x - panel_size.x)),
            std::clamp(y, 0.0f, std::max(0.0f, display.y - panel_size.y))};
}

void capture_screen_layout(config::general_profile::screen_layout& layout, const ImVec2 position,
                           const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
                           const ImVec2 position_scale)
{
    using anchor = config::general_profile::screen_anchor;
    const bool right = position.x + panel_size.x * 0.5f > display.x * 0.5f;
    const bool bottom = position.y + panel_size.y * 0.5f > display.y * 0.5f;
    layout.anchor =
        bottom ? (right ? anchor::bottom_right : anchor::bottom_left) : (right ? anchor::top_right : anchor::top_left);
    const auto safe_x = std::max(0.01f, dpi_scale * position_scale.x);
    const auto safe_y = std::max(0.01f, dpi_scale * position_scale.y);
    layout.offset_x = std::max(0.0f, right ? display.x - position.x - panel_size.x : position.x) / safe_x;
    layout.offset_y = std::max(0.0f, bottom ? display.y - position.y - panel_size.y : position.y) / safe_y;
    layout.version = 1;
}

void migrate_screen_layout(config::general_profile::screen_layout& layout, const ImVec2 panel_size,
                           const ImVec2 display, const float dpi_scale, const ImVec2 position_scale)
{
    if (layout.version >= 1)
        return;
    capture_screen_layout(layout, {layout.legacy_position_x, layout.legacy_position_y}, panel_size, display, dpi_scale,
                          position_scale);
}

[[nodiscard]] float text_width(ImFont* font, const float size, const char* text)
{
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

// Baseline that centers the visible ink of `parts` (not the font's line box) vertically in a box.
[[nodiscard]] float ink_centered_y(ImFont* font, const float size, const std::initializer_list<const char*> parts,
                                   const float box_top, const float box_height)
{
    auto* baked = font->GetFontBaked(size);
    float top = FLT_MAX, bottom = -FLT_MAX;
    if (baked)
    {
        for (const char* part : parts)
            for (const char* c = part; *c; ++c)
            {
                const auto* glyph = baked->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(*c)));
                if (!glyph || !glyph->Visible)
                    continue;
                top = std::min(top, glyph->Y0);
                bottom = std::max(bottom, glyph->Y1);
            }
    }
    if (top > bottom)
    {
        top = 0.0f;
        bottom = size;
    }
    return box_top + (box_height - (bottom - top)) * 0.5f - top;
}

struct watermark_chip
{
    char value[24]{};
    ImU32 value_color{};
};

struct watermark_chips
{
    std::array<watermark_chip, 4> items{};
    int count{};

    [[nodiscard]] std::span<const watermark_chip> view() const
    {
        return {items.data(), static_cast<std::size_t>(count)};
    }
};

[[nodiscard]] watermark_chips collect_chips(const config::general_profile::watermark& cfg, const int fps,
                                            const int ping, const float loss, const float cpu)
{
    watermark_chips chips{};
    if (cfg.show_fps)
    {
        auto& c = chips.items[chips.count++];
        std::snprintf(c.value, sizeof(c.value), "%d FPS", fps);
        c.value_color = k_wm_text;
    }
    if (cfg.show_ping)
    {
        auto& c = chips.items[chips.count++];
        std::snprintf(c.value, sizeof(c.value), "Ms %d", ping);
        c.value_color = threshold_color(static_cast<float>(ping), 80.0f, 150.0f);
    }
    if (cfg.show_loss)
    {
        auto& c = chips.items[chips.count++];
        std::snprintf(c.value, sizeof(c.value), "Loss %.0f%%", loss);
        c.value_color = threshold_color(loss, 2.0f, 15.0f);
    }
    if (cfg.show_cpu)
    {
        auto& c = chips.items[chips.count++];
        std::snprintf(c.value, sizeof(c.value), "CPU %.1f%%", cpu);
        c.value_color = threshold_color(cpu, 8.0f, 25.0f);
    }
    return chips;
}

// Fonts and spacing of the single-line watermark at the current scale.
struct watermark_metrics
{
    ImFont* brand_font{};
    float brand_size{};
    ImFont* text_font{};
    float text_size{};
    float scale{}; // DPI times the user's scale
    float brand_width{};
    float h_pad_left{};
    float h_gap{};
    ImVec2 horizontal_size{};

    [[nodiscard]] float chip_width(const watermark_chip& c) const
    {
        const auto value = text_width(text_font, text_size, c.value);
        return value;
    }
};

// "PASTA" uses the menu's brand font (as the sidebar logo) at a much smaller size; the stats use its regular font.
[[nodiscard]] watermark_metrics measure_watermark(const float dpi_scale, const float user_scale,
                                                  const watermark_chips& chips)
{
    watermark_metrics m{};
    m.scale = dpi_scale * user_scale;
    m.brand_font = resolve_font(render::fonts().menu_brand_30, 1.0f).font;
    m.brand_size = k_hud_brand_size * m.scale;
    const auto text_style = resolve_hud_font(render::fonts().hud_text_40, k_hud_text_size * m.scale);
    m.text_font = text_style.font;
    m.text_size = text_style.size;
    m.brand_width = text_width(m.brand_font, m.brand_size, k_wm_brand);

    m.h_pad_left = k_hud_pad_x * m.scale;
    const auto h_pad_right = k_hud_pad_x * m.scale;
    const auto h_pad_y = k_hud_pad_y * m.scale;
    m.h_gap = 8.0f * m.scale;
    auto h_content = m.brand_width;
    for (int i = 0; i < chips.count; ++i)
        h_content += m.h_gap + m.chip_width(chips.items[i]);
    m.horizontal_size = {h_content + m.h_pad_left + h_pad_right, std::max(m.brand_size, m.text_size) + h_pad_y * 2.0f};

    return m;
}

struct watermark_placement
{
    ImVec2 min{};
    ImVec2 size{};
};

// Keep saved anchors and dragging; the compact Astral watermark is always one line.
[[nodiscard]] watermark_placement place_watermark(config::general_profile::watermark& cfg, const watermark_metrics& m,
                                                  panel_drag& drag, ImVec2& animated_size, const bool interactive,
                                                  const float dpi_scale)
{
    const screen_ui_space ui_space{};
    const auto display = ui_space.display();
    const auto position_scale = ui_space.position_scale();
    const auto target = m.horizontal_size;
    // Grow immediately so newly enabled fields are never clipped during the shrink animation.
    const auto speed = std::clamp(ImGui::GetIO().DeltaTime * 14.0f, 0.0f, 1.0f);
    animated_size.x = animated_size.x < target.x ? target.x : animated_size.x + (target.x - animated_size.x) * speed;
    animated_size.y = target.y;
    migrate_screen_layout(cfg.layout, animated_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(cfg.layout, animated_size, display, dpi_scale, position_scale);
    position = clamp_to_display(drag.update(interactive, position, animated_size, &cfg.layout.scale), animated_size, display);
    if (drag.active())
        capture_screen_layout(cfg.layout, position, animated_size, display, dpi_scale, position_scale);
    return {position, animated_size};
}

void draw_watermark_horizontal(ImDrawList* draw, const watermark_metrics& m, const watermark_chips& chips,
                                const ImVec2 min, const ImVec2 max)
{
    const auto center_y = (min.y + max.y) * 0.5f;
    auto x = min.x + m.h_pad_left;
    draw_readability_text(draw, m.brand_font, m.brand_size, {x, center_y - m.brand_size * 0.5f},
                           k_astral_hud_text, k_wm_brand);
    x += m.brand_width;
    for (const auto& chip : chips.view())
    {
        x += m.h_gap;
        draw_readability_text(draw, m.text_font, m.text_size, {x, center_y - m.text_size * 0.5f},
                               chip.value_color == k_wm_text ? k_astral_hud_text : chip.value_color, chip.value);
        x += m.chip_width(chip);
    }
}

} // namespace

struct panels::state : hud_state
{
};

panels::panels()
    : m_state(std::make_unique<state>())
{
}

panels::~panels() = default;

void panels::draw_watermark(const bool interactive)
{
    auto& cfg = config::general_settings.m_watermark;
    if (!cfg.enabled)
        return;

    auto& state = *m_state;
    state.begin_panel_frame();
    const auto fps = state.frame_rate.tick();
    const auto ping = state.ping.sample();
    const auto loss = state.instability.sample(ping);
    const auto cpu = state.cpu.sample();
    const auto chips = collect_chips(cfg, fps, ping, loss, cpu);

    const auto dpi_scale = readability_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto metrics = measure_watermark(dpi_scale, cfg.layout.scale, chips);
    const auto placement = place_watermark(cfg, metrics, state.watermark, state.watermark_size, interactive, dpi_scale);

    const auto min = placement.min;
    const auto max = placement.min + placement.size;
    state.watermark_bounds = {min, max, true};
    auto* draw = ImGui::GetForegroundDrawList();
    draw_readability_surface(draw, min, max, metrics.scale);

    // Content is clipped to the animated panel so it reveals/hides cleanly while
    // the box grows or shrinks.
    draw->PushClipRect(min, max, true);
    draw_watermark_horizontal(draw, metrics, chips, min, max);
    draw->PopClipRect();
}

void panels::draw_spectator_list(bool interactive)
{
    auto& cfg = config::general_settings.m_spectator_list;
    if (!cfg.enabled)
    {
        return;
    }

    // The 200ms observer-chain scan is collected on the game worker. Rendering
    // consumes an immutable snapshot and performs no process-memory calls.
    const auto specs = game::world().spectators();

    auto& state = *m_state;
    state.begin_panel_frame();
    const auto dpi_scale = readability_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto user_scale = cfg.layout.scale;
    const auto metric_scale = dpi_scale * user_scale;
    const auto title_size = k_hud_title_size * metric_scale;
    const auto title_font = resolve_hud_font(render::fonts().menu_brand_30, title_size).font;
    const auto text_size = k_hud_text_size * metric_scale;
    const auto text_font = resolve_hud_font(render::fonts().hud_text_40, text_size).font;

    const auto measure = [](ImFont* font, float size, const char* text)
    { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x; };

    const auto title = std::string{render::localization::tr("Spectators")};
    const auto count = std::to_string(specs->size());
    const float pad_x = k_hud_pad_x * metric_scale;
    const float pad_y = k_hud_pad_y * metric_scale;
    const float row_gap = k_hud_row_gap * metric_scale;
    const float title_gap = k_hud_title_gap * metric_scale;
    const float row_height = text_size;
    const auto title_w = measure(title_font, title_size, title.c_str());
    const auto count_w = measure(title_font, title_size, count.c_str());
    float inner_w = title_w + 2.0f * (title_gap + count_w);
    for (const auto& s : *specs)
    {
        const float w = measure(text_font, text_size, s.name.c_str());
        inner_w = std::max(inner_w, w);
    }
    inner_w = std::max(inner_w, 170.0f * metric_scale);

    const int row_count = static_cast<int>(specs->size());
    const float panel_w = inner_w + pad_x * 2.0f;
    const float panel_h = pad_y * 2.0f + title_size +
                          (row_count > 0 ? title_gap + row_count * row_height + (row_count - 1) * row_gap : 0.0f);

    const screen_ui_space ui_space{};
    const auto display = ui_space.display();
    const auto position_scale = ui_space.position_scale();
    const ImVec2 panel_size{panel_w, panel_h};
    migrate_screen_layout(cfg.layout, panel_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(cfg.layout, panel_size, display, dpi_scale, position_scale);
    position = clamp_to_display(state.spectators.update(interactive, position, panel_size, &cfg.layout.scale),
                                panel_size, display);
    const panel_bounds avoid[]{state.watermark_bounds};
    position = avoid_panel_overlap(position, panel_size, display, avoid, std::size(avoid), 8.0f * metric_scale);
    position = clamp_to_display(position, panel_size, display);
    if (state.spectators.active())
        capture_screen_layout(cfg.layout, position, panel_size, display, dpi_scale, position_scale);
    state.spectator_bounds = {{position.x, position.y},
                              {position.x + panel_size.x, position.y + panel_size.y},
                              true};

    const ImVec2 draw_min = position;
    const ImVec2 draw_max{position.x + panel_w, position.y + panel_h};

    auto* draw = ImGui::GetForegroundDrawList();
    draw_readability_surface(draw, draw_min, draw_max, metric_scale, title_size + pad_y * 2.0f);

    draw->PushClipRect(draw_min, draw_max, true);
    float y = draw_min.y + pad_y;
    draw_readability_text(draw, title_font, title_size, {draw_min.x + (panel_w - title_w) * 0.5f, y}, IM_COL32_WHITE, title.c_str());
    draw_readability_text(draw, title_font, title_size, {draw_max.x - pad_x - count_w, y},
                       row_count > 0 ? hud_accent() : hud_muted(), count.c_str());
    y += title_size + title_gap;
    for (const auto& s : *specs)
    {
        const auto name_x = draw_min.x + pad_x;
        const auto text_y = y + (row_height - text_size) * 0.5f;
        draw_readability_text(draw, text_font, text_size, {name_x, text_y}, k_astral_hud_text, s.name.c_str());
        y += row_height + row_gap;
    }
    draw->PopClipRect();
}

namespace
{
panel_bounds draw_compact_panel(const char* title, const std::vector<compact_panel_row>& rows,
                        config::general_profile::screen_layout& layout, panel_drag& drag, const bool interactive,
                        const panel_bounds* const avoid, const std::size_t avoid_count)
{
    if (rows.empty() && !interactive)
        return {};
    const auto dpi_scale = readability_dpi_scale();
    layout.scale = std::clamp(layout.scale, 0.55f, 2.0f);
    const auto metric = dpi_scale * layout.scale;
    const auto title_size = k_hud_title_size * metric;
    const auto title_font = resolve_hud_font(render::fonts().menu_brand_30, title_size).font;
    const auto text_size = k_hud_text_size * metric;
    const auto text_font = resolve_hud_font(render::fonts().hud_text_40, text_size).font;
    const auto width_of = [](ImFont* font, float size, const char* value)
    { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x; };
    const auto pad_x = k_hud_pad_x * metric, pad_y = k_hud_pad_y * metric;
    const auto gap = k_hud_row_gap * metric, title_gap = k_hud_title_gap * metric;
    const auto count = std::to_string(rows.size());
    const auto count_w = width_of(title_font, title_size, count.c_str());
    auto inner_w = std::max(170.0f * metric, width_of(title_font, title_size, title) + 2.0f * (title_gap + count_w));
    for (const auto& row : rows)
    {
        const auto state_w = width_of(text_font, text_size, row.state.c_str());
        inner_w = std::max(inner_w, width_of(text_font, text_size, row.name.c_str()) + 24.0f * metric + state_w);
    }
    const auto row_count = std::max<std::size_t>(rows.size(), interactive ? 1 : 0);
    const ImVec2 panel_size{inner_w + pad_x * 2.0f, pad_y * 2.0f + title_size + title_gap + row_count * text_size +
                                                        (row_count ? row_count - 1 : 0) * gap};
    const screen_ui_space ui_space{};
    const auto display = ui_space.display(), position_scale = ui_space.position_scale();
    migrate_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    position = clamp_to_display(drag.update(interactive, position, panel_size), panel_size, display);
    position = avoid_panel_overlap(position, panel_size, display, avoid, avoid_count, 8.0f * metric);
    position = clamp_to_display(position, panel_size, display);
    if (drag.active())
        capture_screen_layout(layout, position, panel_size, display, dpi_scale, position_scale);
    auto* draw = ImGui::GetForegroundDrawList();
    const ImVec2 maximum{position.x + panel_size.x, position.y + panel_size.y};
    draw_readability_surface(draw, position, maximum, metric, title_size + pad_y * 2.0f);
    auto y = position.y + pad_y;
    draw_readability_text(draw, title_font, title_size, {position.x + (panel_size.x - width_of(title_font, title_size, title)) * 0.5f, y}, IM_COL32_WHITE, title);
    draw_readability_text(draw, title_font, title_size, {maximum.x - pad_x - count_w, y},
                       rows.empty() ? hud_muted() : hud_accent(), count.c_str());
    y += title_size + title_gap;
    if (rows.empty())
        draw_readability_text(draw, text_font, text_size, {position.x + pad_x, y}, hud_muted(),
                           render::localization::tr("No active entries"));
    else
        for (const auto& row : rows)
        {
            const auto state_w = width_of(text_font, text_size, row.state.c_str());
            const auto state_x = maximum.x - pad_x - state_w;
            draw_readability_text(draw, text_font, text_size, {position.x + pad_x, y},
                               row.active ? k_astral_hud_text : hud_muted(), row.name.c_str());
            if (row.active)
                draw->AddCircleFilled({state_x - 6.0f * metric, y + text_size * 0.5f},
                                      std::max(1.0f, 1.7f * metric), hud_accent(), 12);
            draw_readability_text(draw, text_font, text_size, {state_x, y},
                               row.active ? hud_accent() : hud_muted(), row.state.c_str());
            y += text_size + gap;
        }
    return {position, maximum, true};
}
} // namespace

void panels::draw_event_log(const bool interactive)
{
    auto& cfg = config::general_settings.m_event_log;
    if (!cfg.enabled)
        return;
    const auto maximum = std::clamp(cfg.max_entries, 1, 5);
    auto entries = features::visuals::event_log().snapshot(cfg.duration, maximum);
    if (entries.empty() && !interactive)
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto dpi = overlay_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto metric = dpi * cfg.layout.scale;
    const auto style = resolve_font(render::fonts().menu_regular_12, cfg.layout.scale);
    auto* font = style.font;
    const auto font_size = style.size;
    const auto row_height = std::max(30.0f * metric, font_size + 16.0f * metric);
    const auto gap = 6.0f * metric;
    auto width = 172.0f * metric;
    for (const auto& entry : entries)
        width = std::max(width, font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, entry.text.c_str()).x + 30.0f * metric);
    const auto visible_rows = std::max<std::size_t>(entries.size(), interactive ? 1 : 0);
    const ImVec2 bounds{width, visible_rows * row_height + (visible_rows ? visible_rows - 1 : 0) * gap};
    const screen_ui_space ui_space{};
    const auto display = ui_space.display(), position_scale = ui_space.position_scale();
    migrate_screen_layout(cfg.layout, bounds, display, dpi, position_scale);
    auto anchor = resolve_screen_layout(cfg.layout, bounds, display, dpi, position_scale);
    anchor = clamp_to_display(m_state->events.update(interactive, anchor, bounds), bounds, display);
    if (m_state->events.active())
        capture_screen_layout(cfg.layout, anchor, bounds, display, dpi, position_scale);
    auto* draw = ImGui::GetForegroundDrawList();
    if (entries.empty())
    {
        const ImVec2 maximum_pos{anchor.x + width, anchor.y + row_height};
        draw->AddRectFilled(anchor, maximum_pos, k_wm_bg, 9.0f * metric);
        draw->AddRect(anchor, maximum_pos, k_wm_border, 9.0f * metric);
        draw->AddText(font, font_size, anchor + ImVec2{15.0f * metric, (row_height - font_size) * 0.5f}, k_wm_muted,
                      render::localization::tr("No active entries"));
        return;
    }
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        const auto age = std::chrono::duration<float>(now - entry.timestamp).count();
        const auto lifetime = std::max(0.5f, cfg.duration);
        const auto fade_in = std::clamp(age / 0.16f, 0.0f, 1.0f);
        const auto fade_out = std::clamp((lifetime - age) / 0.55f, 0.0f, 1.0f);
        const auto alpha = fade_in * fade_in * (3.0f - 2.0f * fade_in) * fade_out * fade_out * (3.0f - 2.0f * fade_out);
        const auto rise = std::clamp(age / lifetime, 0.0f, 1.0f) * 8.0f * metric;
        const auto slide = (1.0f - fade_in) * 18.0f * metric;
        const ImVec2 minimum{anchor.x + slide, anchor.y + static_cast<float>(i) * (row_height + gap) - rise};
        const ImVec2 maximum_pos{minimum.x + width, minimum.y + row_height};
        const auto accent = entry.kind == features::visuals::event_kind::kill      ? IM_COL32(255, 112, 135, 255)
                            : entry.kind == features::visuals::event_kind::hit     ? IM_COL32(120, 215, 255, 255)
                            : entry.kind == features::visuals::event_kind::blocked ? IM_COL32(245, 190, 95, 255)
                                                                                   : IM_COL32(180, 188, 215, 255);
        draw->AddRectFilled(minimum, maximum_pos, with_alpha(k_wm_bg, alpha), 9.0f * metric);
        draw->AddRect(minimum, maximum_pos, with_alpha(k_wm_border, alpha), 9.0f * metric, ImDrawFlags_None,
                      std::max(1.0f, metric));
        draw->AddRectFilled(minimum, {minimum.x + 3.0f * metric, maximum_pos.y}, with_alpha(accent, alpha),
                            9.0f * metric, ImDrawFlags_RoundCornersLeft);
        draw->AddText(font, font_size, minimum + ImVec2{15.0f * metric, (row_height - font_size) * 0.5f},
                      with_alpha(k_wm_text, alpha), entry.text.c_str());
    }
}


// A system message plate in the top-left corner: always the same place and look, not configurable. It reports
// what the player needs to know right now (map geometry missing, compatibility mode, keyboard hook, previous
// crash). Each plate can be closed with its cross while the menu is open; the overlay ignores the mouse otherwise.
void panels::draw_system_notices(const bool interactive)
{
    using diagnostics::readiness::state;
    auto& closed = m_state->closed_notices;
    auto& loading_serial = m_state->loading_serial;
    auto& loading_since = m_state->loading_since;
    const auto now = std::chrono::steady_clock::now();
    auto& health = diagnostics::health();

    ui::notices::inputs input;
    input.geometry = health.geometry.snapshot();
    input.overlay = health.overlay.snapshot();
    input.previous_session_crashed = health.previous_session_crashed.load(std::memory_order_acquire);
    input.input_hook_failed = platform::windows::bound_input().hook_failed();
    if (input.geometry.value == state::loading)
    {
        if (loading_serial != input.geometry.serial)
        {
            loading_serial = input.geometry.serial;
            loading_since = now;
        }
        input.geometry_loading_seconds = std::chrono::duration<double>(now - loading_since).count();
    }
    else
        loading_serial = 0;

    const auto shown = closed.visible(ui::notices::collect(input));
    if (shown.empty())
        return;

    const auto metric = overlay_dpi_scale();
    const auto style = resolve_font(render::fonts().menu_regular_12, 1.1f);
    auto* font = style.font;
    const auto text_size = style.size;
    const auto title_size = text_size * 1.05f;
    const float width = 380.0f * metric, pad = 14.0f * metric, icon = 22.0f * metric, gap = 8.0f * metric;
    const float text_width = width - pad * 2.0f - 4.0f * metric - 24.0f * metric;
    const auto mouse = physical_mouse_position();
    const bool clicked = interactive && ImGui::IsMouseClicked(0);
    auto* draw = ImGui::GetForegroundDrawList();
    float y = 18.0f * metric;
    const float x = 18.0f * metric;
    const ui::notices::notice* to_close = nullptr;

    for (const auto& item : shown)
    {
        const auto* text = render::localization::tr(item.text);
        const auto measured = font->CalcTextSizeA(text_size, FLT_MAX, text_width, text);
        const float height = pad * 2.0f + title_size + 4.0f * metric + measured.y +
                             (interactive ? 0.0f : text_size * 0.9f + 4.0f * metric);
        const ImVec2 minimum{x, y}, maximum{x + width, y + height};
        const ImU32 accent = item.severity == ui::notices::level::error     ? IM_COL32(255, 112, 135, 255)
                             : item.severity == ui::notices::level::warning ? IM_COL32(245, 190, 95, 255)
                                                                            : IM_COL32(167, 139, 250, 255);
        const char* title = item.severity == ui::notices::level::error     ? "Error"
                            : item.severity == ui::notices::level::warning ? "Warning"
                                                                           : "Notice";
        // Same panel language as the event log and watermark: translucent dark body, hairline border, 3 px accent bar.
        const float rounding = 9.0f * metric;
        draw->AddRectFilled(minimum, maximum, k_wm_bg_solid, rounding);
        draw->AddRect(minimum, maximum, k_wm_border, rounding, ImDrawFlags_None, std::max(1.0f, metric));
        draw->AddRectFilled(minimum, {minimum.x + 3.0f * metric, maximum.y}, accent, rounding,
                            ImDrawFlags_RoundCornersLeft);
        const float text_x = minimum.x + pad + 4.0f * metric;
        draw->AddText(font, title_size, {text_x, minimum.y + pad}, accent, render::localization::tr(title));
        draw->AddText(font, text_size, {text_x, minimum.y + pad + title_size + 4.0f * metric}, k_wm_text, text, nullptr,
                      text_width);
        if (!interactive)
            draw->AddText(font, text_size * 0.9f, {text_x, maximum.y - pad - text_size * 0.9f}, k_wm_muted,
                          render::localization::tr("Open the menu to close this message."));

        // Close cross.
        const float cross = 9.0f * metric;
        const ImVec2 cross_center{maximum.x - pad - cross * 0.5f, minimum.y + pad + icon * 0.5f};
        const bool hovered = interactive && mouse.x >= cross_center.x - cross && mouse.x <= cross_center.x + cross &&
                             mouse.y >= cross_center.y - cross && mouse.y <= cross_center.y + cross;
        const ImU32 cross_color = interactive ? (hovered ? k_wm_text : k_wm_muted) : with_alpha(k_wm_muted, 0.45f);
        const float arm = cross * 0.5f;
        draw->AddLine({cross_center.x - arm, cross_center.y - arm}, {cross_center.x + arm, cross_center.y + arm},
                      cross_color, std::max(1.5f, 1.6f * metric));
        draw->AddLine({cross_center.x - arm, cross_center.y + arm}, {cross_center.x + arm, cross_center.y - arm},
                      cross_color, std::max(1.5f, 1.6f * metric));
        if (hovered && clicked)
            to_close = &item;
        y = maximum.y + gap;
    }
    if (to_close)
        closed.close(*to_close);
}

void panels::draw_trigger_debug(const bool interactive)
{
    (void)interactive;
    if (!config::general_settings.trigger_debug_overlay)
        return;
    const auto debug = features::aimbot::aim().trigger_debug_snapshot();
    if (!debug.enabled)
        return;

    const auto metric = overlay_dpi_scale();
    const auto title_size = k_hud_title_size * metric;
    const auto text_size = k_hud_text_size * metric;
    const auto title_font = resolve_hud_font(render::fonts().hud_text_40, title_size).font;
    const auto text_font = resolve_hud_font(render::fonts().hud_text_40, text_size).font;
    const auto width_of = [](ImFont* font, const float size, const char* text)
    { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x; };

    const auto green = IM_COL32(92, 226, 120, 255);
    const auto red = IM_COL32(242, 88, 82, 255);
    const auto yellow = IM_COL32(242, 208, 82, 255);
    const auto bright_red = IM_COL32(255, 38, 38, 255);
    const auto color_for = [](const trigger_debug::decision state, const trigger_debug::reason why)
    {
        if (state == trigger_debug::decision::fire)
            return green;
        if (state == trigger_debug::decision::waiting || trigger_debug::is_waiting(why))
            return yellow;
        return red;
    };
    const auto state_color = debug.stuck ? bright_red : color_for(debug.state, debug.why);

    auto* draw = ImGui::GetForegroundDrawList();
    for (std::size_t index = 0; index < debug.target_count; ++index)
    {
        const auto& target = debug.targets[index];
        if (target.pawn == 0)
            continue;
        const auto screen = game::camera().project(target.label);
        if (!game::camera().projection_valid(screen))
            continue;
        const auto target_state = target.on_target && target.why == trigger_debug::reason::fire
                                      ? trigger_debug::decision::fire
                                      : trigger_debug::is_waiting(target.why) ? trigger_debug::decision::waiting
                                                                              : trigger_debug::decision::blocked;
        const auto color = debug.stuck && target.pawn == debug.pawn ? bright_red : color_for(target_state, target.why);
        const auto text = std::format("{} hb{}/{} {:.0f}ms", trigger_debug::name(target.why), target.hitbox,
                                      target.hitgroup, target.target_age_ms);
        draw_outlined_text(draw, text_font, text_size,
                           {screen.x + 8.0f * metric, screen.y - text_size * 0.5f}, color, text.c_str());
    }

    std::vector<std::string> lines{};
    lines.push_back(std::format("{}", trigger_debug::name(debug.state)));
    lines.push_back(std::format("reason {}", trigger_debug::name(debug.why)));
    lines.push_back(std::format("state {:.0f} ms", debug.state_ms));
    lines.push_back(std::format("on target {:.0f} ms", debug.on_target_ms));
    if (debug.stuck)
        lines.push_back("STUCK - state did not advance");
    if (debug.delay_ms > 0.0f)
        lines.push_back(std::format("delay {:.0f} ms", debug.delay_ms));
    if (debug.cooldown_ms > 0.0f)
        lines.push_back(std::format("cooldown {:.0f} ms", debug.cooldown_ms));
    if (debug.target_age_ms > 0.0f)
        lines.push_back(std::format("target age {:.0f} ms", debug.target_age_ms));
    if (debug.pawn != 0)
        lines.push_back(std::format("hb {} parts 0x{:x} dmg {:.0f} hp {}", debug.hitbox, debug.required_parts,
                                    debug.damage, debug.target_health));
    if (debug.required_hitchance > 0.0f)
        lines.push_back(std::format("hitchance {:.0f} / {:.0f}", debug.hitchance, debug.required_hitchance));
    if (debug.last_shot_ms >= 0.0f)
        lines.push_back(std::format("last shot {:.0f} ms ago", debug.last_shot_ms));
    if (debug.update_age_ms > 250.0f)
        lines.push_back(std::format("last update {:.0f} ms ago", debug.update_age_ms));
    if (debug.recent_count > 0)
    {
        lines.push_back("recent");
        const auto first = debug.recent_count > 3 ? debug.recent_count - 3 : std::size_t{};
        for (auto index = first; index < debug.recent_count; ++index)
        {
            const auto& entry = debug.recent[index];
            lines.push_back(std::format("  {} {}", trigger_debug::name(entry.state),
                                        trigger_debug::name(entry.why)));
        }
    }

    const auto pad = 10.0f * metric;
    auto width = width_of(title_font, title_size, "TRIGGER DEBUG");
    for (const auto& line : lines)
        width = std::max(width, width_of(text_font, text_size, line.c_str()));
    const auto height = pad * 2.0f + title_size + 8.0f * metric + static_cast<float>(lines.size()) * (text_size + 2.0f * metric);
    const auto display = screen_ui_space{}.display();
    const ImVec2 position{display.x - width - pad * 2.0f - 16.0f * metric, 16.0f * metric};
    const ImVec2 maximum{position.x + width + pad * 2.0f, position.y + height};
    draw_panel_surface(draw, position, maximum, 4.0f * metric);

    auto y = position.y + pad;
    draw_outlined_text(draw, title_font, title_size, {position.x + pad, y}, k_wm_text, "TRIGGER DEBUG");
    y += title_size + 4.0f * metric;
    draw->AddLine({position.x + pad, y}, {maximum.x - pad, y}, k_wm_line, 1.0f);
    y += 4.0f * metric;
    for (const auto& line : lines)
    {
        const auto color = line == "STUCK - state did not advance" ? bright_red
                           : line.starts_with("state ") || line == trigger_debug::name(debug.state)
                               ? state_color
                               : k_wm_text;
        draw_outlined_text(draw, text_font, text_size, {position.x + pad, y}, color, line.c_str());
        y += text_size + 2.0f * metric;
    }
}

void panels::draw_keybind_list(const bool interactive)
{
    auto& cfg = config::general_settings.m_keybind_list;
    if (!cfg.enabled)
        return;
    std::vector<compact_panel_row> rows{};
    const auto combat = config::combat_settings.get(game::local_player().weapon_type());
    const auto mode_visible = [&](const int mode)
    {
        return mode == config::combat_profile::activation::always   ? cfg.show_always
               : mode == config::combat_profile::activation::toggle ? cfg.show_toggle
                                                                    : cfg.show_hold;
    };
    const auto add_combat = [&](std::string name, const bool enabled, const int mode, const int key)
    {
        if (!enabled || !mode_visible(mode) || !config::combat_profile::activation_active(mode, key))
            return;
        const auto* mode_name = mode == config::combat_profile::activation::always   ? "always"
                                : mode == config::combat_profile::activation::toggle ? "toggle"
                                                                                     : "hold";
        rows.push_back({std::move(name), mode_name, true});
    };
    add_combat("Aimbot", combat.aimbot.enabled, combat.aimbot.activation_mode, combat.aimbot.key);
    add_combat("Trigger", combat.triggerbot.enabled, combat.triggerbot.activation_mode, combat.triggerbot.key);
    const auto& global = config::combat_settings.global;
    add_combat(std::format("Aim Damage: {}", static_cast<int>(std::lround(global.aimbot_min_damage_override))),
               global.aimbot_enabled && global.aimbot_min_damage_override_enabled,
               global.aimbot_min_damage_override_mode, global.aimbot_min_damage_override_key);
    add_combat(std::format("Trigger Damage: {}", static_cast<int>(std::lround(global.triggerbot_min_damage_override))),
               global.triggerbot_enabled && global.triggerbot_min_damage_override_enabled,
               global.triggerbot_min_damage_override_mode, global.triggerbot_min_damage_override_key);
    const auto& player_esp = config::visual_settings.m_player;
    const auto visual_mode_visible = [&](const int mode)
    {
        return mode == config::visual_profile::player::always_on ? cfg.show_always
               : mode == config::visual_profile::player::toggle  ? cfg.show_toggle
                                                                 : cfg.show_hold;
    };
    if (visual_mode_visible(player_esp.activation_mode) && player_esp.active())
    {
        const auto* mode = player_esp.activation_mode == config::visual_profile::player::always_on ? "always"
                           : player_esp.activation_mode == config::visual_profile::player::toggle  ? "toggle"
                                                                                                   : "hold";
        rows.push_back({"Player ESP", mode, true});
    }
    const auto& radar = config::visual_settings.m_radar;
    if (visual_mode_visible(radar.activation_mode) && radar.active())
    {
        const auto* mode = radar.activation_mode == config::visual_profile::radar::always_on ? "always"
                           : radar.activation_mode == config::visual_profile::radar::toggle  ? "toggle"
                                                                                             : "hold";
        rows.push_back({"Radar", mode, true});
    }
    const auto& misc = config::general_settings;
    if (cfg.show_hold && misc.m_auto_stop.enabled && features::misc::auto_stop().active())
        rows.push_back({"Auto Stop", "active", true});
    // Bunny publishes its real gate state from the movement worker. Use that instead of duplicating its focus,
    // Walk Bot, jump-bind and player checks here, where a transient cursor or menu state can hide an active row.
    if (cfg.show_hold && misc.m_bunny_hop.enabled && !misc.m_walk_bot.enabled && !render::menu_open() &&
        features::misc::bhop().active())
    {
        const auto bunny_jump = game::input_bindings().resolve(game::input_action::jump);
        const auto bunny_jump_key =
            bunny_jump.device == game::input_device::keyboard ? bunny_jump.virtual_key : std::uint16_t{};
        const auto bunny_activation = static_cast<int>(std::clamp(misc.m_bunny_hop.activation_key, 0, 255));
        // The activation key may be the jump key itself; show its name where other rows show the bind state.
        const auto state = bunny_activation == bunny_jump_key ? ui::menu::detail::key_name(bunny_activation)
                                                               : std::string{"hold"};
        rows.push_back({"Bunny Hop", state, true});
    }
    if (cfg.show_always && config::visual_settings.m_no_flash.enabled)
        rows.push_back({"No Flash", "always", true});
    if (cfg.show_always && config::visual_settings.m_no_smoke.enabled)
        rows.push_back({"No Smoke", "always", true});
    auto& state = *m_state;
    state.begin_panel_frame();
    const auto title = std::string{render::localization::tr("Binds")};
    const panel_bounds avoid[]{state.watermark_bounds, state.spectator_bounds};
    state.keybind_bounds = draw_compact_panel(title.c_str(), rows, cfg.layout, state.keybinds, interactive, avoid, std::size(avoid));
}

void panels::draw_bomb_info(bool interactive)
{
    auto& cfg = config::visual_settings.m_bomb;
    auto& layout = config::general_settings.m_bomb_info.layout;
    if (!cfg.enabled || !cfg.show_info_panel)
        return;

    auto state = features::visuals::bomb().info_snapshot();
    if (!state.planted)
    {
        if (!interactive)
            return;
        // Menu-open preview makes the otherwise event-driven panel positionable.
        state.planted = true;
        state.time_remaining = 32.8f;
        state.timer_length = 40.0f;
        state.bomb_site = 0;
        state.predicted_damage = 124;
        state.local_health = 100;
    }

    auto& hud = *m_state;
    hud.begin_panel_frame();
    auto& drag = hud.bomb;
    const auto dpi_scale = readability_dpi_scale();
    layout.scale = std::clamp(layout.scale, 0.55f, 2.0f);
    const auto metric = dpi_scale * layout.scale;
    const auto font = resolve_hud_font(render::fonts().hud_text_40, k_hud_text_size * metric);
    const auto title_font = resolve_hud_font(render::fonts().menu_brand_30, k_hud_title_size * metric);
    const auto pad_x = k_hud_pad_x * metric;
    const auto pad_y = k_hud_pad_y * metric;
    const auto row_height = font.size + k_hud_row_gap * metric;
    const auto title = render::localization::tr("Bomb Timer");
    const auto timer_fraction = std::clamp(state.time_remaining / std::max(state.timer_length, 0.1f), 0.0f, 1.0f);
    const auto warning = IM_COL32(235, 90, 90, 255);
    const auto timer_color = timer_fraction < 0.25f ? warning : hud_accent();
    auto hp_value = std::string("--");
    auto hp_color = hud_muted();
    if (state.predicted_damage >= 0 && state.local_health > 0)
    {
        const auto lethal = state.predicted_damage >= state.local_health;
        hp_value = lethal ? "LETHAL" : std::to_string(std::max(0, state.local_health - state.predicted_damage));
        hp_color = lethal ? warning : IM_COL32(110, 225, 135, 255);
    }
    struct bomb_row
    {
        const char* label;
        std::string value;
        ImU32 color;
    };
    const std::array rows{
        bomb_row{"Site:", state.bomb_site == 1 ? "B" : "A", hud_accent()},
        bomb_row{"HP:", hp_value, hp_color},
        bomb_row{"Time:", std::format("{:.1f} sec", state.time_remaining), timer_color},
        bomb_row{"Defuse:", std::format("{:.1f} sec", state.defuse_remaining),
                 state.defuse_remaining > state.time_remaining ? warning : hud_accent()}};
    const auto row_count = state.being_defused ? 4 : 3;
    auto inner_width = std::max(170.0f * metric, text_width(title_font.font, title_font.size, title));
    for (int index = 0; index < row_count; ++index)
        inner_width = std::max(inner_width, text_width(font.font, font.size, rows[index].label) +
                              24.0f * metric + text_width(font.font, font.size, rows[index].value.c_str()));
    const auto header_height = title_font.size + pad_y * 2.0f;
    const auto bar_height = 3.0f * metric;
    const auto bar_gap = 3.0f * metric;
    const auto bar_count = state.being_defused && state.defuse_length > 0.0f ? 2 : 1;
    const ImVec2 panel_size{inner_width + pad_x * 2.0f,
                           pad_y * 2.0f + title_font.size + k_hud_title_gap * metric +
                           row_count * row_height - k_hud_row_gap * metric + bar_count * (bar_height + bar_gap)};
    const screen_ui_space ui_space{};
    const auto display = ui_space.display(), position_scale = ui_space.position_scale();
    migrate_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    position = clamp_to_display(drag.update(interactive, position, panel_size), panel_size, display);
    const panel_bounds avoid[]{hud.watermark_bounds, hud.spectator_bounds, hud.keybind_bounds};
    position = avoid_panel_overlap(position, panel_size, display, avoid, std::size(avoid), 8.0f * metric);
    if (drag.active())
        capture_screen_layout(layout, position, panel_size, display, dpi_scale, position_scale);
    const ImVec2 maximum{position.x + panel_size.x, position.y + panel_size.y};
    auto* draw = ImGui::GetForegroundDrawList();
    draw_readability_surface(draw, position, maximum, metric, header_height);
    draw->PushClipRect(position, maximum, true);
    draw_readability_text(draw, title_font.font, title_font.size,
                           {position.x + (panel_size.x - text_width(title_font.font, title_font.size, title)) * 0.5f,
                            position.y + pad_y}, IM_COL32_WHITE, title);
    auto y = position.y + pad_y + title_font.size + k_hud_title_gap * metric;
    for (int index = 0; index < row_count; ++index)
    {
        const auto& row = rows[index];
        draw_readability_text(draw, font.font, font.size, {position.x + pad_x, y}, k_astral_hud_text, row.label);
        draw_readability_text(draw, font.font, font.size,
                               {maximum.x - pad_x - text_width(font.font, font.size, row.value.c_str()), y},
                               row.color, row.value.c_str());
        y += row_height;
    }
    const auto progress = [&](const float fraction, const ImU32 color)
    {
        const ImVec2 min{position.x + pad_x, y}, max{maximum.x - pad_x, y + bar_height};
        draw->AddRectFilled(min, max, IM_COL32(58, 62, 76, 255), metric);
        if (fraction > 0.0f)
            draw->AddRectFilled(min, {min.x + inner_width * fraction, max.y}, color, metric);
        y += bar_height + bar_gap;
    };
    progress(timer_fraction, timer_color);
    if (bar_count == 2)
        progress(1.0f - std::clamp(state.defuse_remaining / state.defuse_length, 0.0f, 1.0f), rows[3].color);
    draw->PopClipRect();
}

} // namespace ui::hud

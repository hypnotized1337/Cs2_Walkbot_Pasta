#pragma once

#include <memory>

namespace ui::hud
{
// The three readability overlays share these base sizes at 100% DPI Scale. They are multiplied by the menu's DPI
// Scale setting and each panel's own scale; ImGui bakes the TTF at the draw size, so no atlas rebuild is needed.
inline constexpr float k_hud_text_size = 19.0f;
inline constexpr float k_hud_title_size = 21.0f;
inline constexpr float k_hud_brand_size = 25.0f;
inline constexpr float k_hud_font_raster_size = 40.0f;

// The overlay's HUD panels (watermark, spectators, event log, notices, binds, bomb). Each can be dragged while the
// menu is open (`interactive`); their positions are saved in the configuration. Render thread only.
class panels
{
public:
    panels();
    ~panels();
    panels(const panels&) = delete;
    panels& operator=(const panels&) = delete;

    void draw_watermark(bool interactive);
    void draw_spectator_list(bool interactive);
    void draw_event_log(bool interactive);
    void draw_system_notices(bool interactive);
    void draw_keybind_list(bool interactive);
    void draw_bomb_info(bool interactive);
    void draw_trigger_debug(bool interactive);

private:
    // Samplers, drag state and animation kept between frames.
    struct state;
    std::unique_ptr<state> m_state;
};

} // namespace ui::hud

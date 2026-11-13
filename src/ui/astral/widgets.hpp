// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
#pragma once
// Astral widget kit. Every interactive widget follows the stock ItemSize/ItemAdd/ButtonBehavior skeleton so
// clipping, hover tests, IsItemEdited(), BeginDisabled() and nav keep working; all drawing goes through the spec.
#include "types.hpp"
#include "icons.hpp"
#include "theme.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>

namespace ui::astral::widgets {

struct font_set_t {
    ImFont* regular = nullptr; // Inter SemiBold
    ImFont* bold = nullptr;    // Inter Bold
};

struct column_layout_t {
    ImRect area;
    int count = 0;
    int index = 0;
    float width = 0.0f;
    float cursor = 0.0f;
    float reserve = 0.0f;   // measured height of panels below the fill panel (this frame)
    bool after_fill = false;
    ImGuiID id = 0;
};

struct panel_frame_t {
    ImGuiID id = 0;
    float x = 0.0f, y = 0.0f, w = 0.0f;
    float header = 0.0f;
    bool fill = false;
    const char* title = nullptr;
};

struct accessory_line_t {
    ImGuiID last_accessory = 0;
    int frame = -1;
    float center_y = 0.0f;
    float right = 0.0f;
};

struct context_t {
    ImVec4 accent_start = rgb(0xFF8A84);
    ImVec4 accent_end = rgb(0xFFB27C);
    palette_t pal = make_custom_palette(accent_start, accent_end);
    metrics_t m;
    float scale = 1.0f;
    font_set_t fonts;
    icon_atlas icons;
    ImGuiStorage state;     // animation values keyed by widget id + salt (GetFloat/SetFloat only, never refs)
    double time = 0.0;
    float dt = 0.0f;

    char search[64] = {};
    bool show_paths = false;
    const char* path_tab = "";
    const char* path_page = "";
    const char* path_panel = "";

    column_layout_t layout;
    ImVector<panel_frame_t> panels;
    accessory_line_t accessory;
    ImVec4 picker_backup = ImVec4(0, 0, 0, 0);
    float right_override = 0.0f;   // > 0: content right edge for items (side columns inside popups)
    bool animations = true;
    bool last_dimmed = false;      // search state of the last labeled row, inherited by its accessories
};

[[nodiscard]] context_t& get();
[[nodiscard]] inline float px(float v) { return v * get().scale; }
// Font sizes are rounded to whole pixels: fractional sizes (15px * 125% = 18.75px) defeat hinting and blur stems.
[[nodiscard]] inline float font_px(float v) { return std::floor(v * get().scale + 0.5f); }

void set_scale(float scale);   // rebuilds icons; call outside a frame
void set_accent(const ImVec4& custom_start, const ImVec4& custom_end);
// Replaces the surface/text roles without touching the user's accent. Called every frame by the menu palette sync.
void set_surfaces(const palette_t& surfaces);
void begin_frame();            // update frame time; call after ImGui::NewFrame()
void shutdown();              // after the renderer backend shut down, before ImGui::DestroyContext()
void init_style();

[[nodiscard]] float animate(ImGuiID id, const char* salt, float target, float rate, float initial);
[[nodiscard]] float ease(float t);

// ---- Layout ----------------------------------------------------------------------------------------------------
enum class panel_height { fit, fill };

void begin_columns(const char* id, const ImRect& area, int count);
void next_column();
void end_columns();
bool begin_panel(const char* title, panel_height height = panel_height::fit); // title == nullptr: headerless pane
void end_panel();
[[nodiscard]] ImRect panel_inner_rect();

// ---- Items (panels and popups) --------------------------------------------------------------------------------
using formatter_t = int (*)(char* buf, int size, float value);

bool checkbox(const char* label, bool* v);
bool slider(const char* label, float* v, float min, float max, const char* fmt = "%.0f", formatter_t formatter = nullptr);
bool slider_int(const char* label, int* v, int min, int max, const char* fmt = "%d");
bool combo(const char* label, int* current, const char* const items[], int count);
bool multi_combo(const char* label, unsigned* mask, const char* const items[], int count);
void caption(const char* text);
void color_row(const char* label, color_value_t* color, bool alpha = true);
bool button(const char* label, float width = 0.0f, bool accent = false);
bool input_text(const char* id, const char* hint, char* buf, size_t size);
bool list_row(const char* label, const char* detail, bool selected);
void separator();
void tooltip(const char* text);

// Accessories sit right-aligned on the line of the previous item (checkbox, caption row).
enum class settings_icon { plus, gear };
bool color_dot(const char* id, color_value_t* color, bool alpha = true);
bool begin_settings(const char* id, settings_icon icon = settings_icon::plus, float width = 0.0f);
void end_settings();

// ---- Frame chrome ---------------------------------------------------------------------------------------------
bool card_combo(const char* id, int* current, const char* const items[], int count, const ImRect& frame);

} // namespace ui::astral::widgets

namespace ui::astral {

using context_t = widgets::context_t;

[[nodiscard]] inline context_t& get() { return widgets::get(); }
inline void set_scale(float scale) { widgets::set_scale(scale); }
inline void set_accent(const ImVec4& start, const ImVec4& end) { widgets::set_accent(start, end); }
inline void set_surfaces(const palette_t& surfaces) { widgets::set_surfaces(surfaces); }
inline void begin_frame() { widgets::begin_frame(); }
inline void shutdown() { widgets::shutdown(); }
inline void init_style() { widgets::init_style(); }

} // namespace ui::astral

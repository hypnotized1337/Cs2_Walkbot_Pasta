#include <stdafx.hpp>

// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
#include "widgets.hpp"

#include "draw.hpp"

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::astral::widgets {

namespace {

context_t g_ctx;

constexpr ImGuiWindowFlags k_popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoScrollWithMouse;

[[nodiscard]] float scaled_px(float v) { return draw::snap(px(v)); }

[[nodiscard]] float content_right(ImGuiWindow* window)
{
    return g_ctx.right_override > 0.0f ? g_ctx.right_override : window->WorkRect.Max.x;
}

[[nodiscard]] float body_size() { return font_px(g_ctx.m.font_body); }

[[nodiscard]] bool contains_no_case(const char* begin, const char* end, const char* needle)
{
    const size_t n = std::strlen(needle);
    if (n == 0) return true;
    for (const char* p = begin; p + n <= end; ++p) {
        size_t i = 0;
        while (i < n && std::tolower(static_cast<unsigned char>(p[i])) == std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

// Dims rows that do not match the footer search. Accessories reuse the verdict of the row they belong to.
class search_scope_t {
public:
    explicit search_scope_t(const char* label, bool accessory = false)
    {
        bool dim = false;
        if (GImGui->BeginPopupStack.Size > 0) {
            dim = false;
        } else if (accessory) {
            dim = g_ctx.last_dimmed;
        } else if (g_ctx.search[0] != '\0' && label != nullptr) {
            dim = !contains_no_case(label, ImGui::FindRenderedTextEnd(label), g_ctx.search);
            g_ctx.last_dimmed = dim;
        } else {
            g_ctx.last_dimmed = false;
        }
        if (dim) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.26f);
            pushed_ = true;
        }
    }
    ~search_scope_t()
    {
        if (pushed_) ImGui::PopStyleVar();
    }
    search_scope_t(const search_scope_t&) = delete;
    search_scope_t& operator=(const search_scope_t&) = delete;

private:
    bool pushed_ = false;
};

void show_tooltip(const char* text)
{
    context_t& c = g_ctx;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fade = ImSaturate((GImGui->HoverItemDelayTimer - style.HoverDelayNormal) / 0.12f + 0.15f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * (c.animations ? fade : 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(c.m.tooltip_pad_x), px(c.m.tooltip_pad_y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(c.m.popup_rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c.pal.tooltip);
    ImGui::PushStyleColor(ImGuiCol_Text, c.pal.text);
    if (ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None)) {
        ImGui::PushFont(c.fonts.regular, body_size());
        ImGui::PushTextWrapPos(px(c.m.tooltip_wrap));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void auto_tooltip(const char* label)
{
    context_t& c = g_ctx;
    if (!c.show_paths || label == nullptr) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary)) return;
    const char* end = ImGui::FindRenderedTextEnd(label);
    char path[192];
    ImFormatString(path, IM_ARRAYSIZE(path), "%s > %s%s%s > %.*s", c.path_tab, c.path_page, c.path_page[0] ? " > " : "",
                   c.path_panel, static_cast<int>(end - label), label);
    show_tooltip(path);
}

// Popup chrome shared by settings / bind popups: shadow + rounded body drawn into the popup's own draw list.
void popup_background(ImGuiWindow* window)
{
    context_t& c = g_ctx;
    ImDrawList* dl = window->DrawList;
    const ImVec2 min = window->Pos;
    const ImVec2 max(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
    const float r = px(c.m.popup_rounding);
    dl->PushClipRectFullScreen();
    draw::glow(dl, min, max, r, draw::col(rgb(0x000000), 0.32f), px(16.0f), ImVec2(0.0f, px(4.0f)));
    dl->PopClipRect();
    dl->AddRectFilled(min, max, draw::col(c.pal.popup), r);
    dl->AddRect(min, max, draw::col(c.pal.popup_border, 0.55f), r, 0, 1.0f);
}

// Tracks open->closed transitions per popup so every opening restarts its reveal animation.
float popup_reveal(ImGuiID popup_id, bool open)
{
    context_t& c = g_ctx;
    const ImGuiID was_key = ImHashStr("was_open", 0, popup_id);
    const ImGuiID t_key = ImHashStr("reveal", 0, popup_id);
    const bool was = c.state.GetBool(was_key, false);
    c.state.SetBool(was_key, open);
    if (!open) return 0.0f;
    if (!was) c.state.SetFloat(t_key, 0.0f);
    if (!c.animations) return 1.0f;
    float t = c.state.GetFloat(t_key, 0.0f);
    t = ImMin(1.0f, t + ImMin(ImGui::GetIO().DeltaTime, 1.0f / 15.0f) / 0.16f);
    c.state.SetFloat(t_key, t);
    return t;
}

void join_selected(char* out, int size, const char* const items[], int count, unsigned mask)
{
    int n = 0;
    out[0] = '\0';
    for (int i = 0; i < count && n < size - 1; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        n += ImFormatString(out + n, static_cast<size_t>(size - n), n > 0 ? ", %s" : "%s", items[i]);
    }
    if (n == 0) ImFormatString(out, static_cast<size_t>(size), "None");
}

bool dropdown(ImGuiID popup_id, const ImRect& frame, const char* const items[], int count, int* current, unsigned* mask,
              const char* preview, float text_x)
{
    context_t& c = g_ctx;
    const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const float reveal = popup_reveal(popup_id, open);
    if (!open) return false;

    const float head_h = frame.GetHeight();
    const float item_h = scaled_px(c.m.dropdown_item_h);
    const float full_h = head_h + item_h * static_cast<float>(count) + scaled_px(c.m.dropdown_pad_bottom);
    ImVec2 pos = frame.Min;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float bottom = vp->WorkPos.y + vp->WorkSize.y;
    if (pos.y + full_h > bottom) pos.y = ImMax(vp->WorkPos.y, bottom - full_h);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(frame.GetWidth(), full_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(c.m.frame_rounding));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    const bool visible = ImGui::BeginPopupEx(popup_id, k_popup_flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!visible) return false;

    bool changed = false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float e = 1.0f - (1.0f - reveal) * (1.0f - reveal) * (1.0f - reveal);
    const float vis_h = draw::snap(ImLerp(head_h, full_h, e));
    const ImRect body(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + vis_h));
    const float r = px(c.m.frame_rounding);

    dl->PushClipRectFullScreen();
    draw::glow(dl, body.Min, body.Max, r, draw::col(rgb(0x000000), 0.30f * e), px(14.0f), ImVec2(0.0f, px(4.0f)));
    dl->PopClipRect();
    dl->AddRectFilled(body.Min, body.Max, draw::col(c.pal.dropdown), r);
    dl->PushClipRect(body.Min, body.Max, true);

    ImFont* font = c.fonts.regular;
    const float fs = body_size();
    const float x = body.Min.x + text_x;

    // Header row repeats the value and closes the list.
    {
        const ImRect head(body.Min, ImVec2(body.Max.x, body.Min.y + head_h));
        const ImGuiID head_id = window->GetID("##head");
        ImGui::ItemAdd(head, head_id);
        bool hh = false, hd = false;
        if (ImGui::ButtonBehavior(head, head_id, &hh, &hd)) ImGui::CloseCurrentPopup();
        dl->PushClipRect(head.Min, ImVec2(head.Max.x - scaled_px(24.0f), head.Max.y), true);
        draw::text_cap_centered(dl, font, fs, x, head.GetCenter().y, draw::col(c.pal.text), preview);
        dl->PopClipRect();
        const ImVec2 icon_c(draw::snap(head.Max.x - scaled_px(text_x > scaled_px(9.0f) ? 14.0f : 11.0f)), draw::snap(head.GetCenter().y));
        if (mask != nullptr) draw::minus(dl, icon_c, px(4.5f), scaled_px(2.0f), draw::col(c.pal.text));
        else draw::chevron(dl, ImVec2(icon_c.x, icon_c.y + 0.5f), px(8.0f), px(4.0f), true, draw::col(c.pal.text), px(1.5f));
    }

    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        const float y = body.Min.y + head_h + item_h * static_cast<float>(i);
        const ImRect row(ImVec2(body.Min.x, y), ImVec2(body.Max.x, y + item_h));
        const ImGuiID id = window->GetID("##item");
        ImGui::ItemAdd(row, id);
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(row, id, &hovered, &held);
        if (pressed) {
            if (mask != nullptr) {
                *mask ^= (1u << i);
            } else {
                *current = i;
                ImGui::CloseCurrentPopup();
            }
            changed = true;
        }
        const bool selected = mask != nullptr ? (*mask & (1u << i)) != 0 : *current == i;
        const float slide = animate(id, "slide", hovered ? 1.0f : 0.0f, 20.0f, 0.0f);
        const float sel = animate(id, "sel", selected ? 1.0f : 0.0f, 16.0f, selected ? 1.0f : 0.0f);
        const ImVec4 idle = draw::mix(c.pal.text_dim, c.pal.text, slide);
        const ImVec4 picked = draw::mix(c.pal.accent, c.pal.accent_end, slide * 0.45f);
        draw::text_cap_centered(dl, font, fs, x + px(6.0f) * slide, row.GetCenter().y, draw::col(draw::mix(idle, picked, sel)), items[i]);
        ImGui::PopID();
    }

    dl->PopClipRect();
    ImGui::EndPopup();
    return changed;
}

// ---- Color picker ----------------------------------------------------------------------------------------------

bool parse_hex(const char* text, ImVec4* out)
{
    while (*text == ' ' || *text == '#') ++text;
    unsigned int v = 0;
    const size_t len = std::strlen(text);
    if (len < 6) return false;
    for (size_t i = 0; i < (len >= 8 ? 8u : 6u); ++i) {
        const char ch = text[i];
        unsigned int nib = 0;
        if (ch >= '0' && ch <= '9') nib = static_cast<unsigned int>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') nib = static_cast<unsigned int>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') nib = static_cast<unsigned int>(ch - 'A' + 10);
        else return false;
        v = (v << 4) | nib;
    }
    if (len < 8) v = (v << 8) | 0xFFu;
    *out = ImVec4(static_cast<float>((v >> 24) & 0xFF) / 255.0f, static_cast<float>((v >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((v >> 8) & 0xFF) / 255.0f, static_cast<float>(v & 0xFF) / 255.0f);
    return true;
}

bool drag_area(const char* id_str, const ImRect& bb, ImVec2* t)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(id_str);
    ImGui::ItemAdd(bb, id);
    bool hovered = false, held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    if (!held) return false;
    const ImVec2 m = ImGui::GetIO().MousePos;
    *t = ImVec2(ImSaturate((m.x - bb.Min.x) / bb.GetWidth()), ImSaturate((m.y - bb.Min.y) / bb.GetHeight()));
    return true;
}

void picker_popup(ImGuiID popup_id, color_value_t* cv, bool alpha, const ImVec2& anchor)
{
    context_t& c = g_ctx;
    const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const float reveal = popup_reveal(popup_id, open);
    if (!open) return;

    const float pad = scaled_px(10.0f);
    const float sv = scaled_px(c.m.picker_sv);
    const float bar = scaled_px(c.m.picker_bar);
    const float gap = scaled_px(c.m.picker_gap);
    const float side = scaled_px(c.m.picker_side_w);
    const float w = pad + sv + gap + bar + scaled_px(12.0f) + side + pad;
    const float h = pad + sv + gap + bar + pad;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 pos(anchor.x + scaled_px(14.0f), anchor.y - scaled_px(18.0f));
    pos.x = ImClamp(pos.x, vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - w);
    pos.y = ImClamp(pos.y, vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - h);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(scaled_px(8.0f), scaled_px(c.m.item_spacing)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    if (!ImGui::BeginPopupEx(popup_id, k_popup_flags)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const ImVec2 o = window->Pos;
    const ImVec2 o_max(o.x + w, o.y + h);
    const float r = px(c.m.popup_rounding);
    if (ImGui::IsWindowAppearing()) c.picker_backup = cv->color;

    ImVec4& col = cv->color;
    const ImGuiID hue_key = ImHashStr("hue", 0, popup_id);
    float hue = 0.0f, sat = 0.0f, val = 0.0f;
    ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, hue, sat, val);
    if (sat <= 0.0001f || val <= 0.0001f) hue = c.state.GetFloat(hue_key, hue);
    bool hsv_changed = false;

    dl->PushClipRectFullScreen();
    draw::glow(dl, o, o_max, r, draw::col(ImVec4(col.x, col.y, col.z, 1.0f), 0.30f), px(12.0f));
    draw::glow(dl, o, o_max, r, draw::col(rgb(0x000000), 0.25f), px(16.0f), ImVec2(0.0f, px(4.0f)));
    dl->PopClipRect();
    dl->AddRectFilled(o, o_max, draw::col(c.pal.panel), r);

    // Saturation / value square
    const ImRect sv_rect(ImVec2(o.x + pad, o.y + pad), ImVec2(o.x + pad + sv, o.y + pad + sv));
    ImVec2 t;
    if (drag_area("##sv", sv_rect, &t)) {
        sat = t.x;
        val = 1.0f - t.y;
        hsv_changed = true;
    }
    float hr = 0.0f, hg = 0.0f, hb = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, hr, hg, hb);
    const ImU32 hue_col = draw::col(ImVec4(hr, hg, hb, 1.0f));
    const ImU32 white = draw::col(ImVec4(1, 1, 1, 1));
    const ImU32 black = draw::col(ImVec4(0, 0, 0, 1));
    const ImU32 clear = draw::col(ImVec4(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(sv_rect.Min, sv_rect.Max, white, hue_col, hue_col, white);
    dl->AddRectFilledMultiColor(sv_rect.Min, sv_rect.Max, clear, clear, black, black);

    // Hue strip
    const ImRect hue_rect(ImVec2(sv_rect.Max.x + gap, sv_rect.Min.y), ImVec2(sv_rect.Max.x + gap + bar, sv_rect.Max.y));
    if (drag_area("##hue", hue_rect, &t)) {
        hue = ImMin(t.y, 0.9999f);
        hsv_changed = true;
    }
    draw::hue_strip(dl, hue_rect.Min, hue_rect.Max, true, ImGui::GetStyle().Alpha);

    // Alpha strip + swatch
    const ImRect alpha_rect(ImVec2(sv_rect.Min.x, sv_rect.Max.y + gap), ImVec2(sv_rect.Max.x, sv_rect.Max.y + gap + bar));
    const ImRect swatch(ImVec2(hue_rect.Min.x, alpha_rect.Min.y), ImVec2(hue_rect.Max.x, alpha_rect.Max.y));
    if (alpha && drag_area("##alpha", alpha_rect, &t)) col.w = t.x;
    const float cell = draw::snap(bar * 0.5f);
    if (alpha) {
        draw::checkerboard(dl, alpha_rect.Min, alpha_rect.Max, cell, draw::col(rgb(0xC8C8C8)), draw::col(rgb(0x8C8C8C)));
        dl->AddRectFilledMultiColor(alpha_rect.Min, alpha_rect.Max, draw::col(ImVec4(col.x, col.y, col.z, 0.0f)),
                                    draw::col(ImVec4(col.x, col.y, col.z, 1.0f)), draw::col(ImVec4(col.x, col.y, col.z, 1.0f)),
                                    draw::col(ImVec4(col.x, col.y, col.z, 0.0f)));
    } else {
        dl->AddRectFilled(alpha_rect.Min, alpha_rect.Max, draw::col(ImVec4(col.x, col.y, col.z, 1.0f))); // opaque-only colors
    }
    draw::checkerboard(dl, swatch.Min, swatch.Max, cell, draw::col(rgb(0xC8C8C8)), draw::col(rgb(0x8C8C8C)));
    dl->AddRectFilled(swatch.Min, swatch.Max, draw::col(col));

    if (hsv_changed) {
        ImGui::ColorConvertHSVtoRGB(hue, sat, val, col.x, col.y, col.z);
        c.state.SetFloat(hue_key, hue);
    }

    // Handles
    const ImVec2 sv_handle(sv_rect.Min.x + sat * sv, sv_rect.Min.y + (1.0f - val) * sv);
    dl->AddCircleFilled(sv_handle, px(4.5f), draw::col(ImVec4(col.x, col.y, col.z, 1.0f)));
    dl->AddCircle(sv_handle, px(4.5f), white, 0, px(1.5f));
    const float hy = draw::snap(hue_rect.Min.y + hue * sv);
    dl->AddRectFilled(ImVec2(hue_rect.Min.x - 1.0f, hy - scaled_px(2.0f)), ImVec2(hue_rect.Max.x + 1.0f, hy + scaled_px(2.0f)), black, 1.0f);
    dl->AddRectFilled(ImVec2(hue_rect.Min.x, hy - 1.0f), ImVec2(hue_rect.Max.x, hy + 1.0f), white);
    if (alpha) {
        const float ax = draw::snap(alpha_rect.Min.x + col.w * sv);
        dl->AddRectFilled(ImVec2(ax - scaled_px(2.0f), alpha_rect.Min.y - 1.0f), ImVec2(ax + scaled_px(2.0f), alpha_rect.Max.y + 1.0f), black, 1.0f);
        dl->AddRectFilled(ImVec2(ax - 1.0f, alpha_rect.Min.y), ImVec2(ax + 1.0f, alpha_rect.Max.y), white);
    }

    // Side column
    const float side_x = hue_rect.Max.x + scaled_px(12.0f);
    const float saved_right = c.right_override;
    c.right_override = side_x + side;
    ImGui::SetCursorScreenPos(ImVec2(side_x, sv_rect.Min.y));
    ImGui::BeginGroup(); // new lines restart at side_x, not at the popup's left edge
    slider("Rainbow", &cv->rainbow, 0.0f, 100.0f, "%.0f");
    slider("Pulse", &cv->pulse, 0.0f, 100.0f, "%.0f");
    if (button("Copy", side)) {
        char hex[16];
        ImFormatString(hex, IM_ARRAYSIZE(hex), "#%02X%02X%02X%02X", static_cast<int>(col.x * 255.0f + 0.5f),
                       static_cast<int>(col.y * 255.0f + 0.5f), static_cast<int>(col.z * 255.0f + 0.5f),
                       static_cast<int>(col.w * 255.0f + 0.5f));
        ImGui::SetClipboardText(hex);
    }
    if (button("Paste", side)) {
        ImVec4 parsed;
        const char* clip = ImGui::GetClipboardText();
        if (clip != nullptr && parse_hex(clip, &parsed)) col = alpha ? parsed : ImVec4(parsed.x, parsed.y, parsed.z, col.w);
    }
    if (button("Undo", side)) col = c.picker_backup;
    ImGui::EndGroup();
    c.right_override = saved_right;

    ImGui::EndPopup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

ImVec2 AccessoryCenter(ImGuiID id, float width)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    accessory_line_t& line = g_ctx.accessory;
    float right = 0.0f, cy = 0.0f;
    if (line.frame == g.FrameCount && line.last_accessory == g.LastItemData.ID) {
        right = line.right;
        cy = line.center_y;
    } else {
        right = content_right(window) - px(g_ctx.m.accessory_inset);
        cy = g.LastItemData.Rect.GetCenter().y;
    }
    const ImVec2 center(draw::snap(right - width * 0.5f), draw::snap(cy));
    line.last_accessory = id;
    line.frame = g.FrameCount;
    line.center_y = cy;
    line.right = right - width - px(g_ctx.m.accessory_gap);
    return center;
}

bool slider_impl(const char* label, float* v, float min, float max, const char* fmt, formatter_t formatter, bool integer)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = content_right(window) - pos.x;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + scaled_px(c.m.slider_h)));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    const ImRect track(ImVec2(pos.x, pos.y + scaled_px(c.m.slider_track_y)), ImVec2(pos.x + w, pos.y + scaled_px(c.m.slider_track_y) + scaled_px(c.m.slider_track_h)));

    bool changed = false;
    if (held && max != min) {
        const float t = ImSaturate((ImGui::GetIO().MousePos.x - track.Min.x) / ImMax(track.GetWidth(), 1.0f));
        float nv = ImLerp(min, max, t);
        if (integer) nv = std::round(nv);
        if (nv != *v) {
            *v = nv;
            changed = true;
            ImGui::MarkItemEdited(id);
        }
    }

    const float frac = max != min ? ImSaturate((*v - min) / (max - min)) : 0.0f;
    const float shown = animate(id, "fill", frac, held ? 40.0f : 16.0f, frac);
    const float hot = animate(id, "hot", (hovered || held) ? 1.0f : 0.0f, 18.0f, 0.0f);
    const float act = animate(id, "act", frac > 0.0001f ? 1.0f : 0.0f, 14.0f, frac > 0.0001f ? 1.0f : 0.0f);

    {
        search_scope_t scope(label);
        ImDrawList* dl = window->DrawList;
        ImFont* font = c.fonts.regular;
        const float fs = body_size();
        const ImVec4 tc = draw::mix(draw::mix(c.pal.text_dim, c.pal.text_hover, hot), c.pal.text, ImMax(act, hot * 0.6f));
        const char* label_end = ImGui::FindRenderedTextEnd(label);

        char value[64];
        int n = 0;
        if (formatter != nullptr) n = formatter(value, IM_ARRAYSIZE(value), *v);
        else if (integer) n = ImFormatString(value, IM_ARRAYSIZE(value), fmt, static_cast<int>(std::lround(*v)));
        else n = ImFormatString(value, IM_ARRAYSIZE(value), fmt, static_cast<double>(*v));
        const float vw = draw::text_width(font, fs, value, value + n);

        const float cap_top = pos.y;
        dl->PushClipRect(bb.Min, ImVec2(bb.Max.x - vw - scaled_px(8.0f), bb.Max.y), true);
        draw::text_cap_top(dl, font, fs, pos.x, cap_top, draw::col(tc), label, label_end);
        dl->PopClipRect();
        draw::text_cap_top(dl, font, fs, bb.Max.x - vw, cap_top, draw::col(tc), value, value + n);

        const float tr = track.GetHeight() * 0.5f;
        dl->AddRectFilled(track.Min, track.Max, draw::col(c.pal.frame), tr);
        const float fill_x = track.Min.x + track.GetWidth() * shown;
        if (fill_x > track.Min.x + 0.5f) {
            draw::glow(dl, track.Min, ImVec2(fill_x, track.Max.y), tr, draw::col(c.pal.accent, 0.22f + 0.1f * hot), px(6.0f));
            dl->PushClipRect(track.Min, ImVec2(fill_x, track.Max.y), true);
            draw::rect_gradient(dl, track.Min, track.Max, draw::col(c.pal.accent), draw::col(c.pal.accent_end), tr, 0, true);
            dl->PopClipRect();
        }
    }
    ImGui::RenderNavCursor(bb, id);
    auto_tooltip(label);
    return changed;
}

bool combo_impl(const char* label, int* current, unsigned* mask, const char* const items[], int count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const bool has_label = label_end != label;

    if (current != nullptr) *current = ImClamp(*current, 0, count - 1);

    const ImVec2 pos = window->DC.CursorPos;
    const float w = content_right(window) - pos.x;
    const float top = has_label ? scaled_px(1.0f + c.m.caption_to_frame) : 0.0f;
    const ImRect total(pos, ImVec2(pos.x + w, pos.y + top + scaled_px(c.m.frame_h)));
    const ImRect frame(ImVec2(pos.x, pos.y + top), total.Max);
    ImGui::ItemSize(ImRect(total.Min, ImVec2(total.Max.x, total.Max.y - 1.0f)), 0.0f); // frames overhang their row by 1px
    if (!ImGui::ItemAdd(total, id)) return false;

    const ImGuiID popup_id = ImHashStr("##dropdown", 0, id);
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(frame, id, &hovered, &held);
    const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (pressed && !open) ImGui::OpenPopupEx(popup_id);

    char preview[256];
    if (mask != nullptr) join_selected(preview, IM_ARRAYSIZE(preview), items, count, *mask);
    else ImFormatString(preview, IM_ARRAYSIZE(preview), "%s", items[*current]);

    const float open_t = animate(id, "open", open ? 1.0f : 0.0f, 20.0f, 0.0f);
    const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
    {
        search_scope_t scope(has_label ? label : preview);
        ImDrawList* dl = window->DrawList;
        ImFont* font = c.fonts.regular;
        const float fs = body_size();
        if (has_label)
            draw::text_cap_top(dl, font, fs, pos.x, pos.y,
                             draw::col(draw::mix(c.pal.text_dim, c.pal.text, ImMax(open_t, hot * 0.45f))), label, label_end);
        const float r = px(c.m.frame_rounding);
        dl->AddRectFilled(frame.Min, frame.Max, draw::col(draw::mix(c.pal.frame, c.pal.frame_hover, hot)), r);
        const float tx = scaled_px(c.m.frame_text_x);
        dl->PushClipRect(frame.Min, ImVec2(frame.Max.x - scaled_px(21.0f), frame.Max.y), true);
        draw::text_cap_centered(dl, font, fs, frame.Min.x + tx, frame.GetCenter().y,
                              draw::col(draw::mix(c.pal.text_soft, c.pal.text, hot * 0.5f)), preview);
        dl->PopClipRect();
        const ImU32 icon = draw::col(draw::mix(c.pal.text_soft, c.pal.text, hot * 0.5f));
        const ImVec2 icon_c(draw::snap(frame.Max.x - scaled_px(11.0f)), draw::snap(frame.GetCenter().y));
        if (mask != nullptr) draw::plus(dl, ImVec2(icon_c.x - 0.5f, icon_c.y), px(5.0f), px(1.5f), icon);
        else draw::chevron(dl, ImVec2(icon_c.x, icon_c.y + 0.5f), px(8.0f), px(4.0f), false, icon, px(1.5f));
    }
    ImGui::RenderNavCursor(frame, id);
    auto_tooltip(has_label ? label : nullptr);

    return dropdown(popup_id, frame, items, count, current, mask, preview, scaled_px(c.m.frame_text_x));
}

} // namespace

// ---- context_t ---------------------------------------------------------------------------------------------------

context_t& get() { return g_ctx; }

float ease(float t)
{
    t = ImSaturate(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float animate(ImGuiID id, const char* salt, float target, float rate, float initial)
{
    context_t& c = g_ctx;
    const ImGuiID key = ImHashStr(salt, 0, id);
    if (!c.animations) {
        c.state.SetFloat(key, target);
        return target;
    }
    float v = c.state.GetFloat(key, initial);
    const float dt = ImMin(ImGui::GetIO().DeltaTime, 1.0f / 15.0f);
    v = target + (v - target) * std::exp(-rate * dt);
    if (std::fabs(v - target) < 0.0015f) v = target; // settle exactly so an idle menu stops changing
    c.state.SetFloat(key, v);
    return v;
}

void set_accent(const ImVec4& custom_start, const ImVec4& custom_end)
{
    context_t& c = g_ctx;
    c.accent_start = custom_start;
    c.accent_end = custom_end;
    const auto accent = make_custom_palette(custom_start, custom_end);
    // Accent changes are live and frequent; keep the configured surfaces and text roles intact.
    c.pal.accent = accent.accent;
    c.pal.accent_top = accent.accent_top;
    c.pal.accent_bottom = accent.accent_bottom;
    c.pal.accent_end = accent.accent_end;
    c.pal.title_left = accent.title_left;
    c.pal.title_right = accent.title_right;
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_TextSelectedBg] = draw::fade(c.pal.accent, 0.35f);
    style.Colors[ImGuiCol_NavCursor] = c.pal.accent;
    style.Colors[ImGuiCol_CheckMark] = c.pal.accent;
    style.Colors[ImGuiCol_SliderGrab] = c.pal.accent;
    style.Colors[ImGuiCol_InputTextCursor] = c.pal.accent;
}

void set_surfaces(const palette_t& surfaces)
{
    context_t& c = g_ctx;
    c.pal.window_bg = surfaces.window_bg;
    c.pal.card = surfaces.card;
    c.pal.panel = surfaces.panel;
    c.pal.frame = surfaces.frame;
    c.pal.frame_hover = surfaces.frame_hover;
    c.pal.frame_active = surfaces.frame_active;
    c.pal.dropdown = surfaces.dropdown;
    c.pal.popup = surfaces.popup;
    c.pal.popup_border = surfaces.popup_border;
    c.pal.tooltip = surfaces.tooltip;
    c.pal.text = surfaces.text;
    c.pal.text_soft = surfaces.text_soft;
    c.pal.text_hover = surfaces.text_hover;
    c.pal.text_dim = surfaces.text_dim;
    c.pal.grip = surfaces.grip;
}

void set_scale(float scale)
{
    context_t& c = g_ctx;
    c.scale = ImClamp(scale, 0.5f, 4.0f);
    c.icons.build(c.scale);
    init_style();
}

void init_style()
{
    context_t& c = g_ctx;
    ImGuiStyle s;
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(0.0f, 0.0f);
    s.FramePadding = ImVec2(c.m.frame_text_x, 4.5f);
    s.ItemSpacing = ImVec2(8.0f, c.m.item_spacing);
    s.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    s.WindowRounding = c.m.window_rounding;
    s.ChildRounding = 0.0f;
    s.FrameRounding = c.m.frame_rounding;
    s.PopupRounding = c.m.popup_rounding;
    s.ScrollbarSize = 6.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.PopupBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowMinSize = ImVec2(32.0f, 32.0f);
    s.HoverDelayNormal = 0.45f;
    s.HoverStationaryDelay = 0.12f;
    s.ScaleAllSizes(c.scale);
    s.FontSizeBase = font_px(c.m.font_body);
    s.FontScaleDpi = 1.0f;

    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = c.pal.text;
    col[ImGuiCol_TextDisabled] = c.pal.text_dim;
    col[ImGuiCol_WindowBg] = c.pal.window_bg;
    col[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_PopupBg] = c.pal.popup;
    col[ImGuiCol_Border] = c.pal.popup_border;
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = c.pal.frame;
    col[ImGuiCol_FrameBgHovered] = c.pal.frame_hover;
    col[ImGuiCol_FrameBgActive] = c.pal.frame_active;
    col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ScrollbarGrab] = c.pal.frame_hover;
    col[ImGuiCol_ScrollbarGrabHovered] = c.pal.grip;
    col[ImGuiCol_ScrollbarGrabActive] = c.pal.text_dim;
    col[ImGuiCol_Button] = c.pal.frame;
    col[ImGuiCol_ButtonHovered] = c.pal.frame_hover;
    col[ImGuiCol_ButtonActive] = c.pal.frame_active;
    col[ImGuiCol_Header] = c.pal.frame;
    col[ImGuiCol_HeaderHovered] = c.pal.frame_hover;
    col[ImGuiCol_HeaderActive] = c.pal.frame_active;
    col[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripActive] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
    ImGui::GetStyle() = s;
    set_accent(c.accent_start, c.accent_end);
}

void begin_frame()
{
    context_t& c = g_ctx;
    const ImGuiIO& io = ImGui::GetIO();
    c.dt = io.DeltaTime;
    c.time += static_cast<double>(io.DeltaTime);
}

void shutdown()
{
    g_ctx.icons.release();
    g_ctx.state.Clear();
    g_ctx.panels.clear();
}

// ---- Layout ----------------------------------------------------------------------------------------------------

void begin_columns(const char* id, const ImRect& area, int count)
{
    context_t& c = g_ctx;
    column_layout_t& L = c.layout;
    L.area = area;
    L.count = ImMax(1, count);
    L.index = 0;
    L.width = draw::snap((area.GetWidth() - scaled_px(c.m.gap) * static_cast<float>(L.count - 1)) / static_cast<float>(L.count));
    L.cursor = area.Min.y;
    L.reserve = 0.0f;
    L.after_fill = false;
    L.id = ImGui::GetID(id);
}

static void store_column_reserve()
{
    context_t& c = g_ctx;
    column_layout_t& L = c.layout;
    c.state.SetFloat(ImHashStr("reserve", 0, L.id + static_cast<ImGuiID>(L.index)), L.after_fill ? L.reserve : 0.0f);
}

void next_column()
{
    column_layout_t& L = g_ctx.layout;
    store_column_reserve();
    L.index = ImMin(L.index + 1, L.count - 1);
    L.cursor = L.area.Min.y;
    L.reserve = 0.0f;
    L.after_fill = false;
}

void end_columns() { store_column_reserve(); }

bool begin_panel(const char* title, panel_height height)
{
    context_t& c = g_ctx;
    column_layout_t& L = c.layout;
    ImGuiWindow* parent = ImGui::GetCurrentWindow();

    panel_frame_t p;
    p.id = parent->GetID(title != nullptr ? title : "##pane");
    p.x = L.area.Min.x + static_cast<float>(L.index) * (L.width + scaled_px(c.m.gap));
    p.w = L.index == L.count - 1 ? L.area.Max.x - p.x : L.width;
    p.y = L.cursor;
    p.header = title != nullptr ? scaled_px(c.m.panel_header_h) : 0.0f;
    p.fill = height == panel_height::fill;
    p.title = title;
    c.panels.push_back(p);
    c.path_panel = title != nullptr ? title : "";

    ImDrawList* dl = parent->DrawList;
    const float r = px(c.m.panel_rounding);
    if (title != nullptr) {
        const ImVec2 h0(p.x, p.y), h1(p.x + p.w, p.y + p.header);
        dl->AddRectFilled(h0, h1, draw::col(c.pal.card), r, ImDrawFlags_RoundCornersTop);
        const float fs = font_px(c.m.font_header);
        const float tw = draw::text_width(c.fonts.bold, fs, title, ImGui::FindRenderedTextEnd(title));
        draw::text_cap_centered(dl, c.fonts.bold, fs, p.x + (p.w - tw) * 0.5f, p.y + p.header * 0.5f, draw::col(c.pal.text), title,
                              ImGui::FindRenderedTextEnd(title));
    }

    float body_h = 0.0f;
    ImGuiChildFlags child_flags = ImGuiChildFlags_None;
    if (p.fill) {
        const float reserve = c.state.GetFloat(ImHashStr("reserve", 0, L.id + static_cast<ImGuiID>(L.index)), 0.0f);
        body_h = ImMax(scaled_px(40.0f), L.area.Max.y - p.y - p.header - reserve);
    } else {
        child_flags |= ImGuiChildFlags_AutoResizeY;
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + p.header));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(scaled_px(c.m.panel_pad_x), scaled_px(c.m.panel_pad_y)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(scaled_px(8.0f), scaled_px(c.m.item_spacing)));
    ImGui::BeginChild(p.id, ImVec2(p.w, body_h), child_flags | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(1); // WindowPadding is consumed by Begin; ItemSpacing stays until end_panel

    if (p.fill) {
        ImGuiWindow* child = ImGui::GetCurrentWindow();
        const ImGuiID target_key = ImHashStr("scroll_target", 0, child->ID);
        float target = c.state.GetFloat(target_key, child->Scroll.y);
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
            target -= wheel * px(c.m.scroll_step);
        target = ImClamp(target, 0.0f, child->ScrollMax.y);
        c.state.SetFloat(target_key, target);
        const float smooth = animate(child->ID, "scroll", target, 16.0f, child->Scroll.y);
        if (std::fabs(smooth - child->Scroll.y) > 0.01f) ImGui::SetScrollY(smooth);
    }
    return true;
}

void end_panel()
{
    context_t& c = g_ctx;
    column_layout_t& L = c.layout;
    IM_ASSERT(!c.panels.empty() && "end_panel() without begin_panel()");
    const panel_frame_t p = c.panels.back();
    c.panels.pop_back();

    ImGuiWindow* child = ImGui::GetCurrentWindow();
    child->DC.CursorMaxPos.y += scaled_px(c.m.panel_extra_bottom);

    if (child->ScrollMax.y > 0.5f) {
        const float visible = child->Size.y;
        const float content = visible + child->ScrollMax.y;
        const float inset = scaled_px(6.0f);
        const float track = visible - inset * 2.0f;
        const float thumb = ImMax(scaled_px(24.0f), track * visible / content);
        const float y = child->Pos.y + inset + (track - thumb) * ImSaturate(child->Scroll.y / child->ScrollMax.y);
        const float x1 = child->Pos.x + child->Size.x - scaled_px(c.m.scroll_right);
        const float x0 = x1 - scaled_px(c.m.scroll_w);
        ImDrawList* dl = child->DrawList;
        dl->PushClipRectFullScreen();
        draw::glow(dl, ImVec2(x0, y), ImVec2(x1, y + thumb), (x1 - x0) * 0.5f, draw::col(c.pal.accent, 0.22f), px(5.0f));
        draw::rect_gradient(dl, ImVec2(x0, y), ImVec2(x1, y + thumb), draw::col(c.pal.accent), draw::col(c.pal.accent_end, 0.95f),
                           (x1 - x0) * 0.5f, 0, false);
        dl->PopClipRect();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(1);

    const ImRect body(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    const float r = px(c.m.panel_rounding);
    parent->DrawList->AddRectFilled(body.Min, body.Max, draw::col(c.pal.panel), r,
                                    p.header > 0.0f ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersAll);

    const float bottom = body.Max.y;
    if (L.after_fill) L.reserve += (bottom - p.y) + scaled_px(c.m.gap);
    if (p.fill) {
        L.after_fill = true;
        L.reserve = 0.0f;
    }
    L.cursor = bottom + scaled_px(c.m.gap);
    c.path_panel = "";
}

ImRect panel_inner_rect()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    return window->InnerRect;
}

// ---- Items -----------------------------------------------------------------------------------------------------

bool checkbox(const char* label, bool* v)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    ImFont* font = c.fonts.regular;
    const float fs = body_size();
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const float box = scaled_px(c.m.checkbox);
    const float row_h = scaled_px(c.m.row_h);
    const float gap = scaled_px(c.m.label_gap);
    const float text_w = label_end != label ? draw::text_width(font, fs, label, label_end) : 0.0f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + box + (text_w > 0.0f ? gap + text_w : 0.0f), pos.y + row_h));
    ImGui::ItemSize(bb, 0.0f);
    ImGui::SetNextItemAllowOverlap();
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }

    const float on = animate(id, "on", *v ? 1.0f : 0.0f, 15.0f, *v ? 1.0f : 0.0f);
    const float hot = animate(id, "hot", (hovered || held) ? 1.0f : 0.0f, 18.0f, 0.0f);
    {
        search_scope_t scope(label);
        ImDrawList* dl = window->DrawList;
        const ImVec2 b0(pos.x, pos.y + draw::snap((row_h - box) * 0.5f));
        const ImVec2 b1(b0.x + box, b0.y + box);
        const float r = px(c.m.checkbox_rounding);
        if (on > 0.01f) draw::glow(dl, b0, b1, r, draw::col(c.pal.accent, c.m.glow_alpha * on), px(c.m.glow));
        dl->AddRectFilled(b0, b1, draw::col(draw::mix(c.pal.frame, c.pal.frame_hover, hot)), r);
        if (on > 0.01f) draw::rect_gradient(dl, b0, b1, draw::col(c.pal.accent_top, on), draw::col(c.pal.accent_bottom, on), r, 0, false);
        draw::check(dl, b0, box, draw::col(c.pal.text, ImSaturate(on * 1.4f)), px(2.0f), on);
        if (text_w > 0.0f) {
            const ImVec4 tc = draw::mix(draw::mix(c.pal.text_dim, c.pal.text_hover, hot), c.pal.text, on);
            draw::text_cap_centered(dl, font, fs, b1.x + gap, (b0.y + b1.y) * 0.5f, draw::col(tc), label, label_end);
        }
    }
    ImGui::RenderNavCursor(bb, id);
    auto_tooltip(label);
    return pressed;
}

bool slider(const char* label, float* v, float min, float max, const char* fmt, formatter_t formatter)
{
    return slider_impl(label, v, min, max, fmt, formatter, false);
}

bool slider_int(const char* label, int* v, int min, int max, const char* fmt)
{
    float f = static_cast<float>(*v);
    const bool changed = slider_impl(label, &f, static_cast<float>(min), static_cast<float>(max), fmt, nullptr, true);
    *v = static_cast<int>(std::lround(f));
    return changed;
}

bool combo(const char* label, int* current, const char* const items[], int count)
{
    return combo_impl(label, current, nullptr, items, count);
}

bool multi_combo(const char* label, unsigned* mask, const char* const items[], int count)
{
    return combo_impl(label, nullptr, mask, items, count);
}

void caption(const char* text)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    context_t& c = g_ctx;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(content_right(window), pos.y + scaled_px(c.m.row_h)));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, 0)) return;
    search_scope_t scope(text);
    draw::text_cap_centered(window->DrawList, c.fonts.regular, body_size(), pos.x, bb.GetCenter().y, draw::col(c.pal.text_dim), text,
                          ImGui::FindRenderedTextEnd(text));
}

void color_row(const char* label, color_value_t* color, bool alpha)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const float tw = draw::text_width(c.fonts.regular, body_size(), label, label_end);
    const ImRect bb(pos, ImVec2(pos.x + tw, pos.y + scaled_px(c.m.row_h)));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return;
    const bool hovered = ImGui::IsItemHovered();
    const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
    {
        search_scope_t scope(label);
        draw::text_cap_centered(window->DrawList, c.fonts.regular, body_size(), pos.x, bb.GetCenter().y,
                              draw::col(draw::mix(c.pal.text_soft, c.pal.text, hot)), label, label_end);
    }
    auto_tooltip(label);
    char dot_id[64];
    ImFormatString(dot_id, IM_ARRAYSIZE(dot_id), "%.*s##dot", static_cast<int>(label_end - label), label);
    color_dot(dot_id, color, alpha);
}

bool button(const char* label, float width, bool accent)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float w = width > 0.0f ? width : content_right(window) - pos.x;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + scaled_px(c.m.button_h)));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
    const float down = animate(id, "down", held ? 1.0f : 0.0f, 26.0f, 0.0f);

    search_scope_t scope(label);
    ImDrawList* dl = window->DrawList;
    const float r = px(c.m.frame_rounding);
    if (accent) {
        draw::glow(dl, bb.Min, bb.Max, r, draw::col(c.pal.accent, 0.18f + 0.14f * hot), px(8.0f));
        draw::rect_gradient(dl, bb.Min, bb.Max, draw::col(draw::mix(c.pal.accent_top, c.pal.accent_end, hot * 0.25f)),
                           draw::col(c.pal.accent_bottom), r, 0, false);
    } else {
        dl->AddRectFilled(bb.Min, bb.Max, draw::col(draw::mix(draw::mix(c.pal.frame, c.pal.frame_hover, hot), c.pal.frame_active, down)), r);
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float fs = body_size();
    const float tw = draw::text_width(c.fonts.regular, fs, label, end);
    const ImVec4 tc = accent ? c.pal.text : draw::mix(c.pal.text_soft, c.pal.text, hot);
    draw::text_cap_centered(dl, c.fonts.regular, fs, bb.GetCenter().x - tw * 0.5f, bb.GetCenter().y + down, draw::col(tc), label, end);
    ImGui::RenderNavCursor(bb, id);
    return pressed;
}

bool input_text(const char* id, const char* hint, char* buf, size_t size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const float fs = body_size();
    const float h = scaled_px(c.m.button_h);
    ImGui::PushFont(c.fonts.regular, fs);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(scaled_px(c.m.frame_text_x), ImMax(0.0f, (h - fs) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(c.m.frame_rounding));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, c.pal.text_dim);
    ImGui::SetNextItemWidth(content_right(window) - window->DC.CursorPos.x);
    const bool changed = ImGui::InputTextWithHint(id, hint, buf, size);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    return changed;
}

bool list_row(const char* label, const char* detail, bool selected)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(content_right(window), pos.y + scaled_px(c.m.list_row_h)));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
    const float sel = animate(id, "sel", selected ? 1.0f : 0.0f, 16.0f, selected ? 1.0f : 0.0f);

    search_scope_t scope(label);
    ImDrawList* dl = window->DrawList;
    const float r = px(c.m.frame_rounding);
    dl->AddRectFilled(bb.Min, bb.Max, draw::col(draw::mix(c.pal.frame, c.pal.frame_hover, ImMax(hot, sel * 0.6f)), 0.55f + 0.45f * ImMax(hot, sel)), r);
    if (sel > 0.01f) {
        const ImVec2 a(bb.Min.x, bb.Min.y + scaled_px(7.0f)), b(bb.Min.x + scaled_px(3.0f), bb.Max.y - scaled_px(7.0f));
        draw::glow(dl, a, b, scaled_px(1.5f), draw::col(c.pal.accent, 0.35f * sel), px(6.0f));
        draw::rect_gradient(dl, a, b, draw::col(c.pal.accent, sel), draw::col(c.pal.accent_end, sel), scaled_px(1.5f), 0, false);
    }
    const float fs = body_size();
    const float x = bb.Min.x + scaled_px(12.0f) + scaled_px(4.0f) * sel;
    draw::text_cap_centered(dl, c.fonts.regular, fs, x, bb.GetCenter().y,
                          draw::col(draw::mix(draw::mix(c.pal.text_soft, c.pal.text, hot), c.pal.text, sel)), label,
                          ImGui::FindRenderedTextEnd(label));
    if (detail != nullptr && detail[0] != '\0') {
        const float dw = draw::text_width(c.fonts.regular, fs, detail);
        draw::text_cap_centered(dl, c.fonts.regular, fs, bb.Max.x - scaled_px(12.0f) - dw, bb.GetCenter().y, draw::col(c.pal.text_dim), detail);
    }
    ImGui::RenderNavCursor(bb, id);
    return pressed;
}

void separator()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    context_t& c = g_ctx;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(content_right(window), pos.y + 1.0f));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, 0)) return;
    window->DrawList->AddRectFilled(bb.Min, bb.Max, draw::col(c.pal.card));
}

void tooltip(const char* text)
{
    if (text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary)) show_tooltip(text);
}

bool color_dot(const char* id_str, color_value_t* color, bool alpha)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(id_str);
    const float d = scaled_px(c.m.dot);
    const ImVec2 center = AccessoryCenter(id, d);
    const float hit = d * 0.5f + scaled_px(3.0f);
    const ImRect bb(ImVec2(center.x - hit, center.y - hit), ImVec2(center.x + hit, center.y + hit));
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    const ImGuiID popup_id = ImHashStr("##picker", 0, id);
    if (ImGui::ButtonBehavior(bb, id, &hovered, &held) && !ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None)) ImGui::OpenPopupEx(popup_id);
    const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const float hot = animate(id, "hot", (hovered || open) ? 1.0f : 0.0f, 18.0f, 0.0f);

    {
        search_scope_t scope(nullptr, true);
        ImDrawList* dl = window->DrawList;
        const ImVec4 col = color->resolve(c.time);
        const float r = d * 0.5f + px(1.0f) * hot;
        draw::glow(dl, ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), r,
                   draw::col(ImVec4(col.x, col.y, col.z, 1.0f), 0.28f * ImSaturate(col.w + 0.15f)), px(c.m.glow));
        dl->AddCircleFilled(center, r, draw::col(col));
        if (col.w < 0.97f) dl->AddCircle(center, r - 0.5f, draw::col(ImVec4(col.x, col.y, col.z, ImMin(1.0f, col.w + 0.22f))), 0, 1.0f);
    }
    ImGui::RenderNavCursor(bb, id);
    picker_popup(popup_id, color, alpha, center);
    return false;
}

bool begin_settings(const char* id_str, settings_icon icon, float width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(id_str);
    const float d = scaled_px(icon == settings_icon::gear ? 16.0f : c.m.dot);
    const ImVec2 center = AccessoryCenter(id, d);
    const float hit = d * 0.5f + scaled_px(3.0f);
    const ImRect bb(ImVec2(center.x - hit, center.y - hit), ImVec2(center.x + hit, center.y + hit));
    const ImGuiID popup_id = ImHashStr("##settings", 0, id);
    const ImGuiID was_key = ImHashStr("open_prev", 0, id);
    const bool was_open = c.state.GetBool(was_key, false);

    bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (ImGui::ItemAdd(bb, id)) {
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(bb, id, &hovered, &held) && !open && !was_open) {
            ImGui::OpenPopupEx(popup_id);
            open = true;
        }
        const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);
        const float act = animate(id, "act", open ? 1.0f : 0.0f, 16.0f, 0.0f);

        search_scope_t scope(nullptr, true);
        ImDrawList* dl = window->DrawList;
        if (icon == settings_icon::plus) {
            const float r = d * 0.5f;
            const ImVec4 fill = draw::mix(c.pal.accent, c.pal.accent_end, ImMax(hot * 0.55f, act * 0.75f));
            draw::glow(dl, ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), r,
                       draw::col(fill, c.m.glow_alpha + 0.08f * hot), px(c.m.glow));
            dl->AddCircleFilled(center, r, draw::col(fill));
            draw::plus(dl, center, scaled_px(5.0f), scaled_px(2.0f), draw::col(c.pal.text));
        } else {
            c.icons.draw(dl, icon_id::gear_small, center, draw::col(draw::mix(c.pal.text_soft, c.pal.text, ImMax(hot, act))));
        }
        ImGui::RenderNavCursor(bb, id);
    }
    c.state.SetBool(was_key, open);

    const float reveal = popup_reveal(popup_id, open);
    if (!open) return false;

    const float w = width > 0.0f ? width : scaled_px(c.m.popup_w);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float last_h = c.state.GetFloat(ImHashStr("popup_h", 0, popup_id), scaled_px(120.0f));
    ImVec2 pos(center.x - w * 0.5f, center.y + d * 0.5f + scaled_px(c.m.popup_offset) - (1.0f - ease(reveal)) * scaled_px(6.0f));
    pos.x = ImClamp(pos.x, vp->WorkPos.x + scaled_px(4.0f), vp->WorkPos.x + vp->WorkSize.x - w - scaled_px(4.0f));
    if (pos.y + last_h > vp->WorkPos.y + vp->WorkSize.y) pos.y = center.y - d * 0.5f - scaled_px(c.m.popup_offset) - last_h;

    ImGui::SetNextWindowPos(draw::snap(pos));
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(scaled_px(c.m.popup_pad_x), scaled_px(c.m.popup_pad_y)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(scaled_px(8.0f), scaled_px(c.m.item_spacing)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    if (!ImGui::BeginPopupEx(popup_id, k_popup_flags | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        return false;
    }
    ImGuiWindow* popup = ImGui::GetCurrentWindow();
    c.state.SetFloat(ImHashStr("popup_h", 0, popup_id), popup->Size.y);
    popup_background(popup);
    return true;
}

void end_settings()
{
    ImGui::EndPopup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

bool card_combo(const char* id_str, int* current, const char* const items[], int count, const ImRect& frame)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0) return false;
    context_t& c = g_ctx;
    const ImGuiID id = window->GetID(id_str);
    *current = ImClamp(*current, 0, count - 1);
    if (!ImGui::ItemAdd(frame, id)) return false;

    const ImGuiID popup_id = ImHashStr("##dropdown", 0, id);
    bool hovered = false, held = false;
    const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (ImGui::ButtonBehavior(frame, id, &hovered, &held) && !open) ImGui::OpenPopupEx(popup_id);
    const float hot = animate(id, "hot", hovered ? 1.0f : 0.0f, 18.0f, 0.0f);

    ImDrawList* dl = window->DrawList;
    const float r = px(c.m.frame_rounding);
    dl->AddRectFilled(frame.Min, frame.Max, draw::col(draw::mix(c.pal.frame, c.pal.frame_hover, hot * 0.7f)), r);
    const float tx = scaled_px(c.m.card_text_x);
    const ImU32 tc = draw::col(draw::mix(c.pal.text_soft, c.pal.text, hot * 0.5f));
    dl->PushClipRect(frame.Min, ImVec2(frame.Max.x - scaled_px(26.0f), frame.Max.y), true);
    draw::text_cap_centered(dl, c.fonts.regular, body_size(), frame.Min.x + tx, frame.GetCenter().y, tc, items[*current]);
    dl->PopClipRect();
    draw::chevron(dl, ImVec2(draw::snap(frame.Max.x - scaled_px(c.m.card_chevron_right)), draw::snap(frame.GetCenter().y) + 0.5f), px(9.0f),
                  px(4.5f), false, tc, px(1.5f));
    ImGui::RenderNavCursor(frame, id);

    return dropdown(popup_id, frame, items, count, current, nullptr, items[*current], tx);
}

} // namespace ui::astral::widgets

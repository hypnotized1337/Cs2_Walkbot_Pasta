#include <stdafx.hpp>
#ifdef VESTA_ASTRAL_MENU
#include "../astral/widgets.hpp"
#endif
#include <ui/menu/library_widgets.hpp>
#include <render/fonts.hpp>
#include <render/presentation.hpp>
#include <render/text/localization.hpp>
#include <imgui.h>

#include <unordered_map>

namespace ui::menu::detail
{
namespace
{
#ifdef VESTA_ASTRAL_MENU
constexpr float k_tile_rounding = 6.0f;
#else
constexpr float k_tile_rounding = 2.0f;
#endif
constexpr float k_small_height = 24.0f;
#ifdef VESTA_ASTRAL_MENU
constexpr float k_control_rounding = 4.0f;
constexpr float k_section_rounding = 6.0f;
constexpr float k_card_header_height = 32.0f;
constexpr float k_card_vertical_padding = 11.0f;
#else
constexpr float k_control_rounding = 2.0f;
constexpr float k_card_vertical_padding = 10.0f;
constexpr float k_section_rounding = 5.0f;
#endif

struct library_colors_t
{
    ImVec4 card;
    ImVec4 panel;
    ImVec4 hover;
    ImVec4 border;
    ImVec4 border_light;
    ImVec4 outline;
    ImVec4 line;
    ImVec4 accent;
    ImVec4 text;
    ImVec4 text_dim;
};

library_colors_t library_colors()
{
#ifdef VESTA_ASTRAL_MENU
    const auto& palette = ui::astral::widgets::get().pal;
    return {palette.card, palette.panel, palette.frame_hover, palette.popup_border, palette.frame_active,
            palette.popup_border, palette.frame_active, palette.accent, palette.text, palette.text_dim};
#else
    return {k_bg_card, k_bg_panel, k_bg_hover, k_border, k_border_light, k_outline_dark, k_line, k_accent,
            k_text_main, k_text_muted};
#endif
}

[[nodiscard]] ImVec4 tone_color(const tone kind)
{
    const auto colors = library_colors();
    switch (kind)
    {
        case tone::accent:
            return colors.accent;
        case tone::danger:
            return {0.94f, 0.33f, 0.38f, 1.0f};
        case tone::normal:
            break;
    }
    return colors.text;
}

void stroke(ImDrawList* draw, const ImVec2 center, std::initializer_list<ImVec2> points, const ImU32 color,
            const float thickness = 1.5f)
{
    const ImVec2* previous = nullptr;
    for (const auto& point : points)
    {
        if (previous)
            draw->AddLine(center + *previous, center + point, color, thickness);
        previous = &point;
    }
}

// The entry of each open action menu that waits for its confirming second click.
std::unordered_map<ImGuiID, int> g_armed_actions{};

// BeginDisabled() dims ImGui's own colors through style.Alpha, but these controls paint their own geometry. The item
// flags carry the same state; apply the same alpha here so every custom button fades with the menu.
[[nodiscard]] bool item_disabled() noexcept
{
    const auto* context = ImGui::GetCurrentContext();
    return context && (context->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

[[nodiscard]] ImVec4 dimmed(ImVec4 color, const float alpha)
{
    color.w *= alpha;
    return color;
}

// Pick a readable label colour for a filled accent button instead of assuming the accent is always light.
[[nodiscard]] ImVec4 readable_ink(const ImVec4& background)
{
    const auto luminance = 0.2126f * background.x + 0.7152f * background.y + 0.0722f * background.z;
    return luminance > 0.58f ? ImVec4{0.07f, 0.07f, 0.08f, 1.0f} : ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
}
} // namespace

void draw_glyph(ImDrawList* draw, const glyph icon, const ImVec2 center, const ImU32 color)
{
    switch (icon)
    {
        case glyph::none:
            break;
        case glyph::import:
            stroke(draw, center, {{-6.0f, 1.0f}, {-6.0f, 6.0f}, {6.0f, 6.0f}, {6.0f, 1.0f}}, color, 1.5f);
            stroke(draw, center, {{0.0f, -6.5f}, {0.0f, 2.5f}}, color, 1.5f);
            stroke(draw, center, {{-3.5f, -1.0f}, {0.0f, 2.5f}, {3.5f, -1.0f}}, color, 1.5f);
            break;
        case glyph::folder:
            draw_action_icon(draw, row_action_icon::folder, center, color);
            break;
        case glyph::shield:
            stroke(draw, center,
                   {{0.0f, -6.5f},
                    {5.5f, -4.5f},
                    {5.0f, 1.5f},
                    {0.0f, 6.5f},
                    {-5.0f, 1.5f},
                    {-5.5f, -4.5f},
                    {0.0f, -6.5f}},
                   color, 1.4f);
            stroke(draw, center, {{-2.5f, -0.5f}, {-0.5f, 1.8f}, {2.8f, -2.2f}}, color, 1.4f);
            break;
        case glyph::more:
            for (const float x : {-5.5f, 0.0f, 5.5f})
                draw->AddCircleFilled(center + ImVec2{x, 0.0f}, 1.7f, color, 10);
            break;
    }
}

void wide_card(const char* id, const char* title, const callback_ref body, const float actions_width,
               const callback_ref actions)
{
    // A fixed window inside the page: from the top of the free space to the bottom of the page.
    const auto top = std::max(g_cards_y[0], g_cards_y[1]);
    const auto width = g_cards_width * 2.0f + 16.0f;
    const auto page_bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
    const auto height = std::max(160.0f, page_bottom - (g_cards_origin.y + top) - 1.0f);
    const auto body_min = ImVec2{g_cards_origin.x, g_cards_origin.y + top};
    const auto body_max = body_min + ImVec2{width, height};
    ImGui::SetCursorScreenPos(body_min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, library_colors().card);
    ImGui::PushStyleColor(ImGuiCol_Border, library_colors().outline);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0f, k_card_vertical_padding});
    ImGui::BeginChild(id, {width, height}, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto title_pos = ImGui::GetCursorScreenPos();
    const auto right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    auto* title_font = ImGui::GetFont();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRect(body_min + ImVec2{1.0f, 1.0f}, body_max - ImVec2{1.0f, 1.0f}, packed(library_colors().border), k_section_rounding, 0, 1.0f);
    const auto title_size = ImGui::GetFontSize();
#ifdef VESTA_ASTRAL_MENU
    constexpr auto header_height = k_card_header_height;
#endif
    auto title_text = std::string{render::localization::tr(title)};
    for (auto& character : title_text)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    const auto title_width = title_font->CalcTextSizeA(title_size, FLT_MAX, 0.0f, title_text.c_str()).x;
#ifdef VESTA_ASTRAL_MENU
    const auto title_y = title_pos.y + (header_height - title_size) * 0.5f;
    const auto divider_y = title_pos.y + header_height * 0.5f;
    const auto actions_y = title_pos.y + (header_height - k_small_height) * 0.5f;
#else
    const auto title_y = title_pos.y - 4.0f;
    const auto divider_y = title_pos.y + 18.0f;
    const auto actions_y = title_pos.y + (title_size - k_small_height) * 0.5f;
#endif
    draw->AddText(title_font, title_size,
                  {ImGui::GetWindowPos().x + (width - title_width) * 0.5f, title_y}, packed(library_colors().text),
                  title_text.c_str());
    draw->AddLine({ImGui::GetWindowPos().x + 3.0f, divider_y}, {right - actions_width - 10.0f, divider_y},
                  packed(library_colors().line), 1.0f);
    ImGui::SetCursorScreenPos({right - actions_width, actions_y});
    actions();
    ImGui::SetCursorScreenPos(title_pos);
    ImGui::Dummy({0.0f, title_size});
#ifndef VESTA_ASTRAL_MENU
    ImGui::Dummy({0.0f, 3.0f});
#endif
    body();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    g_cards_y[0] = g_cards_y[1] = top + height + 16.0f;
}

float small_button_width(const char* label, const glyph icon)
{
    const auto text = ImGui::CalcTextSize(render::localization::tr(label));
    return text.x + 24.0f + (icon != glyph::none ? 20.0f : 0.0f);
}

bool small_button(const char* id, const glyph icon, const char* label, const tone kind)
{
    const auto width = small_button_width(label, icon);
    ImGui::InvisibleButton(id, {width, k_small_height});
    const auto clicked = ImGui::IsItemClicked();
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    const auto item = ImGui::GetItemID();
    const auto disabled = item_disabled();
    const auto held = ImGui::IsItemActive() && !disabled;
    const auto focused = ImGui::IsItemFocused() && !disabled;
    const auto hover_t = animate_state(g_hover_animations, item, hovered && !disabled, 0.12f);
    const auto held_t = animate_state(g_active_animations, item, held, 0.08f);
    const auto focus_t = animate_state(g_active_animations, item ^ 0x6f70656eu, focused, 0.12f);
    const auto dim = disabled ? ImGui::GetStyle().Alpha : 1.0f;
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    const auto base = tone_color(kind);
    const auto emphasis = std::max(std::max(hover_t, held_t), focus_t);
    draw->AddRectFilled(min, max, packed(dimmed(library_colors().outline, dim)), k_control_rounding);
    const auto tint = kind == tone::accent ? 0.18f : 0.28f;
    auto fill = mix(library_colors().panel, ImVec4{base.x, base.y, base.z, tint}, emphasis);
    draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, packed(dimmed(fill, dim)), 1.0f);
    auto border = mix(library_colors().border, base, kind == tone::accent ? 0.30f + emphasis * 0.70f : emphasis * 0.70f);
    if (focused)
        border = mix(border, library_colors().accent, 0.75f);
    draw->AddRect(min, max, packed(dimmed(border, dim)), k_control_rounding, 0, 1.0f);
    if (focus_t > 0.01f)
        draw->AddRect(min - ImVec2{2.0f, 2.0f}, max + ImVec2{2.0f, 2.0f},
                      packed(dimmed(library_colors().accent, dim * focus_t)), 3.0f, 0, 1.0f);
    const auto ink = packed(dimmed(kind == tone::accent
                                       ? mix(library_colors().text, base, 0.55f + emphasis * 0.35f)
                                       : mix(library_colors().text_dim, library_colors().text, emphasis),
                                  dim));
    auto x = min.x + 8.0f;
    if (icon != glyph::none)
    {
        draw_glyph(draw, icon, {x + 6.0f, (min.y + max.y) * 0.5f}, ink);
        x += 17.0f;
    }
    const auto* text = render::localization::tr(label);
    draw->AddText({x, min.y + (k_small_height - ImGui::GetTextLineHeight()) * 0.5f}, ink, text);
    return clicked;
}

bool pill_button(const char* id, const char* label, const float width, const tone kind, const float height)
{
    ImGui::InvisibleButton(id, {width, height});
    const auto clicked = ImGui::IsItemClicked();
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    const auto item = ImGui::GetItemID();
    const auto disabled = item_disabled();
    const auto held = ImGui::IsItemActive() && !disabled;
    const auto focused = ImGui::IsItemFocused() && !disabled;
    const auto hover_t = animate_state(g_hover_animations, item, hovered && !disabled, 0.12f);
    const auto held_t = animate_state(g_active_animations, item, held, 0.08f);
    const auto focus_t = animate_state(g_active_animations, item ^ 0x70696c6cu, focused, 0.12f);
    const auto dim = disabled ? ImGui::GetStyle().Alpha : 1.0f;
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    const auto base = tone_color(kind);
    draw->AddRectFilled(min, max, packed(dimmed(library_colors().outline, dim)), k_control_rounding);
    ImVec4 fill = library_colors().panel;
    ImVec4 ink = mix(library_colors().text_dim, library_colors().text, std::max(hover_t, held_t));
    ImVec4 border = mix(library_colors().border, library_colors().border_light, std::max(hover_t, focus_t));
    if (kind == tone::accent)
    {
        const auto normal = ImVec4{library_colors().accent.x, library_colors().accent.y, library_colors().accent.z, 1.0f};
        const auto hover = mix(normal, ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
        const auto pressed = mix(normal, ImVec4{0.0f, 0.0f, 0.0f, 1.0f}, 0.18f);
        fill = mix(normal, hover, hover_t);
        fill = mix(fill, pressed, held_t);
        border = mix(normal, ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.18f + 0.16f * hover_t);
        border = mix(border, ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, focus_t * 0.25f);
        ink = readable_ink(fill);
        draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, packed(dimmed(fill, dim)), 1.0f);
    }
    else
    {
        fill = mix(library_colors().panel, ImVec4{base.x, base.y, base.z, 0.30f}, hover_t);
        fill = mix(fill, ImVec4{base.x, base.y, base.z, 0.42f}, held_t);
        draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, packed(dimmed(fill, dim)), 1.0f);
    }
    draw->AddRect(min, max, packed(dimmed(border, dim)), k_control_rounding, 0, 1.0f);
    if (focus_t > 0.01f)
        draw->AddRect(min - ImVec2{2.0f, 2.0f}, max + ImVec2{2.0f, 2.0f},
                      packed(dimmed(library_colors().accent, dim * focus_t)), 3.0f, 0, 1.0f);
    const auto* text = render::localization::tr(label);
    const auto text_size = ImGui::CalcTextSize(text);
    draw->PushClipRect(min, max, true);
    draw->AddText(
        {min.x + std::max(6.0f, (width - text_size.x) * 0.5f), min.y + (height - text_size.y) * 0.5f + held_t},
        packed(dimmed(ink, dim)), text);
    draw->PopClipRect();
    return clicked;
}

bool hint_input(const char* id, const char* hint, char* buffer, const std::size_t size, const float width,
                const ImGuiInputTextFlags flags)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, library_colors().panel);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, library_colors().hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, library_colors().hover);
    ImGui::PushStyleColor(ImGuiCol_Border, library_colors().border_light);
    ImGui::PushStyleColor(ImGuiCol_Text, library_colors().text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, library_colors().text_dim);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, k_control_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0f, 3.0f});
    ImGui::SetNextItemWidth(width);
    const auto submitted = ImGui::InputTextWithHint(id, render::localization::tr(hint), buffer, size,
                                                    flags | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);
    return submitted;
}

bool styled_button(const char* label, const ImVec2 size, const bool accent)
{
#ifdef VESTA_ASTRAL_MENU
    const auto& palette = ui::astral::widgets::get().pal;
    const auto fill = accent ? palette.accent : palette.frame;
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          accent ? mix(fill, ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, 0.10f) : palette.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent ? palette.accent_end : palette.frame_active);
    ImGui::PushStyleColor(ImGuiCol_Text, accent ? readable_ink(fill) : palette.text_soft);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
#endif
    const bool pressed = ImGui::Button(label, size);
#ifdef VESTA_ASTRAL_MENU
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
#endif
    return pressed;
}

bool hit_region(const char* id, const ImVec2 size)
{
    return ImGui::InvisibleButton(id, size);
}

int action_menu(const char* id, const std::span<const menu_action> actions)
{
    constexpr float button = 24.0f;
    constexpr float row = 28.0f;
    ImGui::PushID(id);
    ImGui::InvisibleButton("##more", {button, button});
    const auto popup_id = ImGui::GetID("##actions");
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup("##actions");
        g_armed_actions.erase(popup_id);
    }
    pointer_cursor_if_hovered();
    const auto disabled = item_disabled();
    const auto open = ImGui::IsPopupOpen("##actions");
    const auto hover_t =
        animate_state(g_hover_animations, ImGui::GetItemID(), (ImGui::IsItemHovered() || open) && !disabled, 0.12f);
    const auto held_t = animate_state(g_active_animations, ImGui::GetItemID() ^ 0x6d6f7265u,
                                      ImGui::IsItemActive() && !disabled, 0.08f);
    const auto focus_t = animate_state(g_active_animations, ImGui::GetItemID() ^ 0x666f6375u,
                                       ImGui::IsItemFocused() && !disabled, 0.12f);
    const auto dim = disabled ? ImGui::GetStyle().Alpha : 1.0f;
    const auto button_min = ImGui::GetItemRectMin();
    const auto button_max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    const auto emphasis = std::max(std::max(hover_t, held_t), focus_t);
    draw->AddRectFilled(button_min, button_max, packed(dimmed(library_colors().outline, dim)), k_control_rounding);
    auto fill = mix(library_colors().panel, library_colors().hover, hover_t);
    fill = mix(fill, ImVec4{library_colors().accent.x, library_colors().accent.y, library_colors().accent.z, 0.16f}, std::max(held_t, focus_t));
    draw->AddRectFilled(button_min + ImVec2{1.0f, 1.0f}, button_max - ImVec2{1.0f, 1.0f},
                        packed(dimmed(fill, dim)), 1.0f);
    draw->AddRect(button_min, button_max, packed(dimmed(mix(library_colors().border, library_colors().accent, 0.30f + emphasis * 0.70f), dim)), k_control_rounding,
                  0, 1.0f);
    draw_glyph(draw, glyph::more, (button_min + button_max) * 0.5f,
               packed(dimmed(mix(library_colors().text_dim, library_colors().accent, emphasis), dim)));

    float width = 170.0f;
    for (const auto& action : actions)
        width = std::max(width, ImGui::CalcTextSize(render::localization::tr(action.label)).x + 36.0f);
    width = std::max(width, ImGui::CalcTextSize(render::localization::tr("Click again to confirm")).x + 36.0f);
    const auto height = 4.0f + row * static_cast<float>(actions.size());
    ImGui::SetNextWindowPos({button_max.x - width, button_max.y + 3.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    int chosen = -1;
    if (ImGui::BeginPopup("##actions", ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar))
    {
        const auto min = ImGui::GetWindowPos();
        const ImVec2 max{min.x + width, min.y + height};
        auto* popup = ImGui::GetWindowDrawList();
        popup->PushClipRectFullScreen();
        draw_popup_surface(popup, min, max, 0.0f);
        popup->PopClipRect();
        const auto armed = g_armed_actions.contains(popup_id) ? g_armed_actions[popup_id] : -1;
        for (int index = 0; index < static_cast<int>(actions.size()); ++index)
        {
            const auto& action = actions[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            ImGui::SetCursorScreenPos({min.x + 2.0f, min.y + 2.0f + row * static_cast<float>(index)});
            ImGui::InvisibleButton("##action", {width - 4.0f, row});
            const auto hovered = ImGui::IsItemHovered();
            pointer_cursor_if_hovered();
            const auto item_min = ImGui::GetItemRectMin();
            const auto item_max = ImGui::GetItemRectMax();
            const auto item_hover = animate_state(g_hover_animations, ImGui::GetItemID(), hovered, 0.10f);
            const auto waiting = armed == index;
            const auto color = action.kind == tone::danger ? tone_color(tone::danger) : library_colors().accent;
            if (item_hover > 0.01f || waiting)
                popup->AddRectFilled(item_min + ImVec2{0.0f, 1.0f}, item_max - ImVec2{0.0f, 1.0f},
                                     packed(waiting ? ImVec4{color.x, color.y, color.z, 0.16f}
                                                    : mix(ImVec4{1, 1, 1, 0},
                                                          ImVec4{color.x, color.y, color.z, 0.10f}, item_hover)),
                                     2.0f);
            const auto* text = render::localization::tr(waiting ? "Click again to confirm" : action.label);
            popup->AddText({item_min.x + 12.0f, item_min.y + (row - ImGui::GetTextLineHeight()) * 0.5f},
                           packed(action.kind == tone::danger ? color : mix(library_colors().text_dim, library_colors().text, item_hover)),
                           text);
            if (ImGui::IsItemClicked())
            {
                if (action.confirm && !waiting)
                    g_armed_actions[popup_id] = index;
                else
                {
                    chosen = index;
                    g_armed_actions.erase(popup_id);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    else
        g_armed_actions.erase(popup_id);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    ImGui::PopID();
    return chosen;
}

void tile_background(ImDrawList* draw, const ImVec2 min, const ImVec2 max, const float hover, const bool active)
{
    auto fill = mix(ImVec4{1.0f, 1.0f, 1.0f, 0.025f}, ImVec4{1.0f, 1.0f, 1.0f, 0.06f}, hover);
    if (active)
        fill = mix(fill, ImVec4{library_colors().accent.x, library_colors().accent.y, library_colors().accent.z, 0.14f}, 0.85f);
    draw->AddRectFilled(min, max, packed(fill), k_tile_rounding);
    const auto edge = active ? ImVec4{library_colors().accent.x, library_colors().accent.y, library_colors().accent.z, 0.55f} : mix(library_colors().border, library_colors().border_light, hover);
    draw->AddRect(min, max, packed(edge), k_tile_rounding);
    if (active)
        draw->AddRectFilled({min.x + 1.0f, min.y + 8.0f}, {min.x + 3.0f, max.y - 8.0f}, packed(library_colors().accent));
}
} // namespace ui::menu::detail

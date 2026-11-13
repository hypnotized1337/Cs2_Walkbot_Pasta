#include <stdafx.hpp>
#include <game/input/bindings.hpp>
#include <config/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <ui/menu/internal.hpp>
#if defined(VESTA_ASTRAL_MENU)
#include <ui/astral/draw.hpp>
#include <ui/astral/widgets.hpp>
#endif

namespace ui::menu::detail
{
#if defined(VESTA_ASTRAL_MENU)
namespace
{
bool widget_disabled()
{
    const auto* context = ImGui::GetCurrentContext();
    return context && (context->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

const ui::astral::palette_t& astral_palette()
{
    return ui::astral::widgets::get().pal;
}

float astral_animation(ImGuiID id, const char* salt, float target, float initial = 0.0f)
{
    return ui::astral::widgets::animate(id, salt, target, 12.0f, initial);
}

void draw_astral_frame(ImDrawList* draw, ImVec2 min, ImVec2 max, ImGuiID id, bool hovered, bool held)
{
    const auto& palette = astral_palette();
    const auto hover = astral_animation(id, "legacy_hover", hovered ? 1.0f : 0.0f);
    const auto press = astral_animation(id, "legacy_press", held ? 1.0f : 0.0f);
    auto fill = ui::astral::draw::mix(palette.frame, palette.frame_hover, hover);
    fill = ui::astral::draw::mix(fill, palette.frame_active, press);
    draw->AddRectFilled(min, max, packed(fill), 4.0f);
    draw->AddRect(min, max, packed(ui::astral::draw::fade(palette.popup_border, 0.8f + hover * 0.2f)), 4.0f, 0, 1.0f);
}
}
#endif

void from_imvec(const ImVec4& value, render::draw::rgba& color)
{
    color.r = static_cast<std::uint8_t>(std::clamp(value.x, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.g = static_cast<std::uint8_t>(std::clamp(value.y, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.b = static_cast<std::uint8_t>(std::clamp(value.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.a = static_cast<std::uint8_t>(std::clamp(value.w, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void toggle_control(bool& value)
{
    // Source checkbox: a square with a one-pixel black ring, no travel or thumb. The hit target stays 24px high
    // so it lines up with the neighbouring fields.
    constexpr auto hit_size = ImVec2{20.0f, 24.0f};
#if !defined(VESTA_ASTRAL_MENU)
    constexpr auto box_size = 20.0f;
#endif
    if (ImGui::InvisibleButton("##toggle", hit_size))
        value = !value;

    const auto raw_min = ImGui::GetItemRectMin();
#if defined(VESTA_ASTRAL_MENU)
    constexpr auto astral_box_size = 16.0f;
    const auto box_min = ImVec2{raw_min.x + 2.0f, raw_min.y + 4.0f};
    const auto box_max = box_min + ImVec2{astral_box_size, astral_box_size};
#else
    const auto box_min = ImVec2{raw_min.x, raw_min.y + 2.0f};
    const auto box_max = box_min + ImVec2{box_size, box_size};
#endif
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    auto* draw = ImGui::GetWindowDrawList();

#if defined(VESTA_ASTRAL_MENU)
    const auto id = ImGui::GetItemID();
    const auto& palette = astral_palette();
    const auto hover = astral_animation(id, "toggle_hover", hovered ? 1.0f : 0.0f);
    const auto checked = astral_animation(id, "toggle_checked", value ? 1.0f : 0.0f);
    const auto rounding = ui::astral::widgets::get().m.checkbox_rounding;
    const auto fill = ui::astral::draw::mix(palette.frame, palette.frame_hover, hover);
    // Paint the resting frame first. The old white underlay became visible while the accent gradient faded out on
    // uncheck, flashing a white square. The gradient now blends over the same frame at every point of the animation.
    draw->AddRectFilled(box_min, box_max, packed(fill), rounding);
    if (checked > 0.01f)
    {
        const auto glow_color = packed(ui::astral::draw::fade(palette.accent, checked * 0.22f));
        ui::astral::draw::glow(draw, box_min, box_max, rounding, glow_color, ui::astral::widgets::px(9.0f));
        const auto accent_top = packed(ui::astral::draw::fade(palette.accent_top, checked));
        const auto accent_bottom = packed(ui::astral::draw::fade(palette.accent_bottom, checked));
        ui::astral::draw::rect_gradient(draw, box_min, box_max, accent_top, accent_bottom, rounding,
                                        ImDrawFlags_RoundCornersAll, false);
        const auto check_color = packed(ui::astral::draw::fade(palette.text, checked));
        ui::astral::draw::check(draw, box_min, 16.0f, check_color, 1.6f, checked);
    }
    else
        draw->AddRect(box_min, box_max, packed(palette.popup_border), rounding, 0, 1.0f);
#else
    draw->AddRectFilled(box_min, box_max, packed(k_outline_dark));
    const auto inner_min = box_min + ImVec2{1.0f, 1.0f};
    const auto inner_max = box_max - ImVec2{1.0f, 1.0f};
    if (value)
    {
        // The checked state is an interaction accent, not a fixed pale colour. Custom red (or any other) accents
        // must reach every toggle, including the ones in popups and the ESP editor.
        draw->AddRectFilled(inner_min, inner_max, packed(k_accent));
    }
    else
    {
        draw->AddRectFilled(inner_min, inner_max, packed(k_bg_panel));
        draw->AddLine({inner_min.x, inner_max.y}, {inner_max.x, inner_min.y}, packed(ImVec4{1, 1, 1, 0.035f}),
                      1.0f);
    }
    draw->AddRect(box_min, box_max, hovered ? packed(k_border_light) : packed(k_outline_dark), 0.0f, 0, 1.0f);
#endif
}

void toggle_row(const char* label, bool& value)
{
    begin_row(label, 20.0f);
    toggle_control(value);
    end_row();
}

void toggle_color_row(const char* label, bool& value, render::draw::rgba& color)
{
    constexpr auto options_width = 20.0f;
    constexpr auto spacing = 6.0f;
    constexpr auto switch_width = 20.0f;
    begin_row(label, options_width + spacing + switch_width);

    const auto picker_id = ImGui::GetID("##picker");
    ImGui::InvisibleButton("##color_options", {options_width, 24.0f});
    const auto options_min = ImGui::GetItemRectMin();
    const auto options_max = ImGui::GetItemRectMax();
    pointer_cursor_if_hovered();
#if defined(VESTA_ASTRAL_MENU)
    const auto disabled = widget_disabled();
    if (!disabled && ImGui::IsItemClicked())
#else
    if (ImGui::IsItemClicked())
#endif
        ImGui::OpenPopup("##picker");

    auto* draw = ImGui::GetWindowDrawList();
    const auto dot_y = (options_min.y + options_max.y) * 0.5f;
#if defined(VESTA_ASTRAL_MENU)
    const auto& palette = astral_palette();
    const auto hover = astral_animation(ImGui::GetItemID(), "color_options_hover",
                                        ImGui::IsItemHovered() ? 1.0f : 0.0f);
    const auto dot_color = packed(ui::astral::draw::mix(palette.text_dim, palette.accent, hover));
#else
    const auto dot_color = IM_COL32_WHITE;
#endif
    for (int dot = 0; dot < 3; ++dot)
        draw->AddCircleFilled({options_min.x + 4.0f + dot * 6.0f, dot_y}, 1.0f, dot_color, 10);

    ImGui::SetCursorScreenPos({options_max.x + spacing, options_min.y});
    toggle_control(value);
    color_picker_popup(color, options_min, options_max, picker_id);
    end_row();
}

void slider_row(const char* label, int& value, int minimum, int maximum, const char* suffix)
{
    slider_row_impl(label, value, minimum, maximum, suffix, 1);
}

void slider_row(const char* label, float& value, float minimum, float maximum, const char* suffix, float step)
{
    slider_row_impl(label, value, minimum, maximum, suffix, step);
}

void slider_percent_row(const char* label, float& value)
{
    auto percent = std::clamp(value, 0.0f, 1.0f) * 100.0f;
    slider_row_impl(label, percent, 0.0f, 100.0f, "%", 1.0f);
    value = percent * 0.01f;
}

void draw_checkmark(ImDrawList* draw, ImVec2 center, ImU32 color, float amount, float thickness)
{
    if (amount <= 0.01f)
        return;

    const auto a = ImVec2{center.x - 4.0f, center.y};
    const auto b = ImVec2{center.x - 1.0f, center.y + 3.0f};
    const auto c = ImVec2{center.x + 5.0f, center.y - 4.0f};
    if (amount < 0.42f)
    {
        const auto t = amount / 0.42f;
        draw->AddLine(a, a + (b - a) * t, color, thickness);
        return;
    }

    draw->AddLine(a, b, color, thickness);
    const auto t = (amount - 0.42f) / 0.58f;
    draw->AddLine(b, b + (c - b) * std::clamp(t, 0.0f, 1.0f), color, thickness);
}

void draw_dropdown_chevron(ImDrawList* draw, ImVec2 center, ImU32 color, float open_amount)
{
    const auto points = ui::menu::chevron(open_amount);
    const auto left = center + ImVec2{points[0].x, points[0].y};
    const auto middle = center + ImVec2{points[1].x, points[1].y};
    const auto right = center + ImVec2{points[2].x, points[2].y};
    draw->AddLine(left, middle, color, 1.55f);
    draw->AddLine(middle, right, color, 1.55f);
}

void select_row(const char* label, int& value, std::span<const char* const> options)
{
    if (options.empty())
        return;

    constexpr auto control_width = 150.0f;
    constexpr auto control_height = 24.0f;
    constexpr auto option_height = 24.0f;

    auto popup_width = control_width;
    for (const auto* option : options)
    {
        popup_width = std::max(popup_width, ImGui::CalcTextSize(render::localization::tr(option)).x + 48.0f);
    }

    begin_row(label, control_width);
    const auto index = std::clamp(value, 0, static_cast<int>(options.size()) - 1);
    ImGui::InvisibleButton("##select_button", {control_width, control_height});
#if defined(VESTA_ASTRAL_MENU)
    const auto disabled = widget_disabled();
    if (!disabled && ImGui::IsItemClicked())
#else
    if (ImGui::IsItemClicked())
#endif
    {
        ImGui::OpenPopup("##options");
    }
    pointer_cursor_if_hovered();
    const auto button_hovered = ImGui::IsItemHovered();
    const auto popup_open = ImGui::IsPopupOpen("##options");
    const auto button_min = ImGui::GetItemRectMin();
    const auto button_max = ImGui::GetItemRectMax();

    auto* button_draw = ImGui::GetWindowDrawList();
#if defined(VESTA_ASTRAL_MENU)
    const auto button_id = ImGui::GetItemID();
    draw_astral_frame(button_draw, button_min, button_max, button_id, button_hovered || popup_open,
                      ImGui::IsItemActive());
#else
    button_draw->AddRectFilled(button_min, button_max, packed(k_outline_dark), 2.0f);
    button_draw->AddRectFilled(button_min + ImVec2{1.0f, 1.0f}, button_max - ImVec2{1.0f, 1.0f},
                               packed(button_hovered ? k_bg_hover : k_bg_panel), 1.0f);
    button_draw->AddRect(button_min, button_max,
                         packed(button_hovered || popup_open ? k_border_light : k_border), 2.0f, 0, 1.0f);
#endif
    const auto* selected_text = render::localization::tr(options[index]);
    const auto label_size = ImGui::CalcTextSize(selected_text);
    // Clipped short of the arrow so an over-long value cannot spill past the
    // button's own background and collide with the chevron.
    button_draw->PushClipRect(button_min, {button_max.x - 20.0f, button_max.y}, true);
    button_draw->AddText({button_min.x + 7.0f, button_min.y + (control_height - label_size.y) * 0.5f},
                         packed(k_text_main), selected_text);
    button_draw->PopClipRect();

    draw_dropdown_chevron(button_draw, {button_max.x - 9.0f, button_min.y + control_height * 0.5f},
                          packed(k_text_muted), popup_open ? 1.0f : 0.0f);

    const auto popup_height = 4.0f + options.size() * option_height;
    auto popup_position = ImVec2{button_min.x, button_max.y + 3.0f};
    if (popup_position.y + popup_height > settings_bounds_max().y - 8.0f)
        popup_position.y = button_min.y - popup_height - 3.0f;
    // Right-aligned with the button, so a popup wider than the control grows
    // back into the card instead of off its right edge.
    popup_position.x = std::max(button_max.x - popup_width, settings_bounds_min().x + 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize({popup_width, popup_height}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    if (ImGui::BeginPopup("##options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                           ImGuiWindowFlags_NoBackground))
    {
#if defined(VESTA_ASTRAL_MENU)
        ImGui::BeginDisabled(disabled);
#endif
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max = ImVec2{popup_min.x + ImGui::GetWindowSize().x, popup_min.y + ImGui::GetWindowSize().y};
        auto* popup_draw = ImGui::GetWindowDrawList();

        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        draw_popup_surface(popup_draw, popup_min, popup_max, 0.0f);
        popup_draw->PopClipRect();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(popup_min + ImVec2{2.0f, 2.0f + option_height * i});
            ImGui::InvisibleButton("##option", {popup_width - 4.0f, option_height});
            const auto option_hovered = ImGui::IsItemHovered();
            pointer_cursor_if_hovered();
            const auto option_min = ImGui::GetItemRectMin();
            const auto option_max = ImGui::GetItemRectMax();
            const auto selected = value == i;
#if defined(VESTA_ASTRAL_MENU)
            const auto& palette = astral_palette();
            const auto row_hover = astral_animation(ImGui::GetItemID(), "option_hover", option_hovered ? 1.0f : 0.0f);
            if (selected)
            {
                popup_draw->AddRectFilled(option_min, option_max, IM_COL32_WHITE, 3.0f);
                ui::astral::draw::rect_gradient(popup_draw, option_min, option_max, packed(palette.accent_top),
                                                packed(palette.accent_bottom), 3.0f, ImDrawFlags_RoundCornersAll, true);
            }
            else if (row_hover > 0.01f)
            {
                popup_draw->AddRectFilled(option_min, option_max,
                                          packed(ui::astral::draw::fade(palette.frame_hover, row_hover)), 3.0f);
            }
            const auto text_pos = ImVec2{option_min.x + 8.0f,
                                         option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f};
            popup_draw->AddText(text_pos, packed(selected ? palette.text : palette.text_soft),
                                render::localization::tr(options[i]));
#else
            if (selected)
            {
                const auto top = IM_COL32(184, 184, 210, 255);
                const auto bottom = IM_COL32(122, 124, 150, 255);
                popup_draw->AddRectFilledMultiColor(option_min, option_max, top, top, bottom, bottom);
            }
            else if (option_hovered)
            {
                popup_draw->AddRectFilled(option_min, option_max, packed(k_bg_hover));
            }
            popup_draw->AddText({option_min.x + 7.0f, option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f},
                                selected ? IM_COL32(12, 12, 14, 255) : packed(k_text_main),
                                render::localization::tr(options[i]));
#endif
            if (ImGui::IsItemClicked())
            {
                value = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
#if defined(VESTA_ASTRAL_MENU)
        ImGui::EndDisabled();
#endif
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    end_row();
}

void multiselect_row(const char* label, int& mask, std::span<const std::pair<const char*, int>> options, int all_mask)
{
    if (options.empty())
        return;

    constexpr auto control_width = 150.0f;
    constexpr auto control_height = 24.0f;
    constexpr auto option_height = 24.0f;
    begin_row(label, control_width);

    std::string summary{};
    if ((mask & all_mask) == all_mask)
        summary = render::localization::tr("All");
    else
    {
        for (const auto& [name, bit] : options)
            if (mask & bit)
                summary += (summary.empty() ? "" : ", ") + std::string(render::localization::tr(name));
        if (summary.empty())
            summary = render::localization::tr("None");
    }

    ImGui::InvisibleButton("##multiselect_button", {control_width, control_height});
#if defined(VESTA_ASTRAL_MENU)
    const auto disabled = widget_disabled();
    if (!disabled && ImGui::IsItemClicked())
#else
    if (ImGui::IsItemClicked())
#endif
        ImGui::OpenPopup("##multi_options");
    pointer_cursor_if_hovered();
    const auto button_hovered = ImGui::IsItemHovered();
    const auto popup_open = ImGui::IsPopupOpen("##multi_options");
    const auto button_min = ImGui::GetItemRectMin();
    const auto button_max = ImGui::GetItemRectMax();

    auto* button_draw = ImGui::GetWindowDrawList();
#if defined(VESTA_ASTRAL_MENU)
    const auto button_id = ImGui::GetItemID();
    draw_astral_frame(button_draw, button_min, button_max, button_id, button_hovered || popup_open,
                      ImGui::IsItemActive());
#else
    button_draw->AddRectFilled(button_min, button_max, packed(k_outline_dark), 2.0f);
    button_draw->AddRectFilled(button_min + ImVec2{1.0f, 1.0f}, button_max - ImVec2{1.0f, 1.0f},
                               packed(button_hovered ? k_bg_hover : k_bg_panel), 1.0f);
    button_draw->AddRect(button_min, button_max,
                         packed(button_hovered || popup_open ? k_border_light : k_border), 2.0f, 0, 1.0f);
#endif
    const auto label_size = ImGui::CalcTextSize(summary.c_str());
    button_draw->PushClipRect(button_min, {button_max.x - 20.0f, button_max.y}, true);
    button_draw->AddText({button_min.x + 7.0f, button_min.y + (control_height - label_size.y) * 0.5f},
                         packed(k_text_main), summary.c_str());
    button_draw->PopClipRect();

    draw_dropdown_chevron(button_draw, {button_max.x - 9.0f, button_min.y + control_height * 0.5f},
                          packed(k_text_muted), popup_open ? 1.0f : 0.0f);

    auto popup_width = control_width;
    for ([[maybe_unused]] const auto& [name, bit] : options)
    {
        popup_width = std::max(popup_width, ImGui::CalcTextSize(render::localization::tr(name)).x + 58.0f);
    }
    const auto popup_height = 4.0f + options.size() * option_height;
    auto popup_position = ImVec2{button_min.x, button_max.y + 3.0f};
    if (popup_position.y + popup_height > settings_bounds_max().y - 8.0f)
        popup_position.y = button_min.y - popup_height - 3.0f;
    popup_position.x = std::max(button_max.x - popup_width, settings_bounds_min().x + 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize({popup_width, popup_height}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    if (ImGui::BeginPopup("##multi_options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                 ImGuiWindowFlags_NoBackground))
    {
#if defined(VESTA_ASTRAL_MENU)
        ImGui::BeginDisabled(disabled);
#endif
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max = ImVec2{popup_min.x + ImGui::GetWindowSize().x, popup_min.y + ImGui::GetWindowSize().y};
        auto* popup_draw = ImGui::GetWindowDrawList();
        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        draw_popup_surface(popup_draw, popup_min, popup_max, 0.0f);
        popup_draw->PopClipRect();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(popup_min + ImVec2{2.0f, 2.0f + option_height * i});
            ImGui::InvisibleButton("##multi_option", {popup_width - 4.0f, option_height});
            const auto option_hovered = ImGui::IsItemHovered();
            pointer_cursor_if_hovered();
            const auto option_min = ImGui::GetItemRectMin();
            const auto option_max = ImGui::GetItemRectMax();
            const auto bit = options[i].second;
            const auto selected = (mask & bit) != 0;
#if defined(VESTA_ASTRAL_MENU)
            const auto& palette = astral_palette();
            const auto hover = astral_animation(ImGui::GetItemID(), "multi_hover", option_hovered ? 1.0f : 0.0f);
            if (hover > 0.01f)
                popup_draw->AddRectFilled(option_min, option_max,
                                          packed(ui::astral::draw::fade(palette.frame_hover, hover)), 3.0f);

            const auto check_min = ImVec2{option_min.x + 6.0f, option_min.y + 4.0f};
            constexpr auto check_size = 16.0f;
            const auto rounding = ui::astral::widgets::get().m.checkbox_rounding;
            const auto check_id = ImGui::GetItemID();
            const auto check_progress = astral_animation(check_id, "multi_checked", selected ? 1.0f : 0.0f);
            const auto check_end = check_min + ImVec2{check_size, check_size};
            const auto check_fill = ui::astral::draw::mix(palette.frame, palette.accent, check_progress);
            popup_draw->AddRectFilled(check_min, check_end, packed(check_fill), rounding);
            if (check_progress > 0.01f)
            {
                ui::astral::draw::glow(popup_draw, check_min, check_end, rounding,
                                       packed(ui::astral::draw::fade(palette.accent, check_progress * 0.2f)), 6.0f);
                ui::astral::draw::rect_gradient(popup_draw, check_min, check_end,
                                                packed(ui::astral::draw::fade(palette.accent_top, check_progress)),
                                                packed(ui::astral::draw::fade(palette.accent_bottom, check_progress)),
                                                rounding, ImDrawFlags_RoundCornersAll, false);
            }
            popup_draw->AddRect(check_min, check_end, packed(palette.popup_border), rounding, 0, 1.0f);
            ui::astral::draw::check(popup_draw, check_min, check_size,
                                    packed(ui::astral::draw::fade(palette.text, check_progress)), 1.5f, check_progress);
            popup_draw->AddText({option_min.x + 29.0f,
                                 option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f},
                                packed(selected ? palette.text : palette.text_soft),
                                render::localization::tr(options[i].first));
#else
            if (option_hovered)
                popup_draw->AddRectFilled(option_min, option_max, packed(k_bg_hover));

            const auto check_min = ImVec2{option_min.x + 6.0f, option_min.y + 5.0f};
            const auto check_max = check_min + ImVec2{14.0f, 14.0f};
            popup_draw->AddRectFilled(check_min, check_max, packed(k_outline_dark));
            popup_draw->AddRectFilled(check_min + ImVec2{1.0f, 1.0f}, check_max - ImVec2{1.0f, 1.0f},
                                      selected ? packed(k_accent) : packed(k_bg_panel));
            if (selected)
            {
                draw_checkmark(popup_draw, (check_min + check_max) * 0.5f, IM_COL32(20, 20, 22, 255), 1.0f, 1.5f);
            }
            popup_draw->AddText({option_min.x + 27.0f,
                                 option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f},
                                packed(selected ? k_text_main : k_text_muted),
                                render::localization::tr(options[i].first));
#endif
            if (ImGui::IsItemClicked())
                mask ^= bit; // toggle, keep popup open
            ImGui::PopID();
        }
#if defined(VESTA_ASTRAL_MENU)
        ImGui::EndDisabled();
#endif
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    end_row();
}

void aim_parts_row(int& mask)
{
    static constexpr std::pair<const char*, int> opts[]{
        {"Head", config::combat_profile::aim_part::head},
        {"Body", config::combat_profile::aim_part::body},
        {"Arms", config::combat_profile::aim_part::arms},
        {"Legs", config::combat_profile::aim_part::legs},
    };
    multiselect_row("Hitboxes", mask, opts, config::combat_profile::aim_part::all);
}

void draw_action_icon(ImDrawList* draw, row_action_icon icon, ImVec2 center, ImU32 color)
{
    switch (icon)
    {
        case row_action_icon::copy:
            draw->AddRect(center + ImVec2{-5.0f, -6.0f}, center + ImVec2{4.0f, 4.0f}, color, 2.0f, 0, 1.35f);
            draw->AddRect(center + ImVec2{-2.0f, -3.0f}, center + ImVec2{7.0f, 7.0f}, color, 2.0f, 0, 1.35f);
            break;
        case row_action_icon::check:
            draw_checkmark(draw, center, color, 1.0f, 1.8f);
            break;
        case row_action_icon::save:
            draw->AddLine(center + ImVec2{0.0f, -6.0f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
            draw->AddLine(center + ImVec2{-3.5f, -0.5f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
            draw->AddLine(center + ImVec2{3.5f, -0.5f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
            draw->AddLine(center + ImVec2{-5.0f, 6.0f}, center + ImVec2{5.0f, 6.0f}, color, 1.5f);
            break;
        case row_action_icon::folder:
            draw->AddRect(center + ImVec2{-7.0f, -3.5f}, center + ImVec2{7.0f, 6.0f}, color, 2.0f, 0, 1.45f);
            draw->AddLine(center + ImVec2{-6.0f, -3.5f}, center + ImVec2{-3.0f, -6.0f}, color, 1.45f);
            draw->AddLine(center + ImVec2{-3.0f, -6.0f}, center + ImVec2{1.0f, -6.0f}, color, 1.45f);
            draw->AddLine(center + ImVec2{1.0f, -6.0f}, center + ImVec2{3.0f, -3.5f}, color, 1.45f);
            break;
        default:
            break;
    }
}

bool button_row(const char* label, const char* text, row_action_icon icon)
{
    constexpr auto control_width = 120.0f;
    constexpr auto control_height = 24.0f;
    begin_row(label, control_width);

    ImGui::InvisibleButton("##button_row", {control_width, control_height});
    const auto clicked = ImGui::IsItemClicked();
    pointer_cursor_if_hovered();
    const auto hovered = ImGui::IsItemHovered();
    const auto held = ImGui::IsItemActive();
    const auto bmin = ImGui::GetItemRectMin();
    const auto bmax = ImGui::GetItemRectMax();

    auto* dl = ImGui::GetWindowDrawList();
#if defined(VESTA_ASTRAL_MENU)
    draw_astral_frame(dl, bmin, bmax, ImGui::GetItemID(), hovered, held);
#else
    dl->AddRectFilled(bmin, bmax, packed(k_outline_dark), 2.0f);
    dl->AddRectFilled(bmin + ImVec2{1.0f, 1.0f}, bmax - ImVec2{1.0f, 1.0f},
                      packed(held ? k_accent : hovered ? k_bg_hover : k_bg_panel), 1.0f);
    dl->AddRect(bmin, bmax, packed(hovered ? k_border_light : k_border), 2.0f, 0, 1.0f);
#endif

    const auto* button_text = render::localization::tr(text);
    const auto ts = ImGui::CalcTextSize(button_text);
    const auto has_icon = icon != row_action_icon::none;
    const auto content_width = ts.x + (has_icon ? 18.0f : 0.0f);
    const auto content_x = bmin.x + (control_width - content_width) * 0.5f;
    const auto press_offset = ImVec2{0.0f, held ? 1.0f : 0.0f};
#if defined(VESTA_ASTRAL_MENU)
    const auto content_color = packed(astral_palette().text);
#else
    const auto content_color = packed(held ? ImVec4{20.0f / 255.0f, 20.0f / 255.0f, 22.0f / 255.0f, 1.0f}
                                           : k_text_main);
#endif
    if (has_icon)
    {
        const auto icon_center = ImVec2{content_x + 6.0f, bmin.y + control_height * 0.5f} + press_offset;
        draw_action_icon(dl, icon, icon_center, content_color);
    }
    const auto text_position = ImVec2{content_x + (has_icon ? 18.0f : 0.0f),
                                      bmin.y + (control_height - ts.y) * 0.5f} + press_offset;
    dl->AddText(text_position, content_color, button_text);

    end_row();
    return clicked;
}

int filter_filename_char(ImGuiInputTextCallbackData* data)
{
    const auto c = data->EventChar;
    if (c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|')
        return 1; // discard
    return 0;
}

void text_input_row(const char* label, char* buffer, std::size_t size)
{
    constexpr auto control_width = 170.0f;
    begin_row(label, control_width);
#if defined(VESTA_ASTRAL_MENU)
    const auto& palette = astral_palette();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, palette.frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, palette.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, palette.frame_active);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.popup_border);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, palette.accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::astral::widgets::get().m.frame_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.0f, 4.0f});
    ImGui::SetNextItemWidth(control_width);
    ImGui::InputText("##text_input", buffer, size, ImGuiInputTextFlags_CallbackCharFilter, filter_filename_char);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(6);
#else
    ImGui::PushStyleColor(ImGuiCol_FrameBg, k_bg_panel);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_Border, k_border);
    ImGui::PushStyleColor(ImGuiCol_Text, k_text_main);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0f, 3.0f});
    ImGui::SetNextItemWidth(control_width);
    ImGui::InputText("##text_input", buffer, size, ImGuiInputTextFlags_CallbackCharFilter, filter_filename_char);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
#endif
    end_row();
}

void keybind_row(const char* label, int& value, bool keyboard_only, bool game_control)
{
    constexpr auto control_width = 130.0f;
    begin_row(label, control_width);
    const auto listening = g_listening_key == &value;
    const auto pulse = listening ? 0.8f + std::sin(static_cast<float>(ImGui::GetTime()) * 6.28318f) * 0.2f : 1.0f;
#if defined(VESTA_ASTRAL_MENU)
    const auto& palette = astral_palette();
    const auto listening_fill = ui::astral::draw::mix(palette.frame, palette.accent, 0.26f);
    const auto listening_color = ImVec4{listening_fill.x, listening_fill.y, listening_fill.z, pulse};
    ImGui::PushStyleColor(ImGuiCol_Button, listening ? listening_color : palette.frame);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, listening ? palette.accent : palette.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.frame_active);
    ImGui::PushStyleColor(ImGuiCol_Border, palette.popup_border);
    ImGui::PushStyleColor(ImGuiCol_Text, palette.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::astral::widgets::get().m.frame_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
#else
    ImGui::PushStyleColor(ImGuiCol_Button, listening ? ImVec4{k_accent.x, k_accent.y, k_accent.z, pulse} : k_bg_panel);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, listening ? k_accent : k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_Border, k_border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
#endif
    const auto text = listening ? std::string("...") : std::format("[ {} ]", key_name(value));

    if (ImGui::Button(text.c_str(), {control_width, 24.0f}) && !listening)
    {
        g_listening_key = &value;
        g_listening_start_frame = ImGui::GetFrameCount();
        g_listening_armed = false;
    }
    pointer_cursor_if_hovered();
#if defined(VESTA_ASTRAL_MENU)
    const auto disabled = widget_disabled();
#endif
    ImGui::PopStyleVar(2);
#if defined(VESTA_ASTRAL_MENU)
    ImGui::PopStyleColor(5);
#else
    ImGui::PopStyleColor(4);
#endif

#if defined(VESTA_ASTRAL_MENU)
    if (!disabled && g_listening_key == &value && ImGui::GetFrameCount() > g_listening_start_frame)
#else
    if (g_listening_key == &value && ImGui::GetFrameCount() > g_listening_start_frame)
#endif
    {
        if (!g_listening_armed)
        {
            if (bind_input_is_down())
            {
                end_row();
                return;
            }
            g_listening_armed = true;
            end_row();
            return;
        }

        if (const int key = pressed_bind_key())
        {
            const bool mouse =
                key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2;
            if ((!keyboard_only || !mouse) &&
                (!game_control || key == VK_ESCAPE || !config::is_lifecycle_key(static_cast<std::uint16_t>(key))))
            {
                value = key == VK_ESCAPE ? 0 : key;
                g_listening_key = nullptr;
                g_listening_armed = false;
            }
        }
    }
    end_row();
}

void color_row(const char* label, render::draw::rgba& color)
{
    begin_row(label, 32.0f);
    const auto picker_id = ImGui::GetID("##picker");
    ImGui::InvisibleButton("##preview", {32.0f, 24.0f});
    pointer_cursor_if_hovered();
#if defined(VESTA_ASTRAL_MENU)
    const auto disabled = widget_disabled();
    if (!disabled && ImGui::IsItemClicked())
#else
    if (ImGui::IsItemClicked())
#endif
        ImGui::OpenPopup("##picker");

    const auto hovered = ImGui::IsItemHovered();
    const auto item_min = ImGui::GetItemRectMin();
    const auto item_max = ImGui::GetItemRectMax();
    const auto preview_min = ImVec2{item_min.x + 1.0f, item_min.y + 4.0f};
    const auto preview_max = ImVec2{item_max.x - 1.0f, item_max.y - 4.0f};
    auto* draw = ImGui::GetWindowDrawList();
#if defined(VESTA_ASTRAL_MENU)
    const auto& palette = astral_palette();
    const auto rounding = ui::astral::widgets::get().m.checkbox_rounding;
    ui::astral::draw::checkerboard(draw, preview_min, preview_max, 5.0f, packed(palette.frame), packed(palette.panel));
    draw->AddRectFilled(preview_min, preview_max, packed(to_imvec(color)), rounding);
    const auto hover = astral_animation(ImGui::GetItemID(), "color_hover", hovered ? 1.0f : 0.0f);
    draw->AddRect(preview_min, preview_max, packed(hover > 0.01f ? palette.accent : palette.popup_border), rounding, 0,
                  1.0f + hover * 0.5f);
#else
    draw->AddRectFilled(preview_min, preview_max, IM_COL32(150, 150, 150, 255));
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            const auto a = ImVec2{preview_min.x + x * 5.2f, preview_min.y + y * 7.0f};
            const auto b = ImVec2{std::min(a.x + 5.2f, preview_max.x), std::min(a.y + 7.0f, preview_max.y)};
            draw->AddRectFilled(a, b, (x + y) % 2 ? IM_COL32(112, 112, 112, 255) : IM_COL32(190, 190, 190, 255));
        }
    }
    draw->AddRectFilled(preview_min, preview_max, packed(to_imvec(color)));
    draw->AddRect(preview_min, preview_max, hovered ? IM_COL32_WHITE : packed(k_outline_dark), 0.0f, 0, 1.0f);
#endif

    color_picker_popup(color, item_min, item_max, picker_id);
    end_row();
}

void humanizer_preview(int amount, int smoothing, const config::combat_profile::humanizer_settings& settings)
{
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    constexpr auto height = 76.0f;
    ImGui::InvisibleButton("##humanizer_preview", {width, height});
    auto* draw = ImGui::GetWindowDrawList();
    const auto left = origin + ImVec2{5.0f, height * 0.68f};
    const auto right = origin + ImVec2{width - 8.0f, height * 0.38f};
    const auto h = std::clamp(amount / 100.0f, 0.0f, 1.0f);
    const auto step_speed = std::clamp(settings.max_step / 15.0f, 0.25f, 4.0f);
    const auto duration = std::clamp((0.34f + smoothing * 0.032f) / std::sqrt(step_speed), 0.28f, 2.5f);
    const auto reaction = (settings.reaction_min_ms + settings.reaction_max_ms) * 0.0005f;
    const auto cycle = reaction + duration + 0.45f;
    const auto elapsed = static_cast<float>(ImGui::GetTime());
    const auto local = std::fmod(elapsed, cycle);
    const auto cycle_index = static_cast<int>(elapsed / cycle);
    const auto progress = local <= reaction ? 0.0f : std::clamp((local - reaction) / duration, 0.0f, 1.0f);
    const auto gravity = std::clamp(settings.gravity / 9.0f, 0.15f, 2.25f);
    const auto eased = 1.0f - std::pow(1.0f - progress, gravity);
    const auto bend = (settings.curve - 0.5f) * 38.0f * h;
    const auto damping = std::clamp(settings.damping, 0.0f, 1.0f);
    const auto wind = settings.wind * 0.85f * h;
    const auto overshoot_cycle = (cycle_index * 37) % 100 < static_cast<int>(settings.overshoot_chance);
    const auto overshoot = overshoot_cycle ? settings.overshoot_amount * 34.0f * h : 0.0f;
    const auto path_point = [&](float t)
    {
        const auto settle = std::sin(t * 3.14159265f);
        const auto overshoot_shape = t < 0.78f ? std::sin(t / 0.78f * 1.5707963f) : 1.0f - (t - 0.78f) / 0.22f;
        const auto x = std::lerp(left.x, right.x, t) + overshoot * overshoot_shape;
        const auto arc = settle * bend;
        const auto oscillation = std::sin(t * 18.0f + 0.7f) * wind * std::pow(1.0f - t, 0.35f + damping * 2.5f);
        const auto micro = std::sin(t * 71.0f + 1.9f) * settings.jitter * 1.4f * h * settle;
        return ImVec2{x, std::lerp(left.y, right.y, t) - arc + oscillation + micro};
    };
    ImVec2 previous = path_point(0.0f);
    for (int segment = 1; segment <= 48; ++segment)
    {
        const auto t = segment / 48.0f;
        const auto next = path_point(t);
        draw->AddLine(previous, next, IM_COL32(255, 255, 255, 31), 1.0f);
        previous = next;
    }
    const auto deadzone = 2.5f + settings.deadzone * 3.0f;
    draw->AddCircle(right, deadzone, IM_COL32(255, 255, 255, 48), 20, 1.0f);
    draw->AddCircleFilled(right, 1.7f, IM_COL32(232, 235, 244, 180), 16);
    for (int tail = 8; tail >= 0; --tail)
    {
        const auto t = std::max(0.0f, eased - tail * (0.012f + damping * 0.01f));
        const auto alpha = static_cast<int>(28 + (8 - tail) * 18);
        draw->AddCircleFilled(path_point(t), tail == 0 ? 2.8f : 1.4f, IM_COL32(214, 226, 255, alpha), 16);
    }
}

void dock_player_bar(config::visual_profile::player::layout_element& layout, int dock)
{
    const auto scale = layout.scale;
    switch (dock)
    {
        case 0:
            layout = {-0.07f, 0.50f, scale};
            break;
        case 1:
            layout = {0.50f, -0.04f, scale};
            break;
        case 2:
            layout = {0.50f, 1.012f, scale};
            break;
        case 3:
            layout = {1.04f, 0.50f, scale};
            break;
        default:
            break;
    }
}

void player_weapon_settings_rows(config::visual_profile::player::weapon& weapon)
{
    int display = static_cast<int>(weapon.display);
    static constexpr const char* displays[]{"Text", "Icon", "Text + Icon"};
    select_row("Display", display, displays);
    weapon.display = static_cast<config::visual_profile::player::weapon::display_type>(display);
    color_row("Text Color", weapon.text_color);
    color_row("Icon Color", weapon.icon_color);
    toggle_popup_row("Ammo Indicator", weapon.ammo.enabled, 2,
                     [&]
                     {
                         toggle_row("Exact Ammo Count", weapon.ammo.show_count);
                         color_row("Empty Color", weapon.ammo.empty_color);
                     });
}

void player_info_flag_settings_rows(config::visual_profile::player::info_flags& flags)
{
    using flag = config::visual_profile::player::info_flags::flag;
    static constexpr std::pair<const char*, int> options[]{
        {"Money", flag::money},   {"Armor", flag::armor},       {"Defuse Kit", flag::kit},
        {"Scoped", flag::scoped}, {"Defusing", flag::defusing}, {"Flashed", flag::flashed},
        {"Ping", flag::ping},     {"Distance", flag::distance}, {"Bomb Damage", flag::bomb_damage}};
    int mask = flags.flags;
    multiselect_row("Active Flags", mask, options, (1 << 9) - 1);
    flags.flags = static_cast<std::uint16_t>(mask);

    // Which flag's style is being edited is view state; ImGui keeps it per widget id.
    int& selected = *ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##info_flag_edit"), 0);
    static constexpr const char* names[]{"Money",   "Armor", "Defuse Kit", "Scoped",     "Defusing",
                                         "Flashed", "Ping",  "Distance",   "Bomb Damage"};
    select_row("Edit Flag", selected, names);
    auto& style = selected_info_flag_style(flags, selected);
    color_row("Color", style.color);
    slider_row("Scale", style.scale, 0.55f, 2.0f, "x", 0.05f);
}

void chams_material_rows(config::visual_profile::chams::material& m)
{
    select_row("Material", m.type, k_chams_materials);
    color_row("Color", m.color);
    toggle_row("Wireframe", m.wireframe);

    // Only the parameters the selected material actually reads are shown;
    // the rest keep their values so switching type back restores the tuning.
    switch (m.type)
    {
        case config::visual_profile::chams::shaded:
            slider_row("Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f);
            slider_row("Metalness", m.metalness, 0.0f, 1.0f, "", 0.01f);
            break;

        case config::visual_profile::chams::glow:
            slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
            break;

        case config::visual_profile::chams::glow_outline:
            slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
            slider_row("Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f);
            slider_row("Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f);
            break;

        case config::visual_profile::chams::iridescent:
            slider_row("Strength", m.strength, 0.0f, 1.0f, "", 0.01f);
            slider_row("Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f);
            break;

        case config::visual_profile::chams::water_flow:
            slider_row("Speed", m.speed, 0.0f, 4.0f, "", 0.05f);
            break;

        case config::visual_profile::chams::glossy:
            slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
            slider_row("Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f);
            slider_row("Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f);
            color_row("Tint", m.tint);
            break;

        default:
            break;
    }
}
} // namespace ui::menu::detail

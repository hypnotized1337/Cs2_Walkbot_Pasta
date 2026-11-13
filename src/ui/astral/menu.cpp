#include <stdafx.hpp>

// Presentation adapted from Astral-Remake by kdt255; see third_party/astral/LICENSE.
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>
#include <ui/overlay/overlay.hpp>
#include <ui/astral/icons.hpp>
#include <ui/astral/widgets.hpp>
#include <render/fonts.hpp>
#include <render/presentation.hpp>
#include <render/text/localization.hpp>

namespace ui
{
using namespace ui::menu::detail;

namespace
{
constexpr float k_header_height = 50.0f;
constexpr float k_footer_height = 26.0f;

void header_decoration(ImDrawList& draw, const ImVec2 origin, const ImVec2 size)
{
    const auto time = static_cast<float>(ImGui::GetTime());
    draw.PushClipRect(origin, origin + size, true);
    for (int ribbon = 0; ribbon < 2; ++ribbon)
    {
        for (int i = 0; i <= 72; ++i)
        {
            const auto x = size.x * static_cast<float>(i) / 72.0f;
            const auto y = size.y * 0.5f + std::sin(x / size.x * 6.28f + time * 0.42f + ribbon * 2.89f) * 13.5f;
            draw.PathLineTo(origin + ImVec2{x, y});
        }
        draw.PathStroke(IM_COL32(255, 255, 255, 22), ImDrawFlags_None, 1.0f);
    }
    for (int i = 0; i < 20; ++i)
    {
        const auto x = 170.0f + std::fmod(i * 37.3f + time * (5.0f + i % 5), 235.0f);
        const auto y = 9.0f + std::fmod(i * 17.7f, 32.0f);
        const auto alpha = static_cast<int>((0.5f + 0.25f * std::sin(time * 1.7f + i)) * 160.0f);
        const auto p = origin + ImVec2{x, y};
        draw.AddLine(p - ImVec2{2.0f, 0.0f}, p + ImVec2{2.0f, 0.0f}, IM_COL32(255, 255, 255, alpha));
        draw.AddLine(p - ImVec2{0.0f, 2.0f}, p + ImVec2{0.0f, 2.0f}, IM_COL32(255, 255, 255, alpha));
    }
    draw.PopClipRect();
}
} // namespace

void menu_t::draw_astral(float display_width, float display_height)
{
    astral::widgets::begin_frame();
    ImGui::SetNextWindowPos(initial_menu_layout_position(display_width, display_height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({k_menu_width, k_menu_surface_height}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1, 1, 1, 1});
    ImGui::Begin("##vesta_astral_menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    const auto min = ImGui::GetWindowPos();
    const auto max = min + ImVec2{k_menu_width, k_menu_surface_height};
    g_menu_min = min;
    g_menu_max = max;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, packed(k_bg_base), 6.0f);
    const auto accent = k_accent;
    const auto saved_style = ImGui::GetStyle();
    astral::widgets::set_accent(accent, accent);
    ImGui::GetStyle() = saved_style;
    const auto dark_accent = ImVec4{accent.x * 0.62f, accent.y * 0.62f, accent.z * 0.62f, 1};
    draw->AddRectFilled(min, {max.x, min.y + k_header_height}, packed(accent), 6.0f, ImDrawFlags_RoundCornersTop);
    const auto vertex_start = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, {max.x, min.y + k_header_height}, IM_COL32_WHITE, 6.0f, ImDrawFlags_RoundCornersTop);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, vertex_start, draw->VtxBuffer.Size,
                                                 min, {max.x, min.y}, packed(accent), packed(dark_accent));
    header_decoration(*draw, min, {k_menu_width, k_header_height});

    ImGui::PushFont(astral::widgets::get().fonts.bold, 17.5f);
    draw->AddText(min + ImVec2{17, 17}, IM_COL32_WHITE, "PASTA(retard)");
    ImGui::SetCursorPos({0, 0});
    ImGui::InvisibleButton("##header_drag", {420, k_header_height});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        ImGui::SetWindowPos(min + ImGui::GetIO().MouseDelta);

    static constexpr const char* labels[]{"Aimbot", "Triggerbot", "Visuals", "Walk Bot", "Automation", "Misc", "Configs"};
    static constexpr astral::icon_id icons[]{astral::icon_id::mouse, astral::icon_id::crosshair, astral::icon_id::eye,
        astral::icon_id::person, astral::icon_id::lightning, astral::icon_id::gear, astral::icon_id::list};
    for (int i = 0; i < 7; ++i)
    {
        ImGui::PushID(i);
        const ImVec2 center{min.x + 446.0f + i * 52.0f, min.y + 25.0f};
        ImGui::SetCursorScreenPos(center - ImVec2{22, 20});
        if (ImGui::InvisibleButton(labels[i], {44, 40}) && m_page != i)
        {
            m_page = i;
            reset_content_animation();
        }
        const auto hover = ImGui::IsItemHovered();
        auto* storage = ImGui::GetStateStorage();
        const auto id = ImGui::GetID("selected");
        const auto target = m_page == i ? 1.0f : hover ? 0.65f : 0.3f;
        const auto blend = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 14.0f);
        const auto selected = storage->GetFloat(id, target) + (target - storage->GetFloat(id, target)) * blend;
        storage->SetFloat(id, selected);
        astral::widgets::get().icons.draw(draw, icons[i], center, IM_COL32(255, 255, 255, static_cast<int>(255 * selected)));
        if (m_page == i)
            draw->AddLine(center + ImVec2{-10, 20}, center + ImVec2{10, 20}, IM_COL32_WHITE, 2.0f);
        if (hover)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", render::localization::tr(labels[i]));
        }
        ImGui::PopID();
    }
    ImGui::PopFont();

    ImGui::SetCursorPos({0, k_header_height});
    ImGui::BeginChild("##astral_content", {k_menu_width, k_menu_surface_height - k_header_height - k_footer_height},
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushID(m_page);
    if (m_page == 4)
        draw_astral_automation();
    else
    {
        // Layout stays in place while shared helpers use the Astral presentation.
        push_menu_font(render::fonts().menu_source_18);
        switch (m_page)
        {
        case 0: draw_aimbot(); break;
        case 1: draw_triggerbot(); break;
        case 2: draw_visuals(); break;
        case 3: draw_walk_bot(); break;
        case 5: draw_misc(); break;
        case 6: draw_configs(); break;
        }
        ImGui::PopFont();
    }
    ImGui::PopID();
    ImGui::EndChild();

    draw->AddLine({min.x + 12, max.y - k_footer_height}, {max.x - 12, max.y - k_footer_height}, IM_COL32(40, 40, 40, 255));
    push_menu_font(render::fonts().menu_regular_12);
    draw->AddText({min.x + 13, max.y - 20}, IM_COL32(157, 157, 157, 255), "vesta");
    const auto* page_name = render::localization::tr(labels[m_page]);
    draw->AddText({max.x - 13 - ImGui::CalcTextSize(page_name).x, max.y - 20}, IM_COL32(157, 157, 157, 255), page_name);
    ImGui::PopFont();
    ImGui::SetCursorPos({0, 0});
    ImGui::Dummy({0, 0});
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}
} // namespace ui

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
constexpr float k_pasta_pi = 3.14159265358979323846f;

[[nodiscard]] ImVec2 pasta_point(const ImVec2 center, const float scale, const float cosine, const float sine,
                                const float x, const float y)
{
    return {center.x + (x * cosine - y * sine) * scale, center.y + (x * sine + y * cosine) * scale};
}

// Small silhouettes, all built around the particle center so the field can rotate and drift them without separate
// state: farfalle, penne rigate, conchiglie and rotelle, the four pasta types from the reference sheet that keep
// their silhouette at header size. At this size a shape only has a handful of pixels, so every outline stays convex
// and every stroke stays thick: thin wiggles read as noise instead of pasta.
void pasta_bowtie(ImDrawList& draw, const ImVec2 center, const float scale, const float angle, const ImU32 color)
{
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto point = [&](const float x, const float y) { return pasta_point(center, scale, cosine, sine, x, y); };
    // Farfalle: two triangular wings that overlap at the centre line, so the pinch stays solid without a hub that
    // would turn the whole silhouette into an asterisk.
    const ImVec2 left[]{point(-1.0f, -0.64f), point(0.04f, -0.22f), point(0.04f, 0.22f), point(-1.0f, 0.64f)};
    const ImVec2 right[]{point(1.0f, -0.64f), point(1.0f, 0.64f), point(-0.04f, 0.22f), point(-0.04f, -0.22f)};
    draw.AddConvexPolyFilled(left, 4, color);
    draw.AddConvexPolyFilled(right, 4, color);
}

void pasta_penne(ImDrawList& draw, const ImVec2 center, const float scale, const float angle, const ImU32 color)
{
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto point = [&](const float x, const float y) { return pasta_point(center, scale, cosine, sine, x, y); };
    // Penne rigate: the diagonal-cut tube plus two ridges scored along its length, the detail that keeps it from
    // reading as a plain bar at this size.
    const ImVec2 body[]{point(-0.95f, -0.40f), point(0.68f, -0.40f), point(0.95f, 0.40f), point(-0.68f, 0.40f)};
    draw.AddConvexPolyFilled(body, 4, color);
    constexpr float k_ridges[]{-0.14f, 0.14f};
    for (const auto offset : k_ridges)
        draw.AddLine(point(-0.72f, offset), point(0.72f, offset), IM_COL32(0, 0, 0, 42), 0.8f);
}

void pasta_shell(ImDrawList& draw, const ImVec2 center, const float scale, const float angle, const ImU32 color)
{
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto point = [&](const float x, const float y) { return pasta_point(center, scale, cosine, sine, x, y); };
    // Conchiglie: a fan body, three bumps along the outer edge for the scallops and three scored ribs. The bumps are
    // separate circles so the outline stays convex, which is what ImDrawList fills reliably at this size.
    constexpr int k_segments = 16;
    ImVec2 fan[k_segments + 2];
    fan[0] = point(0.0f, 0.58f);
    for (int i = 0; i <= k_segments; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(k_segments);
        const auto arc = 1.08f * k_pasta_pi + t * 0.84f * k_pasta_pi;
        fan[i + 1] = point(std::cos(arc) * 0.86f, std::sin(arc) * 0.86f);
    }
    draw.AddConvexPolyFilled(fan, k_segments + 2, color);
    constexpr float k_scallops[]{1.19f, 1.50f, 1.81f};
    for (const auto scallop : k_scallops)
    {
        const auto arc = scallop * k_pasta_pi;
        draw.AddCircleFilled(point(std::cos(arc) * 0.86f, std::sin(arc) * 0.86f), scale * 0.12f, color);
    }
    constexpr float k_ribs[]{1.22f, 1.50f, 1.78f};
    for (const auto rib : k_ribs)
    {
        const auto arc = rib * k_pasta_pi;
        draw.AddLine(fan[0], point(std::cos(arc) * 0.72f, std::sin(arc) * 0.72f), IM_COL32(0, 0, 0, 38), 0.9f);
    }
}

void pasta_rotelle(ImDrawList& draw, const ImVec2 center, const float scale, const float angle, const ImU32 color)
{
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    const auto point = [&](const float x, const float y) { return pasta_point(center, scale, cosine, sine, x, y); };
    // Rotelle: a rim, a hub and four spokes. The spokes are what keep it from reading as a plain letter O.
    draw.AddCircle(center, scale * 0.80f, color, 20, scale * 0.24f);
    draw.AddCircleFilled(center, scale * 0.15f, color);
    for (int i = 0; i < 4; ++i)
    {
        const auto arc = static_cast<float>(i) * 0.5f * k_pasta_pi;
        draw.AddLine(point(std::cos(arc) * 0.15f, std::sin(arc) * 0.15f),
                     point(std::cos(arc) * 0.68f, std::sin(arc) * 0.68f), color, scale * 0.17f);
    }
}

void pasta_particle(ImDrawList& draw, const int index, const ImVec2 center, const float scale, const float angle,
                    const ImU32 color)
{
    switch (index % 4)
    {
    case 0: pasta_bowtie(draw, center, scale, angle, color); break;
    case 1: pasta_penne(draw, center, scale, angle, color); break;
    case 2: pasta_shell(draw, center, scale, angle, color); break;
    default: pasta_rotelle(draw, center, scale, angle, color); break;
    }
}

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
    // The field occupies the clear space between the wordmark and the section icons. Drift, bob and turn are all
    // functions of ImGui time, so opening the menu always resumes the same deterministic animation.
    constexpr int k_pasta_count = 8;
    constexpr float k_field_left = 160.0f;
    constexpr float k_field_width = 236.0f;
    for (int i = 0; i < k_pasta_count; ++i)
    {
        // Evenly spaced lanes that sway around their slot: the field drifts as one band, so the shapes never bunch
        // into a clump the way independent speeds do after a few minutes of menu time.
        const auto lane = static_cast<float>(i) / static_cast<float>(k_pasta_count) * k_field_width;
        const auto drift = std::fmod(lane + time * 3.4f + std::sin(time * 0.5f + static_cast<float>(i) * 1.7f) * 3.0f,
                                     k_field_width);
        const auto x = k_field_left + drift;
        const auto y = 12.0f + std::fmod(static_cast<float>(i) * 11.3f, 26.0f) +
                       std::sin(time * 0.7f + static_cast<float>(i) * 1.9f) * 1.3f;
        // A shallow shared tilt keeps every silhouette readable as pasta; arbitrary spins turned them into glyphs.
        const auto scale = 5.8f + static_cast<float>(i % 3) * 0.45f;
        const auto angle = -0.28f + static_cast<float>(i % 4) * 0.16f;
        const auto edge = std::clamp(std::min(drift, k_field_width - drift) / 10.0f, 0.0f, 1.0f);
        // Steady alpha: a twinkle made half the field look faded and dirty on the light accent bar.
        const auto alpha = static_cast<int>(236.0f * edge);
        pasta_particle(draw, i, origin + ImVec2{x, y}, scale, angle, IM_COL32(255, 252, 244, alpha));
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
    draw->AddText({min.x + 13, max.y - 20}, IM_COL32(157, 157, 157, 255), "pasta");
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

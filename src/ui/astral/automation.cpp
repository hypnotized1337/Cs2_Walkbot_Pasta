#include <stdafx.hpp>
#include <config/settings.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/menu.hpp>
#include <ui/astral/widgets.hpp>
#include <ui/astral/draw.hpp>

namespace ui
{
void menu_t::draw_astral_automation()
{
    namespace widgets = astral::widgets;
    auto& settings = config::general_settings;
    auto& walk = settings.m_walk_bot;
    auto& context = widgets::get();
    const auto saved_style = ImGui::GetStyle();
    widgets::init_style();
    const auto tr = [](const char* key) { return render::localization::tr(key); };

    m_content_animation = std::min(1.0f, m_content_animation + std::min(ImGui::GetIO().DeltaTime, 1.0f / 15.0f) / 0.24f);
    const auto transition = widgets::ease(m_content_animation);
    const auto origin = ImGui::GetWindowPos() + ImVec2{14, 12 + (1.0f - transition) * 8.0f};
    const auto width = ImGui::GetWindowSize().x - 28.0f;
    const auto card_max = origin + ImVec2{width, 42};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, saved_style.Alpha * transition);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, card_max, astral::draw::col(context.pal.card), 6.0f);
    astral::draw::text_cap_centered(draw, context.fonts.bold, 18.0f, origin.x + 14.0f, origin.y + 21.0f,
                                  astral::draw::col(context.pal.text), tr("Automation"));

    const ImRect area{origin + ImVec2{0, 54}, ImVec2{card_max.x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 14}};
    widgets::begin_columns("automation", area, 2);
    widgets::begin_panel(tr("MATCH AUTOMATION"));
    widgets::checkbox(tr("Auto Accept Match"), &settings.auto_accept);
    widgets::checkbox(tr("Auto Rejoin"), &walk.auto_rejoin);
    if (widgets::begin_settings("auto_rejoin", widgets::settings_icon::gear))
    {
        const char* modes[]{tr("Deathmatch"), tr("Casual"), tr("Competitive"), tr("Wingman"), tr("Premier")};
        widgets::combo(tr("Game Mode"), &walk.game_mode, modes, 5);
        walk.game_mode = std::clamp(walk.game_mode, 0, 4);
        const char* teams[]{tr("Terrorists"), tr("Counter-Terrorists")};
        widgets::combo(tr("Team"), &walk.team, teams, 2);
        walk.team = std::clamp(walk.team, 0, 1);
        widgets::end_settings();
    }
    widgets::end_panel();
    widgets::next_column();
    widgets::begin_panel(tr("Auto Disconnect"));
    widgets::checkbox(tr("Auto Disconnect"), &walk.auto_disconnect);
    if (widgets::begin_settings("auto_disconnect", widgets::settings_icon::gear))
    {
        widgets::slider_int(tr("Score"), &walk.disconnect_score, 0, 2000);
        widgets::slider_int(tr("Minutes On Server"), &walk.disconnect_minutes, 0, 60, "%d min");
        widgets::slider_int(tr("Random Extra Time"), &walk.disconnect_spread, 0, 100, "%d%%");
        widgets::end_settings();
    }
    widgets::end_panel();
    widgets::end_columns();
    ImGui::PopStyleVar();
    ImGui::GetStyle() = saved_style;
}
} // namespace ui

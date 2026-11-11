#include <stdafx.hpp>
#include <config/settings.hpp>
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>
#include <imgui.h>

namespace ui
{
using namespace ui::menu::detail;

void menu_t::draw_automation()
{
    auto& p = config::general_settings;
    auto& walk = p.m_walk_bot;

    ImGui::SetCursorPos({14.0f, 12.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##automation_cards", {772.0f, 578.0f}, false);
    begin_cards("##automation_grid");

    card_in_column(
        "automation_match", "MATCH AUTOMATION", 3, 0,
        [&]
        {
            toggle_row("Auto Accept Match", p.auto_accept);
            toggle_popup_row("Auto Rejoin", walk.auto_rejoin, 2,
                             [&]
                             {
                                 static constexpr const char* game_modes[]{
                                     "Deathmatch", "Casual", "Competitive", "Wingman", "Premier"};
                                 select_row("Game Mode", walk.game_mode, game_modes);
                                 walk.game_mode = std::clamp(walk.game_mode, 0, 4);
                                 static constexpr const char* teams[]{"Terrorists", "Counter-Terrorists"};
                                 select_row("Team", walk.team, teams);
                                 walk.team = std::clamp(walk.team, 0, 1);
                             });
            toggle_popup_row("Auto Disconnect", walk.auto_disconnect, 3,
                             [&]
                             {
                                 slider_row("Score", walk.disconnect_score, 0, 2000);
                                 slider_row("Minutes On Server", walk.disconnect_minutes, 0, 60, " min");
                                 slider_row("Random Extra Time", walk.disconnect_spread, 0, 100, "%");
                             });
        });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui

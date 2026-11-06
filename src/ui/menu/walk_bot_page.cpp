#include <stdafx.hpp>
#include <platform/windows/storage.hpp>
#include <platform/windows/open_folder.hpp>
#include <config/settings.hpp>
#include <ui/menu/menu.hpp>
#include <imgui.h>
#include <ui/menu/internal.hpp>

namespace ui
{
using namespace ui::menu::detail;

void menu_t::draw_walk_bot()
{
    auto& walk = config::general_settings.m_walk_bot;
    ImGui::SetCursorPos({14.0f, 24.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##walk_bot_cards", {682.0f, 612.0f}, false);
    begin_cards("##walk_bot_grid");

    card_in_column("walk_bot", "WALK BOT", 3, 0,
                   [&]
                   {
                       toggle_row("Enable Walk Bot", walk.enabled);
                       static constexpr const char* sources[]{"Random Points", "Recorded Routes"};
                       select_row("Routes", walk.mode, sources);
                       walk.mode = std::clamp(walk.mode, 0, config::general_profile::walk_bot::mode_count - 1);
                       settings_popup_row("Behaviour", 3,
                                          [&]
                                          {
                                              toggle_row("Hunt Enemies", walk.hunt);
                                              toggle_row("Turn Camera", walk.look);
                                              toggle_row("Break Obstacles", walk.breakables);
                                          });
                   });
    // Spray lives with the walk bot's other combat switches: one toggle here, its burst tuning in the row's popup.
    const auto combat_rows = walk.autofire_spray ? 6 : 5;
    card_in_column("walk_combat", "COMBAT", combat_rows, 0,
                   [&]
                   {
                       toggle_popup_row("Own Aim", walk.aim, 6,
                                        [&]
                                        {
                                            slider_row("Hand Speed", walk.aim_speed, 0.4f, 2.5f, "x", 0.1f);
                                            slider_row("Smoothing", walk.aim_smoothing, 0, 100, "%");
                                            slider_row("Jitter", walk.aim_jitter, 0.0f, 1.5f, "°", 0.05f);
                                            slider_row("Reaction Time (ms)", walk.aim_reaction_ms, 0, 500);
                                            toggle_row("Body If Lethal", walk.aim_body_lethal);
                                            aim_parts_row(walk.aim_hitboxes);
                                        });
                       toggle_row("Autofire (Triggerbot Settings)", walk.autofire);
                       toggle_popup_row("Spray Mode (Beta)", walk.autofire_spray, walk.spray_burst ? 2 : 1,
                                        [&]
                                        {
                                            toggle_row("Burst Cooldown", walk.spray_burst);
                                            if (walk.spray_burst)
                                                slider_row("Cooldown (ms)", walk.spray_burst_cooldown_ms, 100, 1500);
                                        });
                       if (walk.autofire_spray)
                           slider_row("Burst Bullet Count", walk.spray_burst_count, 3, 30);
                       toggle_popup_row(
                           "Rear Defense", walk.rear_defense, 5,
                           [&]
                           {
                               slider_row("Distance", walk.rear_defense_distance, 100.0f, 1500.0f, " u", 25.0f);
                               slider_row("Rear FOV", walk.rear_defense_fov, 40.0f, 180.0f, "°", 5.0f);
                               slider_row("Smoothing", walk.rear_defense_smoothing, 0, 100, "%");
                               aim_parts_row(walk.rear_defense_hitboxes);
                               static constexpr const char* weapons[]{"Primary", "Knife"};
                               select_row("Weapon", walk.rear_defense_weapon, weapons);
                           });
                       toggle_row("React To Hits", walk.react_to_hits);
                   });
    // Recording: press the key, walk the route yourself (jumps and crouches are kept), press it again. Routes are
    // kept per map and side and played by "Recorded Routes".
    card_in_column(
        "walk_display", "ROUTE DISPLAY", 3, 1,
        [&]
        {
            toggle_popup_row("Show Route", walk.show_path, 2,
                             [&]
                             {
                                 color_row("Route Color", walk.path_color);
                                 slider_row("Route Thickness", walk.path_thickness, 0.5f, 6.0f, "px", 0.5f);
                             });
            toggle_popup_row("Show Nav Mesh", walk.show_mesh, 2,
                             [&]
                             {
                                 color_row("Mesh Color", walk.mesh_color);
                                 slider_row("Mesh Distance", walk.mesh_distance, 200.0f, 4000.0f, "u", 50.0f);
                             });
            // Recording: press the key, walk the route yourself (jumps and crouches are kept), press it
            // again. Routes are kept per map and side and played by "Recorded Routes"; the switch draws them.
            toggle_popup_row("Recorded Routes", walk.show_recordings, 2,
                             [&]
                             {
                                 keybind_row("Record Route Key", walk.record_key, true);
                                 if (button_row("Folder", "Open", row_action_icon::folder))
                                     (void)platform::windows::open_folder(
                                         platform::windows::runtime_storage::paths().root / L"walkbot");
                             });
        });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui

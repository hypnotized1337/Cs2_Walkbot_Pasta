#include <stdafx.hpp>
#include <platform/windows/storage.hpp>
#include <platform/windows/open_folder.hpp>
#include <config/settings.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/menu.hpp>
#include <imgui.h>
#include <ui/menu/internal.hpp>
#include <ui/menu/combat_widgets.hpp>
#include <format>

namespace ui
{
using namespace ui::menu::detail;

void menu_t::draw_walk_bot()
{
    auto& p = config::general_settings;
    auto& walk = p.m_walk_bot;
    using walk_settings = config::general_profile::walk_bot;

    // The buy preset editor moved here with Auto Buy. Keep its existing clamps and stored layout so old configs load
    // unchanged.
    walk.health_shot_health = std::clamp(walk.health_shot_health, 1, 99);
    walk.health_shot_damage_ms = std::clamp(walk.health_shot_damage_ms, 0, 15000);
    walk.health_shot_near_distance = std::clamp(walk.health_shot_near_distance, 0, 2000);
    walk.buy_edit = std::clamp(walk.buy_edit, 0, static_cast<int>(walk_settings::buy_mode_count) - 1);
    auto& preset = walk.buy_presets[static_cast<std::size_t>(walk.buy_edit)];
    preset.count = std::clamp(preset.count, 0, walk_settings::k_buy_items);
    const bool preset_editable = walk.buy_edit == walk_settings::buy_default || preset.custom;
    const int buy_rows = 1 + (walk.buy_edit != walk_settings::buy_default ? 1 : 0) +
                         (preset_editable ? 1 + preset.count + (preset.count < walk_settings::k_buy_items ? 1 : 0) : 0);

    ImGui::SetCursorPos({14.0f, 12.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##walk_bot_cards", {772.0f, 578.0f}, false);
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
    // Spray lives with the walk bot's other combat switches: one toggle here, no burst tuning to configure.
    const auto combat_rows = 5;
    card_in_column("walk_combat", "COMBAT", combat_rows, 0,
                   [&]
                   {
                       toggle_popup_row("Own Aim", walk.aim, 7,
                                        [&]
                                        {
                                            slider_row("Hand Speed", walk.aim_speed, 0.4f, 2.5f, "x", 0.1f);
                                            slider_row("Smoothing", walk.aim_smoothing, 0, 100, "%");
                                            slider_row("Jitter", walk.aim_jitter, 0.0f, 1.5f, "°", 0.05f);
                                            slider_row("Reaction Time (ms)", walk.aim_reaction_ms, 0, 500);
                                            toggle_row("Body If Lethal", walk.aim_body_lethal);
                                            aim_parts_row(walk.aim_hitboxes);
                                            ui::menu::combat::multipoint_row(walk.aim_multipoint,
                                                                             walk.aim_multipoint_config);
                                        });
                       toggle_row("Autofire (Triggerbot Settings)", walk.autofire);
                       toggle_popup_row("Spray Mode (Beta)", walk.autofire_spray, 1,
                                        [&] { toggle_row("Spray Log (CSV)", walk.spray_log); });
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

    // These controls used to live on Automations. They stay available only while the Walk Bot itself is enabled; the
    // stored values are left untouched, so turning the Walk Bot back on restores the previous switches.
    card_in_column(
        "walk_automation", "WALK BOT AUTOMATION", 7, 1,
        [&]
        {
            const auto automation_locked = !walk.enabled;
            const auto controls_min = ImGui::GetCursorScreenPos();
            if (automation_locked)
            {
                ImGui::BeginDisabled();
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.55f);
            }
            {
                const widget_alpha_scope alpha(automation_locked ? 0.55f : 1.0f);
                toggle_row("Auto Crouch", p.auto_crouch);
                toggle_row("Auto Scope", walk.auto_scope);
                toggle_popup_row("Auto Bonus Weapon", p.m_auto_bonus.enabled, 2,
                                 [&]
                                 {
                                     keybind_row("Pickup Key", p.m_auto_bonus.key, true, false);
                                     slider_row("Interval", p.m_auto_bonus.interval_ms, 250, 5000, " ms");
                                 });
                toggle_popup_row("Auto Knife", walk.auto_knife, 4,
                                 [&]
                                 {
                                     slider_row("Distance", walk.auto_knife_distance, 80.0f, 600.0f, " u", 10.0f);
                                     slider_row("Respawn Grace (ms)", walk.auto_knife_respawn_grace_ms, 0, 5000);
                                     toggle_row("Knife Only When Target Is AFK", walk.auto_knife_afk_only);
                                     toggle_row("Show Eligibility Debug", walk.auto_knife_debug);
                                 });
                toggle_popup_row("Auto Health Shot", walk.auto_health_shot, 5,
                                 [&]
                                 {
                                     slider_row("Health Below", walk.health_shot_health, 1, 99);
                                     slider_row("No Damage For", walk.health_shot_damage_ms, 0, 15000, " ms");
                                     slider_row("No Enemy Within", walk.health_shot_near_distance, 0, 2000, " u");
                                     keybind_row("Use Bind", walk.health_shot_key, true);
                                     toggle_row("Show Debug", walk.health_shot_debug);
                                 });
                toggle_row("Auto Reload", walk.auto_reload);
                toggle_popup_row(
                    "Auto Buy", walk.auto_buy, buy_rows,
                    [&]
                    {
                        static constexpr const char* presets[]{"Default", "Premier", "Competitive",
                                                               "Wingman", "Casual",  "Deathmatch"};
                        select_row("Preset", walk.buy_edit, presets);
                        if (walk.buy_edit != walk_settings::buy_default)
                            toggle_row("Own Preset", preset.custom);
                        if (!preset_editable)
                            return;
                        toggle_row("One Random Item", preset.random_one);
                        int remove = -1;
                        for (int i = 0; i < preset.count; ++i)
                        {
                            auto& item = preset.items[static_cast<std::size_t>(i)];
                            static constexpr const char* side_names[]{"", " T", " CT"};
                            const auto label =
                                std::format("{} {}{}{}", render::localization::tr("Item"),
                                            item.code[0] ? item.code.data() : "?",
                                            side_names[std::clamp(item.side, 0, 2)],
                                            item.money_rule == 1   ? std::format(" < ${}", item.money)
                                            : item.money_rule == 2 ? std::format(" >= ${}", item.money)
                                                                   : std::string{});
                            const auto row = std::format("{}###buy_item_{}", label, i);
                            settings_popup_row(
                                row.c_str(), 6,
                                [&]
                                {
                                    toggle_row("Enabled", item.enabled);
                                    text_input_row("Buy Code", item.code.data(), item.code.size());
                                    static constexpr const char* sides[]{"Both Sides", "T", "CT"};
                                    select_row("Side", item.side, sides);
                                    static constexpr const char* rules[]{"Any Money", "Less Than", "At Least"};
                                    select_row("Money", item.money_rule, rules);
                                    slider_row("Amount", item.money, 0, 16000, "$");
                                    if (button_row("Remove Item", "Remove", row_action_icon::none))
                                        remove = i;
                                });
                        }
                        if (remove >= 0)
                        {
                            for (int i = remove; i + 1 < preset.count; ++i)
                                preset.items[static_cast<std::size_t>(i)] =
                                    preset.items[static_cast<std::size_t>(i + 1)];
                            preset.items[static_cast<std::size_t>(--preset.count)] = {};
                        }
                        if (preset.count < walk_settings::k_buy_items &&
                            button_row("Add Item", "Add", row_action_icon::none))
                        {
                            preset.items[static_cast<std::size_t>(preset.count)] = {true, {}, 0, 0, 2000};
                            ++preset.count;
                        }
                    });
            }
            if (automation_locked)
            {
                const auto controls_max = ImGui::GetCursorScreenPos();
                ImGui::PopStyleVar();
                ImGui::EndDisabled();
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                    ImGui::IsMouseHoveringRect(controls_min, controls_max))
                {
                    ImGui::SetTooltip("%s", render::localization::tr("Enable Walkbot to use these features"));
                }
            }
        });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui

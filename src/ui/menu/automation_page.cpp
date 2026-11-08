#include <stdafx.hpp>
#include <config/settings.hpp>
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>
#include <imgui.h>
#include <format>

namespace ui
{
using namespace ui::menu::detail;

void menu_t::draw_automation()
{
    auto& p = config::general_settings;
    auto& walk = p.m_walk_bot;

    // Auto Buy preset editor state, kept here with the other automation controls.
    using walk_settings = config::general_profile::walk_bot;
    walk.health_shot_health = std::clamp(walk.health_shot_health, 1, 99);
    walk.health_shot_damage_ms = std::clamp(walk.health_shot_damage_ms, 0, 15000);
    walk.health_shot_near_distance = std::clamp(walk.health_shot_near_distance, 0, 2000);
    walk.buy_edit = std::clamp(walk.buy_edit, 0, static_cast<int>(walk_settings::buy_mode_count) - 1);
    auto& preset = walk.buy_presets[static_cast<std::size_t>(walk.buy_edit)];
    preset.count = std::clamp(preset.count, 0, walk_settings::k_buy_items);
    const bool preset_editable = walk.buy_edit == walk_settings::buy_default || preset.custom;
    const int buy_rows = 1 + (walk.buy_edit != walk_settings::buy_default ? 1 : 0) +
                         (preset_editable ? 1 + preset.count + (preset.count < walk_settings::k_buy_items ? 1 : 0) : 0);

    ImGui::SetCursorPos({14.0f, 24.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##automation_cards", {682.0f, 612.0f}, false);
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

    card_in_column(
        "automation_buy", "BUY AUTOMATION", 1, 0,
        [&]
        {
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
                            preset.items[static_cast<std::size_t>(i)] = preset.items[static_cast<std::size_t>(i + 1)];
                        preset.items[static_cast<std::size_t>(--preset.count)] = {};
                    }
                    if (preset.count < walk_settings::k_buy_items &&
                        button_row("Add Item", "Add", row_action_icon::none))
                    {
                        preset.items[static_cast<std::size_t>(preset.count)] = {true, {}, 0, 0, 2000};
                        ++preset.count;
                    }
                });
        });

    card_in_column(
        "automation_combat", "COMBAT AUTOMATION", 6, 1,
        [&]
        {
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
        });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui

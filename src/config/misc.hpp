#pragma once

#include <windows.h>

#include <cstdint>
#include <array>

#include <foundation/color.hpp>
#include <config/combat.hpp>
#include <config/game_controls.hpp>

namespace config
{

struct general_profile
{
    enum class screen_anchor : std::uint8_t
    {
        top_left,
        top_right,
        bottom_left,
        bottom_right
    };

    struct screen_layout
    {
        constexpr screen_layout(const float x = 40.0f, const float y = 40.0f)
            : offset_x(x)
            , offset_y(y)
            , legacy_position_x(x)
            , legacy_position_y(y)
        {
        }

        screen_anchor anchor{screen_anchor::top_left};
        float offset_x{40.0f};
        float offset_y{40.0f};
        float scale{1.0f};
        int version{1};

        // Read only while migrating configs that stored absolute position_x/y.
        float legacy_position_x{40.0f};
        float legacy_position_y{40.0f};
    };

    struct bullet_tracers
    {
        bool enabled{false};
        foundation::rgba color{192, 203, 229, 200};
        float duration{2.0f};
        float thickness{1.5f};
        bool bloom{true};
        int max_count{40};
        bool draw_cubes{true};
        bool draw_line{true};
        float cube_half{1.25f};
        foundation::rgba cube_edge_color{192, 203, 229, 230};
        float cube_face_alpha{38.0f};
        float fade_near{45.0f};
        float fade_far{160.0f};
    } m_bullet_tracers{};

    struct hitmarker
    {
        bool enabled{true};
        foundation::rgba color{245, 247, 250, 255};
        float size{8.0f};
        float gap{3.0f};
        float thickness{1.5f};
        float duration{0.55f};
    } m_hitmarker{};

    struct hitsound
    {
        bool enabled{true};
        int style{4}; // 0 soft, 1 glass, 2 pluck, 3 crisp, 4 flesh
        float volume{0.55f};
        bool show_damage{true};
        foundation::rgba damage_color{255, 108, 112, 255};
        float damage_size{13.0f};
        float damage_duration{0.75f};
        float damage_rise{30.0f};
    } m_hitsound{};

    struct watermark
    {
        bool enabled{true};
        screen_layout layout{40.0f, 40.0f};
        bool vertical{};
        bool show_ping{true};
        bool show_loss{true};
        bool show_cpu{true};
        bool show_fps{true};
    } m_watermark{};

    // Movable panel (like the watermark) listing the players currently
    // spectating the local player, with their observer mode.
    struct spectator_list
    {
        bool enabled{false};
        bool show_avatars{true};
        screen_layout layout{40.0f, 320.0f};
    } m_spectator_list{};

    struct event_log
    {
        bool enabled{false};
        screen_layout layout{40.0f, 410.0f};
        float duration{5.0f};
        int max_entries{5};
        bool show_shots{true};
        bool show_hits{true};
        bool show_kills{true};
        bool show_misses{true};
        bool show_blocked{true};
        bool show_info{true};
    } m_event_log{};

    struct keybind_list
    {
        bool enabled{false};
        screen_layout layout{40.0f, 260.0f};
        bool show_always{true};
        bool show_hold{true};
        bool show_toggle{true};
    } m_keybind_list{};

    struct bomb_info
    {
        screen_layout layout{40.0f, 200.0f};
    } m_bomb_info{};

    // Walk bot (its own menu page): walks the player along routes over the map's navigation mesh by pressing the
    // movement keys and turning the view like a player, plus round and match automation around it.
    struct walk_bot
    {
        // Where the routes come from. Jumps and ladders are always used; the game keys (use, reload, buy menu) are the
        // game binds in Misc > General.
        enum mode_t : int
        {
            random_points = 0,
            recorded = 1,
            mode_count
        };
        bool enabled{false};
        int mode{random_points};
        bool hunt{false}; // heads for a known enemy instead, back to the route source when there is none
        bool look{true};  // turns the view along the way, yielding to aim, triggers, the menu and the player's mouse
        bool breakables{true};       // breaks vents, boards and glass on the way with attack (doors are always opened)
        int record_key{VK_F8};       // starts and stops recording a route
        bool show_recordings{false}; // draws the recorded routes of this map and side
        // Fighting: its own aim (a player's hand: a quick smooth turn onto the chosen hitboxes, then tracking), the
        // trigger bot as autofire (its settings from the Triggerbot tab), and turning towards hits from unseen enemies.
        bool aim{true};
        float aim_speed{1.0f}; // the hand's speed: 1 is a quick player, 2 a very fast one
        int aim_hitboxes{combat_profile::aim_part::head}; // aim-part bitmask of the hitboxes it may aim at
        // Optional multipoint aim: the centre plus capsule ends and lateral points. Off keeps one centre per part.
        bool aim_multipoint{false};
        combat_profile::multipoint_settings aim_multipoint_config{};
        bool aim_body_lethal{true}; // takes a chest shot instead of the head when the chest shot would kill
        int aim_smoothing{20};      // extra ease on the fight hand: 0 mechanical, 100 calm
        float aim_jitter{0.08f};    // micro movement of the hand while it tracks, in degrees
        int aim_reaction_ms{100};   // how long a newly seen enemy must stay in sight before the fight starts
        // Scopes sniper rifles (AWP, SSG 08, SCAR-20, G3SG1) while the own aim fights. One secondary-attack tap puts
        // the scope up; after a kill the knife slot (3) is selected, which leaves the scope, and the primary slot (1)
        // is drawn again for the next target.
        bool auto_scope{true};
        bool autofire{true};
        bool autofire_spray{false}; // beta: holds the trigger on SMGs, rifles and machine guns instead of one press per shot
        bool spray_log{false};      // opt-in: writes one CSV row per spray tick to %TEMP%\vesta_uitest\logs
        // Close-quarters knife: an enemy inside this range is finished with slot 3 and a secondary attack instead of
        // the gun. The AFK check keeps the bot from chasing an active player.
        bool auto_knife{false};
        float auto_knife_distance{300.0f};
        bool auto_knife_afk_only{true};
        int auto_knife_respawn_grace_ms{1500}; // a respawned enemy cannot be knifed before this grace has passed
        bool auto_knife_debug{false};          // draws each enemy's knife anchor, spawn, moved flag and eligibility
        // Rear defense: an enemy in the cone behind the player is turned on at once. It has its own aim settings and
        // can engage with the primary or the knife.
        bool rear_defense{false};
        float rear_defense_distance{600.0f};
        float rear_defense_fov{120.0f}; // full width of the cone centred behind the view
        int rear_defense_hitboxes{combat_profile::aim_part::head | combat_profile::aim_part::body};
        int rear_defense_smoothing{10}; // 0 snaps, 100 turns like the normal fight hand
        int rear_defense_weapon{0};     // 0 primary, 1 knife
        bool react_to_hits{true};
        bool show_path{true};
        foundation::rgba path_color{120, 170, 255, 220};
        float path_thickness{2.0f};
        bool show_mesh{true};
        foundation::rgba mesh_color{235, 240, 255, 46};
        float mesh_distance{1400.0f};

        bool auto_buy{false};
        // Buy presets: the default one and one per mode (used instead of the default when `custom`). Each item is a
        // buy-menu code (the digits pressed in the menu), a side and a money condition; fixed buffers keep the
        // settings trivially copyable (the overlay watches them byte by byte).
        struct buy_item
        {
            bool enabled{};
            std::array<char, 4> code{};
            int side{};       // 0 both, 1 T, 2 CT
            int money_rule{}; // 0 any amount, 1 less than `money`, 2 at least `money`
            int money{2000};
        };
        static constexpr int k_buy_items = 10;
        struct buy_preset
        {
            bool custom{};     // a mode preset is used only when set; otherwise the default applies
            bool random_one{}; // buy one of the items that apply, at random, instead of all of them
            int count{};       // items in use, the first `count` of `items`
            std::array<buy_item, k_buy_items> items{};
        };
        enum buy_mode : int
        {
            buy_default,
            buy_premier,
            buy_competitive,
            buy_wingman,
            buy_casual,
            buy_deathmatch,
            buy_mode_count
        };
        // Every preset starts empty: the user adds the items a mode needs ("Add Item").
        std::array<buy_preset, buy_mode_count> buy_presets{};
        int buy_edit{}; // the preset the menu shows
        bool auto_reload{true};
        // Auto health shot: a standalone automation (it does not need the walk bot). Below `health_shot_health`, once
        // `health_shot_damage_ms` have passed since the last hit, while Auto Knife is idle and while the bot is not
        // aiming at or shooting anyone, the game bind that draws the health shot (use weapon_healthshot, X by default)
        // is pressed until the syringe is in hand, the primary attack is held for the injection, and a firearm is
        // drawn again. A use is not started while a living enemy is within `health_shot_near_distance` units.
        bool auto_health_shot{false};
        int health_shot_health{50}; // use the health shot below this health
        int health_shot_damage_ms{5000}; // wait this long after taking damage before using
        // Hold the use while a living enemy is this close. 0 disables the gate. Drawing and injecting keep the hands
        // and the weapon slot busy well past a second, which is the worst moment to have someone come round a corner.
        int health_shot_near_distance{400};
        int health_shot_key{'X'};   // the game bind that draws the health shot
        bool health_shot_debug{false}; // draws the Auto Health Shot state on screen

        bool auto_rejoin{false};
        int game_mode{0}; // match_logic::game_mode: deathmatch, casual, competitive, wingman, premier
        int team{0};      // 0 T, 1 CT
        bool auto_disconnect{false};
        int disconnect_score{450};
        int disconnect_minutes{4};
        int disconnect_spread{20}; // percent of random extra time
    } m_walk_bot{};

    game_controls m_game_controls{};

    struct auto_stop
    {
        bool enabled{false};
        bool physical_fire{true};
        bool predictive_trigger{true};
        float stop_speed{8.0f};
        float required_shoot_speed{34.0f};
    } m_auto_stop{};
    // Draws the triggerbot decision trace. Off by default so the trace costs nothing unless it is being diagnosed.
    bool trigger_debug_overlay{false};

    // Interface language: 0 English, 1 Russian (render::localization::id).
    static constexpr int k_language_count = 2;
    int language{0};
    float menu_scale{1.0f};
    float overlay_opacity{0.86f};
    struct interface_palette
    {
        // Insomnia-style defaults: near-black surfaces, hairline warm-grey borders and a periwinkle accent.
        foundation::rgba background{13, 13, 13, 255};
        foundation::rgba panel{18, 18, 18, 255};
        foundation::rgba card{13, 13, 13, 255};
        foundation::rgba popup{10, 11, 9, 255};
        foundation::rgba accent{180, 180, 208, 255};
        foundation::rgba text{199, 199, 199, 255};
        foundation::rgba muted_text{128, 128, 130, 255};
        foundation::rgba border{36, 34, 33, 255};
        foundation::rgba hover{30, 32, 34, 255};
    } palette{};

    bool auto_accept{true};
    // Hold the game's duck bind while the aimbot is engaged with a live target; the duck is lifted when that target
// dies or breaks the sight line.
    bool auto_crouch{false};
    struct auto_bonus
    {
        bool enabled{false};
        int key{'G'};
        int interval_ms{1000};
    } m_auto_bonus{};
    bool obs_bypass{false};
    bool limit_fps{true};
    int fps_limit{240};
    bool use_gpu{true};
    // ui::resource_priority levels: 0 game first, 1 balanced, 2 overlay first. The defaults are the long-standing
    // behaviour: the same CPU class as the game, GPU work below it.
    int cpu_priority{1};
    int gpu_priority{0};
};

} // namespace config

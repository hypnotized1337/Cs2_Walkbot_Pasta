#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/misc/match_flow.hpp>
#include <features/misc/misc.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/level.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <simulation/ballistics.hpp>

#include <algorithm>

namespace features::misc
{
using namespace walk_detail;

walk_bot_t& walk_bot()
{
    static walk_bot_t value{};
    return value;
}

bool walk_bot_t::same_level(const player_state& player, const foundation::vec3& point)
{
    return same_level_impl(player.on_ground || player.on_ladder, player.origin, point);
}

bool walk_bot_t::read_player(player_state& out) const
{
    const auto pawn = game::local_player().pawn();
    if (!pawn || !game::local_player().alive())
        return false;
    const auto& process = game::bound_memory().process;
    std::uintptr_t node{};
    std::uint32_t flags{};
    std::uint8_t move_type{};
    if (!process.copy(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id), &node, sizeof(node)) || !node ||
        !process.copy(node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id), &out.origin, sizeof(out.origin)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id), &out.velocity, sizeof(out.velocity)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id), &flags, sizeof(flags)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_MoveType"_id), &move_type, sizeof(move_type)))
        return false;
    if (!std::isfinite(out.origin.x) || !std::isfinite(out.origin.y) || !std::isfinite(out.origin.z))
        return false;
    if (!game::camera().sample(out.eye, out.view))
        return false;
    // The shot direction is built from the client's base view (v_angle) plus AimPunchServices' bullet scale. The
    // rendered CViewRender may carry only a fraction of the punch, so it cannot be used as the shot origin direction.
    out.base_view = out.view;
    foundation::vec3 base_view{};
    if (process.copy(pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id), &base_view, sizeof(base_view)) &&
        std::isfinite(base_view.x) && std::isfinite(base_view.y) && std::isfinite(base_view.z))
        out.base_view = base_view;
    out.pawn = pawn;
    out.controller = game::local_player().controller();
    out.team = game::local_player().team();
    out.on_ground = (flags & 1u) != 0u;
    out.on_ladder = move_type == k_movetype_ladder;
    out.in_buy_zone = process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bInBuyZone"_id));
    out.buy_menu_open = process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bIsBuyMenuOpen"_id));
    const auto pawn_flag = [&](const std::int32_t offset) { return offset > 0 && process.load<bool>(pawn + offset); };
    // The current schema keeps these on C_CSPlayerPawn and the move flag on C_CSPlayerPawnBase (verified live; asking
    // the other class finds nothing, which once made every deathmatch buy window look closed).
    out.immune = pawn_flag(SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_id));
    out.moved = pawn_flag(SCHEMA("C_CSPlayerPawnBase", "m_bHasMovedSinceSpawn"_id));
    if (const auto spawn_offset = SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_id); spawn_offset > 0)
        out.spawn_time = process.load<float>(pawn + spawn_offset);
    // The weapon in hand usually answers this without walking the inventory: reading the slot list costs up to
    // sixteen entity lookups plus a vdata read each tick, and it is only needed when the hand is not a main weapon.
    out.has_main_weapon = walk_logic::main_weapon_type(game::local_player().weapon_type());
    if (!out.has_main_weapon)
        if (const auto services = process.load<std::uintptr_t>(pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id)))
        {
            // m_hMyWeapons: a count and a pointer to the handles.
            const auto list = services + SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_id);
            const auto count = std::clamp(process.load<std::int32_t>(list), 0, 16);
            const auto handles = process.load<std::uintptr_t>(list + 8);
            std::array<std::uint32_t, 16> held{};
            if (count > 0 && handles && process.copy(handles, held.data(), static_cast<std::size_t>(count) * 4))
                for (int i = 0; i < count && !out.has_main_weapon; ++i)
                    if (const auto weapon = game::entity_index().lookup(held[static_cast<std::size_t>(i)]))
                        if (const auto vdata =
                                process.load<std::uintptr_t>(weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8))
                            out.has_main_weapon = walk_logic::main_weapon_type(
                                process.load<std::uint32_t>(vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id)));
        }
    if (const auto services_offset = SCHEMA("CCSPlayerController", "m_pInGameMoneyServices"_id);
        services_offset > 0 && out.controller)
        if (const auto services = process.load<std::uintptr_t>(out.controller + services_offset))
            out.money = process.load<std::int32_t>(services +
                                                   SCHEMA("CCSPlayerController_InGameMoneyServices", "m_iAccount"_id));
    return true;
}

// The secondary attack is a mouse button in the default binds and can be any key the game accepts. A scope tap is a
// real press and release: CS2 toggles the scope on the press, so holding the button would not scope out again.
bool walk_bot_t::set_secondary_control(owned_control& control, const bool pressed)
{
    return input_lifecycle::transition(
        control, pressed,
        [](const game::input_binding& binding, const bool down)
        {
            using platform::windows::pointer_action;
            switch (binding.device)
            {
                case game::input_device::keyboard:
                    return platform::windows::bound_input().key(binding.virtual_key, down);
                case game::input_device::mouse_primary:
                    return platform::windows::bound_input().pointer(
                        0, 0, down ? pointer_action::primary_down : pointer_action::primary_up);
                case game::input_device::mouse_secondary:
                    return platform::windows::bound_input().pointer(
                        0, 0, down ? pointer_action::secondary_down : pointer_action::secondary_up);
                case game::input_device::mouse_middle:
                    return platform::windows::bound_input().pointer(
                        0, 0, down ? pointer_action::middle_down : pointer_action::middle_up);
                case game::input_device::mouse_auxiliary1:
                    return platform::windows::bound_input().pointer(
                        0, 0, down ? pointer_action::auxiliary1_down : pointer_action::auxiliary1_up);
                case game::input_device::mouse_auxiliary2:
                    return platform::windows::bound_input().pointer(
                        0, 0, down ? pointer_action::auxiliary2_down : pointer_action::auxiliary2_up);
                default:
                    return false;
            }
        });
}

bool walk_bot_t::set_scope_control(const bool pressed)
{
    return set_secondary_control(m_scope, pressed);
}

namespace
{
// The scope goes up with one secondary-attack tap. Leaving it is a weapon switch: 3 selects the knife (which drops the
// scope at once), and 1 draws the sniper again for the next target. The waits only cover the game's switch/zoom
// animation; a failed switch is retried, so a dropped key cannot leave the bot stuck scoped.
constexpr auto k_scope_tap_hold{std::chrono::milliseconds(40)};
constexpr auto k_scope_wait{std::chrono::milliseconds(500)};
constexpr auto k_knife_wait{std::chrono::milliseconds(350)};
constexpr auto k_draw_wait{std::chrono::milliseconds(500)};
constexpr auto k_switch_retry{std::chrono::milliseconds(350)};
constexpr std::uint16_t k_knife_key{'3'};
constexpr std::uint16_t k_primary_key{'1'};
constexpr std::uint16_t k_pistol_key{'2'};
constexpr int k_switch_attempts{3};
} // namespace

// Keeps the sniper scope in step with the fight. m_scope_wanted is set from this tick's combat plan. One tap enters
// the scope when a target is taken; when that target dies (or another target is taken) the knife key leaves the scope,
// and the next fight draws the sniper again before tapping the scope. Every switch is checked against the weapon the
// game actually reports, with a few retries, so a dropped key cannot leave the player scoped forever.
void walk_bot_t::servo_scope(const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;

    // The knife owns the hands while it is out. Release a scope tap that was in flight and leave the scope state
    // idle; the knife slot itself drops a scope anyway.
    if (m_knife_target)
    {
        if (m_scope.pressed)
            (void)set_scope_control(false);
        m_scope_state = scope_state::idle;
        m_scope_target = 0;
        m_scope_next = {};
        return;
    }

    // A scope tap is a real press and release; finish the release before deciding anything else.
    if (m_scope.pressed)
    {
        if (now < m_scope_release_at)
            return;
        (void)set_scope_control(false);
        return;
    }

    // Never type weapon keys into a menu or another window. The game hides the system cursor while playing.
    const auto can_press = []
    {
        CURSORINFO cursor{};
        cursor.cbSize = sizeof(cursor);
        const auto cursor_shown =
            ::GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr;
        return !cursor_shown && game_window_in_front();
    };

    const auto weapon_type = game::local_player().weapon_type();
    const auto sniper = weapon_type == game::rules::precision;
    const auto weapon = simulation::ballistics().ctx();
    const auto scoped =
        weapon.valid && weapon.weapon_type == game::rules::precision && weapon.is_scoped;
    const auto reloading =
        weapon.valid && weapon.weapon_type == game::rules::precision && weapon.is_reloading;
    // The scope was put up for one enemy. The walk bot clears its fight target when that enemy dies (or the fight is
    // given up), and a new target replaces it; either way the knife goes out. Turning the walk bot or its aim off also
    // ends the scope's ownership.
    const auto fight_over =
        m_scope_target && (m_fight_target != m_scope_target || !cfg.enabled || !cfg.aim);

    const auto holster = [&]
    {
        if (!sniper)
        {
            m_scope_state = scope_state::idle;
            m_scope_target = 0;
            m_scope_next = {};
            return;
        }
        if (!can_press())
            return;
        tap_key(k_knife_key);
        m_scope_state = scope_state::holstering;
        m_scope_attempts = 0;
        m_scope_next = now + k_knife_wait;
        m_scope_target = 0;
    };
    const auto enter_scope = [&]
    {
        if (!can_press())
            return;
        if (!set_scope_control(true))
            return;
        m_scope_release_at = now + k_scope_tap_hold;
        m_scope_state = scope_state::scoping;
        m_scope_target = m_fight_target;
        m_scope_next = now + k_scope_wait;
    };

    switch (m_scope_state)
    {
        case scope_state::scoping:
        {
            if (fight_over || !cfg.auto_scope)
            {
                holster();
                return;
            }
            if (scoped)
            {
                m_scope_state = scope_state::scoped;
                m_scope_attempts = 0;
                m_scope_next = {};
                return;
            }
            if (!sniper)
            {
                m_scope_state = scope_state::idle;
                m_scope_target = 0;
                m_scope_next = {};
                return;
            }
            if (now < m_scope_next)
                return;
            if (reloading)
            {
                m_scope_next = now + std::chrono::milliseconds(100);
                return;
            }
            if (m_scope_attempts >= k_switch_attempts)
            {
                m_scope_state = scope_state::idle;
                m_scope_target = 0;
                m_scope_next = now + std::chrono::seconds(1);
                return;
            }
            ++m_scope_attempts;
            enter_scope();
            return;
        }
        case scope_state::scoped:
        {
            if (fight_over || !cfg.auto_scope)
            {
                holster();
                return;
            }
            if (!sniper)
            {
                m_scope_state = scope_state::idle;
                m_scope_target = 0;
                m_scope_next = {};
                return;
            }
            if (!scoped)
            {
                if (!weapon.valid || reloading)
                {
                    m_scope_next = now + std::chrono::milliseconds(100);
                    return;
                }
                if (now < m_scope_next)
                    return;
                if (m_scope_attempts >= k_switch_attempts)
                {
                    m_scope_state = scope_state::idle;
                    m_scope_target = 0;
                    m_scope_next = now + std::chrono::seconds(1);
                    return;
                }
                ++m_scope_attempts;
                enter_scope();
                return;
            }
            m_scope_attempts = 0;
            return;
        }
        case scope_state::holstering:
        {
            if (!sniper)
            {
                // Only the knife is our own switch. If the player chose a grenade or a pistol meanwhile, leave their
                // weapon alone instead of pulling the sniper back out.
                m_scope_state = weapon_type == game::rules::knife ? scope_state::holstered : scope_state::idle;
                m_scope_next = {};
                return;
            }
            if (now < m_scope_next)
                return;
            if (m_scope_attempts >= k_switch_attempts)
            {
                m_scope_state = scope_state::idle;
                m_scope_next = now + std::chrono::milliseconds(500);
                return;
            }
            if (!can_press())
                return;
            ++m_scope_attempts;
            tap_key(k_knife_key);
            m_scope_next = now + k_switch_retry;
            return;
        }
        case scope_state::holstered:
        {
            if (!cfg.auto_scope || now < m_scope_next)
                return;
            if (!sniper && weapon_type != game::rules::knife)
            {
                m_scope_state = scope_state::idle;
                m_scope_next = {};
                return;
            }
            // Draw the sniper again straight after the knife, before the next enemy appears: 3 leaves the scope, 1
            // returns to the primary slot so the next scope-in does not have to wait for a weapon deploy.
            if (sniper)
            {
                m_scope_state = scope_state::drawing;
                m_scope_attempts = 0;
                m_scope_next = now + std::chrono::milliseconds(100);
                return;
            }
            if (!can_press())
                return;
            tap_key(k_primary_key);
            m_scope_state = scope_state::drawing;
            m_scope_attempts = 0;
            m_scope_next = now + k_draw_wait;
            return;
        }
        case scope_state::drawing:
        {
            if (!cfg.auto_scope)
            {
                m_scope_state = scope_state::idle;
                m_scope_next = {};
                return;
            }
            if (now < m_scope_next)
                return;
            if (!sniper)
            {
                if (m_scope_attempts >= k_switch_attempts)
                {
                    m_scope_state = scope_state::holstered;
                    m_scope_next = {};
                    return;
                }
                if (!can_press())
                    return;
                ++m_scope_attempts;
                tap_key(k_primary_key);
                m_scope_next = now + k_switch_retry;
                return;
            }
            if (!weapon.valid || reloading)
            {
                m_scope_next = now + std::chrono::milliseconds(100);
                return;
            }
            if (!m_scope_wanted)
            {
                m_scope_state = scope_state::idle;
                m_scope_next = {};
                return;
            }
            if (scoped)
            {
                m_scope_state = scope_state::scoped;
                m_scope_target = m_fight_target;
                m_scope_attempts = 0;
                m_scope_next = {};
                return;
            }
            m_scope_attempts = 0;
            enter_scope();
            return;
        }
        case scope_state::idle:
        default:
            break;
    }

    if (!cfg.auto_scope || !m_scope_wanted || !sniper || now < m_scope_next)
        return;
    if (scoped)
    {
        // The scope is already up (manual, or left from a previous fight): take it over.
        m_scope_state = scope_state::scoped;
        m_scope_target = m_fight_target;
        m_scope_attempts = 0;
        m_scope_next = {};
        return;
    }
    if (!weapon.valid || reloading)
        return;
    m_scope_attempts = 0;
    enter_scope();
}

// The knife finish: slot 3 is drawn, the feet walk at the enemy and the secondary attack is held for one swing at a
// time while the view stays on it. The walk bot's gun autofire and scope are suspended for the whole engagement.
void walk_bot_t::handle_knife(const player_state& player, const combat_plan& plan, const clock::time_point now)
{
    // Auto scope may have put the knife out itself after a kill. Either way, a gun was on the player before this
    // engagement and has to be drawn again when it ends.
    const auto scope_weapon_switch = m_scope_state == scope_state::holstering ||
                                     m_scope_state == scope_state::holstered ||
                                     m_scope_state == scope_state::drawing;
    m_scope_wanted = false;
    if (m_scope.pressed)
        (void)set_scope_control(false);
    m_scope_state = scope_state::idle;
    m_scope_target = 0;
    m_scope_next = {};
    features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
    features::aimbot::publish_walk_autofire_parts(0);
    features::aimbot::g_walk_autofire_spray.store(false, std::memory_order_release);

    if (m_knife_attack.pressed && now >= m_knife_release)
        (void)set_secondary_control(m_knife_attack, false);

    if (plan.enemy != m_knife_target)
    {
        m_knife_target = plan.enemy;
        m_knife_next_switch = {};
        m_knife_next_attack = {};
    }
    if (!plan.enemy || game::bound_memory().process.load<std::int32_t>(
                            plan.enemy + SCHEMA("C_BaseEntity", "m_iHealth"_id)) <= 0)
    {
        m_knife_target = 0;
        m_knife_locked = false;
        (void)set_secondary_control(m_knife_attack, false);
        return;
    }

    const auto distance = distance_2d(player.origin, plan.target);
    // Commit before drawing the knife. From this tick on the engagement owns the weapon slot, and the restore path
    // refuses to draw the gun while the lock target is alive and inside the leash.
    if (m_rear_target != plan.enemy && distance <= k_knife_lock_enter_range)
        m_knife_locked = true;

    const auto weapon_type = game::local_player().weapon_type();
    if (!m_knife_restore && (weapon_type != game::rules::knife || scope_weapon_switch))
    {
        m_knife_restore = true;
        m_knife_restore_key = player.has_main_weapon ? k_primary_key : k_pistol_key;
        m_knife_restore_at = {};
    }
    if (weapon_type != game::rules::knife && now >= m_knife_next_switch)
    {
        // Give the deploy animation time before repeating the slot key: retapping mid-draw can restart it.
        tap_key(k_knife_key);
        m_knife_next_switch = now + std::chrono::milliseconds(600);
    }

    const auto to = plan.view.point - player.eye;
    const auto want_yaw = degrees(std::atan2(to.y, to.x));
    const auto want_pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
    const auto off = std::hypot(foundation::wrap_yaw(want_yaw - player.view.y), want_pitch - player.view.x);
    // Keep closing until body collision range, and stab while closing: stopping at the edge of the knife's reach
    // made a target that stopped a few units too far away stand there swinging at air.
    constexpr float k_knife_stop = 42.0f;
    constexpr float k_knife_attack_reach = 72.0f;
    if (distance > k_knife_stop)
    {
        (void)set_control(m_duck, false);
        (void)set_control(m_jump, false);
        press_towards(plan.target - player.origin, player.view.y);
    }
    else
    {
        for (auto* control : {&m_forward, &m_back, &m_left, &m_right, &m_duck, &m_jump})
            (void)set_control(*control, false);
    }
    if (weapon_type == game::rules::knife && distance <= k_knife_attack_reach && !m_knife_attack.pressed &&
        now >= m_knife_next_attack && off < 20.0f)
    {
        if (set_secondary_control(m_knife_attack, true))
        {
            m_knife_release = now + std::chrono::milliseconds(200);
            m_knife_next_attack = now + std::chrono::milliseconds(1000);
        }
        else
            m_knife_next_attack = now + std::chrono::milliseconds(60);
    }
    look(player, plan.view, false, now);
}

// Draws the slot the knife feature switched away from, once no knife plan owns the hands any more.
void walk_bot_t::restore_knife_weapon(const clock::time_point now)
{
    if (!m_knife_restore)
        return;
    // A committed Auto-Knife engagement owns the hands. Do not draw the gun back while its target is still alive and
    // inside the lock, even if the fight plan has a one-tick gap.
    if (m_knife_locked && m_knife_target)
        return;
    if (game::local_player().weapon_type() != game::rules::knife)
    {
        m_knife_restore = false;
        return;
    }
    if (now < m_knife_restore_at)
        return;
    tap_key(m_knife_restore_key);
    m_knife_restore_at = now + std::chrono::milliseconds(500);
}

// Rear defense: the nearest enemy in the rear cone is latched, the view turns on it with the rear settings, and the
// chosen weapon engages. The primary path reuses the walk bot's autofire and scope; the knife path calls the same
// knife handler as the close-range feature.
void walk_bot_t::handle_rear(const player_state& player, const combat_plan& plan, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!plan.enemy || game::bound_memory().process.load<std::int32_t>(
                            plan.enemy + SCHEMA("C_BaseEntity", "m_iHealth"_id)) <= 0)
    {
        m_rear_target = 0;
        m_knife_target = 0;
        m_knife_locked = false;
        (void)set_secondary_control(m_knife_attack, false);
        return;
    }
    m_rear_target = plan.enemy;
    m_fight_target = plan.enemy;
    m_fight_seen_at = now;
    m_react_until = {};

    if (plan.weapon == 1)
    {
        m_knife_target = plan.enemy;
        handle_knife(player, plan, now);
        return;
    }

    m_knife_target = 0;
    m_knife_locked = false;
    if (m_knife_attack.pressed)
        (void)set_secondary_control(m_knife_attack, false);
    const auto held_type = game::local_player().weapon_type();
    const auto needs_gun = player.has_main_weapon ? !walk_logic::main_weapon_type(held_type)
                                                 : !game::rules::is_firearm(held_type);
    if (needs_gun && now >= m_rear_next_switch)
    {
        tap_key(player.has_main_weapon ? k_primary_key : k_pistol_key);
        m_rear_next_switch = now + std::chrono::milliseconds(500);
    }
    for (auto* control : {&m_forward, &m_back, &m_left, &m_right, &m_duck, &m_jump})
        (void)set_control(*control, false);

    // A scope left after a previous kill belongs to that kill. A scope tap already in flight, or a scope already up,
    // simply takes the rear target; the old post-kill holster/draw states are dropped so the sniper is not cycled.
    if (m_scope_state == scope_state::holstering || m_scope_state == scope_state::holstered ||
        m_scope_state == scope_state::drawing)
    {
        m_scope_state = scope_state::idle;
        m_scope_target = 0;
        m_scope_next = {};
    }
    if (m_scope_state == scope_state::scoped || m_scope_state == scope_state::scoping)
        m_scope_target = plan.enemy;
    m_scope_wanted = true;
    servo_scope(now);

    const auto weapon = simulation::ballistics().ctx();
    const auto scope_wait = cfg.auto_scope && weapon.valid && weapon.weapon_type == game::rules::precision &&
                            !weapon.is_scoped && m_scope_state != scope_state::scoped;
    features::aimbot::publish_walk_autofire_parts(plan.view.part);
    features::aimbot::g_walk_autofire.store(!scope_wait, std::memory_order_release);
    look(player, plan.view, false, now);
}

// Whether a game screen is over the world: the pause menu, the buy menu, the console, team select, a popup, the end of
// the match. The game shows the system cursor exactly while one of them takes the mouse (in play it is hidden and the
// mouse is relative), so the cursor is the first test; the screens the match automation tracks by their Panorama
// panels are the second.
bool walk_bot_t::game_ui_open(const player_state& player) const
{
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (::GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr)
        return true;
    return player.buy_menu_open || match_flow().holds_input();
}

void walk_bot_t::tick()
{
    if (m_reset_requested.exchange(false, std::memory_order_acq_rel))
        reset_state();

    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto now = clock::now();
    // The scope follows the state the last tick asked for: while a fight continues, that keeps it up without
    // cycling; the plan below asks for the next state.
    servo_scope(now);
    m_scope_wanted = false;
    release_tapped_key(!cfg.enabled);
    if (!cfg.enabled)
    {
        // A buy sequence belongs to the walk bot session. Drop it on disable so re-enabling cannot type stale keys.
        m_buy_queue.clear();
        m_buy_waiting_menu = false;
        m_buy_first_key = {};
        m_next_buy_key = {};
        auto_bonus().set_auto_knife_active(false);
        m_knife_locked = false;
        restore_knife_weapon(now);
        m_knife_restore = false;
        {
            std::scoped_lock lock(m_knife_watch_mutex);
            m_knife_watch.clear();
        }
        m_knife_target = 0;
        m_rear_target = 0;
        m_scope_wanted = false;
        (void)set_secondary_control(m_knife_attack, false);
        features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
        features::aimbot::publish_walk_autofire_parts(0);
        features::aimbot::g_walk_autofire_spray.store(false, std::memory_order_release);
        servo_scope(now);
        if (m_walking || !m_gated_keys.empty() || m_recording_active)
        {
            stop_walking();
            std::scoped_lock lock(m_route_mutex);
            m_recording_active = false;
        }
        return;
    }

    const auto graph = simulation::nav_mesh().current();
    if (graph != m_graph)
    {
        stop_walking();
        m_banned.clear();
        m_recent.clear();
        m_unreachable.clear();
        std::scoped_lock lock(m_route_mutex);
        m_graph = graph;
    }
    bind_controls();
    {
        std::scoped_lock lock(m_route_mutex);
        m_walking = true;
    }

    player_state player{};
    if (!read_player(player))
    {
        auto_bonus().set_auto_knife_active(false);
        // A missing pawn (rather than a dead one, which stays readable while spectating) is a map load or a
        // reconnect. The controller can stay valid across the reload, so the controller alone is not enough: the
        // pawn going away is what starts the clock for the first readable frame to rebuild from scratch.
        if (game::local_player().pawn())
            m_player_unavailable_since = {};
        else if (m_player_unavailable_since == clock::time_point{})
            m_player_unavailable_since = now;
        // Dead (or not readable): the route of this life is over.
        if (m_was_alive)
            reset_route();
        m_was_alive = false;
        m_previous_origin_valid = false;
        // A death drops the scope by itself; do not carry a knife/draw state into the next life.
        m_scope_state = scope_state::idle;
        m_scope_target = 0;
        m_scope_next = {};
        m_knife_restore = false;
        {
            std::scoped_lock lock(m_knife_watch_mutex);
            m_knife_watch.clear();
        }
        m_knife_target = 0;
        m_knife_locked = false;
        m_rear_target = 0;
        (void)set_secondary_control(m_knife_attack, false);
        features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
        features::aimbot::publish_walk_autofire_parts(0);
        note_idle(3);
        release_keys();
        return;
    }
    // A long gap with no local pawn is a map load or a reconnect. Dead player pawns stay readable while spectating,
    // so a normal death does not take this path. Do not carry the previous level's route, bans and UI gate across it,
    // even when the map name and nav mesh never changed.
    const auto unavailable_since = m_player_unavailable_since;
    m_player_unavailable_since = {};
    if (unavailable_since != clock::time_point{} && now - unavailable_since > std::chrono::seconds(1))
    {
        reset_state();
        // Stale EndOfMatch/round state can otherwise keep the walk bot's input gate closed after a restart.
        match_flow().request_reset();
        return;
    }
    // The game clock rewinding means a new map or match. The old route, nav graph and per-map decisions belong to
    // the previous level, so drop them here even when the map has the same name and the nav mesh was not reloaded.
    if (m_spawn_time > 0.0f && player.spawn_time + 0.01f < m_spawn_time)
    {
        reset_state();
        match_flow().request_reset();
        return;
    }
    // A new life, a bot takeover or a teleport starts from a new spot: plan afresh. CS2 reuses the pawn after a
    // respawn, so the pawn alone does not tell.
    // A spawn is told by the game's spawn time, which changes on every spawn: also the ones that move a living player
    // (warmup ending, a new round), which reuse the pawn and do not kill it.
    const bool teleported = m_previous_origin_valid && player.origin.distance(m_previous_origin) > 200.0f;
    const bool respawned = player.spawn_time != m_spawn_time;
    m_spawn_time = player.spawn_time;
    if (player.pawn != m_pawn || !m_was_alive || teleported || respawned)
    {
        if (player.pawn != m_pawn || !m_was_alive || respawned)
        {
            ++m_life;
            m_spawned_at = now;
        }
        m_pawn = player.pawn;
        m_banned.clear();
        m_hunt_target = 0;
        {
            std::scoped_lock lock(m_knife_watch_mutex);
            m_knife_watch.clear();
        }
        reset_route();
        m_player_until = {};
    }
    m_was_alive = true;
    m_previous_origin = player.origin;
    m_previous_origin_valid = true;
    // The walking direction, smoothed: random destinations prefer to keep it.
    if (const auto ground = flat(player.velocity); ground.length() > 60.0f)
    {
        const auto blended = m_heading * 0.95f + flat_direction(ground) * 0.05f;
        m_heading = flat_direction(blended);
    }
    {
        std::scoped_lock lock(m_route_mutex);
        m_last_origin = player.origin;
    }

    // Recording takes the player's own walk: the bot keeps its hands off meanwhile.
    record(player, now);
    if (m_recording_active)
    {
        auto_bonus().set_auto_knife_active(false);
        m_knife_restore = false;
        m_knife_target = 0;
        m_knife_locked = false;
        m_rear_target = 0;
        features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
        features::aimbot::publish_walk_autofire_parts(0);
        note_idle(8);
        release_keys();
        look(player, {}, true, now);
        return;
    }
    // Recordings follow the map and the team (played back, or only drawn).
    if (cfg.mode == config::general_profile::walk_bot::recorded || cfg.show_recordings)
    {
        const auto level = game::level().name();
        auto map = level ? *level : std::string{};
        if (const auto slash = map.find_last_of("/\\"); slash != std::string::npos)
            map = map.substr(slash + 1);
        if (const auto dot = map.find_last_of('.'); dot != std::string::npos)
            map.resize(dot);
        if (map != m_recordings_map || player.team != m_recordings_team)
            load_recordings(map, player.team);
    }

    const bool focused = focus_ready();
    // Keys only go to the game in front: a buy sequence cut off by Alt+Tab is dropped, not typed into another window.
    if (game_window_in_front())
        auto_buy(player, now);
    else if (!m_buy_queue.empty())
        m_buy_queue.clear();

    const bool ui_open = game_ui_open(player);
    const bool movement_taken =
        auto_stop().active() || nade_helper().movement_taken() || !m_buy_queue.empty() || buy_holds_movement(player);
    // The player's own movement wins: while gated their keys are seen here, once released the plain key state.
    bool player_moving = false;
    for (const auto* control : {&m_forward, &m_back, &m_left, &m_right})
        if (control->binding.device == game::input_device::keyboard && control->binding.virtual_key &&
            !control->pressed && platform::windows::bound_input().physical_key_down(control->binding.virtual_key))
            player_moving = true;
    if (player_moving)
        m_player_until = now + std::chrono::milliseconds(1500);

    // Autofire: the trigger bot runs (with its own settings) while the walk bot has the game in play. With auto scope
    // on a sniper rifle, it waits for the scope: an unscoped sniper shot is a wasted shot.
    const auto weapon = simulation::ballistics().ctx();
    const auto sniper = weapon.valid && weapon.weapon_type == game::rules::precision;
    // Spray is a hold for the game's automatic weapon classes: SMGs, rifles and machine guns. Publishing it for a
    // sniper or shotgun would let the trigger's hold path and its seed stand-down reason about a mode that does not
    // apply to that weapon.
    const auto spray_weapon = weapon.valid && game::rules::is_spray_weapon(weapon.weapon_type);
    features::aimbot::g_walk_autofire_spray.store(cfg.autofire && cfg.autofire_spray && spray_weapon,
                                                  std::memory_order_release);
    // The scope servo already confirmed the scope for this fight; do not keep autofire off if the ballistics snapshot
    // lags a frame behind that confirmation.
    const auto scope_confirmed = m_scope_state == scope_state::scoped;
    const auto scope_wait = cfg.auto_scope && sniper && !weapon.is_scoped && !scope_confirmed;
    if (!focused || ui_open || movement_taken || now < m_player_until)
    {
        auto_bonus().set_auto_knife_active(false);
        note_idle(!focused ? 2u : ui_open ? 6u : movement_taken ? 4u : 7u);
        // Stop the held spray when the player takes the movement back, or when a grenade throw, the buy menu or
        // another feature owns the movement. Auto Stop is the trigger's own brake and must not cancel the burst.
        const bool auto_stop_brake =
            movement_taken && auto_stop().active() && focused && !ui_open && now >= m_player_until && cfg.autofire &&
            !scope_wait;
        if (!auto_stop_brake)
        {
            features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
            features::aimbot::publish_walk_autofire_parts(0);
        }
        release_keys();
        look(player, {}, true, now);
        return;
    }
    if (!graph)
    {
        auto_bonus().set_auto_knife_active(false);
        features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
        features::aimbot::publish_walk_autofire_parts(0);
        note_idle(1);
        release_keys();
        return;
    }
    gate_movement(true);

    // Fighting comes first: an enemy in sight stops the walk for the aim, a spawn-protected one is walked round
    // sideways, a hit from someone unseen turns the view while the walk goes on.
    const auto fight = combat(player, now);
    const auto knife_engagement =
        (fight.what == combat_plan::kind::knife && fight.enemy != 0) ||
        (fight.what == combat_plan::kind::rear && fight.weapon == 1 && fight.enemy != 0);
    auto_bonus().set_auto_knife_active(knife_engagement);
    // Publish the chosen part before the permission: the trigger runs on another worker, and must not see autofire
    // enabled with the previous fight's part for a pass. This is the part that owns the selected point, including a
    // multipoint cap or side.
    const auto engaging = fight.what == combat_plan::kind::engage;
    features::aimbot::publish_walk_autofire_parts(engaging ? fight.view.part : 0);
    // Publish the permission only once the plan is known. The trigger worker runs at 250 Hz and could otherwise see
    // a true published for the current tick before handle_knife() suspends the gun autofire for a knife engagement.
    features::aimbot::g_walk_autofire.store(cfg.autofire && !scope_wait && !knife_engagement,
                                            std::memory_order_release);
    // The knife leaves a different slot selected. Put the gun back as soon as the knife engagement is over, before
    // the next fight (and the auto scope) starts. The rear handler draws its own primary when that is its weapon.
    if (fight.what != combat_plan::kind::knife && fight.what != combat_plan::kind::rear)
        restore_knife_weapon(now);

    if (fight.what != combat_plan::kind::knife && fight.what != combat_plan::kind::rear)
    {
        m_knife_target = 0;
        m_knife_locked = false;
        (void)set_secondary_control(m_knife_attack, false);
    }
    m_scope_wanted = engaging;
    servo_scope(now);
    if (fight.what == combat_plan::kind::knife)
    {
        handle_knife(player, fight, now);
        return;
    }
    if (fight.what == combat_plan::kind::rear)
    {
        handle_rear(player, fight, now);
        return;
    }
    if (engaging)
    {
        note_idle(9);
        for (auto* control : {&m_forward, &m_back, &m_left, &m_right, &m_duck, &m_jump})
            (void)set_control(*control, false);
        look(player, fight.view, false, now);
        return;
    }
    if (fight.what == combat_plan::kind::evade)
    {
        press_towards(fight.wish, player.view.y);
        (void)set_control(m_jump, false);
        (void)set_control(m_duck, false);
        look(player, fight.view, false, now);
        return;
    }

    // A plan for the selected mode; hunt keeps re-planning as its target moves.
    std::shared_ptr<const path> route;
    {
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }
    const auto mode = cfg.mode;
    // Hunting replaces the route with one to a known enemy (re-planned as it moves); with nobody to hunt the route
    // source below takes over.
    if (cfg.hunt)
        (void)plan_hunt(*graph, player, now);
    {
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }
    if (!route)
    {
        if (now < m_next_plan)
        {
            release_keys();
            return;
        }
        const bool planned = (mode == config::general_profile::walk_bot::recorded && plan_recording(*graph, player)) ||
                             plan_random(*graph, player);
        if (!planned)
        {
            note_idle(5);
            m_next_plan = now + std::chrono::milliseconds(500);
            release_keys();
            return;
        }
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }

    scan_obstacles(*route, player, now);
    const auto view = drive(*graph, *route, player, now);
    bool route_dropped = false;
    {
        std::scoped_lock lock(m_route_mutex);
        route_dropped = m_path != route;
    }
    if (route_dropped)
    {
        // drive() gave up on a crossing and dropped the route; the next tick plans again. look() sends input, so it
        // stays outside the lock the render thread also takes.
        look(player, fight.what == combat_plan::kind::react ? fight.view : view, false, now);
        return;
    }

    const auto progress = m_progress.load(std::memory_order_acquire);
    // Arrived: remember the point (random walks avoid going back to it) and pick the next one on the next tick.
    if (progress >= route->back().distance - 24.0f && distance_2d(player.origin, route->back().position) < 40.0f)
    {
        m_recent.push_back(route->back().position);
        if (m_recent.size() > 6)
            m_recent.pop_front();
        release_keys();
        reset_route();
        return;
    }
    const bool pressing = m_forward.pressed || m_back.pressed || m_left.pressed || m_right.pressed;
    watch_progress(*graph, *route, player, now, pressing);
    auto_reload(player, route.get(), now);
    look(player, fight.what == combat_plan::kind::react ? fight.view : check_corners(*graph, player, view, now), false,
         now);
}
} // namespace features::misc

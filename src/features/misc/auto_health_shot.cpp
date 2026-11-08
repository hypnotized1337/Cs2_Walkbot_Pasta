#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <foundation/identity.hpp>
#include <features/misc/walk_bot_logic.hpp>
#include <features/aimbot/aimbot.hpp>
#include <config/settings.hpp>
#include <game/input/bindings.hpp>
#include <game/input/action_state.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <render/presentation.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

namespace features::misc
{
namespace
{
// Auto Health Shot is the Auto Bonus Weapon pattern: press the key for one game input sample, release it, and try
// again after the game has had time to act on it. The syringe needs a real press and release, and a bind that does not
// draw it (no item, wrong key) must not be typed forever.
constexpr auto k_hold = std::chrono::milliseconds(40);
constexpr auto k_retry = std::chrono::milliseconds(100);
constexpr auto k_draw_interval = std::chrono::milliseconds(400);
constexpr auto k_restore_interval = std::chrono::milliseconds(500);
constexpr auto k_restore_settle = std::chrono::milliseconds(250); // the game switches back by itself first
// The health shot plays a deploy animation; pressing the attack as soon as the syringe appears lands on the draw
// instead of the use. Let the syringe settle in hand first.
constexpr auto k_deploy_delay = std::chrono::milliseconds(600);
// Safety cap while the attack is held. The syringe is consumed (and leaves the hand) well before this, which ends the
// hold early; the cap only makes sure a stuck use cannot hold the mouse button forever.
constexpr auto k_injection_cap = std::chrono::seconds(4);
constexpr auto k_success_delay = std::chrono::milliseconds(1500);
constexpr auto k_cooldown = std::chrono::seconds(5);
// How long the aimbot's last engagement still counts as "aiming at someone". Its tick publishes a fresh timestamp
// every time it holds a target or fires; once the tick stops (no aim key, no target, trigger released) the timestamp
// ages out instead of leaving the last selected pawn behind.
constexpr auto k_engage_window = std::chrono::milliseconds(250);
constexpr int k_draw_attempts = 4;
constexpr int k_primary_attempts = 2; // taps at slot 1 before a sidearm-only restore is accepted
constexpr int k_restore_attempts = 4; // two at slot 1, then two at slot 2
constexpr std::uint16_t k_primary_key{'1'};
constexpr std::uint16_t k_pistol_key{'2'};

// Input may only go to a focused game with no Vesta menu, chat or console in the way. The combat worker already
// gates on most of this, but the injection holds the primary attack for two seconds and must never click the desktop,
// another application or the Vesta menu.
[[nodiscard]] bool input_window_ready()
{
    if (!render::combat_input_ready() || render::menu_open() || game::input_bindings().text_entry_active())
        return false;
    const auto game = ::FindWindowW(nullptr, L"Counter-Strike 2");
    return game && ::GetAncestor(::GetForegroundWindow(), GA_ROOT) == game;
}

// The local player's position, read the way the walk bot's knife range reads it: pawn -> game scene node ->
// m_vecAbsOrigin. The sampled player list cannot supply it: the world sampler collects only the *other* players, so
// the local pawn is never in it and looking it up there always failed.
[[nodiscard]] bool read_local_origin(foundation::vec3& origin)
{
    const auto pawn = game::local_player().pawn();
    if (!pawn)
        return false;
    const auto& process = game::bound_memory().process;
    const auto node = process.load<std::uintptr_t>(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id));
    if (!node || !process.copy(node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id), &origin, sizeof(origin)))
        return false;
    return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
}

// Distance from the local player to the nearest living enemy, or a negative value when there is none. The use waits
// while an enemy is this close: drawing and injecting keep the hands and the weapon slot busy for well over a second,
// which is the worst moment for someone to come round a corner. Teammates do not count, or a player moving with their
// team could never use a health shot at all.
[[nodiscard]] float nearest_enemy_distance()
{
    const auto players = game::world().players();
    if (!players)
        return -1.0f;
    auto origin = foundation::vec3{};
    if (!read_local_origin(origin))
        return -1.0f;
    const auto self = game::local_player().pawn();
    auto nearest = -1.0f;
    for (const auto& player : *players)
    {
        if (!player.pawn || player.pawn == self || player.health <= 0 || !game::local_player().is_enemy(player.team))
            continue;
        const auto distance = player.origin.distance(origin);
        if (nearest < 0.0f || distance < nearest)
            nearest = distance;
    }
    return nearest;
}

[[nodiscard]] bool send_binding(const game::input_binding& binding, const bool down)
{
    using platform::windows::pointer_action;
    switch (binding.device)
    {
        case game::input_device::keyboard:
            return platform::windows::bound_input().key(binding.virtual_key, down);
        case game::input_device::mouse_primary:
            return platform::windows::bound_input().pointer(0, 0, down ? pointer_action::primary_down
                                                                        : pointer_action::primary_up);
        case game::input_device::mouse_secondary:
            return platform::windows::bound_input().pointer(0, 0, down ? pointer_action::secondary_down
                                                                        : pointer_action::secondary_up);
        case game::input_device::mouse_middle:
            return platform::windows::bound_input().pointer(0, 0, down ? pointer_action::middle_down
                                                                        : pointer_action::middle_up);
        case game::input_device::mouse_auxiliary1:
            return platform::windows::bound_input().pointer(0, 0, down ? pointer_action::auxiliary1_down
                                                                        : pointer_action::auxiliary1_up);
        case game::input_device::mouse_auxiliary2:
            return platform::windows::bound_input().pointer(0, 0, down ? pointer_action::auxiliary2_down
                                                                        : pointer_action::auxiliary2_up);
        default:
            return false;
    }
}
} // namespace

// A keyboard control bound to the configured key. The key can change in the menu; the old one is released before the
// binding is replaced, exactly like a player letting go before pressing another key.
bool auto_health_shot_t::send_key(owned_control& control, const std::uint16_t key, const bool pressed)
{
    if (control.binding.device != game::input_device::keyboard || control.binding.virtual_key != key)
    {
        if (control.pressed && !input_lifecycle::transition(control, false, send_binding))
            return false;
        control = {};
        control.binding.device = game::input_device::keyboard;
        control.binding.virtual_key = key;
    }
    return input_lifecycle::transition(control, pressed, send_binding);
}

// The injection is the game's primary attack, so resolve it like the walk bot does: a mouse button in the default binds
// and any key the game accepts.
bool auto_health_shot_t::send_attack(const bool pressed)
{
    if (!m_attack.binding)
        m_attack.binding = game::input_bindings().resolve(game::input_action::attack);
    if (!m_attack.binding)
        return false;
    return input_lifecycle::transition(m_attack, pressed, send_binding);
}

bool auto_health_shot_t::release_all()
{
    bool complete = true;
    for (auto* control : {&m_key, &m_slot, &m_attack})
    {
        if (control->pressed && !input_lifecycle::transition(*control, false, send_binding))
            complete = false;
        if (!control->pressed)
            *control = {};
    }
    return complete;
}

// The user asked for a simple gate: only when the bot is not currently shooting or aiming at anyone. The aimbot
// publishes view_engaged_ticks on every tick that holds a target or fires, so a stale target from an old fight cannot
// keep this blocked. g_walk_autofire is only the walk bot's permission to fire, not an engagement; the parts flag is
// set only while it actually has an enemy in sight, and autofire_parts == 0 while it is merely walking.
auto_health_shot_t::block auto_health_shot_t::combat_block(const std::chrono::steady_clock::time_point now)
{
    const auto engaged_ticks = features::aimbot::aim().view_engaged_ticks();
    if (engaged_ticks != 0)
    {
        const std::chrono::steady_clock::time_point engaged_at{std::chrono::steady_clock::duration{engaged_ticks}};
        if (engaged_at <= now && now - engaged_at <= k_engage_window)
            return block::aiming;
    }
    if (features::aimbot::g_walk_autofire_parts.load(std::memory_order_acquire) != 0)
        return block::autofire;
    if (platform::windows::bound_input().injected_primary_down() ||
        game::physical_action_down(game::input_action::attack, platform::windows::bound_input()))
        return block::attack_held;
    return block::none;
}

void auto_health_shot_t::sync_auto_bonus_priority() noexcept
{
    // Auto Bonus taps its pickup key and switches to the bonus weapon. While the health shot is drawing, injecting or
    // restoring, that tap would swap the weapon out from under the syringe, so claim the slot for the whole use.
    auto_bonus().set_auto_health_shot_active(m_phase != phase::idle);
}

void auto_health_shot_t::publish_debug(const block reason) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    auto deadline = m_next_action;
    if (reason == block::cooldown)
        deadline = m_retry_at;
    else if (reason == block::recent_damage)
        deadline = m_damage_until;
    else if (m_phase == phase::injecting)
        deadline = m_attack.pressed ? m_injection_until : m_inject_at;
    const auto left = deadline > now ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
                                     : 0;
    m_debug_phase.store(static_cast<std::uint8_t>(m_phase), std::memory_order_relaxed);
    m_debug_block.store(static_cast<std::uint8_t>(reason), std::memory_order_relaxed);
    m_debug_attempts.store(m_attempts, std::memory_order_relaxed);
    m_debug_attack.store(m_attack.pressed, std::memory_order_relaxed);
    m_debug_key.store(m_key.pressed || m_slot.pressed, std::memory_order_relaxed);
    m_debug_near.store(static_cast<int>(std::lround(m_near_distance)), std::memory_order_relaxed);
    m_debug_next_ms.store(static_cast<int>(left), std::memory_order_relaxed);
}

void auto_health_shot_t::reset()
{
    (void)release_all();
    m_phase = phase::idle;
    m_attempts = 0;
    m_key_started = {};
    m_slot_started = {};
    m_next_action = {};
    m_inject_at = {};
    m_injection_until = {};
    sync_auto_bonus_priority();
    // The worker also resets this when the game, the overlay input or the menu takes the tick away. Keep the debug
    // truthful instead of leaving the last combat/phase state on screen while nothing is running.
    publish_debug(block::not_running);
}

void auto_health_shot_t::tick()
{
    const auto now = std::chrono::steady_clock::now();
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
    {
        reset();
        publish_debug(block::disabled);
        return;
    }
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto health = game::local_player().health();
    const auto held = game::local_player().weapon_type();
    const auto syringe = held == game::rules::healthshot;
    m_debug_health.store(health, std::memory_order_relaxed);
    m_debug_syringe.store(syringe, std::memory_order_relaxed);

    // Any decrease is damage taken; remember when it happened so the use waits the configured window out. This runs
    // even while the feature is off so the history is not lost when the menu is opened. The dead branch clears it.
    if (m_last_health >= 0 && health < m_last_health)
        m_damage_at = now;
    m_last_health = health;

    if (!cfg.auto_health_shot)
    {
        reset();
        publish_debug(block::disabled);
        return;
    }
    const auto key = std::clamp(cfg.health_shot_key, 0, 255);
    if (key == 0)
    {
        reset();
        publish_debug(block::no_key);
        return;
    }
    // A dead player, or a spectator whose camera sits in someone else's head, has no use for a health shot.
    if (!game::local_player().alive() || game::local_player().spectated_pawn() != 0 || health <= 0)
    {
        reset();
        // A respawn is not recent damage: drop the history while there is no pawn to heal.
        m_last_health = 0;
        m_damage_at = {};
        m_damage_until = {};
        publish_debug(block::dead);
        return;
    }
    // Chat, the console, the Vesta menu or another window owns the input right now.
    if (!input_window_ready())
    {
        if (release_all())
        {
            m_phase = phase::idle;
            m_attempts = 0;
        }
        sync_auto_bonus_priority();
        publish_debug(block::input_blocked);
        return;
    }

    // Auto Knife owns the weapon and the attack button for its whole engagement. Drawing a syringe mid-stab (or
    // restoring a firearm while the knife chase runs) breaks both automations, so stay out completely and abort a use
    // that was already in progress.
    if (auto_bonus().auto_knife_active())
    {
        if (release_all())
        {
            m_phase = phase::idle;
            m_attempts = 0;
            m_inject_at = {};
            m_injection_until = {};
        }
        sync_auto_bonus_priority();
        publish_debug(block::auto_knife);
        return;
    }

    const auto threshold = std::clamp(cfg.health_shot_health, 1, 99);
    const auto damage_window = std::chrono::milliseconds(std::clamp(cfg.health_shot_damage_ms, 0, 15000));
    const auto near_limit = static_cast<float>(std::clamp(cfg.health_shot_near_distance, 0, 2000));
    auto blocked = block::none;

    if (m_phase == phase::idle)
    {
        // A release that did not take is retried before anything new is pressed.
        if (!release_all())
        {
            sync_auto_bonus_priority();
            publish_debug(blocked);
            return;
        }
        if (health >= threshold)
            blocked = block::health_ok;
        else if (now < m_retry_at)
            blocked = block::cooldown;
        else if (damage_window.count() > 0 && m_damage_at.time_since_epoch().count() != 0 &&
                 now < m_damage_at + damage_window)
        {
            m_damage_until = m_damage_at + damage_window;
            blocked = block::recent_damage;
        }
        // `near` is a legacy Windows macro, hence the longer name. A disabled gate asks for no distance at all.
        else if (const auto near_distance = near_limit > 0.0f ? nearest_enemy_distance() : -1.0f;
                 near_distance >= 0.0f && near_distance <= near_limit)
        {
            m_near_distance = near_distance;
            blocked = block::player_near;
        }
        else if (const auto reason = combat_block(now); reason != block::none)
            blocked = reason;
        else if (auto_bonus().key_pressed())
        {
            // A bonus pickup tap is still down. Starting the draw now would put two weapon-switch keys in flight; the
            // tap lasts at most one Auto Bonus hold, so just wait for the next tick.
        }
        else
        {
            m_phase = syringe ? phase::injecting : phase::drawing;
            m_attempts = 0;
            m_next_action = now;
            m_inject_at = syringe ? now + k_deploy_delay : std::chrono::steady_clock::time_point{};
            m_injection_until = {};
        }
    }

    switch (m_phase)
    {
        case phase::idle:
            break;
        case phase::drawing:
        {
            if (syringe)
            {
                // In hand: let go of the bind before the injection owns the mouse, and give the deploy animation
                // time to finish before the attack.
                if (m_key.pressed && !input_lifecycle::transition(m_key, false, send_binding))
                    break;
                m_phase = phase::injecting;
                m_attempts = 0;
                m_inject_at = now + k_deploy_delay;
                m_injection_until = {};
                m_next_action = now;
                break;
            }
            if (m_key.pressed)
            {
                if (now - m_key_started >= k_hold &&
                    input_lifecycle::transition(m_key, false, send_binding))
                    m_next_action = now + k_draw_interval;
                break;
            }
            if (now < m_next_action)
                break;
            if (m_attempts >= k_draw_attempts)
            {
                // The bind did nothing: no health shot in the inventory, or the wrong key. Do not type it forever.
                m_phase = phase::idle;
                m_attempts = 0;
                m_retry_at = now + k_cooldown;
                blocked = block::no_syringe;
                break;
            }
            if (send_key(m_key, static_cast<std::uint16_t>(key), true))
            {
                ++m_attempts;
                m_key_started = now;
            }
            else
                m_next_action = now + k_retry;
            break;
        }
        case phase::injecting:
        {
            if (!syringe)
            {
                // The game took the syringe away by itself (the injection finished or the slot changed): draw a
                // firearm.
                if (m_attack.pressed && !input_lifecycle::transition(m_attack, false, send_binding))
                    break;
                m_phase = phase::restoring;
                m_attempts = 0;
                m_next_action = now + k_restore_settle;
                break;
            }
            if (!m_attack.pressed)
            {
                if (now < m_inject_at)
                    break;
                if (send_attack(true))
                    m_injection_until = now + k_injection_cap;
                else
                {
                    m_phase = phase::restoring;
                    m_attempts = 0;
                    m_next_action = now + k_restore_settle;
                }
                break;
            }
            // Held until the syringe is consumed (the branch above) or the safety cap expires.
            if (now < m_injection_until)
                break;
            if (!input_lifecycle::transition(m_attack, false, send_binding))
                break;
            m_phase = phase::restoring;
            m_attempts = 0;
            m_next_action = now + k_restore_settle;
            break;
        }
        case phase::restoring:
        {
            // The game normally switches back to the previous weapon by itself. Slot 1 is asked for first; a sidearm
            // already in hand is accepted after both primary taps, so a player without a primary is not left waiting.
            const bool main = walk_logic::main_weapon_type(held);
            const bool firearm = game::rules::is_firearm(held);
            if (main || (firearm && m_attempts >= k_primary_attempts))
            {
                if (release_all())
                {
                    m_phase = phase::idle;
                    m_attempts = 0;
                    m_next_action = {};
                    m_retry_at = now + k_success_delay;
                }
                break;
            }
            if (m_slot.pressed)
            {
                if (now - m_slot_started >= k_hold &&
                    input_lifecycle::transition(m_slot, false, send_binding))
                    m_next_action = now + k_restore_interval;
                break;
            }
            if (m_attempts >= k_restore_attempts)
            {
                if (firearm)
                {
                    if (release_all())
                    {
                        m_phase = phase::idle;
                        m_attempts = 0;
                        m_next_action = {};
                        m_retry_at = now + k_success_delay;
                    }
                }
                else
                {
                    // No slot key took: stop pressing and try the whole use later.
                    m_phase = phase::idle;
                    m_attempts = 0;
                    m_retry_at = now + k_cooldown;
                    blocked = block::no_firearm;
                }
                break;
            }
            if (now < m_next_action)
                break;
            const auto slot_key = m_attempts < k_primary_attempts ? k_primary_key : k_pistol_key;
            if (send_key(m_slot, slot_key, true))
            {
                ++m_attempts;
                m_slot_started = now;
            }
            else
                m_next_action = now + k_retry;
            break;
        }
    }

    sync_auto_bonus_priority();
    publish_debug(blocked);
}

void auto_health_shot_t::on_render(render::draw::draw_list& draw_list)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!cfg.health_shot_debug)
        return;
    const auto* font = render::fonts().menu_regular_12;
    if (!font || !font->im_font)
        return;

    const auto current_phase = static_cast<phase>(m_debug_phase.load(std::memory_order_relaxed));
    const auto current_block = static_cast<block>(m_debug_block.load(std::memory_order_relaxed));
    const auto health = m_debug_health.load(std::memory_order_relaxed);
    const auto attempts = m_debug_attempts.load(std::memory_order_relaxed);
    const auto syringe = m_debug_syringe.load(std::memory_order_relaxed);
    const auto attack = m_debug_attack.load(std::memory_order_relaxed);
    const auto key_down = m_debug_key.load(std::memory_order_relaxed);
    const auto next_ms = m_debug_next_ms.load(std::memory_order_relaxed);
    const auto nearest = m_debug_near.load(std::memory_order_relaxed);
    const auto threshold = std::clamp(cfg.health_shot_health, 1, 99);
    const auto near_limit = static_cast<float>(std::clamp(cfg.health_shot_near_distance, 0, 2000));

    const char* phase_name = "IDLE";
    switch (current_phase)
    {
        case phase::idle:
            break;
        case phase::drawing:
            phase_name = "DRAWING";
            break;
        case phase::injecting:
            phase_name = "INJECTING";
            break;
        case phase::restoring:
            phase_name = "RESTORING";
            break;
    }

    std::string status;
    switch (current_block)
    {
        case block::disabled:
            status = "feature off";
            break;
        case block::no_key:
            status = "no bind key";
            break;
        case block::dead:
            status = "not alive";
            break;
        case block::health_ok:
            status = std::format("health {} >= {}", health, threshold);
            break;
        case block::auto_knife:
            status = "auto knife engaged";
            break;
        case block::recent_damage:
            status = std::format("recent damage, wait {:.1f}s", static_cast<float>(next_ms) / 1000.0f);
            break;
        case block::player_near:
            status = std::format("enemy {:.0f} u away (limit {:.0f} u)", static_cast<float>(nearest), near_limit);
            break;
        case block::aiming:
            status = "aimbot or trigger engaged";
            break;
        case block::autofire:
            status = "walk-bot autofire engaged";
            break;
        case block::attack_held:
            status = "attack button held";
            break;
        case block::cooldown:
            status = std::format("cooldown {:.1f}s", static_cast<float>(next_ms) / 1000.0f);
            break;
        case block::no_syringe:
            status = "bind did not draw the syringe";
            break;
        case block::no_firearm:
            status = "no firearm after the use";
            break;
        case block::input_blocked:
            status = "menu, chat or window has input";
            break;
        case block::not_running:
            status = "not running";
            break;
        case block::none:
            if (current_phase == phase::injecting)
            {
                if (attack)
                    status = std::format("holding attack {:.1f}s left", static_cast<float>(next_ms) / 1000.0f);
                else
                    status = std::format("deploying {:.1f}s", static_cast<float>(next_ms) / 1000.0f);
            }
            else if (current_phase != phase::idle)
                status = std::format("next action {:.1f}s", static_cast<float>(next_ms) / 1000.0f);
            else
                status = "ready";
            break;
    }

    const std::array<std::string, 4> lines{
        std::format("AUTO HEALTH SHOT  [{}]", phase_name),
        std::format("HP {}  below {}  bind {}  no enemy within {}", health, threshold,
                    std::clamp(cfg.health_shot_key, 0, 255),
                    near_limit > 0.0f ? std::format("{:.0f} u", near_limit) : std::string("off")),
        status,
        std::format("syringe {}  lmb {}  key {}  presses {}/{}", syringe ? "YES" : "NO", attack ? "DOWN" : "up",
                    key_down ? "DOWN" : "up", attempts, k_draw_attempts),
    };

    constexpr float line_height = 15.0f;
    constexpr render::draw::rgba text_color{225, 232, 245, 245};
    constexpr render::draw::rgba detail_color{185, 196, 215, 230};
    constexpr render::draw::rgba ready_color{115, 235, 150, 255};
    constexpr render::draw::rgba busy_color{255, 196, 90, 230};
    constexpr render::draw::rgba blocked_color{255, 115, 120, 255};

    const auto accent = !cfg.auto_health_shot                      ? blocked_color
                        : current_block != block::none             ? blocked_color
                        : current_phase == phase::injecting        ? ready_color
                        : current_phase != phase::idle             ? busy_color
                                                                   : ready_color;
    auto width = 0.0f;
    for (const auto& line : lines)
        width = std::max(width, render::draw::measure_text(line, font).first);
    width = std::ceil(width);
    const auto panel_height = line_height * static_cast<float>(lines.size()) + 10.0f;
    constexpr float x = 14.0f;
    constexpr float y = 64.0f;
    draw_list.add_rect_filled(x - 6.0f, y - 5.0f, width + 12.0f, panel_height, render::draw::rgba{6, 9, 14, 190});
    draw_list.add_rect_filled(x - 6.0f, y - 5.0f, 2.0f, panel_height, accent);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto color = i == 0 ? text_color : i == 2 ? accent : detail_color;
        draw_list.add_text(x, y + line_height * static_cast<float>(i), lines[i], font, color,
                           render::draw::text_style::shadowed);
    }
}

} // namespace features::misc

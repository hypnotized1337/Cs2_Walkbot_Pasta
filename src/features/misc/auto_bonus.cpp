#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/input/bindings.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>

#include <algorithm>
#include <chrono>

namespace features::misc
{
namespace
{
// The game samples keyboard input at its own tick rate; a 40 ms hold is long enough for one tap to register.
constexpr auto k_hold = std::chrono::milliseconds(40);
constexpr auto k_retry = std::chrono::milliseconds(100);

[[nodiscard]] int weapon_owner_rank(const auto_bonus_t::weapon_owner owner) noexcept
{
    switch (owner)
    {
        case auto_bonus_t::weapon_owner::none:
            return 0;
        case auto_bonus_t::weapon_owner::bonus:
            return 1;
        case auto_bonus_t::weapon_owner::health_shot:
            return 2;
        case auto_bonus_t::weapon_owner::knife:
            return 3;
        case auto_bonus_t::weapon_owner::auto_switch:
            return 4;
    }
    return 0;
}
} // namespace

void auto_bonus_t::set_auto_knife_active(const bool active) noexcept
{
    if (active)
        (void)claim(weapon_owner::knife);
    else
        release(weapon_owner::knife);
}

void auto_bonus_t::set_auto_health_shot_active(const bool active) noexcept
{
    if (active)
        (void)claim(weapon_owner::health_shot);
    else
        release(weapon_owner::health_shot);
}

void auto_bonus_t::set_auto_switch_active(const bool active) noexcept
{
    if (active)
        (void)claim(weapon_owner::auto_switch);
    else
        release(weapon_owner::auto_switch);
}

// A pickup tap is a blind press of the pickup key. The game acts on it a frame or two later, so the active weapon is
// sampled for a short window after the key is released. A firearm in hand that was not there when the tap began is the
// bonus weapon. Reading it back lets Auto Bonus stop tapping while that weapon is empty or reloading, instead of
// switching the slot out from under its own reload.
void auto_bonus_t::observe_bonus_weapon(const std::chrono::steady_clock::time_point now)
{
    if (m_tap_watch_until == std::chrono::steady_clock::time_point{})
        return;
    if (now >= m_tap_watch_until)
    {
        m_tap_watch_until = {};
        return;
    }
    const auto current = game::local_player().weapon();
    if (!current || current == m_tap_weapon || !game::rules::is_firearm(game::local_player().weapon_type()))
        return;
    m_bonus_weapon = current;
    m_tap_watch_until = {};
}

bool auto_bonus_t::bonus_weapon_needs_time() const
{
    const auto current = game::local_player().weapon();
    if (!m_bonus_weapon || current != m_bonus_weapon)
        return false;
    const auto& process = game::bound_memory().process;
    const auto clip = process.load<std::int32_t>(current + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id));
    return clip <= 0 || process.load<bool>(current + SCHEMA("C_CSWeaponBase", "m_bInReload"_id));
}

bool auto_bonus_t::claim(const weapon_owner next) noexcept
{
    auto current = m_owner.load(std::memory_order_acquire);
    const auto next_rank = weapon_owner_rank(next);
    for (;;)
    {
        if (next_rank < weapon_owner_rank(current))
            return false;
        if (current == next)
            return true;
        if (m_owner.compare_exchange_weak(current, next, std::memory_order_release,
                                          std::memory_order_acquire))
            return true;
    }
}

void auto_bonus_t::release(const weapon_owner expected) noexcept
{
    auto current = expected;
    (void)m_owner.compare_exchange_strong(current, weapon_owner::none, std::memory_order_release,
                                          std::memory_order_acquire);
}

bool auto_bonus_t::send_key(const bool pressed)
{
    if (!m_key.binding)
        return false;
    if (!input_lifecycle::transition(
            m_key, pressed,
            [](const game::input_binding& binding, const bool down)
            {
                return binding.device == game::input_device::keyboard &&
                       platform::windows::bound_input().key(binding.virtual_key, down);
            }))
        return false;
    if (pressed)
        (void)claim(weapon_owner::bonus);
    else
        release(weapon_owner::bonus);
    return true;
}

void auto_bonus_t::reset()
{
    if (m_key.pressed)
        (void)send_key(false);
    if (!m_key.pressed)
    {
        m_key = {};
        release(weapon_owner::bonus);
    }
    m_next_tap = {};
    m_press_started = {};
    m_tap_weapon = 0;
    m_tap_watch_until = {};
    m_bonus_weapon = 0;
}

void auto_bonus_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.m_walk_bot.enabled || !snapshot->general.m_auto_bonus.enabled)
    {
        reset();
        return;
    }

    const auto& cfg = snapshot->general.m_auto_bonus;
    const auto key = std::clamp(cfg.key, 0, 255);
    if (key == 0)
    {
        reset();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    observe_bonus_weapon(now);
    const auto current_owner = owner();
    const auto auto_knife_active = current_owner == weapon_owner::knife;
    const auto health_shot_active = current_owner == weapon_owner::health_shot;
    if (m_key.binding.device != game::input_device::keyboard ||
        m_key.binding.virtual_key != static_cast<std::uint16_t>(key))
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_key = {};
        m_key.binding.device = game::input_device::keyboard;
        m_key.binding.virtual_key = static_cast<std::uint16_t>(key);
        m_next_tap = now;
    }

    // Auto Weapon Switch owns the slot from its release handshake until it switches back. A pickup tap now would put
    // a second slot key in flight, so stop the current tap and wait for the owner to clear.
    if (current_owner == weapon_owner::auto_switch)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        return;
    }

    // Auto Knife owns the weapon slot. Stop the current tap and keep the next one deferred until Auto Knife clears
    // the owner (the green flag). The 100 ms retry delay gives the knife's restore or the next weapon draw time to
    // settle before a pickup key can switch the slot again.
    if (auto_knife_active)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        m_auto_knife_was_active = true;
        return;
    }
    if (m_auto_knife_was_active)
    {
        // The hold can start and end between two Auto Bonus ticks; the settle still applies instead of tapping now.
        m_auto_knife_was_active = false;
        m_next_tap = std::max(m_next_tap, now + k_retry);
    }

    // The health shot is drawing, injecting or restoring the weapon. A pickup tap here can switch the slot to the
    // bonus weapon and cancel the draw or the injection, so release the current tap and defer until it is idle.
    if (health_shot_active)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        m_auto_health_shot_was_active = true;
        return;
    }
    if (m_auto_health_shot_was_active)
    {
        m_auto_health_shot_was_active = false;
        m_next_tap = std::max(m_next_tap, now + k_retry);
    }

    // The pickup tap already produced a firearm. While that weapon is empty or reloading, another pickup tap can
    // switch the slot away from the reload and cancel it. Wait for the weapon to be usable again.
    if (bonus_weapon_needs_time())
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        return;
    }

    if (m_key.pressed)
    {
        if (now - m_press_started >= k_hold && send_key(false))
        {
            m_tap_watch_until = now + std::chrono::milliseconds(750);
            m_next_tap = now + std::chrono::milliseconds(std::clamp(cfg.interval_ms, 250, 5000));
        }
        return;
    }
    if (now < m_next_tap)
        return;
    if (game::input_bindings().text_entry_active())
    {
        m_next_tap = now + k_retry;
        return;
    }
    if (send_key(true))
    {
        m_press_started = now;
        m_tap_weapon = game::local_player().weapon();
        m_tap_watch_until = {};
    }
    else
        m_next_tap = now + k_retry;
}

} // namespace features::misc

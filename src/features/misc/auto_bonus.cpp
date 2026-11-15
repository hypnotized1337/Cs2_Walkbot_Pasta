#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <config/settings.hpp>
#include <game/input/bindings.hpp>
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

    if (m_key.pressed)
    {
        if (now - m_press_started >= k_hold && send_key(false))
        {
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
        m_press_started = now;
    else
        m_next_tap = now + k_retry;
}

} // namespace features::misc

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
}

bool auto_bonus_t::send_key(const bool pressed)
{
    if (!m_key.binding)
        return false;
    return input_lifecycle::transition(
        m_key, pressed,
        [](const game::input_binding& binding, const bool down)
        {
            return binding.device == game::input_device::keyboard &&
                   platform::windows::bound_input().key(binding.virtual_key, down);
        });
}

void auto_bonus_t::reset()
{
    if (m_key.pressed)
        (void)send_key(false);
    if (!m_key.pressed)
        m_key = {};
    m_next_tap = {};
    m_press_started = {};
}

void auto_bonus_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.m_auto_bonus.enabled)
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
    const auto auto_knife_active = m_auto_knife_active.load(std::memory_order_acquire);
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
    // this flag (the green flag). The 100 ms retry delay gives the knife's restore or the next weapon draw time to
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

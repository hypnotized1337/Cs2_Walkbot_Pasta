#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <game/state/constants.hpp>
#include <config/settings.hpp>
#include <render/presentation.hpp>
#include <game/state/services.hpp>
#include <config/misc.hpp>
#include <platform/windows/input.hpp>
#include <game/memory/bound_services.hpp>
#include <game/input/bindings.hpp>
#include <diagnostics/journal.hpp>
#include <features/misc/misc.hpp>
#include <features/misc/match_flow.hpp>

// Ported from Vesta (https://github.com/Read1dno/vesta), Apache-2.0. Only the Bunny Hop branch of Vesta's shared
// Bunny/Edge state machine is kept; Edge Jump and its collision prediction are intentionally absent.

namespace features::misc
{
namespace
{
[[nodiscard]] bool game_has_input_focus() noexcept
{
    const auto foreground = ::GetForegroundWindow();
    const auto root = foreground ? ::GetAncestor(foreground, GA_ROOT) : nullptr;
    DWORD process_id{};
    if (root)
        ::GetWindowThreadProcessId(root, &process_id);
    return process_id != 0 && process_id == game::bound_memory().process.process_id();
}
} // namespace

void bhop_t::reset_sample() noexcept
{
    m_last_simulation_time = -1.0f;
    m_last_bunny_tap = {};
}

void bhop_t::release(const bool restore_physical_jump) noexcept
{
    const auto jump_key = m_jump_key;
    const auto gate_was_requested = m_gate_requested;
    m_gate_requested = false;

    // key_gate_held() describes the physical key the gate swallowed. Read it before clearing the gate, then put the
    // player's own press back only when the caller says the game is in a state where input belongs to the player.
    const auto restore = restore_physical_jump && gate_was_requested && jump_key != 0 &&
                         platform::windows::bound_input().key_gate_held();
    platform::windows::bound_input().set_key_gate(0, false);
    if (restore)
        platform::windows::bound_input().key(jump_key, true);
    reset_sample();
}

void bhop_t::tick()
{
    const auto now = std::chrono::steady_clock::now();
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
    {
        m_jump_key = 0;
        m_reported_jump_key = -1;
        m_rules_known = false;
        m_bunny_pawn = 0;
        release(false);
        return;
    }

    const auto& bunny = snapshot->general.m_bunny_hop;
    const auto jump_binding = game::input_bindings().resolve(game::input_action::jump);
    const auto resolved_key =
        jump_binding.device == game::input_device::keyboard ? jump_binding.virtual_key : std::uint16_t{};

    if (m_reported_jump_key != resolved_key)
    {
        m_reported_jump_key = resolved_key;
        diagnostics::emit(resolved_key ? diagnostics::event::bhop_binding_available
                                       : diagnostics::event::bhop_binding_unavailable);
    }

    const auto game_active = game_has_input_focus();
    const auto menu_open = render::menu_open();
    const auto text_entry_active = game::input_bindings().text_entry_active();
    const auto pawn = game::local_player().pawn();
    const auto player_ready = pawn && game::local_player().alive();
    // Restoring a swallowed jump into a menu, another window or a dead player would send input where it does not
    // belong. Only a live, focused game with the menu closed may receive that restoration.
    const auto restore_allowed = game_active && !menu_open && !text_entry_active && player_ready;

    if (m_jump_key != resolved_key)
    {
        release(restore_allowed);
        platform::windows::bound_input().set_key_gate(0, false);
        m_jump_key = resolved_key;
    }

    if (pawn != m_bunny_pawn)
    {
        release(restore_allowed);
        m_bunny_pawn = pawn;
    }

    if (!bunny.enabled)
    {
        release(restore_allowed);
        return;
    }

    // Walk Bot owns movement while it is enabled. Bunny must not gate or tap jump at the same time.
    if (snapshot->general.m_walk_bot.enabled)
    {
        release(false);
        return;
    }

    if (!game_active || menu_open || text_entry_active || !player_ready || m_jump_key == 0)
    {
        release(false);
        return;
    }

    if (now >= m_next_rules_check)
    {
        m_next_rules_check = now + std::chrono::milliseconds(250);
        const auto rules = read_game_rules();
        if (rules.valid)
        {
            const auto changed = m_rules_known && (rules.object != m_rules_object || rules.rounds != m_rules_rounds);
            m_rules_object = rules.object;
            m_rules_rounds = rules.rounds;
            m_rules_known = true;
            if (changed)
            {
                release(restore_allowed);
                return;
            }
        }
        else
        {
            m_rules_known = false;
        }
    }

    const auto activation_key = static_cast<int>(std::clamp(bunny.activation_key, 0, 255));
    const auto activation_is_jump = activation_key != 0 && activation_key == m_jump_key;
    const auto activation_held =
        input_lifecycle::activation_held(activation_key, [](const std::uint16_t key)
                                         { return platform::windows::bound_input().physical_key_down(key); });
    const auto gate_requested = activation_is_jump || activation_held;

    if (m_gate_requested != gate_requested)
    {
        if (gate_requested)
        {
            platform::windows::bound_input().set_key_gate(m_jump_key, true);
            m_gate_requested = true;
        }
        else
        {
            release(restore_allowed);
        }
    }

    if (!gate_requested)
    {
        reset_sample();
        return;
    }

    const auto bunny_active = platform::windows::bound_input().key_gate_ready() &&
                              (activation_is_jump ? platform::windows::bound_input().key_gate_held()
                                                  : activation_held);
    if (!bunny_active)
    {
        reset_sample();
        return;
    }

    std::uint32_t flags{};
    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id), &flags, sizeof(flags)))
    {
        release(restore_allowed);
        return;
    }
    if ((flags & 1u) == 0u)
        return;

    float simulation_time{};
    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_BaseEntity", "m_flSimulationTime"_id), &simulation_time,
                                           sizeof(simulation_time)) ||
        !std::isfinite(simulation_time) || simulation_time == m_last_simulation_time ||
        now - m_last_bunny_tap <= std::chrono::milliseconds(16))
    {
        return;
    }

    const std::array tap{
        platform::windows::input_gateway::key_transition{m_jump_key, true},
        platform::windows::input_gateway::key_transition{m_jump_key, false},
    };
    if (platform::windows::bound_input().keys(tap))
    {
        m_last_simulation_time = simulation_time;
        m_last_bunny_tap = now;
    }
}
} // namespace features::misc

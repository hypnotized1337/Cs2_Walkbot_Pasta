#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <features/misc/match_flow.hpp>
#include <features/misc/misc.hpp>
#include <features/aimbot/aimbot.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/log_directory.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <platform/windows/storage.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace features::misc
{
using namespace walk_detail;

namespace
{
constexpr auto k_switch_ack_timeout = std::chrono::milliseconds(50);
constexpr auto k_switch_interval = std::chrono::milliseconds(350);
constexpr auto k_switch_verify_timeout = std::chrono::milliseconds(300);
constexpr auto k_reload_verify_timeout = std::chrono::milliseconds(750);
constexpr int k_switch_attempts = 2;

[[nodiscard]] int clip_of(const std::uintptr_t weapon)
{
    if (!weapon)
        return 0;
    return game::bound_memory().process.load<std::int32_t>(weapon + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id));
}

[[nodiscard]] bool reloading_of(const std::uintptr_t weapon)
{
    if (!weapon)
        return false;
    return game::bound_memory().process.load<bool>(weapon + SCHEMA("C_CSWeaponBase", "m_bInReload"_id));
}

[[nodiscard]] std::string slot_text(const int key, const char* name)
{
    if (key <= 0 || key > 255)
        return std::format("{} unbound", name);
    return std::format("{} key {}", name, key);
}

[[nodiscard]] int clamped_slot(const int key)
{
    // Mouse buttons are virtual keys 1..6; the slot tap is a keyboard press, so those binds are not usable here.
    return key > 6 && key < 256 ? key : 0;
}
} // namespace

// One line per decision, written only while the user enabled the debug log. The formatter deliberately avoids reading
// more game state: the caller passes what the decision was based on.
void walk_bot_t::log_auto_switch(const char* reason, const std::uintptr_t weapon, const std::uint32_t type,
                                 const int clip, const std::string& candidate, const std::string& result)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.m_walk_bot.auto_weapon_switch.debug_log)
        return;
    try
    {
        const auto directory = diagnostics::default_logs_directory();
        if (directory.empty())
            return;
        std::error_code error{};
        std::filesystem::create_directories(directory, error);
        std::ofstream out(directory / L"auto-weapon-switch.log", std::ios::app);
        if (!out)
            return;
        SYSTEMTIME local{};
        ::GetLocalTime(&local);
        out << std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} | reason={} | current=0x{:x} type={} clip={} | "
                           "candidate={} | owner={} | result={}\n",
                           local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                           local.wMilliseconds, reason, static_cast<std::uint64_t>(weapon), type, clip,
                           candidate.empty() ? "none" : candidate, static_cast<int>(auto_bonus().owner()), result);
    }
    catch (...)
    {
        // Diagnostics must never change the switch behaviour.
    }
}

// Releases everything the switch may still own. `forget` also clears the dry weapon that was already handled; map,
// match, death and disable resets pass true, while an ordinary finish passes false so the same dry magazine cannot
// start a new switch chain.
void walk_bot_t::release_auto_weapon_switch(const bool forget)
{
    if (m_auto_switch_phase != auto_switch_phase::idle || auto_bonus().auto_switch_active())
    {
        release_tapped_key(true);
        features::aimbot::g_weapon_switch_hold_aim.store(false, std::memory_order_release);
        features::aimbot::g_weapon_switch_release_request.store(false, std::memory_order_release);
        features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
        auto_bonus().set_auto_switch_active(false);
    }
    m_auto_switch_phase = auto_switch_phase::idle;
    m_auto_switch_source = 0;
    m_auto_switch_source_type = 0;
    m_auto_switch_source_slot = 0;
    m_auto_switch_target_slot = 0;
    m_auto_switch_started = {};
    m_auto_switch_action_at = {};
    m_auto_switch_verify_at = {};
    m_auto_switch_return_at = {};
    m_auto_switch_next_attempt = {};
    m_auto_switch_attempts = 0;
    if (forget)
        m_auto_switch_handled = 0;
}

void walk_bot_t::service_auto_weapon_switch(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot.auto_weapon_switch;

    if (!cfg.enabled)
    {
        if (m_auto_switch_phase != auto_switch_phase::idle || auto_bonus().auto_switch_active())
            release_auto_weapon_switch(true);
        m_auto_switch_handled = 0;
        return;
    }

    // The dry weapon is handled until it has ammo again. A reload, a pickup or a manual weapon change clears the mark
    // on the next tick.
    if (m_auto_switch_handled && clip_of(m_auto_switch_handled) > 0)
        m_auto_switch_handled = 0;

    const auto current_weapon = game::local_player().weapon();
    const auto current_type = game::local_player().weapon_type();

    if (m_auto_switch_phase != auto_switch_phase::idle && !auto_bonus().auto_switch_active())
    {
        // Something else released the owner (a reset path, or a new session). Do not keep tapping into a slot the
        // feature no longer owns.
        release_auto_weapon_switch(false);
        return;
    }

    switch (m_auto_switch_phase)
    {
        case auto_switch_phase::releasing:
        {
            if (features::aimbot::g_weapon_switch_release_ack.load(std::memory_order_acquire))
            {
                log_auto_switch("release_ack", m_auto_switch_source, m_auto_switch_source_type,
                                clip_of(m_auto_switch_source), slot_text(m_auto_switch_target_slot, "target"),
                                "hands released");
                m_auto_switch_phase = auto_switch_phase::waiting;
                m_auto_switch_action_at =
                    std::max(now, m_auto_switch_started +
                                      std::chrono::milliseconds(std::clamp(cfg.switch_delay_ms, 0, 250)));
                return;
            }
            if (now - m_auto_switch_started < k_switch_ack_timeout)
                return;
            log_auto_switch("release_timeout", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), slot_text(m_auto_switch_target_slot, "target"),
                            "aborted: no combat ack");
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::waiting:
        {
            if (now < m_auto_switch_action_at)
                return;
            if (!m_auto_switch_target_slot)
            {
                log_auto_switch("switch", m_auto_switch_source, m_auto_switch_source_type,
                                clip_of(m_auto_switch_source), "none", "aborted: target slot unbound");
                release_auto_weapon_switch(false);
                return;
            }
            tap_key(m_auto_switch_target_slot);
            ++m_auto_switch_attempts;
            m_auto_switch_phase = auto_switch_phase::verifying;
            m_auto_switch_verify_at = now + k_switch_verify_timeout;
            m_auto_switch_next_attempt = now + k_switch_interval;
            log_auto_switch("switch", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), slot_text(m_auto_switch_target_slot, "target"),
                            "slot key sent");
            return;
        }
        case auto_switch_phase::verifying:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            if (active && active != m_auto_switch_source && game::rules::is_firearm(type))
            {
                features::aimbot::g_weapon_switch_hold_aim.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_request.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
                log_auto_switch("verified", active, type, active_clip, slot_text(m_auto_switch_target_slot, "target"),
                                "firearm in hand");
                // A second empty magazine is not a reason to fire another slot key at it. Go back once and let the
                // normal reload path (or the one-shot nudge below) deal with the source weapon.
                if (active_clip <= 0 || reloading_of(active))
                {
                    log_auto_switch("candidate_empty", active, type, active_clip,
                                    slot_text(m_auto_switch_source_slot, "source"),
                                    "switching back without firing");
                    m_auto_switch_phase = auto_switch_phase::return_release;
                    m_auto_switch_started = now;
                    features::aimbot::g_weapon_switch_hold_aim.store(true, std::memory_order_release);
                    features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
                    features::aimbot::g_weapon_switch_release_request.store(true, std::memory_order_release);
                    return;
                }
                if (std::clamp(cfg.switch_back_ms, 0, 3000) <= 0)
                {
                    log_auto_switch("keep", active, type, active_clip, "switch-back disabled", "owner released");
                    release_auto_weapon_switch(false);
                    return;
                }
                m_auto_switch_phase = auto_switch_phase::engaged;
                m_auto_switch_return_at = now + std::chrono::milliseconds(std::clamp(cfg.switch_back_ms, 0, 3000));
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            if (m_auto_switch_attempts < k_switch_attempts && now >= m_auto_switch_next_attempt)
            {
                tap_key(m_auto_switch_target_slot);
                ++m_auto_switch_attempts;
                m_auto_switch_verify_at = now + k_switch_verify_timeout;
                m_auto_switch_next_attempt = now + k_switch_interval;
                log_auto_switch("retry", m_auto_switch_source, m_auto_switch_source_type,
                                clip_of(m_auto_switch_source), slot_text(m_auto_switch_target_slot, "target"),
                                "slot key sent again");
                return;
            }
            log_auto_switch("no_candidate", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), slot_text(m_auto_switch_target_slot, "target"),
                            "aborted: active weapon did not change");
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::engaged:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            if (!active || !game::rules::is_firearm(type))
            {
                log_auto_switch("lost", active, type, clip_of(active), "engaged", "released: no firearm in hand");
                release_auto_weapon_switch(false);
                return;
            }
            if (active == m_auto_switch_source)
            {
                log_auto_switch("returned_early", active, type, clip_of(active), "source selected", "owner released");
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_return_at)
                return;
            if (!m_auto_switch_source_slot)
            {
                log_auto_switch("return", active, type, clip_of(active), "source slot unbound", "owner released");
                release_auto_weapon_switch(false);
                return;
            }
            m_auto_switch_phase = auto_switch_phase::return_release;
            m_auto_switch_started = now;
            features::aimbot::g_weapon_switch_hold_aim.store(true, std::memory_order_release);
            features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
            features::aimbot::g_weapon_switch_release_request.store(true, std::memory_order_release);
            log_auto_switch("return", active, type, clip_of(active), slot_text(m_auto_switch_source_slot, "source"),
                            "release requested");
            return;
        }
        case auto_switch_phase::return_release:
        {
            if (!features::aimbot::g_weapon_switch_release_ack.load(std::memory_order_acquire))
            {
                if (now - m_auto_switch_started < k_switch_ack_timeout)
                    return;
                log_auto_switch("return_timeout", game::local_player().weapon(), game::local_player().weapon_type(),
                                clip_of(game::local_player().weapon()),
                                slot_text(m_auto_switch_source_slot, "source"), "aborted: no combat ack");
                release_auto_weapon_switch(false);
                return;
            }
            tap_key(m_auto_switch_source_slot);
            m_auto_switch_phase = auto_switch_phase::return_verify;
            m_auto_switch_verify_at = now + k_switch_verify_timeout;
            log_auto_switch("return", game::local_player().weapon(), game::local_player().weapon_type(),
                            clip_of(game::local_player().weapon()), slot_text(m_auto_switch_source_slot, "source"),
                            "slot key sent");
            return;
        }
        case auto_switch_phase::return_verify:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            if (active && type == m_auto_switch_source_type)
            {
                features::aimbot::g_weapon_switch_hold_aim.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_request.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
                log_auto_switch("returned", active, type, active_clip, slot_text(m_auto_switch_source_slot, "source"),
                                "source in hand");
                if (active_clip > 0 && !reloading_of(active))
                {
                    log_auto_switch("reload", active, type, active_clip, "ready", "no reload needed");
                    release_auto_weapon_switch(false);
                    return;
                }
                if (cfg.enabled && snapshot->general.m_walk_bot.auto_reload)
                {
                    log_auto_switch("reload", active, type, active_clip, "Auto Reload enabled",
                                    "reload delegated");
                    release_auto_weapon_switch(false);
                    return;
                }
                const auto reload_key = std::clamp(snapshot->general.m_game_controls.reload, 0, 255);
                if (reload_key <= 0)
                {
                    log_auto_switch("reload", active, type, active_clip, "reload key unbound", "no nudge sent");
                    release_auto_weapon_switch(false);
                    return;
                }
                tap_key(static_cast<std::uint16_t>(reload_key));
                m_auto_switch_phase = auto_switch_phase::reload_verify;
                m_auto_switch_verify_at = now + k_reload_verify_timeout;
                log_auto_switch("reload", active, type, active_clip, "one nudge", "reload key sent");
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            log_auto_switch("return_failed", active, type, active_clip, slot_text(m_auto_switch_source_slot, "source"),
                            "aborted: source did not come back");
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::reload_verify:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            if (type != m_auto_switch_source_type)
            {
                log_auto_switch("reload", active, type, active_clip, "weapon changed", "stopped");
                release_auto_weapon_switch(false);
                return;
            }
            if (active_clip > 0 || reloading_of(active))
            {
                log_auto_switch("reload", active, type, active_clip, "one nudge", "reload observed");
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            log_auto_switch("reload", active, type, active_clip, "one nudge", "no reload observed; stopped");
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::idle:
        default:
            break;
    }

    if (!current_weapon || !game::rules::is_firearm(current_type))
        return;
    const auto current_clip = clip_of(current_weapon);
    if (current_clip > 0)
        return;
    if (m_auto_switch_handled == current_weapon)
        return;
    if (now < m_auto_switch_next_attempt)
        return;
    // Auto Knife owns a live engagement. A gun that runs dry during that chase must wait until the knife is finished;
    // taking the slot back would strand the knife and leave the player with a slot key in flight.
    if (auto_bonus().auto_knife_active() || m_knife_locked || m_knife_target != 0)
        return;
    if (auto_bonus().owner() != auto_bonus_t::weapon_owner::none && !auto_bonus().auto_switch_active())
        return;

    const auto rules = read_game_rules();
    if (rules.valid && rules.freeze)
    {
        m_auto_switch_next_attempt = now + std::chrono::milliseconds(500);
        return;
    }
    const auto pawn = game::local_player().pawn();
    const auto defusing_offset = SCHEMA("C_CSPlayerPawn", "m_bIsDefusing"_id);
    if (pawn && defusing_offset > 0 &&
        game::bound_memory().process.load<bool>(pawn + defusing_offset))
    {
        m_auto_switch_next_attempt = now + std::chrono::milliseconds(500);
        return;
    }

    const auto primary = clamped_slot(cfg.primary_slot);
    const auto pistol = clamped_slot(cfg.pistol_slot);
    const auto source_is_sidearm = current_type == game::rules::sidearm;
    if (source_is_sidearm && !player.has_main_weapon)
    {
        // A primary can still be picked up while the pistol is dry; try again later instead of marking this magazine
        // as permanently handled.
        m_auto_switch_next_attempt = now + std::chrono::seconds(1);
        log_auto_switch("no_candidate", current_weapon, current_type, current_clip, "primary not carried",
                        "deferred");
        return;
    }
    const auto source_slot = source_is_sidearm ? pistol : primary;
    const auto target_slot = source_is_sidearm ? primary : pistol;
    if (!source_slot || !target_slot || source_slot == target_slot)
    {
        m_auto_switch_next_attempt = now + std::chrono::seconds(1);
        log_auto_switch("no_candidate", current_weapon, current_type, current_clip,
                        slot_text(source_slot, "source") + ", " + slot_text(target_slot, "target"),
                        "deferred: both slots must be bound to different keyboard keys");
        return;
    }

    auto_bonus().set_auto_switch_active(true);
    if (!auto_bonus().auto_switch_active())
    {
        log_auto_switch("owner_busy", current_weapon, current_type, current_clip,
                        slot_text(target_slot, "target"), "deferred: slot owned");
        m_auto_switch_next_attempt = now + k_switch_interval;
        return;
    }

    m_auto_switch_phase = auto_switch_phase::releasing;
    m_auto_switch_source = current_weapon;
    m_auto_switch_source_type = current_type;
    m_auto_switch_source_slot = static_cast<std::uint16_t>(source_slot);
    m_auto_switch_target_slot = static_cast<std::uint16_t>(target_slot);
    m_auto_switch_started = now;
    m_auto_switch_action_at = now + std::chrono::milliseconds(std::clamp(cfg.switch_delay_ms, 0, 250));
    m_auto_switch_verify_at = {};
    m_auto_switch_return_at = {};
    m_auto_switch_next_attempt = now + k_switch_interval;
    m_auto_switch_attempts = 0;
    m_auto_switch_handled = current_weapon;
    features::aimbot::g_weapon_switch_hold_aim.store(true, std::memory_order_release);
    features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
    features::aimbot::g_weapon_switch_release_request.store(true, std::memory_order_release);
    // Switching away from a scoped rifle leaves the scope without pressing the knife slot. Auto Scope resumes after
    // the switch back when its own fight asks for it again.
    if (m_scope.pressed)
        (void)set_scope_control(false);
    m_scope_state = scope_state::idle;
    m_scope_target = 0;
    m_scope_next = {};
    m_scope_attempts = 0;
    log_auto_switch("trigger", current_weapon, current_type, current_clip, slot_text(target_slot, "target"),
                    "release requested");
}
} // namespace features::misc

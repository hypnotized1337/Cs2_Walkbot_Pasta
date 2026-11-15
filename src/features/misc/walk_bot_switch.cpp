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
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace features::misc
{
using namespace walk_detail;

namespace
{
constexpr auto k_switch_ack_timeout = std::chrono::milliseconds(50);
constexpr auto k_switch_interval = std::chrono::milliseconds(350);
constexpr auto k_switch_verify_timeout = std::chrono::milliseconds(300);
constexpr auto k_reload_verify_timeout = std::chrono::milliseconds(750);
constexpr auto k_switch_window = std::chrono::seconds(5);
constexpr auto k_no_candidate_retry = std::chrono::seconds(1);
constexpr auto k_candidate_reloading_retry = std::chrono::milliseconds(500);
constexpr auto k_engaged_grace = std::chrono::milliseconds(300);
constexpr auto k_return_retry = std::chrono::milliseconds(250);
constexpr auto k_return_log_interval = std::chrono::seconds(1);
constexpr int k_switch_attempts = 2;
constexpr int k_max_engagement_switches = 2;
constexpr int k_max_window_switches = 3;

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

[[nodiscard]] bool is_primary_type(const std::uint32_t type) noexcept
{
    return game::rules::is_firearm(type) && type != game::rules::sidearm;
}

[[nodiscard]] std::string slot_text(const int key, const char* name)
{
    if (key <= 0 || key > 255)
        return std::format("{} unbound", name);
    return std::format("{} key {}", name, key);
}

[[nodiscard]] std::string weapon_text(const std::uintptr_t weapon, const int key, const char* name)
{
    if (!weapon)
        return slot_text(key, name);
    return std::format("0x{:x} {}", static_cast<std::uint64_t>(weapon), slot_text(key, name));
}

[[nodiscard]] int clamped_slot(const int key)
{
    // Mouse buttons are virtual keys 1..6; the slot tap is a keyboard press, so those binds are not usable here.
    return key > 6 && key < 256 ? key : 0;
}

// One held firearm as it reads this tick. The active weapon and every candidate are read the same way: clip from the
// weapon entity, reload from the weapon base. A candidate that has no ammo or is reloading cannot be a destination.
struct held_weapon
{
    std::uintptr_t weapon{};
    std::uint32_t type{};
    int clip{};
    bool reloading{};
    bool primary{};

    [[nodiscard]] bool can_fire() const noexcept
    {
        return weapon != 0 && game::rules::is_firearm(type) && clip > 0 && !reloading;
    }
};

struct slot_scan
{
    bool carried{};
    bool ready{};
    bool all_dry{true};
    held_weapon sample{};
};

// m_hMyWeapons is the inventory the game itself switches from, so it is read directly instead of trusting a cached
// weapon type or a default-zero clip on an entity that is not in hand.
void collect_held_firearms(std::vector<held_weapon>& out)
{
    out.clear();
    const auto pawn = game::local_player().pawn();
    if (!pawn)
        return;
    const auto& process = game::bound_memory().process;
    const auto services = process.load<std::uintptr_t>(pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id));
    if (!services)
        return;
    const auto list = services + SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_id);
    const auto count = std::clamp(process.load<std::int32_t>(list), 0, 16);
    const auto handles = process.load<std::uintptr_t>(list + 8);
    std::array<std::uint32_t, 16> held{};
    if (count <= 0 || !handles || !process.copy(handles, held.data(), static_cast<std::size_t>(count) * 4))
        return;

    for (int i = 0; i < count; ++i)
    {
        const auto weapon = game::entity_index().lookup(held[static_cast<std::size_t>(i)]);
        if (!weapon)
            continue;
        const auto vdata =
            process.load<std::uintptr_t>(weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8);
        if (!vdata)
            continue;
        const auto type = process.load<std::uint32_t>(vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id));
        if (!game::rules::is_firearm(type))
            continue;
        out.push_back({weapon, type, clip_of(weapon), reloading_of(weapon), is_primary_type(type)});
    }
}

[[nodiscard]] slot_scan scan_slot(const std::vector<held_weapon>& weapons, const bool primary,
                                  const std::unordered_map<std::uintptr_t, std::uint32_t>& dry)
{
    slot_scan result{};
    for (const auto& weapon : weapons)
    {
        if (weapon.primary != primary)
            continue;
        if (!result.carried)
        {
            result.carried = true;
            result.sample = weapon;
        }
        // A latched weapon is not a destination. The latch is normally cleared above as soon as the fresh read sees
        // ammo, so this is also a guard against a momentary stale read promoting an empty weapon.
        if (dry.contains(weapon.weapon))
            continue;
        if (weapon.can_fire())
        {
            result.ready = true;
            result.sample = weapon;
            break;
        }
        if (weapon.clip > 0)
            result.all_dry = false;
    }
    return result;
}
} // namespace

// One line per decision, written only while the user enabled the debug log. The formatter deliberately avoids reading
// more game state: the caller passes the clip and reload values the decision was based on.
void walk_bot_t::log_auto_switch(const char* reason, const std::uintptr_t weapon, const std::uint32_t type,
                                 const int clip, const std::string& candidate, const std::string& result,
                                 const int candidate_clip, const bool candidate_reloading)
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
        const auto candidate_clip_text = candidate_clip >= 0 ? std::to_string(candidate_clip) : std::string{"-"};
        out << std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} | reason={} | current=0x{:x} type={} clip={} "
                           "reload={} | candidate={} clip={} reload={} | owner={} | result={}\n",
                           local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                           local.wMilliseconds, reason, static_cast<std::uint64_t>(weapon), type, clip,
                           reloading_of(weapon) ? 1 : 0, candidate.empty() ? "none" : candidate, candidate_clip_text,
                           candidate_reloading ? 1 : 0, static_cast<int>(auto_bonus().owner()), result);
    }
    catch (...)
    {
        // Diagnostics must never change the switch behaviour.
    }
}

// A latch is keyed by the weapon handle and is cleared only after that exact weapon reads ammo again. Switching,
// starting a reload, an owner change or an ordinary tick never clears it, so two empty weapons cannot bounce forever.
void walk_bot_t::latch_auto_switch_dry(const std::uintptr_t weapon, const std::uint32_t type, const int clip,
                                       const char* reason, const int candidate_clip, const bool candidate_reloading)
{
    if (!weapon)
        return;
    const auto [it, inserted] = m_auto_switch_dry.try_emplace(weapon, type);
    if (!inserted)
    {
        it->second = type;
        return;
    }
    log_auto_switch("dry_latch", weapon, type, clip, reason, "latched until clip > 0", candidate_clip,
                    candidate_reloading);
}

void walk_bot_t::refresh_auto_switch_latches()
{
    for (auto it = m_auto_switch_dry.begin(); it != m_auto_switch_dry.end();)
    {
        const auto weapon = it->first;
        const auto type = it->second;
        const auto clip = clip_of(weapon);
        if (clip > 0)
        {
            log_auto_switch("dry_clear", weapon, type, clip, "dry latch", "clip above zero");
            if (m_auto_switch_nudged == weapon)
                m_auto_switch_nudged = 0; // this weapon really did get ammo; a later empty magazine may nudge once again
            it = m_auto_switch_dry.erase(it);
            continue;
        }
        ++it;
    }
}

void walk_bot_t::note_auto_switch(const clock::time_point now, const std::uintptr_t weapon, const std::uint32_t type,
                                  const int clip, const int candidate_clip, const bool candidate_reloading)
{
    ++m_auto_switch_engagement_switches;
    while (!m_auto_switch_history.empty() && now - m_auto_switch_history.front() >= k_switch_window)
        m_auto_switch_history.pop_front();
    m_auto_switch_history.push_back(now);
    // Every verified slot change starts the ordinary switch cooldown, including the switch back. This keeps the
    // configured 350 ms minimum interval even when a new dry engagement begins immediately.
    m_auto_switch_next_attempt = now + k_switch_interval;
    if (m_auto_switch_stand_down)
        return;
    if (m_auto_switch_engagement_switches < k_max_engagement_switches &&
        m_auto_switch_history.size() < static_cast<std::size_t>(k_max_window_switches))
        return;

    m_auto_switch_stand_down = true;
    log_auto_switch("limit_reached", weapon, type, clip, "switch cap",
                    std::format("standing down: {}/{} this engagement, {}/{} in 5 s",
                                m_auto_switch_engagement_switches, k_max_engagement_switches,
                                m_auto_switch_history.size(), k_max_window_switches),
                    candidate_clip, candidate_reloading);
}

// An engagement ends only once the switch sequence is idle and the weapon in hand can actually fire. A weapon that is
// reloading, or still dry, keeps the per-engagement count and the stand-down in place.
void walk_bot_t::reset_auto_switch_engagement(const clock::time_point now)
{
    if (m_auto_switch_phase != auto_switch_phase::idle || auto_bonus().auto_switch_active())
        return;
    const auto weapon = game::local_player().weapon();
    const auto type = game::local_player().weapon_type();
    if (!weapon || !game::rules::is_firearm(type) || clip_of(weapon) <= 0 || reloading_of(weapon))
        return;
    if (m_auto_switch_engagement_switches == 0 && !m_auto_switch_stand_down && !m_auto_switch_both_dry_logged &&
        m_auto_switch_nudged == 0)
        return;
    m_auto_switch_engagement_switches = 0;
    m_auto_switch_stand_down = false;
    m_auto_switch_both_dry_logged = false;
    m_auto_switch_nudged = 0;
    log_auto_switch("engagement_end", weapon, type, clip_of(weapon), "loaded and idle",
                    std::format("counters reset; {} switches in the rolling 5 s", m_auto_switch_history.size()));
}

// A recent fight or a target that flickers in and out keeps the timed switch-back on hold. The return is only a
// convenience; firing the weapon that is already loaded matters more.
bool walk_bot_t::auto_switch_engaged(const clock::time_point now) const
{
    if (m_fight_target && now - m_fight_seen_at < k_engaged_grace)
        return true;
    if (m_knife_target || m_knife_locked || m_rear_target)
        return true;
    return recent(features::aimbot::aim().view_engaged_ticks(), now, k_engaged_grace);
}

// Releases everything the switch may still own. `reset_latches` is only for a new map, life or session; an ordinary
// finish keeps the dry latches and the rolling switch history. The next-attempt time is kept on an ordinary finish so a
// slot tap that did not take cannot be repeated on the very next movement tick.
void walk_bot_t::release_auto_weapon_switch(const bool reset_latches)
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
    m_auto_switch_target = 0;
    m_auto_switch_source_type = 0;
    m_auto_switch_target_type = 0;
    m_auto_switch_source_slot = 0;
    m_auto_switch_target_slot = 0;
    m_auto_switch_started = {};
    m_auto_switch_action_at = {};
    m_auto_switch_verify_at = {};
    m_auto_switch_return_at = {};
    m_auto_switch_attempts = 0;
    if (!reset_latches)
        return;

    m_auto_switch_dry.clear();
    m_auto_switch_history.clear();
    m_auto_switch_engagement_switches = 0;
    m_auto_switch_nudged = 0;
    m_auto_switch_stand_down = false;
    m_auto_switch_both_dry_logged = false;
    m_auto_switch_next_attempt = {};
}

void walk_bot_t::service_auto_weapon_switch(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot.auto_weapon_switch;
    (void)player; // The inventory is the authority for both slots; read_player's cached primary flag can lag a pickup.

    if (!cfg.enabled)
    {
        if (m_auto_switch_phase != auto_switch_phase::idle || auto_bonus().auto_switch_active())
            release_auto_weapon_switch(false);
        return;
    }

    while (!m_auto_switch_history.empty() && now - m_auto_switch_history.front() >= k_switch_window)
        m_auto_switch_history.pop_front();
    refresh_auto_switch_latches();
    reset_auto_switch_engagement(now);

    const auto current_weapon = game::local_player().weapon();
    const auto current_type = game::local_player().weapon_type();

    // An idle phase must not retain the auto-switch owner; releasing it here also clears a stale hold after a reset
    // path changed ownership without going through this feature.
    if (m_auto_switch_phase == auto_switch_phase::idle && auto_bonus().auto_switch_active())
        release_auto_weapon_switch(false);

    // The one-shot reload nudge runs without claiming the slot; everything else in the phase machine must still own it.
    if (m_auto_switch_phase != auto_switch_phase::idle && m_auto_switch_phase != auto_switch_phase::reload_verify &&
        !auto_bonus().auto_switch_active())
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
                                clip_of(m_auto_switch_source), weapon_text(m_auto_switch_target,
                                                                          m_auto_switch_target_slot, "target"),
                                "hands released", clip_of(m_auto_switch_target),
                                reloading_of(m_auto_switch_target));
                m_auto_switch_phase = auto_switch_phase::waiting;
                m_auto_switch_action_at =
                    std::max(now, m_auto_switch_started +
                                      std::chrono::milliseconds(std::clamp(cfg.switch_delay_ms, 0, 250)));
                return;
            }
            if (now - m_auto_switch_started < k_switch_ack_timeout)
                return;
            log_auto_switch("release_timeout", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), weapon_text(m_auto_switch_target,
                                                                      m_auto_switch_target_slot, "target"),
                            "aborted: no combat ack", clip_of(m_auto_switch_target), reloading_of(m_auto_switch_target));
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

            // The destination is checked again immediately before the tap. A candidate that emptied or started a
            // reload during the release handshake and the switch delay is not a destination.
            const auto source_clip = clip_of(m_auto_switch_source);
            if (source_clip > 0 && !reloading_of(m_auto_switch_source))
            {
                log_auto_switch("switch", m_auto_switch_source, m_auto_switch_source_type, source_clip,
                                weapon_text(m_auto_switch_target, m_auto_switch_target_slot, "target"),
                                "aborted: source has ammo again", clip_of(m_auto_switch_target),
                                reloading_of(m_auto_switch_target));
                release_auto_weapon_switch(false);
                return;
            }
            const auto target_clip = clip_of(m_auto_switch_target);
            const auto target_reloading = reloading_of(m_auto_switch_target);
            if (!m_auto_switch_target || !game::rules::is_firearm(m_auto_switch_target_type) || target_clip <= 0 ||
                target_reloading)
            {
                if (target_clip <= 0)
                    latch_auto_switch_dry(m_auto_switch_target, m_auto_switch_target_type, target_clip,
                                          "candidate cannot fire", target_clip, target_reloading);
                log_auto_switch("no_candidate", m_auto_switch_source, m_auto_switch_source_type, clip_of(m_auto_switch_source),
                                weapon_text(m_auto_switch_target, m_auto_switch_target_slot, "target"),
                                "aborted: candidate cannot fire now", target_clip, target_reloading);
                release_auto_weapon_switch(false);
                return;
            }

            tap_key(m_auto_switch_target_slot);
            ++m_auto_switch_attempts;
            m_auto_switch_phase = auto_switch_phase::verifying;
            m_auto_switch_verify_at = now + k_switch_verify_timeout;
            m_auto_switch_next_attempt = now + k_switch_interval;
            log_auto_switch("switch", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), weapon_text(m_auto_switch_target,
                                                                      m_auto_switch_target_slot, "target"),
                            "slot key sent", target_clip, target_reloading);
            return;
        }
        case auto_switch_phase::verifying:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            const auto active_reloading = reloading_of(active);
            if (active && active == m_auto_switch_target)
            {
                features::aimbot::g_weapon_switch_hold_aim.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_request.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
                if (active_clip > 0 && !active_reloading && game::rules::is_firearm(type))
                {
                    log_auto_switch("verified", active, type, active_clip,
                                    weapon_text(m_auto_switch_target, m_auto_switch_target_slot, "target"),
                                    "candidate in hand with ammo", active_clip, active_reloading);
                    note_auto_switch(now, active, type, active_clip, active_clip, active_reloading);
                    if (m_auto_switch_stand_down)
                    {
                        log_auto_switch("keep", active, type, active_clip, "switch cap",
                                        "standing down on the loaded candidate", active_clip, active_reloading);
                        release_auto_weapon_switch(false);
                        return;
                    }
                    if (std::clamp(cfg.switch_back_ms, 0, 3000) <= 0)
                    {
                        log_auto_switch("keep", active, type, active_clip, "switch-back disabled",
                                        "owner released", active_clip, active_reloading);
                        release_auto_weapon_switch(false);
                        return;
                    }
                    m_auto_switch_phase = auto_switch_phase::engaged;
                    m_auto_switch_return_at =
                        now + std::chrono::milliseconds(std::clamp(cfg.switch_back_ms, 0, 3000));
                    return;
                }

                // The slot tap really did change the weapon but the fresh read says it cannot fire. Latch it, do not
                // send another slot key, and stand down for this dry engagement. A reloading candidate with ammo is
                // not dry, but it still must not trigger a switch back.
                if (active_clip <= 0)
                    latch_auto_switch_dry(active, type, active_clip, "candidate empty after switch", active_clip,
                                          active_reloading);
                log_auto_switch(active_clip <= 0 ? "candidate_empty" : "candidate_not_ready", active, type,
                                active_clip, "after switch",
                                active_clip <= 0 ? "standing down; no switch back into an empty weapon"
                                                 : "standing down; candidate reloading",
                                active_clip, active_reloading);
                note_auto_switch(now, active, type, active_clip, active_clip, active_reloading);
                if (active_clip <= 0)
                {
                    m_auto_switch_stand_down = true;
                    if (!m_auto_switch_both_dry_logged)
                    {
                        m_auto_switch_both_dry_logged = true;
                        log_auto_switch("both_dry", active, type, active_clip, "source and target",
                                        "both dry, standing down", active_clip, active_reloading);
                    }
                }
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            if (m_auto_switch_attempts < k_switch_attempts && now >= m_auto_switch_next_attempt)
            {
                const auto target_clip = clip_of(m_auto_switch_target);
                const auto target_reloading = reloading_of(m_auto_switch_target);
                if (!m_auto_switch_target || target_clip <= 0 || target_reloading)
                {
                    if (target_clip <= 0)
                        latch_auto_switch_dry(m_auto_switch_target, m_auto_switch_target_type, target_clip,
                                              "candidate cannot fire", target_clip, target_reloading);
                    log_auto_switch("no_candidate", m_auto_switch_source, m_auto_switch_source_type,
                                    clip_of(m_auto_switch_source),
                                    weapon_text(m_auto_switch_target, m_auto_switch_target_slot, "target"),
                                    "aborted before retry: candidate cannot fire", target_clip, target_reloading);
                    release_auto_weapon_switch(false);
                    return;
                }
                tap_key(m_auto_switch_target_slot);
                ++m_auto_switch_attempts;
                m_auto_switch_verify_at = now + k_switch_verify_timeout;
                m_auto_switch_next_attempt = now + k_switch_interval;
                log_auto_switch("retry", m_auto_switch_source, m_auto_switch_source_type,
                                clip_of(m_auto_switch_source), weapon_text(m_auto_switch_target,
                                                                          m_auto_switch_target_slot, "target"),
                                "slot key sent again", target_clip, target_reloading);
                return;
            }
            log_auto_switch("no_candidate", m_auto_switch_source, m_auto_switch_source_type,
                            clip_of(m_auto_switch_source), weapon_text(m_auto_switch_target,
                                                                      m_auto_switch_target_slot, "target"),
                            "aborted: active weapon did not change", clip_of(m_auto_switch_target),
                            reloading_of(m_auto_switch_target));
            m_auto_switch_stand_down = true;
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::engaged:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            const auto active_reloading = reloading_of(active);
            if (!active || !game::rules::is_firearm(type))
            {
                log_auto_switch("lost", active, type, active_clip, "engaged", "released: no firearm in hand",
                                active_clip, active_reloading);
                release_auto_weapon_switch(false);
                return;
            }
            if (active != m_auto_switch_target)
            {
                log_auto_switch("lost", active, type, active_clip,
                                weapon_text(m_auto_switch_target, m_auto_switch_target_slot, "target"),
                                "released: the player changed weapon", active_clip, active_reloading);
                release_auto_weapon_switch(false);
                return;
            }
            if (active_clip <= 0)
            {
                latch_auto_switch_dry(active, type, active_clip, "candidate ran dry", active_clip, active_reloading);
                log_auto_switch("candidate_empty", active, type, active_clip, "after switch",
                                "standing down; let the current reload finish", active_clip, active_reloading);
                m_auto_switch_stand_down = true;
                if (!m_auto_switch_both_dry_logged)
                {
                    m_auto_switch_both_dry_logged = true;
                    log_auto_switch("both_dry", active, type, active_clip, "source and target",
                                    "both dry, standing down", active_clip, active_reloading);
                }
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_return_at)
                return;
            if (!m_auto_switch_source_slot)
            {
                log_auto_switch("return", active, type, active_clip, "source slot unbound", "owner released",
                                active_clip, active_reloading);
                release_auto_weapon_switch(false);
                return;
            }

            // The timed switch-back is allowed only when the original weapon is loaded right now. A reloading or empty
            // source is not a destination; staying on the loaded candidate is the safe outcome.
            const auto source_clip = clip_of(m_auto_switch_source);
            const auto source_reloading = reloading_of(m_auto_switch_source);
            if (!m_auto_switch_source || !game::rules::is_firearm(m_auto_switch_source_type) || source_clip <= 0 ||
                source_reloading)
            {
                if (source_clip <= 0)
                    latch_auto_switch_dry(m_auto_switch_source, m_auto_switch_source_type, source_clip,
                                          "source not loaded for switch-back", source_clip, source_reloading);
                log_auto_switch("return_blocked", active, type, active_clip,
                                weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                "staying on the loaded candidate", source_clip, source_reloading);
                release_auto_weapon_switch(false);
                return;
            }

            if (auto_switch_engaged(now))
            {
                if (now >= m_auto_switch_next_attempt)
                {
                    log_auto_switch("return_wait", active, type, active_clip,
                                    weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                    "engagement or flickering target; retrying", source_clip, source_reloading);
                    m_auto_switch_next_attempt = now + k_return_log_interval;
                }
                m_auto_switch_return_at = now + k_return_retry;
                return;
            }
            if (m_auto_switch_stand_down || m_auto_switch_engagement_switches >= k_max_engagement_switches ||
                m_auto_switch_history.size() >= static_cast<std::size_t>(k_max_window_switches))
            {
                if (!m_auto_switch_stand_down)
                {
                    m_auto_switch_stand_down = true;
                    log_auto_switch("limit_reached", active, type, active_clip,
                                    weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                    "switch-back skipped at the cap", source_clip, source_reloading);
                }
                release_auto_weapon_switch(false);
                return;
            }

            m_auto_switch_phase = auto_switch_phase::return_release;
            m_auto_switch_started = now;
            features::aimbot::g_weapon_switch_hold_aim.store(true, std::memory_order_release);
            features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
            features::aimbot::g_weapon_switch_release_request.store(true, std::memory_order_release);
            log_auto_switch("return", active, type, active_clip,
                            weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                            "release requested", source_clip, source_reloading);
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
                                weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                "aborted: no combat ack", clip_of(m_auto_switch_source),
                                reloading_of(m_auto_switch_source));
                release_auto_weapon_switch(false);
                return;
            }
            if (auto_switch_engaged(now))
            {
                log_auto_switch("return_wait", game::local_player().weapon(), game::local_player().weapon_type(),
                                clip_of(game::local_player().weapon()),
                                weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                "aborted: engagement started", clip_of(m_auto_switch_source),
                                reloading_of(m_auto_switch_source));
                release_auto_weapon_switch(false);
                return;
            }
            const auto source_clip = clip_of(m_auto_switch_source);
            const auto source_reloading = reloading_of(m_auto_switch_source);
            if (source_clip <= 0 || source_reloading)
            {
                if (source_clip <= 0)
                    latch_auto_switch_dry(m_auto_switch_source, m_auto_switch_source_type, source_clip,
                                          "source cannot fire before switch-back", source_clip, source_reloading);
                log_auto_switch("return_blocked", game::local_player().weapon(), game::local_player().weapon_type(),
                                clip_of(game::local_player().weapon()),
                                weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                "aborted: source cannot fire now", source_clip, source_reloading);
                release_auto_weapon_switch(false);
                return;
            }
            tap_key(m_auto_switch_source_slot);
            m_auto_switch_phase = auto_switch_phase::return_verify;
            m_auto_switch_verify_at = now + k_switch_verify_timeout;
            log_auto_switch("return", game::local_player().weapon(), game::local_player().weapon_type(),
                            clip_of(game::local_player().weapon()),
                            weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                            "slot key sent", source_clip, source_reloading);
            return;
        }
        case auto_switch_phase::return_verify:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            const auto active_reloading = reloading_of(active);
            if (active && active == m_auto_switch_source)
            {
                features::aimbot::g_weapon_switch_hold_aim.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_request.store(false, std::memory_order_release);
                features::aimbot::g_weapon_switch_release_ack.store(false, std::memory_order_release);
                log_auto_switch("returned", active, type, active_clip,
                                weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                                active_clip > 0 && !active_reloading ? "source in hand with ammo"
                                                                     : "source in hand but not ready",
                                active_clip, active_reloading);
                note_auto_switch(now, active, type, active_clip, active_clip, active_reloading);
                if (active_clip <= 0)
                    latch_auto_switch_dry(active, type, active_clip, "returned source empty", active_clip,
                                          active_reloading);
                // A returned source that cannot fire is not a reason to jump back to the other weapon. Stop here and
                // let this weapon reload; the latch on it clears only when the fresh read sees ammo again.
                if (active_clip <= 0 || active_reloading)
                    m_auto_switch_stand_down = true;
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            log_auto_switch("return_failed", active, type, active_clip,
                            weapon_text(m_auto_switch_source, m_auto_switch_source_slot, "source"),
                            "aborted: source did not come back", clip_of(m_auto_switch_source),
                            reloading_of(m_auto_switch_source));
            release_auto_weapon_switch(false);
            return;
        }
        case auto_switch_phase::reload_verify:
        {
            const auto active = game::local_player().weapon();
            const auto type = game::local_player().weapon_type();
            const auto active_clip = clip_of(active);
            const auto active_reloading = reloading_of(active);
            if (!active || active != m_auto_switch_source)
            {
                log_auto_switch("reload", active, type, active_clip, "weapon changed", "stopped", active_clip,
                                active_reloading);
                release_auto_weapon_switch(false);
                return;
            }
            if (active_clip > 0 || active_reloading)
            {
                log_auto_switch("reload", active, type, active_clip, "one nudge", "reload observed", active_clip,
                                active_reloading);
                release_auto_weapon_switch(false);
                return;
            }
            if (now < m_auto_switch_verify_at)
                return;
            log_auto_switch("reload", active, type, active_clip, "one nudge", "no reload observed; stopped",
                            active_clip, active_reloading);
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
    const auto source_slot = source_is_sidearm ? pistol : primary;
    const auto target_slot = source_is_sidearm ? primary : pistol;
    const auto target_is_primary = source_is_sidearm;
    if (!source_slot || !target_slot || source_slot == target_slot)
    {
        m_auto_switch_next_attempt = now + std::chrono::seconds(1);
        log_auto_switch("no_candidate", current_weapon, current_type, current_clip,
                        slot_text(source_slot, "source") + ", " + slot_text(target_slot, "target"),
                        "deferred: both slots must be bound to different keyboard keys");
        return;
    }

    const auto nudge_current = [&]() -> bool
    {
        if (snapshot->general.m_walk_bot.auto_reload || reloading_of(current_weapon) ||
            m_auto_switch_nudged == current_weapon)
            return false;
        const auto reload_key = std::clamp(snapshot->general.m_game_controls.reload, 0, 255);
        if (reload_key <= 0)
            return false;
        tap_key(static_cast<std::uint16_t>(reload_key));
        m_auto_switch_nudged = current_weapon;
        m_auto_switch_source = current_weapon;
        m_auto_switch_source_type = current_type;
        m_auto_switch_source_slot = static_cast<std::uint16_t>(source_slot);
        m_auto_switch_phase = auto_switch_phase::reload_verify;
        m_auto_switch_verify_at = now + k_reload_verify_timeout;
        m_auto_switch_next_attempt = now + std::chrono::seconds(1);
        log_auto_switch("reload", current_weapon, current_type, current_clip, "one nudge",
                        "reload key sent once for this dry engagement");
        return true;
    };

    std::vector<held_weapon> weapons{};
    collect_held_firearms(weapons);
    if (weapons.empty())
    {
        latch_auto_switch_dry(current_weapon, current_type, current_clip, "inventory unreadable");
        m_auto_switch_next_attempt = now + k_no_candidate_retry;
        log_auto_switch("no_candidate", current_weapon, current_type, current_clip, "inventory",
                        "deferred: held weapons unreadable");
        (void)nudge_current();
        return;
    }

    const auto target = scan_slot(weapons, target_is_primary, m_auto_switch_dry);
    if (!target.carried)
    {
        latch_auto_switch_dry(current_weapon, current_type, current_clip, "target slot empty");
        m_auto_switch_next_attempt = now + k_no_candidate_retry;
        log_auto_switch("no_candidate", current_weapon, current_type, current_clip,
                        weapon_text(0, target_slot, "target"), "deferred: target slot has no firearm");
        (void)nudge_current();
        return;
    }

    if (!target.ready)
    {
        for (const auto& weapon : weapons)
            if (weapon.primary == target_is_primary && weapon.clip <= 0)
                latch_auto_switch_dry(weapon.weapon, weapon.type, weapon.clip, "candidate cannot fire", weapon.clip,
                                      weapon.reloading);
        if (target.all_dry)
        {
            if (!m_auto_switch_both_dry_logged)
            {
                m_auto_switch_both_dry_logged = true;
                log_auto_switch("both_dry", current_weapon, current_type, current_clip,
                                weapon_text(target.sample.weapon, target_slot, "target"),
                                "both dry, standing down", target.sample.clip, target.sample.reloading);
            }
        }
        else
        {
            log_auto_switch("no_candidate", current_weapon, current_type, current_clip,
                            weapon_text(target.sample.weapon, target_slot, "target"),
                            "deferred: candidate cannot fire now", target.sample.clip, target.sample.reloading);
        }
        m_auto_switch_next_attempt =
            now + (target.all_dry ? k_no_candidate_retry : k_candidate_reloading_retry);
        (void)nudge_current();
        return;
    }

    const auto candidate = target.sample;
    // A limit or a failed/candidate-empty switch stands the feature down until the weapon in hand can fire again. The
    // no-candidate paths above may still send the single reload nudge; nothing here may send another slot key.
    if (m_auto_switch_stand_down)
    {
        // The feature is standing down, not the weapon: the single nudge may still start this weapon's reload. Throttle
        // the inventory scan as well; the no-candidate paths below set their own retry.
        m_auto_switch_next_attempt = now + k_no_candidate_retry;
        (void)nudge_current();
        return;
    }
    if (m_auto_switch_engagement_switches >= k_max_engagement_switches ||
        m_auto_switch_history.size() >= static_cast<std::size_t>(k_max_window_switches))
    {
        if (!m_auto_switch_stand_down)
        {
            m_auto_switch_stand_down = true;
            log_auto_switch("limit_reached", current_weapon, current_type, current_clip,
                            weapon_text(candidate.weapon, target_slot, "target"),
                            std::format("standing down: {}/{} this engagement, {}/{} in 5 s",
                                        m_auto_switch_engagement_switches, k_max_engagement_switches,
                                        m_auto_switch_history.size(), k_max_window_switches),
                            candidate.clip, candidate.reloading);
        }
        return;
    }

    auto_bonus().set_auto_switch_active(true);
    if (!auto_bonus().auto_switch_active())
    {
        log_auto_switch("owner_busy", current_weapon, current_type, current_clip,
                        weapon_text(candidate.weapon, target_slot, "target"), "deferred: slot owned", candidate.clip,
                        candidate.reloading);
        m_auto_switch_next_attempt = now + k_switch_interval;
        return;
    }

    m_auto_switch_phase = auto_switch_phase::releasing;
    m_auto_switch_source = current_weapon;
    m_auto_switch_target = candidate.weapon;
    m_auto_switch_source_type = current_type;
    m_auto_switch_target_type = candidate.type;
    m_auto_switch_source_slot = static_cast<std::uint16_t>(source_slot);
    m_auto_switch_target_slot = static_cast<std::uint16_t>(target_slot);
    m_auto_switch_started = now;
    m_auto_switch_action_at = now + std::chrono::milliseconds(std::clamp(cfg.switch_delay_ms, 0, 250));
    m_auto_switch_verify_at = {};
    m_auto_switch_return_at = {};
    m_auto_switch_next_attempt = now + k_switch_interval;
    m_auto_switch_attempts = 0;
    latch_auto_switch_dry(current_weapon, current_type, current_clip, "source empty", candidate.clip,
                          candidate.reloading);
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
    log_auto_switch("trigger", current_weapon, current_type, current_clip,
                    weapon_text(candidate.weapon, target_slot, "target"), "release requested", candidate.clip,
                    candidate.reloading);
}
} // namespace features::misc

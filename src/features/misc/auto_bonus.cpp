#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/log_directory.hpp>
#include <foundation/identity.hpp>
#include <game/input/bindings.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace features::misc
{
namespace
{
// The game samples keyboard input at its own tick rate; a 40 ms hold is long enough for one tap to register.
constexpr auto k_hold = std::chrono::milliseconds(40);
constexpr auto k_retry = std::chrono::milliseconds(100);
// Auto Bonus stays off the pickup key for this long after Auto Weapon Switch last owned or resolved the slot. The
// dry-latch check below can hold it off for longer, until the remembered weapon actually reads ammo again.
constexpr auto k_auto_switch_hold_off = std::chrono::milliseconds(1500);
constexpr auto k_change_window = std::chrono::seconds(5);
constexpr int k_max_engagement_changes = 2;
constexpr int k_max_window_changes = 3;
constexpr auto k_skip_log_interval = std::chrono::seconds(1);

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

[[nodiscard]] std::uint32_t weapon_type_of(const std::uintptr_t weapon)
{
    if (!weapon)
        return 0;
    const auto& process = game::bound_memory().process;
    const auto vdata =
        process.load<std::uintptr_t>(weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8);
    if (!vdata)
        return 0;
    return process.load<std::uint32_t>(vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id));
}
} // namespace

// The shared diagnostic line for both weapon automations. It serializes file writes, then snapshots the owner and the
// latch map; callers must not hold m_automation_mutex when they call it.
void log_weapon_automation(const char* reason, const std::uintptr_t weapon, const std::uint32_t type, const int clip,
                           const bool reloading, const std::string& candidate, const std::string& result,
                           const int candidate_clip, const bool candidate_reloading, const int engagement_changes,
                           const int window_changes)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.m_walk_bot.auto_weapon_switch.debug_log)
        return;
    try
    {
        static std::mutex log_mutex;
        std::scoped_lock log_lock(log_mutex);
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
                           "reload={} | candidate={} clip={} reload={} | owner={} | latches={} | changes={}/{} "
                           "engage, {}/{} in 5 s | result={}\n",
                           local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                           local.wMilliseconds, reason, static_cast<std::uint64_t>(weapon), type, clip,
                           reloading ? 1 : 0, candidate.empty() ? "none" : candidate, candidate_clip_text,
                           candidate_reloading ? 1 : 0, static_cast<int>(auto_bonus().owner()),
                           auto_bonus().weapon_latch_summary(), engagement_changes, k_max_engagement_changes,
                           window_changes, k_max_window_changes, result);
    }
    catch (...)
    {
        // Diagnostics must never change the weapon behaviour.
    }
}

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

void auto_bonus_t::latch_weapon_dry(const std::uintptr_t weapon, const std::uint32_t type, const int clip,
                                    const char* reason, const int candidate_clip, const bool candidate_reloading)
{
    if (!weapon)
        return;
    bool inserted = false;
    const auto resolved_type = type != 0 ? type : weapon_type_of(weapon);
    {
        std::scoped_lock lock(m_automation_mutex);
        const auto [it, added] = m_dry.try_emplace(weapon, dry_entry{resolved_type, clip});
        if (!added)
            it->second = dry_entry{resolved_type, clip};
        inserted = added;
    }
    if (!inserted)
        return;
    const auto engagement = weapon_changes_this_engagement(std::chrono::steady_clock::now());
    const auto window = weapon_changes_in_window(std::chrono::steady_clock::now());
    log_weapon_automation("dry_latch", weapon, resolved_type, clip, reloading_of(weapon), reason,
                          "latched until clip > 0", candidate_clip, candidate_reloading, engagement, window);
}

// The only place a dry latch is removed. A fresh clip read above zero is the sole clear condition; switching,
// re-equipping, a handle refresh or a feature tick cannot clear it.
void auto_bonus_t::refresh_weapon_latches()
{
    struct cleared_entry
    {
        std::uintptr_t weapon{};
        std::uint32_t type{};
        int clip{};
    };
    std::vector<cleared_entry> cleared{};
    {
        std::scoped_lock lock(m_automation_mutex);
        for (auto it = m_dry.begin(); it != m_dry.end();)
        {
            const auto clip = clip_of(it->first);
            if (clip > 0)
            {
                cleared.push_back({it->first, it->second.type, clip});
                it = m_dry.erase(it);
                continue;
            }
            it->second.clip = clip;
            ++it;
        }
    }
    for (const auto& entry : cleared)
    {
        const auto engagement = weapon_changes_this_engagement(std::chrono::steady_clock::now());
        const auto window = weapon_changes_in_window(std::chrono::steady_clock::now());
        log_weapon_automation("dry_clear", entry.weapon, entry.type, entry.clip, false, "dry latch",
                              "clip above zero", -1, false, engagement, window);
    }
}

bool auto_bonus_t::weapon_dry(const std::uintptr_t weapon) const
{
    if (!weapon)
        return false;
    std::scoped_lock lock(m_automation_mutex);
    return m_dry.contains(weapon);
}

bool auto_bonus_t::has_dry_weapon() const
{
    std::scoped_lock lock(m_automation_mutex);
    return !m_dry.empty();
}

std::string auto_bonus_t::weapon_latch_summary() const
{
    std::scoped_lock lock(m_automation_mutex);
    if (m_dry.empty())
        return "none";
    std::string summary;
    for (const auto& [weapon, entry] : m_dry)
    {
        if (!summary.empty())
            summary += ',';
        summary += std::format("0x{:x}:{}", static_cast<std::uint64_t>(weapon), entry.clip);
    }
    return summary;
}

bool auto_bonus_t::begin_bonus_tap()
{
    std::scoped_lock lock(m_automation_mutex);
    if (m_pickup_pending || !m_dry.empty() || owner() != weapon_owner::none ||
        std::chrono::steady_clock::now() < m_auto_switch_hold_until || m_change_stand_down ||
        m_change_history.size() >= static_cast<std::size_t>(k_max_window_changes))
        return false;
    if (!claim(weapon_owner::bonus))
        return false;
    // The reservation is set before the key goes down and cleared only when the key is released. Auto Weapon Switch's
    // claim takes this same mutex, so it cannot put a slot key into the pickup window.
    m_pickup_pending = true;
    // The game can apply the pickup after the 40 ms key has been released. Keep the change observer in this
    // automation's context for the same window used to identify the bonus weapon.
    m_tap_activity_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
    return true;
}

void auto_bonus_t::end_bonus_tap()
{
    std::scoped_lock lock(m_automation_mutex);
    m_pickup_pending = false;
    if (!m_key.pressed)
        release(weapon_owner::bonus);
}

bool auto_bonus_t::claim_auto_switch_slot()
{
    std::scoped_lock lock(m_automation_mutex);
    if (m_pickup_pending)
        return false;
    return claim(weapon_owner::auto_switch);
}

bool auto_bonus_t::pickup_tap_pending() const
{
    std::scoped_lock lock(m_automation_mutex);
    return m_pickup_pending;
}

void auto_bonus_t::reset_weapon_automation()
{
    {
        std::scoped_lock lock(m_automation_mutex);
        m_dry.clear();
        m_change_history.clear();
        m_last_active = 0;
        m_engagement_changes = 0;
        m_change_stand_down = false;
        m_pickup_pending = false;
        m_tap_activity_until = {};
        m_auto_switch_hold_until = {};
    }
    m_bonus_weapon.store(0, std::memory_order_release);
}

bool auto_bonus_t::observe_weapon(const std::uintptr_t weapon, const std::chrono::steady_clock::time_point now)
{
    if (!weapon)
        return false;
    bool limit_reached = false;
    int engagement = 0;
    int window = 0;
    int clip = 0;
    bool reloading = false;
    std::uint32_t type = 0;
    {
        std::scoped_lock lock(m_automation_mutex);
        if (m_last_active == 0)
        {
            m_last_active = weapon;
            return false;
        }
        if (weapon == m_last_active)
            return false;
        m_last_active = weapon;
        const bool automation_context = !m_dry.empty() || owner() != weapon_owner::none ||
                                        now < m_tap_activity_until || now < m_auto_switch_hold_until;
        if (!automation_context)
            return false;

        while (!m_change_history.empty() && now - m_change_history.front() >= k_change_window)
            m_change_history.pop_front();
        ++m_engagement_changes;
        m_change_history.push_back(now);
        engagement = m_engagement_changes;
        window = static_cast<int>(m_change_history.size());
        if (!m_change_stand_down &&
            (m_engagement_changes >= k_max_engagement_changes || window >= k_max_window_changes))
        {
            m_change_stand_down = true;
            limit_reached = true;
            clip = clip_of(weapon);
            reloading = reloading_of(weapon);
            type = weapon_type_of(weapon);
        }
    }
    if (!limit_reached)
        return false;

    log_weapon_automation("limit_reached", weapon, type, clip, reloading, "shared weapon change cap",
                          std::format("standing down: {}/{} this engagement, {}/{} in 5 s", engagement,
                                      k_max_engagement_changes, window, k_max_window_changes),
                          clip, reloading, engagement, window);
    return true;
}

// An engagement ends when no known weapon is dry and the weapon in hand is loaded and not reloading. The rolling
// 5-second history deliberately survives: it limits the next engagement too.
void auto_bonus_t::refresh_weapon_engagement(const std::chrono::steady_clock::time_point now)
{
    std::uintptr_t weapon{};
    std::uint32_t type{};
    int clip{};
    int engagement{};
    int window{};
    {
        std::scoped_lock lock(m_automation_mutex);
        while (!m_change_history.empty() && now - m_change_history.front() >= k_change_window)
            m_change_history.pop_front();
        if (!m_dry.empty() || owner() != weapon_owner::none || m_engagement_changes == 0)
            return;
        weapon = game::local_player().weapon();
        type = game::local_player().weapon_type();
        clip = clip_of(weapon);
        if (!weapon || !game::rules::is_firearm(type) || clip <= 0 || reloading_of(weapon))
            return;
        engagement = m_engagement_changes;
        window = static_cast<int>(m_change_history.size());
        m_engagement_changes = 0;
        // The rolling 5 s cap outlives a single engagement. Keep the stand-down while the window is still full; the
        // next weapon_change_stand_down() call clears it once the oldest change ages out.
        if (m_change_history.size() < static_cast<std::size_t>(k_max_window_changes))
            m_change_stand_down = false;
    }
    log_weapon_automation("engagement_end", weapon, type, clip, false, "loaded and idle",
                          std::format("counters reset; {} changes in the rolling 5 s", window), -1, false,
                          engagement, window);
}

bool auto_bonus_t::weapon_change_stand_down(const std::chrono::steady_clock::time_point now)
{
    refresh_weapon_engagement(now);
    std::scoped_lock lock(m_automation_mutex);
    while (!m_change_history.empty() && now - m_change_history.front() >= k_change_window)
        m_change_history.pop_front();
    if (m_change_history.size() >= static_cast<std::size_t>(k_max_window_changes))
        m_change_stand_down = true;
    else if (m_engagement_changes == 0)
        m_change_stand_down = false;
    return m_change_stand_down;
}

int auto_bonus_t::weapon_changes_this_engagement(const std::chrono::steady_clock::time_point now)
{
    refresh_weapon_engagement(now);
    std::scoped_lock lock(m_automation_mutex);
    return m_engagement_changes;
}

int auto_bonus_t::weapon_changes_in_window(const std::chrono::steady_clock::time_point now)
{
    refresh_weapon_engagement(now);
    std::scoped_lock lock(m_automation_mutex);
    while (!m_change_history.empty() && now - m_change_history.front() >= k_change_window)
        m_change_history.pop_front();
    return static_cast<int>(m_change_history.size());
}

void auto_bonus_t::note_auto_switch_activity(const std::chrono::steady_clock::time_point now)
{
    std::scoped_lock lock(m_automation_mutex);
    m_auto_switch_hold_until = std::max(m_auto_switch_hold_until, now + k_auto_switch_hold_off);
}

std::chrono::steady_clock::time_point auto_bonus_t::auto_switch_hold_until() const
{
    std::scoped_lock lock(m_automation_mutex);
    return m_auto_switch_hold_until;
}

// A pickup tap is a blind press of the pickup key. The game acts on it a frame or two later, so the active weapon is
// sampled for a short window after the key is released. A firearm in hand that was not there when the tap began is the
// bonus weapon. Auto Weapon Switch activity cancels the observation so its slot key cannot be mistaken for a bonus.
void auto_bonus_t::observe_bonus_weapon(const std::chrono::steady_clock::time_point now)
{
    if (m_tap_watch_until == std::chrono::steady_clock::time_point{})
        return;
    if (now >= m_tap_watch_until || owner() == weapon_owner::auto_switch ||
        now < auto_switch_hold_until())
    {
        m_tap_watch_until = {};
        return;
    }
    const auto current = game::local_player().weapon();
    if (!current || current == m_tap_weapon || !game::rules::is_firearm(game::local_player().weapon_type()))
        return;
    m_bonus_weapon.store(current, std::memory_order_release);
    m_tap_watch_until = {};
    log_bonus_decision("bonus_observed", "pickup produced a firearm", true);
}

bool auto_bonus_t::bonus_weapon_needs_time()
{
    const auto bonus = m_bonus_weapon.load(std::memory_order_acquire);
    if (!bonus)
        return false;
    const auto clip = clip_of(bonus);
    const auto reloading = reloading_of(bonus);
    if (clip <= 0)
        latch_weapon_dry(bonus, weapon_type_of(bonus), clip, "bonus weapon dry", clip, reloading);
    return clip <= 0 || reloading;
}

void auto_bonus_t::log_bonus_decision(const char* reason, const char* result, const bool force)
{
    const auto now = std::chrono::steady_clock::now();
    if (!force && result == m_last_log_reason && now < m_next_log)
        return;
    m_last_log_reason = result;
    m_next_log = now + k_skip_log_interval;

    const auto current = game::local_player().weapon();
    const auto type = game::local_player().weapon_type();
    const auto clip = clip_of(current);
    const auto reloading = reloading_of(current);
    const auto bonus = m_bonus_weapon.load(std::memory_order_acquire);
    const auto bonus_clip = bonus ? clip_of(bonus) : -1;
    const auto bonus_reloading = bonus ? reloading_of(bonus) : false;
    std::string candidate{};
    if (bonus)
        candidate = std::format("bonus=0x{:x} clip={} reload={} latched={}", static_cast<std::uint64_t>(bonus),
                                bonus_clip, bonus_reloading ? 1 : 0, weapon_dry(bonus) ? 1 : 0);
    else
        candidate = "bonus=none";
    const auto engagement = weapon_changes_this_engagement(now);
    const auto window = weapon_changes_in_window(now);
    log_weapon_automation(reason, current, type, clip, reloading, candidate, result, bonus_clip, bonus_reloading,
                          engagement, window);
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
    {
        release(weapon_owner::bonus);
        end_bonus_tap();
    }
    return true;
}

void auto_bonus_t::reset()
{
    if (m_key.pressed)
        (void)send_key(false);
    if (!m_key.pressed)
    {
        m_key = {};
        end_bonus_tap();
    }
    m_next_tap = {};
    m_press_started = {};
    m_tap_weapon = 0;
    m_tap_watch_until = {};
    // Keep m_bonus_weapon across a menu pause or a feature toggle. Forgetting it would allow a blind pickup tap to
    // re-equip the same dry weapon; the shared latch is only cleared by a fresh clip read above zero.
}

void auto_bonus_t::tick()
{
    const auto now = std::chrono::steady_clock::now();
    if (!game::local_player().alive())
    {
        if (m_was_alive)
            reset_weapon_automation();
        m_was_alive = false;
        reset();
        return;
    }
    m_was_alive = true;

    // Observe before the feature-off early return: Auto Weapon Switch and Auto Knife also run while this feature is off,
    // and every active-weapon change in a dry/owned engagement has to reach the shared cap.
    refresh_weapon_latches();
    const auto current = game::local_player().weapon();
    const auto current_type = game::local_player().weapon_type();
    const auto current_clip = clip_of(current);
    const auto current_reloading = reloading_of(current);
    if (current && game::rules::is_firearm(current_type) && current_clip <= 0)
        latch_weapon_dry(current, current_type, current_clip, "active weapon dry", current_clip, current_reloading);
    (void)observe_weapon(current, now);

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

    const auto current_owner = owner();
    const auto auto_knife_active = current_owner == weapon_owner::knife;
    const auto health_shot_active = current_owner == weapon_owner::health_shot;

    // Auto Weapon Switch owns the slot from its release handshake through the switch-back or give-up path. A pickup
    // tap now would put a second slot key in flight, and its tap observation must not mistake that switch for a bonus.
    if (current_owner == weapon_owner::auto_switch)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_tap_watch_until = {};
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "owner is auto_switch");
        return;
    }

    const auto hold_until = auto_switch_hold_until();
    if (now < hold_until)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_tap_watch_until = {};
        m_next_tap = std::max(m_next_tap, hold_until);
        log_bonus_decision("bonus_skip", "hold-off after auto switch");
        return;
    }

    if (weapon_change_stand_down(now))
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_tap_watch_until = {};
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "weapon change limit");
        return;
    }

    // Auto Knife owns the weapon slot. Stop the current tap and keep the next one deferred until Auto Knife clears
    // the owner. Its weapon changes are counted by the shared observer.
    if (auto_knife_active)
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_tap_watch_until = {};
        m_next_tap = now + k_retry;
        m_auto_knife_was_active = true;
        log_bonus_decision("bonus_skip", "owner is auto_knife");
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
        m_tap_watch_until = {};
        m_next_tap = now + k_retry;
        m_auto_health_shot_was_active = true;
        log_bonus_decision("bonus_skip", "owner is health_shot");
        return;
    }
    if (m_auto_health_shot_was_active)
    {
        m_auto_health_shot_was_active = false;
        m_next_tap = std::max(m_next_tap, now + k_retry);
    }

    observe_bonus_weapon(now);

    // The active firearm itself needs time. This covers a manually held bonus weapon that Auto Bonus has not observed
    // yet, and stops a pickup tap from switching away from its reload.
    if (current && game::rules::is_firearm(current_type) && (current_clip <= 0 || current_reloading))
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "active weapon needs reload");
        return;
    }

    // The remembered bonus weapon may not be in hand: after Auto Weapon Switch takes the slot away, its clip still
    // reads zero. It must not be picked up again until a fresh read shows ammo.
    if (bonus_weapon_needs_time())
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "bonus weapon latched dry");
        return;
    }
    // A pickup tap is blind about which weapon it will select. Any dry latch means the selection could be that empty
    // weapon, so Auto Bonus stays off the key until every known latch has been cleared by a fresh ammo read.
    if (has_dry_weapon())
    {
        if (m_key.pressed && !send_key(false))
            return;
        m_press_started = {};
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "dry weapon latched");
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
        log_bonus_decision("bonus_skip", "text entry active");
        return;
    }

    // Last check immediately before the key goes down: another worker may have taken the slot, latched the bonus dry
    // or hit the change cap since the top of this tick.
    if (weapon_change_stand_down(now) || bonus_weapon_needs_time() || has_dry_weapon())
    {
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "state changed before tap");
        return;
    }
    if (!begin_bonus_tap())
    {
        m_next_tap = now + k_retry;
        log_bonus_decision("bonus_skip", "slot taken before tap");
        return;
    }
    // Sample the weapon before the key goes down; the game can apply the pickup before the next tick, and the tap
    // watch must compare against what was in hand when the key was sent.
    m_tap_weapon = game::local_player().weapon();
    m_tap_watch_until = {};
    if (send_key(true))
    {
        m_press_started = now;
        log_bonus_decision("bonus_tap", "pickup key sent", true);
    }
    else
    {
        end_bonus_tap();
        m_next_tap = now + k_retry;
    }
}

} // namespace features::misc

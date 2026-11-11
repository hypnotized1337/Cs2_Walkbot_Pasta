#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <foundation/math/vector.hpp>

namespace features::aimbot::trigger_debug
{
// A trigger pass ends in one of these. `stuck` is a blocked state that the detector has decided is not explained by a
// normal wait (held button, impossible timer, unknown state).
enum class decision : std::uint8_t
{
    idle,
    fire,
    blocked,
    waiting,
    stuck,
};

// Short reason codes. Keep these stable: the HUD prints them verbatim and the stuck log is meant to be searchable.
enum class reason : std::uint8_t
{
    unspecified,
    fire,
    disabled,
    independent_seed,
    key_not_held,
    walkbot_disabled,
    weapon_not_ready,
    reloading,
    no_target,
    teammate,
    target_dead,
    target_stale,
    target_lost,
    invulnerable,
    not_visible,
    outside_hitbox,
    smoke,
    min_damage,
    lethal_only,
    hitchance,
    delay_pending,
    cooldown,
    auto_stop_wait,
    input_rejected,
    held_pending,
    schedule_pending,
    legit_check,
};

[[nodiscard]] inline const char* name(const reason value) noexcept
{
    switch (value)
    {
        case reason::unspecified:
            return "UNSPECIFIED";
        case reason::fire:
            return "FIRE";
        case reason::disabled:
            return "DISABLED";
        case reason::independent_seed:
            return "INDEPENDENT_SEED";
        case reason::key_not_held:
            return "KEY_NOT_HELD";
        case reason::walkbot_disabled:
            return "WALKBOT_DISABLED";
        case reason::weapon_not_ready:
            return "WEAPON_NOT_READY";
        case reason::reloading:
            return "RELOADING";
        case reason::no_target:
            return "NO_TARGET";
        case reason::teammate:
            return "TEAMMATE";
        case reason::target_dead:
            return "TARGET_DEAD";
        case reason::target_stale:
            return "TARGET_STALE";
        case reason::target_lost:
            return "TARGET_LOST";
        case reason::invulnerable:
            return "INVULNERABLE";
        case reason::not_visible:
            return "NOT_VISIBLE";
        case reason::outside_hitbox:
            return "OUTSIDE_HITBOX";
        case reason::smoke:
            return "SMOKE";
        case reason::min_damage:
            return "MIN_DAMAGE";
        case reason::lethal_only:
            return "LETHAL_ONLY";
        case reason::hitchance:
            return "HITCHANCE";
        case reason::delay_pending:
            return "DELAY_PENDING";
        case reason::cooldown:
            return "COOLDOWN";
        case reason::auto_stop_wait:
            return "AUTO_STOP_WAIT";
        case reason::input_rejected:
            return "INPUT_REJECTED";
        case reason::held_pending:
            return "HELD_PENDING";
        case reason::schedule_pending:
            return "SCHEDULE_PENDING";
        case reason::legit_check:
            return "LEGIT_CHECK";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline const char* name(const decision value) noexcept
{
    switch (value)
    {
        case decision::idle:
            return "IDLE";
        case decision::fire:
            return "FIRE";
        case decision::blocked:
            return "NO FIRE";
        case decision::waiting:
            return "WAITING";
        case decision::stuck:
            return "STUCK";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline bool is_waiting(const reason value) noexcept
{
    return value == reason::delay_pending || value == reason::cooldown || value == reason::auto_stop_wait ||
           value == reason::hitchance;
}

struct target
{
    std::uintptr_t pawn{};
    foundation::vec3 label{};
    reason why{reason::unspecified};
    int hitbox{-1};
    int hitgroup{-1};
    int required_parts{};
    float hit_distance{-1.0f};
    float damage{};
    float target_age_ms{};
    int health{};
    bool on_target{};
};

struct decision_entry
{
    std::chrono::steady_clock::time_point at{};
    decision state{decision::idle};
    reason why{reason::unspecified};
    std::uintptr_t pawn{};
    int hitbox{-1};
    float delay_ms{};
    float cooldown_ms{};
    float target_age_ms{};
    bool stuck{};
};

// The combat worker fills one of these per trigger pass. It is local to the pass; the persistent ring lives on the
// aimbot instance.
struct frame
{
    bool enabled{};
    decision state{decision::blocked};
    reason why{reason::unspecified};
    std::uintptr_t pawn{};
    int hitbox{-1};
    int hitgroup{-1};
    int required_parts{};
    float damage{};
    float delay_ms{};
    float cooldown_ms{};
    float target_age_ms{};
    float hitchance{};
    float required_hitchance{};
    float min_damage{};
    int target_health{};
    bool on_target{};
    bool shot_fired{};
    bool trigger_active{};
    bool walk_autofire{};
    bool weapon_ready{};
    bool reloading{};
    int clip{};
    std::array<target, 24> targets{};
    std::size_t target_count{};

    void add_target(const target& value) noexcept
    {
        if (target_count < targets.size())
            targets[target_count++] = value;
    }
};

// A render-thread copy. The recent ring is intentionally shorter than the 100-entry combat ring; the full ring is
// written to the stuck log.
struct snapshot
{
    bool enabled{};
    decision state{decision::idle};
    reason why{reason::unspecified};
    std::uintptr_t pawn{};
    int hitbox{-1};
    int hitgroup{-1};
    int required_parts{};
    float damage{};
    float delay_ms{};
    float cooldown_ms{};
    float target_age_ms{};
    float hitchance{};
    float required_hitchance{};
    float min_damage{};
    int target_health{};
    float state_ms{};
    float on_target_ms{};
    float last_shot_ms{-1.0f};
    float update_age_ms{};
    bool stuck{};
    std::array<target, 24> targets{};
    std::size_t target_count{};
    std::array<decision_entry, 8> recent{};
    std::size_t recent_count{};
};
} // namespace features::aimbot::trigger_debug

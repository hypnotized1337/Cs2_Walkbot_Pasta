#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <features/aimbot/aimbot.hpp>
#include <config/combat.hpp>
#include <config/settings.hpp>
#include <game/body/body.hpp>
#include <game/input/bindings.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <simulation/aim_parts.hpp>
#include <simulation/collision.hpp>

#include <chrono>

namespace features::misc
{
namespace
{
// Never hold the aimbot indefinitely if the game does not report the duck (for example after a game update).
constexpr auto k_duck_wait = std::chrono::milliseconds(100);
// A trigger target is published as a view target; keep the crouch alive across the one-tick gap between aim ticks.
constexpr auto k_view_target_grace = std::chrono::milliseconds(250);
// Once a target has been crouched for, bridge short gaps in combat target publication. A longer gap means the target
// was lost and the duck is released.
constexpr auto k_target_loss_grace = std::chrono::milliseconds(250);
// A duck lowers the eye by roughly this much in CS2. The exposure check runs from the lower eye so a target that is
// only peeking over cover is not crouched away from.
constexpr float k_crouch_eye_drop = 18.0f;
// "Decently exposed": more than one hitbox in sight and at least one below the head. A head above a wall does not
// count: the same wall hides it as soon as the view drops.
constexpr int k_exposure_min_visible = 2;
constexpr int k_exposure_min_below_head = 1;
// The gate has to stay open before the duck goes down, and has to stay closed for a moment before it is lifted, so a
// single sampled frame cannot start the crouch/release cycle.
constexpr auto k_exposure_confirm = std::chrono::milliseconds(100);
constexpr auto k_exposure_release_grace = std::chrono::milliseconds(150);
// After a target was hidden by the duck, wait this long before trying again. Standing back up usually restores the
// sight line, so without the pause the bot would immediately duck again.
constexpr auto k_exposure_retry = std::chrono::milliseconds(600);

// How much of the enemy a ray from `eye` can reach. `sampled` is false when the collision world is unavailable: the
// gate then fails open and keeps the old behaviour instead of disabling Auto Crouch on maps without collision data.
struct exposure
{
    int visible{};
    int below_head{};
    bool sampled{};

    [[nodiscard]] bool decent() const noexcept
    {
        return !sampled || (visible >= k_exposure_min_visible && below_head >= k_exposure_min_below_head);
    }
};

[[nodiscard]] exposure sample_exposure(const game::player_snapshot& player, const foundation::vec3& eye,
                                       const simulation::collision_world& world)
{
    exposure result{};
    // Nothing to judge by: fail open instead of silently disabling Auto Crouch on maps or frames the reader cannot
    // resolve.
    if (!world.valid() || !player.bones.is_valid() || player.hitboxes.count <= 0)
        return result;
    result.sampled = true;
    for (const auto& hitbox : player.hitboxes)
    {
        if (hitbox.index < 0 || !player.bones.covers(hitbox.bone))
            continue;
        const auto box = game::place_hitbox(player.bones.bones[static_cast<std::size_t>(hitbox.bone)], hitbox);
        if (world.trace_ray(eye, box.center()).hit)
            continue;
        ++result.visible;
        if (simulation::aim_part_of(hitbox, game::hitbox_data().hitgroup_from_hitbox(hitbox.index)) !=
            config::combat_profile::aim_part::head)
            ++result.below_head;
    }
    return result;
}
} // namespace

bool auto_crouch_t::set_duck(const bool pressed)
{
    if (!m_duck.binding)
        m_duck.binding = game::input_bindings().resolve(game::input_action::duck);
    return input_lifecycle::transition(
        m_duck, pressed,
        [](const game::input_binding& binding, const bool down)
        {
            return binding.device == game::input_device::keyboard &&
                   platform::windows::bound_input().key(binding.virtual_key, down);
        });
}

void auto_crouch_t::reset()
{
    if (m_duck.pressed)
        (void)set_duck(false);
    if (!m_duck.pressed)
        m_duck = {};
    m_engaged_target = 0;
    m_target_seen_at = {};
    // The retry pause is kept: it is what stops a crouch that hid the enemy from being re-pressed on the next tick.
    m_exposure_ok_at = {};
    m_exposure_bad_at = {};
    m_waiting_since = {};
    m_waiting_for_duck = false;
    m_duck_wait_timed_out = false;
}

void auto_crouch_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.auto_crouch || game::input_bindings().text_entry_active())
    {
        reset();
        return;
    }

    // The knife needs full movement speed for the close-range chase. Never hold a duck for it; auto-crouch resumes as
    // soon as a firearm is drawn again.
    if (game::local_player().weapon_type() == game::rules::knife)
    {
        reset();
        return;
    }

    const auto& combat = snapshot->combat.get(game::local_player().weapon_type());
    const auto& aim = combat.aimbot;
    const auto& trigger = combat.triggerbot;
    const auto aim_active =
        aim.enabled && config::combat_profile::activation_active(aim.activation_mode, aim.key);
    const auto trigger_active =
        trigger.enabled && config::combat_profile::activation_active(trigger.activation_mode, trigger.key);
    if ((!aim_active && !trigger_active) || !game::local_player().alive())
    {
        reset();
        return;
    }

    const auto players = game::world().players();
    if (!players)
    {
        reset();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    // The aimbot stores this target at the end of its previous tick. The view target carries a trigger target through
    // the gap before the next tick, and the crosshair covers the first trigger tick.
    auto target = features::aimbot::aim().current_engaged_target();
    auto target_is_live = target != 0;
    if (!target)
    {
        const auto target_ticks = features::aimbot::aim().view_target_ticks();
        const auto now_ticks = now.time_since_epoch().count();
        const auto grace_ticks =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(k_view_target_grace).count();
        if (target_ticks != 0 && now_ticks >= target_ticks && now_ticks - target_ticks <= grace_ticks)
            target = features::aimbot::aim().view_engaged_target();
    }
    if (!target)
    {
        const auto crosshair = game::local_player().crosshair_id();
        if (crosshair > 0 && crosshair < 0x7fff)
            target = game::entity_index().lookup_index(static_cast<std::uint32_t>(crosshair));
        target_is_live = target != 0;
    }

    const auto find_player = [&players](const std::uintptr_t pawn) -> const game::player_snapshot*
    {
        for (const auto& player : *players)
        {
            if (player.pawn == pawn)
                return &player;
        }
        return nullptr;
    };

    auto target_alive = false;
    if (target)
    {
        if (const auto* player = find_player(target))
        {
            // A selected target that is dead or no longer an enemy ends the engagement immediately.
            if (player->health <= 0 || !game::local_player().is_enemy(player->team))
            {
                reset();
                return;
            }
            target_alive = true;
        }
    }

    // Combat publishes its target once per tick. Keep the duck while that same pawn is still alive even when the
    // publication is briefly missing; a target switch is picked up above and keeps the held key untouched.
    if (!target_alive && m_engaged_target != 0 && now - m_target_seen_at <= k_target_loss_grace)
    {
        if (const auto* player = find_player(m_engaged_target);
            player && player->health > 0 && game::local_player().is_enemy(player->team))
        {
            target = m_engaged_target;
            target_is_live = false;
            target_alive = true;
        }
    }

    // Only a dead or genuinely lost target ends the engagement and releases the duck.
    if (!target_alive)
    {
        reset();
        return;
    }
    if (m_engaged_target != target)
    {
        m_engaged_target = target;
        m_target_seen_at = now;
    }
    else if (target_is_live)
    {
        m_target_seen_at = now;
    }

    // Exposure gate. A duck drops the eye by roughly 18 units, and against an enemy who is only peeking over cover that
    // sight line is the whole fight: the crouch hid him, releasing brought him back, and the bot cycled between the two
    // without ever firing. Duck only while the enemy stays decently exposed from the eye the crouch will leave us with.
    if (now < m_exposure_retry_at)
    {
        reset();
        return;
    }
    if (const auto* engaged = find_player(target))
    {
        auto eye = game::camera().origin();
        if (!game::local_player().ducked())
            eye.z -= k_crouch_eye_drop;
        if (sample_exposure(*engaged, eye, simulation::collision()).decent())
        {
            // The gate has to stay open for a moment before the duck goes down, so one lucky sample cannot start it.
            if (m_exposure_ok_at == std::chrono::steady_clock::time_point{})
                m_exposure_ok_at = now;
            m_exposure_bad_at = {};
            if (!m_duck.pressed && now - m_exposure_ok_at < k_exposure_confirm)
                return;
        }
        else
        {
            // ...and stay closed for a moment before a held duck is lifted, so a single sampled frame cannot end it.
            m_exposure_ok_at = {};
            if (m_exposure_bad_at == std::chrono::steady_clock::time_point{})
                m_exposure_bad_at = now;
            const auto grace = m_duck.pressed ? k_exposure_release_grace : std::chrono::milliseconds(0);
            if (now - m_exposure_bad_at >= grace)
            {
                if (m_duck.pressed)
                    m_exposure_retry_at = now + k_exposure_retry;
                reset();
                return;
            }
        }
    }

    // Keep sending the held state every tick. transition() only emits a key event when the state changes, so this does
    // not repeat key-down events while the target stays alive.
    if (!set_duck(true))
    {
        m_waiting_for_duck = false;
        m_waiting_since = {};
        return;
    }

    // Once the game reports the duck, the combat worker may run the aimbot and triggerbot. If its snapshot never
    // reports it, fail open instead of blocking combat forever while still holding the key.
    if (game::local_player().ducked())
    {
        m_waiting_for_duck = false;
        m_waiting_since = {};
        m_duck_wait_timed_out = false;
        return;
    }
    if (m_duck_wait_timed_out)
        return;
    if (!m_waiting_for_duck)
    {
        m_waiting_since = now;
        m_waiting_for_duck = true;
        return;
    }
    if (now - m_waiting_since >= k_duck_wait)
    {
        m_waiting_for_duck = false;
        m_duck_wait_timed_out = true;
    }
}

} // namespace features::misc

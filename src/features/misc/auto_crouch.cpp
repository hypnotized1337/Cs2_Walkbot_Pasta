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
// Lowering the camera can put a wall between the eye and an enemy who was only just visible while standing. The
// crouch decision is therefore made from the eye the duck would produce, not the current standing eye. Once the duck
// is committed, hold through a narrower angle; only losing every hitbox makes it stand back up. This keeps the old
// benefit of not standing up after every shot while avoiding a crouch that hides a barely exposed target.
constexpr auto k_sight_confirm = std::chrono::milliseconds(100);
constexpr auto k_sight_release_grace = std::chrono::milliseconds(150);
// CS2's standing eye is 64 units and its crouched eye is 46; this is the drop the crouched sight sample uses.
constexpr auto k_crouch_eye_drop = 18.0f;
constexpr auto k_min_crouch_hitboxes = 2;

// What `eye` can reach. `sampled` is false when the collision world is unavailable: the gate then fails open and
// holds the duck instead of disabling Auto Crouch on maps without collision data.
struct sight
{
    int visible{};
    int total{};
    bool sampled{};

    [[nodiscard]] bool seen() const noexcept { return !sampled || visible > 0; }

    // The initial crouch is only allowed when the lower eye keeps enough of the enemy exposed. A map without
    // collision data still fails open. A single-hitbox target (rare) is accepted rather than making Auto Crouch
    // permanently unusable for it.
    [[nodiscard]] bool exposed_for_crouch() const noexcept
    {
        return !sampled || visible >= std::min(total, k_min_crouch_hitboxes);
    }
};

[[nodiscard]] sight sample_sight(const game::player_snapshot& player, const foundation::vec3& eye,
                                 const simulation::collision_world& world)
{
    sight result{};
    // Nothing to judge by: fail open instead of silently disabling Auto Crouch on maps or frames the reader
    // cannot resolve.
    if (!world.valid() || !player.bones.is_valid() || player.hitboxes.count <= 0)
        return result;
    result.sampled = true;
    for (const auto& hitbox : player.hitboxes)
    {
        if (hitbox.index < 0 || !player.bones.covers(hitbox.bone))
            continue;
        ++result.total;
        const auto box = game::place_hitbox(player.bones.bones[static_cast<std::size_t>(hitbox.bone)], hitbox);
        if (!world.trace_ray(eye, box.center()).hit)
            ++result.visible;
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
    m_sight_ok_at = {};
    m_sight_bad_at = {};
    m_waiting_since = {};
    m_waiting_for_duck = false;
    m_duck_wait_timed_out = false;
}

void auto_crouch_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot || !snapshot->general.m_walk_bot.enabled || !snapshot->general.auto_crouch ||
        game::input_bindings().text_entry_active())
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

    // Sight gate. A crouch lowers the eye, so the initial decision samples the eye the duck would produce. If a
    // barely exposed enemy would disappear behind the lower viewpoint, stay standing and wait for a better angle.
    // Once committed, hold through a narrow angle; only a complete loss of the target releases the duck.
    if (const auto* engaged = find_player(target))
    {
        const auto eye = game::camera().origin();
        const auto gate_eye = game::local_player().ducked()
                                  ? eye
                                  : foundation::vec3{eye.x, eye.y, eye.z - k_crouch_eye_drop};
        const auto gate = sample_sight(*engaged, gate_eye, simulation::collision());
        const auto enough = m_duck.pressed ? gate.seen() : gate.exposed_for_crouch();
        if (enough)
        {
            // Stay open for a moment before the duck goes down, so one lucky sample cannot start it.
            if (m_sight_ok_at == std::chrono::steady_clock::time_point{})
                m_sight_ok_at = now;
            m_sight_bad_at = {};
            if (!m_duck.pressed && now - m_sight_ok_at < k_sight_confirm)
                return;
        }
        else
        {
            m_sight_ok_at = {};
            if (!m_duck.pressed)
            {
                // The crouched eye would lose too much of the target: do not duck into the wall.
                m_sight_bad_at = {};
                return;
            }
            // Stay closed for a moment before a held duck is lifted, so one sampled frame cannot end it.
            if (m_sight_bad_at == std::chrono::steady_clock::time_point{})
                m_sight_bad_at = now;
            if (now - m_sight_bad_at >= k_sight_release_grace)
            {
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

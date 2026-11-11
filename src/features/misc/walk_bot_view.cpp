#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/aimbot/flick_profile.hpp>
#include <features/trigger/seed_trigger.hpp>
#include <foundation/identity.hpp>
#include <foundation/scope_exit.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <simulation/collision.hpp>
#include <simulation/nav_mesh.hpp>
#include <simulation/punch_snapshot.hpp>

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace features::misc
{
using namespace walk_detail;

// Turns the view the way a player looks where they walk, and only while nothing else turns it. "Something else" is
// another feature sending mouse movement, the aimbot or a trigger holding a target, or any rotation this walker did
// not send (the player's own mouse). It comes back after a human pause once all that has stopped; after a kill it
// comes back at once. The turn always eases in from rest.
void walk_bot_t::look(const player_state& player, const gaze& target, const bool blocked, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    // Publish the fight hand's error on every call, including the early returns below. The spray gate and its log read
    // these; a sample that stopped updating must be visible as stale, not mistaken for an old on-target reading.
    const bool fight_gaze = target.how == gaze::mode::fight;
    // The game builds the shot from the base view plus the bullet-scale punch, not from the rendered camera alone.
    // Aiming at the camera leaves the predicted impact above the target by the punch (the Follow Recoil gap). Read the
    // same two values the game uses so the fight hand closes its loop on where the bullets actually go.
    foundation::vec3 aim_punch{};
    if (fight_gaze && player.pawn)
        if (const auto punch = simulation::read_cached_punch(
                game::bound_memory().process, player.pawn,
                SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id)))
            aim_punch = *punch;
    auto impact_view = player.base_view + aim_punch;
    impact_view.x = std::clamp(impact_view.x, -89.0f, 89.0f);
    impact_view.y = foundation::wrap_yaw(impact_view.y);
    float aim_sample_error = 999.0f;
    float aim_sample_delta = 0.0f;
    int aim_sample_px_x = 0;
    int aim_sample_px_y = 0;
    bool aim_sample_valid = false;
    const foundation::scope_exit publish_aim_sample(
        [&]() noexcept
        {
            features::aimbot::publish_walk_autofire_aim(fight_gaze && aim_sample_valid, aim_sample_error,
                                                        aim_sample_delta, aim_sample_px_x, aim_sample_px_y);
        });
    if (fight_gaze)
    {
        const auto to = target.point - player.eye;
        const auto target_yaw = degrees(std::atan2(to.y, to.x));
        const auto target_pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
        aim_sample_error =
            std::hypot(foundation::wrap_yaw(target_yaw - impact_view.y), target_pitch - impact_view.x);
        aim_sample_valid = std::isfinite(aim_sample_error);
    }
    // Rear defense is an emergency turn and the knife chase has to face its target: both still take the view when
    // Turn Camera is off, unlike the walking camera.
    const bool knife = target.how == gaze::mode::fight && m_knife_target != 0;
    if (!snapshot->general.m_walk_bot.look && target.how != gaze::mode::rear && !knife)
    {
        aim_sample_valid = false; // no turn camera: the fight hand is not steering, whatever the angle says
        return;
    }
    if (now - m_view_last_step < std::chrono::milliseconds(4))
        return; // ~250 updates a second is plenty for a hand
    const auto dt = m_view_last_step == clock::time_point{}
                        ? 0.004f
                        : std::clamp(std::chrono::duration<float>(now - m_view_last_step).count(), 0.001f, 0.05f);
    m_view_last_step = now;

    // Rotation that was not sent by this walker.
    bool foreign = false;
    if (m_view_observed_valid)
    {
        const foundation::vec2 seen{player.view.x - m_view_observed.x,
                                    foundation::wrap_yaw(player.view.y - m_view_observed.y)};
        const auto pending_sqr = m_view_pending.x * m_view_pending.x + m_view_pending.y * m_view_pending.y;
        const auto consumed =
            pending_sqr > 1e-6f
                ? std::clamp((seen.x * m_view_pending.x + seen.y * m_view_pending.y) / pending_sqr, 0.0f, 1.0f)
                : 0.0f;
        const auto unexplained = std::hypot(seen.x - m_view_pending.x * consumed, seen.y - m_view_pending.y * consumed);
        m_view_pending = {m_view_pending.x * (1.0f - consumed), m_view_pending.y * (1.0f - consumed)};
        foreign = std::isfinite(unexplained) && unexplained > 0.35f;
    }
    m_view_observed = player.view;
    m_view_observed_valid = true;

    // The aimbot or a trigger holding a target. Once that target is dead the fight is over: neither its last
    // engagement nor the last aim movement holds the view any more, and there is no pause before turning back.
    const auto& aim = features::aimbot::aim();
    const bool engaged = recent(aim.view_engaged_ticks(), now, std::chrono::milliseconds(250));
    if (engaged)
        m_view_target = aim.view_engaged_target();
    const bool target_dead = m_view_target && game::bound_memory().process.load<std::int32_t>(
                                                  m_view_target + SCHEMA("C_BaseEntity", "m_iHealth"_id)) <= 0;
    const bool aim_holds = (engaged && !target_dead) || features::trigger::seed_trigger().input_pending();
    const bool pointer_busy =
        recent(platform::windows::bound_input().foreign_pointer_ticks(), now, std::chrono::milliseconds(200));
    const bool rear = target.how == gaze::mode::rear;
    foreign = (foreign && !target_dead) || aim_holds || (pointer_busy && !target_dead);
    // In its own fight, and for a moment after being hit, the view is the walk bot's: the recoil of its autofire, the
    // aim punch of a hit and the trigger bot's "engaged" mark are not another hand, and must not stop the aim (that
    // was the pause after every shot or hit). A rear threat takes the view at once: this is the one turn that must not
    // wait for the aimbot's or the player's hand to settle.
    const bool own_view =
        target.how == gaze::mode::fight || rear || now - m_hit_at < std::chrono::milliseconds(500);
    if (own_view)
    {
        foreign = false;
        m_view_resume_at = std::min(m_view_resume_at, now);
        if (rear)
        {
            m_view_holding = false;
            m_view_ramp_from = now;
        }
    }

    if (blocked || foreign)
    {
        // Hands off. Coming back takes a human moment after the other hand stops, then the turn eases in.
        if (!m_view_holding)
        {
            m_view_holding = true;
            m_view_pause = std::chrono::milliseconds(std::uniform_int_distribution<int>(450, 1200)(m_random));
        }
        m_view_resume_at = now + m_view_pause;
        m_view_ramp_from = m_view_resume_at;
        m_view_velocity = {};
        m_view_pending = {};
        m_mouse_remainder = {};
        aim_sample_valid = false; // the hand is not steering while another hand owns the view
        return;
    }
    if (target_dead && m_view_holding)
    {
        // The kill ended the fight: turn back now.
        m_view_resume_at = now;
        m_view_ramp_from = now;
    }
    if (target_dead)
        m_view_target = 0;
    m_view_holding = false;
    if (!rear && now < m_view_resume_at)
    {
        aim_sample_valid = false; // waiting out the other hand: no new aim command is going out
        return;
    }

    const auto to = target.point - player.eye;
    if (flat(to).length() < 8.0f && target.how == gaze::mode::route)
        return;
    auto target_yaw = degrees(std::atan2(to.y, to.x));
    // Walking along the route the eyes rest a little below the horizon; every other gaze looks at its point.
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(m_random) < dt * 0.25f)
        m_rest_pitch = std::uniform_real_distribution<float>(1.5f, 8.0f)(m_random);
    float target_pitch = m_rest_pitch;
    if (target.how == gaze::mode::fixed_pitch)
        target_pitch = target.pitch;
    else if (target.how != gaze::mode::route)
        target_pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
    target_pitch = std::clamp(target_pitch, -80.0f, 80.0f);
    // Looking around (walking, holding a corner) stays about at eye level: a player does not stare at the floor or
    // the sky, even when the corner held is on another floor. The fight, doors, breakables and ladders look where
    // they must.
    if (target.how == gaze::mode::route || target.how == gaze::mode::check)
        target_pitch = std::clamp(target_pitch, -8.0f, 10.0f);
    const bool fight = target.how == gaze::mode::fight;
    // The fight hand is not a machine: a little micro movement on both angles while it tracks, so the view never sits
    // perfectly still on a head. It stops with the fight, so walking and looking at corners are unaffected.
    if (fight)
    {
        const auto jitter = std::clamp(snapshot->general.m_walk_bot.aim_jitter, 0.0f, 1.5f);
        if (jitter > 0.0f)
        {
            m_noise_phase += dt;
            target_pitch += jitter * (std::sin(m_noise_phase * 37.0f) * 0.6f +
                                      std::sin(m_noise_phase * 71.0f + 1.7f) * 0.4f);
            target_yaw += jitter * (std::sin(m_noise_phase * 41.0f + 0.9f) * 0.6f +
                                    std::sin(m_noise_phase * 79.0f + 2.3f) * 0.4f);
        }
    }
    const foundation::vec2 control_view = fight ? foundation::vec2{impact_view.x, impact_view.y}
                                                : foundation::vec2{player.view.x, player.view.y};
    const foundation::vec2 error{target_pitch - control_view.x, foundation::wrap_yaw(target_yaw - control_view.y)};
    const auto error_size = std::hypot(error.x, error.y);
    aim_sample_error = error_size;
    aim_sample_valid = std::isfinite(aim_sample_error);

    // The hand. A clear change of where to look is a turn: minimum-jerk over a Fitts-law duration (quick and smooth,
    // no jump and no shake), each step covering its share of what is still left so a moving point is followed, bent
    // into a slight arc that is gone at its end. What remains after it, and any small drift of the point, is followed
    // smoothly. The fight is the fastest hand; the optional jitter above is the only movement added on top.
    const auto snapshot_speed = snapshot->general.m_walk_bot.aim_speed;
    const auto smoothing = std::clamp(snapshot->general.m_walk_bot.aim_smoothing, 0, 100) * 0.01f;
    const auto rear_smoothing = std::clamp(snapshot->general.m_walk_bot.rear_defense_smoothing, 0, 100) * 0.01f;
    const bool checking = target.how == gaze::mode::check || target.how == gaze::mode::aim;
    // Holding a corner: a calm, slow hand from afar, quicker the nearer the corner is.
    const auto urgency = std::clamp(target.urgency, 0.0f, 1.0f);
    const auto hand_speed = rear        ? std::lerp(3.2f, 0.9f, rear_smoothing)
                            : fight      ? std::clamp(snapshot_speed, 0.4f, 2.5f) * 1.3f
                            : checking   ? std::clamp(snapshot_speed, 0.4f, 2.5f) * (0.35f + 0.65f * urgency)
                                         : std::clamp(snapshot_speed, 0.4f, 2.5f) * 0.45f;
    const auto turn_threshold = rear ? 0.75f : fight ? 2.0f : checking ? 3.0f : 5.0f;
    const foundation::vec2 goal{target_pitch, target_yaw};
    // A turn is for a new spot only (the point moved in the world, not the angle drifting while walking past a fixed
    // spot): starting a new short turn every time the follow lagged a few degrees was the pulsing "shake" at walls.
    // A fight target is followed as it moves; only a big jump of it is a new turn.
    const bool new_spot = target.point.distance(m_turn_point) > (fight ? 96.0f : rear ? 64.0f : 48.0f);
    if (error_size > turn_threshold && new_spot)
    {
        m_turn_point = target.point;
        m_turn_active = true;
        m_turn_progress = 0.0f;
        m_turn_duration = rear ? std::lerp(0.06f, 0.45f, rear_smoothing)
                               : features::aimbot::flick::duration(error_size, hand_speed) *
                                     std::uniform_real_distribution<float>(0.92f, 1.1f)(m_random) *
                                     (1.0f + smoothing * 1.4f);
        m_turn_curve = std::uniform_real_distribution<float>(-0.12f, 0.12f)(m_random);
        m_turn_goal = goal;
    }
    foundation::vec2 step{};
    if (m_turn_active)
    {
        const auto from = m_turn_progress;
        const auto next = std::min(1.0f, from + dt / std::max(m_turn_duration, 1e-3f));
        const auto share = features::aimbot::flick::share(from, next);
        m_turn_progress = next;
        step = {error.x * share, error.y * share};
        const auto arc = m_turn_curve * std::sin(next * std::numbers::pi_v<float>);
        step = {step.x + step.y * arc, step.y - step.x * arc};
        if (next >= 1.0f)
            m_turn_active = false;
    }
    else
    {
        // Following: quick and exact in a fight, calmer when only looking.
        const auto tau = rear       ? 0.015f + rear_smoothing * 0.20f
                         : fight      ? 0.04f + smoothing * 0.22f
                         : checking ? 0.35f - 0.2f * urgency
                                    : 0.25f;
        const auto blend = 1.0f - std::exp(-dt / tau);
        step = {error.x * blend, error.y * blend};
    }
    // Coming back after the view was taken eases in. The rear turn is already timed by its own smoothing.
    const auto ramp_t = std::clamp(std::chrono::duration<float>(now - m_view_ramp_from).count() / 0.25f, 0.0f, 1.0f);
    const auto ramp = rear ? 1.0f : ramp_t * ramp_t * (3.0f - 2.0f * ramp_t);
    step = {step.x * ramp, step.y * ramp};
    aim_sample_delta = std::hypot(step.x, step.y);
    m_view_velocity = {step.x / dt, step.y / dt};

    constexpr float k_m_yaw = 0.022f;
    const auto sensitivity = game::variables().get<float>(CONVAR("sensitivity"_id));
    const auto fov_adjust = game::bound_memory().process.load<float>(
        player.pawn + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id));
    const auto per_count = sensitivity * k_m_yaw * (fov_adjust > 0.0f && std::isfinite(fov_adjust) ? fov_adjust : 1.0f);
    if (!(per_count > 0.0f) || !std::isfinite(per_count))
    {
        aim_sample_valid = false;
        return;
    }
    m_mouse_remainder.x += -m_view_velocity.y * dt / per_count;
    m_mouse_remainder.y += m_view_velocity.x * dt / per_count;
    const auto dx = static_cast<int>(m_mouse_remainder.x);
    const auto dy = static_cast<int>(m_mouse_remainder.y);
    m_mouse_remainder.x -= static_cast<float>(dx);
    m_mouse_remainder.y -= static_cast<float>(dy);
    if ((dx || dy) && platform::windows::bound_input().walker_pointer(dx, dy))
    {
        aim_sample_px_x = dx;
        aim_sample_px_y = dy;
        m_view_pending = {m_view_pending.x + static_cast<float>(dy) * per_count,
                          m_view_pending.y - static_cast<float>(dx) * per_count};
    }
}
// Where a player holds the crosshair while walking: the corner the nearest enemy will come round. A route over the
// navigation mesh is laid from the player to each hidden enemy; the corner is the last point of the shortest one that
// is still in sight, where that route turns behind the wall, and the crosshair waits there at head height. The enemy
// only tells which corner: the view never points through a wall at it. The corner is worked out twice a second and
// held for at least 1.2 s unless it moves far or another enemy becomes the nearest, so the view does not hop between
// spots. With no hidden enemy the eyes look ahead along the way.
walk_bot_t::gaze walk_bot_t::check_corners(const simulation::nav_graph& graph, const player_state& player,
                                           const gaze& along, const clock::time_point now)
{
    if (along.how != gaze::mode::route)
        return along; // a jump, a ladder or a door takes the view as it needs
    const auto heading = flat_direction(along.point - player.origin);
    const gaze ahead{
        gaze::mode::check,
        foundation::vec3{player.eye.x + heading.x * 800.0f, player.eye.y + heading.y * 800.0f, player.eye.z}, 0.0f,
        0.0f};
    const auto& world = simulation::collision();
    if (!world.valid())
        return ahead;

    if (now >= m_prefire_next)
    {
        m_prefire_next = now + std::chrono::milliseconds(500);
        const auto players = game::world().players();
        const auto start = graph.locate(player.origin);
        // Hidden enemies, nearest first; routes to the two nearest.
        std::vector<const game::player_snapshot*> hidden;
        if (players && start >= 0)
            for (const auto& other : *players)
                if (other.health > 0 && other.pawn && other.pawn != player.pawn &&
                    game::local_player().is_enemy(other.team) && other.origin.distance(player.origin) < 3000.0f &&
                    world.trace_ray(player.eye, other.origin + foundation::vec3{0.0f, 0.0f, 64.0f}).hit)
                    hidden.push_back(&other);
        std::ranges::sort(hidden, [&](const auto* x, const auto* y)
                          { return x->origin.distance(player.origin) < y->origin.distance(player.origin); });
        if (hidden.size() > 2)
            hidden.resize(2);
        float best_length = 1e9f;
        std::optional<foundation::vec3> corner;
        std::uintptr_t corner_enemy = 0;
        for (const auto* enemy : hidden)
        {
            const auto goal = graph.locate(enemy->origin, 160.0f);
            if (goal < 0)
                continue;
            const auto steps = graph.route(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(goal),
                                           enemy->origin, true, true);
            if (steps.empty())
                continue;
            std::vector<foundation::vec3> line{player.origin};
            for (const auto& leg : steps)
                line.push_back(leg.point);
            line.push_back(enemy->origin);
            float length = 0.0f;
            for (std::size_t i = 1; i < line.size(); ++i)
                length += line[i].distance(line[i - 1]);
            if (length >= best_length)
                continue;
            // Along the route in 24-unit steps: the last point in sight before the first one that is not.
            const auto head = [](const foundation::vec3& floor) { return floor + foundation::vec3{0.0f, 0.0f, 64.0f}; };
            std::optional<foundation::vec3> last_seen;
            bool hidden_reached = false;
            for (std::size_t i = 1; i < line.size() && !hidden_reached; ++i)
            {
                const auto from = line[i - 1];
                const auto to = line[i];
                const auto pieces = std::max(1, static_cast<int>(from.distance(to) / 24.0f));
                for (int p = 1; p <= pieces; ++p)
                {
                    const auto sample = from + (to - from) * (static_cast<float>(p) / static_cast<float>(pieces));
                    if (world.trace_ray(player.eye, head(sample)).hit)
                    {
                        hidden_reached = true;
                        break;
                    }
                    last_seen = sample;
                }
            }
            if (!hidden_reached || !last_seen || last_seen->distance(player.origin) < 96.0f)
                continue;
            best_length = length;
            corner = head(*last_seen);
            corner_enemy = enemy->pawn;
        }
        if (!corner)
            m_prefire_valid = false;
        else
        {
            // Keep the corner held unless it moved far (or the nearest enemy changed) and it was held long enough.
            const bool far_move = !m_prefire_valid || corner->distance(m_prefire_point) > 160.0f;
            const bool held_enough = now - m_prefire_since > std::chrono::milliseconds(1200);
            if (!m_prefire_valid || (far_move && (held_enough || corner_enemy != m_prefire_enemy)))
            {
                m_prefire_point = *corner;
                m_prefire_since = now;
                m_prefire_enemy = corner_enemy;
            }
            else if (!far_move)
                m_prefire_point = *corner; // the same corner, refined: followed smoothly
            m_prefire_valid = true;
        }
    }
    if (!m_prefire_valid)
        return ahead;
    // The nearer the corner, the quicker the hand.
    const auto distance = flat(m_prefire_point - player.origin).length();
    const auto urgency = std::clamp(1.0f - (distance - 200.0f) / 1300.0f, 0.0f, 1.0f);
    return {gaze::mode::check, m_prefire_point, 0.0f, urgency};
}
} // namespace features::misc

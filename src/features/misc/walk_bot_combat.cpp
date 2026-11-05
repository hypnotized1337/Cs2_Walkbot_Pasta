#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/body/body.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <simulation/aim_parts.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/collision.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace features::misc
{
using namespace walk_detail;

namespace
{
// What a player sees: the horizontal field of view of CS2's default 16:9 view, and a vertical one to match.
constexpr float k_half_fov_yaw = 53.0f;
constexpr float k_half_fov_pitch = 37.0f;

// The centre of the head hitbox, or where a head would be.
[[nodiscard]] foundation::vec3 head_of(const game::player_snapshot& other)
{
    if (other.bones.is_valid())
        for (const auto& box : other.hitboxes)
        {
            if (box.index < 0 || box.bone < 0 || box.bone >= static_cast<int>(other.bones.bones.size()) ||
                game::hitbox_data().hitgroup_from_hitbox(box.index) != game::rules::hitgroup::head)
                continue;
            return game::place_hitbox(other.bones.bones[static_cast<std::size_t>(box.bone)], box).center();
        }
    return other.origin + foundation::vec3{0.0f, 0.0f, other.is_ducked ? 46.0f : 64.0f};
}

// How far `point` is from the middle of the view, as yaw and pitch offsets in degrees.
[[nodiscard]] foundation::vec2 view_offset(const foundation::vec3& eye, const foundation::vec3& view,
                                           const foundation::vec3& point)
{
    const auto to = point - eye;
    const auto yaw = degrees(std::atan2(to.y, to.x));
    const auto pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
    return {pitch - view.x, foundation::wrap_yaw(yaw - view.y)};
}

// A hitbox centre the walk bot may aim at, and whether one in sight exists at all.
struct aim_choice
{
    foundation::vec3 point{};
    int part{};
    bool valid{};
};

// Picks the point its own aim goes to from the hitboxes the user allowed. The selected parts have a fixed order
// (head, body, arms, legs), so selecting the head keeps the one-tap aim and deselecting it moves the aim down; a
// checked body point that would kill wins over the head when "Body If Lethal" is on. Every point has to be in sight,
// so an enemy behind cover by the chosen part is not fought through it.
template<typename visible_fn>
[[nodiscard]] aim_choice choose_aim_point(const game::player_snapshot& other, const int hitbox_mask,
                                          const bool body_lethal, const foundation::vec3& eye,
                                          const foundation::vec3& view, visible_fn&& visible)
{
    using part = config::combat_profile::aim_part;
    // Only checked parts are candidates. An empty mask means no target; there is no implicit head fallback.
    const auto mask = hitbox_mask & part::all;
    if (!other.bones.is_valid() || other.hitboxes.count <= 0)
    {
        if ((mask & part::head) == 0)
            return {};
        const auto head = head_of(other);
        return {head, part::head, visible(head)};
    }

    struct best_point
    {
        foundation::vec3 point{};
        int part{};
        float angle{std::numeric_limits<float>::max()};
        bool found{};
    };
    constexpr std::array parts{part::head, part::body, part::arms, part::legs};
    std::array<best_point, parts.size()> best{};
    best_point best_lethal_body{};

    for (const auto& box : other.hitboxes)
    {
        if (box.index < 0 || box.bone < 0 || box.bone >= static_cast<int>(other.bones.bones.size()))
            continue;
        const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(box.index);
        const auto body_part = simulation::aim_part_of(box, hitgroup);
        if ((mask & body_part) == 0)
            continue;
        // Body If Lethal may only prefer a body point the user actually checked; it never widens the mask.
        const auto may_be_lethal_body = body_lethal && body_part == part::body;
        const auto point = game::place_hitbox(other.bones.bones[static_cast<std::size_t>(box.bone)], box).center();
        auto lethal_body = false;
        if (may_be_lethal_body)
        {
            // The same range falloff the trigger's direct trace uses: a body shot only counts as lethal when it
            // would still kill at this distance.
            const auto& weapon = simulation::ballistics().pen().get_weapon_data();
            const auto distance = eye.distance(point);
            auto damage = simulation::ballistics().pen().get_max_damage(hitgroup, other.armor, other.has_helmet,
                                                                        other.team);
            damage *= std::pow(std::clamp(weapon.range_modifier, 0.0f, 1.0f), distance / 500.0f);
            lethal_body = weapon.damage > 0.0f && distance <= weapon.range && other.health > 0 &&
                          damage + 0.001f >= static_cast<float>(other.health);
        }
        if (!visible(point))
            continue;
        const auto offset = view_offset(eye, view, point);
        const auto angle = std::hypot(offset.x, offset.y);
        const auto slot = body_part == part::body   ? std::size_t{1}
                          : body_part == part::arms ? std::size_t{2}
                          : body_part == part::legs ? std::size_t{3}
                                                    : std::size_t{0};
        if (angle < best[slot].angle)
            best[slot] = {point, body_part, angle, true};
        if (lethal_body && angle < best_lethal_body.angle)
            best_lethal_body = {point, body_part, angle, true};
    }

    if (best_lethal_body.found)
        return {best_lethal_body.point, best_lethal_body.part, true};
    for (std::size_t i = 0; i < parts.size(); ++i)
        if ((mask & parts[i]) != 0 && best[i].found)
            return {best[i].point, best[i].part, true};
    return {};
}

// The rear cone is a full width centred on the direction opposite the view: 120 degrees means the nearest 120
// degrees of the player's back.
[[nodiscard]] bool in_rear_cone(const foundation::vec3& eye, const foundation::vec3& view,
                                const foundation::vec3& point, const float cone)
{
    const auto offset = view_offset(eye, view, point);
    return std::abs(offset.y) >= 180.0f - std::clamp(cone, 10.0f, 180.0f) * 0.5f;
}
} // namespace

// What a player does about enemies. One in sight (inside the 106 degree view, nothing in between, not spawn-protected)
// is fought: the feet stop and the view goes onto the chosen hitbox with the walk bot's own hand, the autofire shoots. A
// spawn-protected one close by is watched and walked round sideways at a distance until it can be fought. A hit from
// someone not in sight turns the view quickly to roughly where it came from (a player knows the side, not the spot)
// while walking on; whoever shows up there is then fought.
walk_bot_t::combat_plan walk_bot_t::combat(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!cfg.rear_defense)
        m_rear_target = 0;
    const auto health = game::local_player().health();
    const bool hit = m_last_health > 0 && health > 0 && health < m_last_health;
    m_last_health = health;
    if (hit)
        m_hit_at = now;
    const auto players = game::world().players();
    if (!players)
        return {};
    const auto& world = simulation::collision();
    const auto game_time = game::local_player().game_time();
    const auto respawn_grace = std::chrono::milliseconds(std::clamp(cfg.auto_knife_respawn_grace_ms, 0, 5000));
    const auto in_sight = [&](const foundation::vec3& point)
    { return !world.valid() || !world.trace_ray(player.eye, point).hit; };
    // A knife needs a walkable line at leg height, not just a head visible over a wall. The eye-to-head trace clears
    // low cover, which made the bot draw the knife and press into a wall for enemies on the far side of it.
    const auto knife_reachable = [&](const game::player_snapshot& other)
    {
        if (!world.valid())
            return true;
        const auto from = player.origin + foundation::vec3{0.0f, 0.0f, 20.0f};
        const auto to = other.origin + foundation::vec3{0.0f, 0.0f, 20.0f};
        return !world.trace_ray(from, to).hit;
    };
    // One record per enemy: the first place it was seen and whether it ever left that place. A target that was moving
    // when first seen, or moved later, is never a knife candidate until its record is refreshed by a respawn. This is
    // the same idea as remembering spawn points, but works for every map and mode without map-specific data: a pause
    // in movement no longer makes an active player look AFK.
    // A pawn that was just seen for the first time is not necessarily freshly spawned: use the game's spawn time
    // stamp when it is readable, and only carry over the part of the grace the life has left.
    const auto life_deadline = [&](const float spawn)
    {
        if (spawn <= 0.0f || game_time <= 0.0f)
            return now;
        const auto age = std::chrono::duration<float>(game_time - spawn);
        const auto remaining = std::chrono::duration<float>(respawn_grace) - age;
        return remaining > std::chrono::duration<float>::zero()
                   ? now + std::chrono::duration_cast<clock::duration>(remaining)
                   : now;
    };
    const auto update_knife_watch = [&]
    {
        std::scoped_lock lock(m_knife_watch_mutex);
        const auto& process = game::bound_memory().process;
        const auto spawn_offset = SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_id);
        for (const auto& other : *players)
        {
            if (!other.pawn || other.pawn == player.pawn || !game::local_player().is_enemy(other.team))
                continue;
            if (other.health <= 0)
            {
                // Keep the record over the death so the respawn can be recognised as a new life; the five second
                // prune below still removes it if the pawn disappears for good.
                if (const auto it = m_knife_watch.find(other.pawn); it != m_knife_watch.end())
                {
                    it->second.dead = true;
                    it->second.moved = true;
                    it->second.seen = now;
                }
                continue;
            }
            const auto speed = flat(other.velocity).length();
            const auto spawn = spawn_offset > 0 ? process.load<float>(other.pawn + spawn_offset) : 0.0f;
            auto it = m_knife_watch.find(other.pawn);
            if (it == m_knife_watch.end())
            {
                knife_watch watch{};
                watch.anchor = other.origin;
                watch.last = other.origin;
                watch.seen = now;
                watch.still_since = now;
                watch.respawn_until = life_deadline(spawn);
                watch.spawn = spawn;
                watch.moved = speed > 12.0f;
                m_knife_watch.emplace(other.pawn, watch);
                continue;
            }
            auto& watch = it->second;
            const auto jump = other.origin.distance(watch.last);
            const auto spawn_changed = watch.spawn != 0.0f && spawn > 0.0f && spawn != watch.spawn;
            if (jump > 256.0f || spawn_changed || watch.dead)
            {
                // A respawn or teleport starts a new life; the old anchor must not disqualify the new position.
                watch.anchor = other.origin;
                watch.last = other.origin;
                watch.still_since = now;
                watch.respawn_until = now + respawn_grace;
                watch.moved = speed > 12.0f;
                watch.dead = false;
            }
            else
            {
                // Compare with the anchor as well: a target that drifts slowly can move a long way without a
                // single large step, and the point of the record is that it ever left the place it was first seen.
                const auto from_anchor = other.origin.distance(watch.anchor);
                if (jump > 6.0f || from_anchor > 8.0f || speed > 12.0f)
                {
                    watch.moved = true;
                    watch.still_since = now;
                }
                watch.last = other.origin;
            }
            if (spawn > 0.0f)
                watch.spawn = spawn;
            watch.seen = now;
        }
        for (auto it = m_knife_watch.begin(); it != m_knife_watch.end();)
        {
            if (now - it->second.seen > std::chrono::seconds(5))
                it = m_knife_watch.erase(it);
            else
                ++it;
        }
    };
    update_knife_watch();
    // The respawn grace applies to every auto-knife selection, whether or not the AFK filter is on.
    const auto knife_grace_ready = [&](const std::uintptr_t pawn)
    {
        std::scoped_lock lock(m_knife_watch_mutex);
        const auto it = m_knife_watch.find(pawn);
        return it == m_knife_watch.end() || knife_grace_ok(it->second, now);
    };
    const auto knife_afk_ready = [&](const std::uintptr_t pawn)
    {
        std::scoped_lock lock(m_knife_watch_mutex);
        const auto it = m_knife_watch.find(pawn);
        return it != m_knife_watch.end() && knife_grace_ok(it->second, now) && knife_still_ok(it->second, now);
    };

    const game::player_snapshot* fight = nullptr;
    float fight_score = 0.0f;
    const game::player_snapshot* protected_one = nullptr;
    float protected_distance = 1200.0f;
    for (const auto& other : *players)
    {
        if (other.health <= 0 || !other.pawn || other.pawn == player.pawn || !game::local_player().is_enemy(other.team))
            continue;
        const auto head = head_of(other);
        if (other.invulnerable)
        {
            // A protected enemy is still walked round even when the chosen hitboxes cannot be aimed at yet.
            const auto offset = view_offset(player.eye, player.view, head);
            const bool in_view = std::abs(offset.y) <= k_half_fov_yaw && std::abs(offset.x) <= k_half_fov_pitch;
            if (in_view && in_sight(head))
            {
                const auto distance = other.origin.distance(player.origin);
                if (distance < protected_distance)
                {
                    protected_distance = distance;
                    protected_one = &other;
                }
            }
            continue;
        }
        const auto aim = choose_aim_point(other, cfg.aim_hitboxes, cfg.aim_body_lethal, player.eye, player.view,
                                          in_sight);
        if (!aim.valid)
            continue;
        const auto offset = view_offset(player.eye, player.view, aim.point);
        const bool in_view = std::abs(offset.y) <= k_half_fov_yaw && std::abs(offset.x) <= k_half_fov_pitch;
        if (!in_view)
            continue;
        // The one nearest the crosshair, and the one already fought when it is about as near.
        auto score = std::hypot(offset.x, offset.y) + other.origin.distance(player.origin) * 0.01f;
        if (other.pawn == m_fight_target)
            score *= 0.6f;
        if (!fight || score < fight_score)
        {
            fight = &other;
            fight_score = score;
        }
    }

    // Rear defense is latched: after the first 180-degree turn the enemy is no longer behind the view, so the rear
    // cone is only used to acquire it. The engagement ends when it dies, hides or gets too far away.
    if (cfg.rear_defense && m_rear_target)
    {
        for (const auto& other : *players)
        {
            if (other.pawn != m_rear_target || other.health <= 0 || other.invulnerable ||
                !game::local_player().is_enemy(other.team))
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance > cfg.rear_defense_distance * 1.35f)
                break;
            const auto aim = choose_aim_point(other, cfg.rear_defense_hitboxes, false, player.eye, player.view,
                                              in_sight);
            if (!aim.valid)
                break;
            combat_plan plan{};
            plan.what = combat_plan::kind::rear;
            plan.view = {gaze::mode::rear, aim.point, 0.0f, 0.0f, aim.part};
            plan.enemy = other.pawn;
            plan.target = other.origin;
            plan.distance = distance;
            plan.weapon = std::clamp(cfg.rear_defense_weapon, 0, 1);
            // A knife cannot cross low cover; fall back to the gun rather than walking into the wall.
            if (plan.weapon == 1 && !knife_reachable(other))
                plan.weapon = 0;
            return plan;
        }
        m_rear_target = 0;
    }

    // Acquisition: nearest visible enemy inside the rear cone and its own proximity threshold.
    if (cfg.rear_defense)
    {
        const game::player_snapshot* rear = nullptr;
        aim_choice rear_aim{};
        float rear_distance = cfg.rear_defense_distance;
        for (const auto& other : *players)
        {
            if (other.health <= 0 || !other.pawn || other.pawn == player.pawn || other.invulnerable ||
                !game::local_player().is_enemy(other.team))
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance > rear_distance)
                continue;
            const auto aim = choose_aim_point(other, cfg.rear_defense_hitboxes, false, player.eye, player.view,
                                              in_sight);
            if (!aim.valid || !in_rear_cone(player.eye, player.view, aim.point, cfg.rear_defense_fov))
                continue;
            rear = &other;
            rear_aim = aim;
            rear_distance = distance;
        }
        if (rear)
        {
            combat_plan plan{};
            plan.what = combat_plan::kind::rear;
            plan.view = {gaze::mode::rear, rear_aim.point, 0.0f, 0.0f, rear_aim.part};
            plan.enemy = rear->pawn;
            plan.target = rear->origin;
            plan.distance = rear_distance;
            plan.weapon = std::clamp(cfg.rear_defense_weapon, 0, 1);
            if (plan.weapon == 1 && !knife_reachable(*rear))
                plan.weapon = 0;
            return plan;
        }
    }

    // A knife engagement is latched too: the enemy is followed while it is alive, visible and inside the leash.
    if (m_knife_target && (cfg.auto_knife || (cfg.rear_defense && cfg.rear_defense_weapon == 1)))
    {
        const auto leash = m_rear_target == m_knife_target ? cfg.rear_defense_distance * 1.35f
                                                           : cfg.auto_knife_distance * 1.5f;
        for (const auto& other : *players)
        {
            if (other.pawn != m_knife_target || other.health <= 0 || !other.pawn || other.invulnerable ||
                !game::local_player().is_enemy(other.team))
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance > leash || std::abs(other.origin.z - player.origin.z) > 48.0f)
                break;
            // A rear-defense knife target is an active threat and may move. The close-range feature's respawn grace
            // and AFK switch also end the chase if its chosen target is too fresh or has moved since first seen.
            if (m_rear_target != m_knife_target)
            {
                if (!knife_grace_ready(other.pawn) ||
                    (cfg.auto_knife_afk_only && !knife_afk_ready(other.pawn)))
                {
                    m_knife_target = 0;
                    break;
                }
            }
            if (!knife_reachable(other))
            {
                m_knife_target = 0;
                break;
            }
            const auto head = head_of(other);
            if (!in_sight(head))
                break;
            combat_plan plan{};
            plan.what = combat_plan::kind::knife;
            plan.view = {gaze::mode::fight, head, 0.0f, 0.0f, config::combat_profile::aim_part::head};
            plan.enemy = other.pawn;
            plan.target = other.origin;
            plan.distance = distance;
            return plan;
        }
        m_knife_target = 0;
    }

    // Acquisition: the nearest visible enemy inside the close-range threshold, from any direction. The rear defense
    // above already took the enemies behind when it is enabled.
    if (cfg.auto_knife)
    {
        const game::player_snapshot* close = nullptr;
        float close_distance = cfg.auto_knife_distance;
        for (const auto& other : *players)
        {
            if (other.health <= 0 || !other.pawn || other.pawn == player.pawn || other.invulnerable ||
                !game::local_player().is_enemy(other.team))
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance > close_distance || std::abs(other.origin.z - player.origin.z) > 48.0f)
                continue;
            if (!knife_grace_ready(other.pawn))
                continue;
            if (cfg.auto_knife_afk_only && !knife_afk_ready(other.pawn))
                continue;
            if (!knife_reachable(other))
                continue;
            const auto head = head_of(other);
            if (!in_sight(head))
                continue;
            close = &other;
            close_distance = distance;
        }
        if (close)
        {
            combat_plan plan{};
            plan.what = combat_plan::kind::knife;
            plan.view = {gaze::mode::fight, head_of(*close), 0.0f, 0.0f, config::combat_profile::aim_part::head};
            plan.enemy = close->pawn;
            plan.target = close->origin;
            plan.distance = close_distance;
            return plan;
        }
    }

    if (fight && cfg.aim)
    {
        // A newly seen enemy is registered for the reaction time first: the bot keeps walking while it takes the
        // sight in, then stops and aims. The same enemy keeps its registration while it stays in sight.
        if (m_fight_react_target != fight->pawn)
        {
            m_fight_react_target = fight->pawn;
            m_fight_react_until = now + std::chrono::milliseconds(std::clamp(cfg.aim_reaction_ms, 0, 500));
        }
        m_fight_target = fight->pawn;
        m_fight_seen_at = now;
        m_react_until = {};
        if (now >= m_fight_react_until)
        {
            const auto aim = choose_aim_point(*fight, cfg.aim_hitboxes, cfg.aim_body_lethal, player.eye,
                                              player.view, in_sight);
            // No valid point means no selected bone is visible; do not fall back to the head.
            if (!aim.valid)
                return {};
            return {combat_plan::kind::engage, {gaze::mode::fight, aim.point, 0.0f, 0.0f, aim.part}, {}};
        }
    }
    // The enemy just fought stays the target for a moment after a sight line flickers (an arm, a box edge), so the
    // view does not jump between it and a corner every tick.
    if (cfg.aim && m_fight_target && now - m_fight_seen_at < std::chrono::milliseconds(300))
    {
        if (now >= m_fight_react_until)
            for (const auto& other : *players)
                if (other.pawn == m_fight_target && other.health > 0 && !other.invulnerable)
                {
                    const auto aim = choose_aim_point(other, cfg.aim_hitboxes, cfg.aim_body_lethal, player.eye,
                                                      player.view, in_sight);
                    if (aim.valid)
                        return {combat_plan::kind::engage, {gaze::mode::fight, aim.point, 0.0f, 0.0f, aim.part},
                                {}};
                }
    }
    else
    {
        m_fight_target = 0;
        m_fight_react_target = 0;
        m_fight_react_until = {};
    }

    if (protected_one)
    {
        // Sideways round it, keeping some distance; the side stays until it is blocked (then the other one).
        const auto toward = flat_direction(protected_one->origin - player.origin);
        const foundation::vec3 side{-toward.y * m_evade_side, toward.x * m_evade_side, 0.0f};
        if (world.valid())
        {
            const auto knee = player.origin + foundation::vec3{0.0f, 0.0f, 26.0f};
            if (world.trace_ray(knee, knee + side * 48.0f).hit)
                m_evade_side = -m_evade_side;
        }
        const auto away = protected_distance < 450.0f ? toward * -0.5f : foundation::vec3{};
        return {combat_plan::kind::evade, {gaze::mode::fight, head_of(*protected_one), 0.0f}, side + away};
    }

    if (cfg.react_to_hits && hit)
    {
        // The likely shooter: the nearest enemy that can see the player. Only its side is known: a quick turn (the
        // fight's hand) goes there give or take 15 degrees, at its head height; once it is in view it is fought.
        const game::player_snapshot* shooter = nullptr;
        float nearest = 1e9f;
        for (const auto& other : *players)
        {
            if (other.health <= 0 || !other.pawn || other.pawn == player.pawn ||
                !game::local_player().is_enemy(other.team) || other.invulnerable)
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance < nearest && in_sight(head_of(other)))
            {
                nearest = distance;
                shooter = &other;
            }
        }
        const auto toward = shooter
                                ? flat_direction(shooter->origin - player.origin)
                                : foundation::vec3{-std::cos(player.view.y * std::numbers::pi_v<float> / 180.0f),
                                                   -std::sin(player.view.y * std::numbers::pi_v<float> / 180.0f), 0.0f};
        const auto spread =
            std::uniform_real_distribution<float>(-15.0f, 15.0f)(m_random) * std::numbers::pi_v<float> / 180.0f;
        const foundation::vec3 rough{toward.x * std::cos(spread) - toward.y * std::sin(spread),
                                     toward.x * std::sin(spread) + toward.y * std::cos(spread), 0.0f};
        const auto reach = shooter ? std::min(nearest, 1500.0f) : 600.0f;
        m_react_point = player.eye + rough * reach;
        if (shooter)
            m_react_point.z = shooter->origin.z + 64.0f;
        m_react_until = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(1200, 1600)(m_random));
    }
    if (now < m_react_until)
        return {combat_plan::kind::react, {gaze::mode::fight, m_react_point, 0.0f}, {}};
    return {};
}
} // namespace features::misc

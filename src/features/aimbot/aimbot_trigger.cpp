#include <stdafx.hpp>
#include <diagnostics/log_directory.hpp>
#include <diagnostics/performance.hpp>
#include <foundation/scope_exit.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

#include <fstream>

namespace features::aimbot
{
aimbot_t::trigger_result aimbot_t::trace_direction(const foundation::vec3& eye_pos, const foundation::vec3& direction,
                                                   const std::vector<game::player_snapshot>& players,
                                                   const config::combat_profile::triggerbot& cfg,
                                                   const int hitbox_parts, const std::uintptr_t focus_pawn,
                                                   trigger_debug::frame* trace) const
{
    constexpr auto max_range{8192.0f};
    const auto trace_enabled = trace && trace->enabled;
    if (trace_enabled)
    {
        trace->target_count = 0;
        trace->on_target = false;
        trace->pawn = 0;
        trace->hitbox = -1;
        trace->hitgroup = -1;
        trace->damage = 0.0f;
        trace->target_age_ms = 0.0f;
    }
    const auto game_time = trace_enabled ? game::local_player().game_time() : 0.0f;
    // Trigger prediction follows the sampled target state only. Network ping is not
    // an aim-ahead horizon; server lag compensation consumes the historical sample.
    // The aim (the walk bot's hand or the aimbot itself) always sits on the target's current bone, so the trace has to
    // test the same geometry. The shooter's own velocity is deliberately not part of this lead: for a hitscan shot it
    // only moves the ray's origin, and subtracting it displaced the traced skeleton opposite to the player's movement —
    // up to 17 units while walking, more than a head hitbox. That is what left the crosshair on a standing enemy while
    // every trigger pass traced past him.
    const auto prediction_time =
        cfg.predictive ? std::clamp(static_cast<float>(cfg.delay) * 0.001f + game::rules::simulation_step, 0.0f, 0.12f)
                       : 0.0f;

    struct direct_candidate
    {
        const game::player_snapshot* player{};
        game::skeleton_reader::data bones{};
        float distance{max_range};
        int hitbox{-1};
        int hitgroup{-1};
    } best{};

    for (const auto& player : players)
    {
        if (!game::local_player().is_enemy(player.team))
        {
            continue;
        }

        trigger_debug::target* debug_target = nullptr;
        if (trace_enabled)
        {
            trigger_debug::target entry{};
            entry.pawn = player.pawn;
            entry.label = player.origin + foundation::vec3{0.0f, 0.0f, player.is_ducked ? 46.0f : 64.0f};
            entry.required_parts = hitbox_parts;
            entry.health = player.health;
            entry.why = trigger_debug::reason::outside_hitbox;
            if (player.simulation_time > 0.0f && game_time >= player.simulation_time)
                entry.target_age_ms = (game_time - player.simulation_time) * 1000.0f;
            trace->add_target(entry);
            debug_target = &trace->targets[trace->target_count - 1];
        }

        if (player.health <= 0)
        {
            if (debug_target)
                debug_target->why = trigger_debug::reason::target_dead;
            continue;
        }
        if (player.invulnerable)
        {
            if (debug_target)
                debug_target->why = trigger_debug::reason::invulnerable;
            continue;
        }
        if (player.hitboxes.count <= 0)
        {
            if (debug_target)
                debug_target->why = trigger_debug::reason::target_stale;
            continue;
        }

        const auto simulation_tick_offset = SCHEMA("C_BaseEntity", "m_nSimulationTick"_id);
        const auto tick_before = game::bound_memory().process.load<std::int32_t>(player.pawn + simulation_tick_offset);
        auto bones = game::skeletons().get(player.bone_cache);
        const auto tick_after = game::bound_memory().process.load<std::int32_t>(player.pawn + simulation_tick_offset);
        if (tick_before != tick_after || !bones.is_valid())
            bones = player.bones;
        if (!bones.is_valid())
        {
            if (debug_target)
                debug_target->why = trigger_debug::reason::target_stale;
            continue;
        }

        if (cfg.predictive)
        {
            auto target_velocity = game::bound_memory().process.load<foundation::vec3>(
                player.pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
            if (!std::isfinite(target_velocity.x) || !std::isfinite(target_velocity.y) ||
                !std::isfinite(target_velocity.z))
            {
                target_velocity = player.velocity;
            }
            const auto offset = target_velocity * prediction_time;
            for (auto& bone : bones.bones)
            {
                bone.position += offset;
            }
        }

        for (const auto& hitbox : player.hitboxes)
        {
            const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hitbox.index);
            if ((hitbox_parts & simulation::aim_part_of(hitbox, hitgroup)) == 0)
                continue;
            if (hitbox.index < 0 || hitbox.bone < 0 || hitbox.bone >= 128)
            {
                if (debug_target)
                    debug_target->why = trigger_debug::reason::target_stale;
                continue;
            }
            const auto& bone = bones.bones[hitbox.bone];
            const auto rotation_length = bone.rotation.x * bone.rotation.x + bone.rotation.y * bone.rotation.y +
                                         bone.rotation.z * bone.rotation.z + bone.rotation.w * bone.rotation.w;
            if (!std::isfinite(bone.position.x) || !std::isfinite(bone.position.y) || !std::isfinite(bone.position.z) ||
                !std::isfinite(rotation_length) || rotation_length < 0.5f || rotation_length > 1.5f ||
                bone.position.distance_sqr(player.origin) > 512.0f * 512.0f)
            {
                if (debug_target)
                    debug_target->why = trigger_debug::reason::target_stale;
                continue;
            }
            const auto capsule = game::place_hitbox(bone, hitbox);
            float distance{};
            if (foundation::ray_capsule_entry(eye_pos, direction, capsule.start, capsule.end, capsule.radius,
                                              distance) &&
                distance < best.distance)
            {
                best = {&player, bones, distance, hitbox.index, hitgroup};
                if (debug_target)
                {
                    debug_target->hitbox = hitbox.index;
                    debug_target->hitgroup = hitgroup;
                    debug_target->hit_distance = distance;
                }
            }
        }
    }

    // The walk bot's autofire: the game's own crosshair names the enemy it is fighting even when the instantaneous
    // camera ray misses every capsule — the hand is still closing on the bone, or the punch of the last shot moved the
    // view after the bones were read. Aim at the nearest allowed hitbox of that enemy instead of reporting no target:
    // a lined-up enemy must never be watched without a shot.
    auto shot_direction = direction;
    if (!best.player && focus_pawn != 0)
    {
        auto found = false;
        auto nearest_offset = 0.0f;
        for (const auto& player : players)
        {
            if (player.pawn != focus_pawn || player.health <= 0 || player.invulnerable || !player.bones.is_valid() ||
                player.hitboxes.count <= 0)
            {
                continue;
            }
            for (const auto& hitbox : player.hitboxes)
            {
                if (hitbox.index < 0 || !player.bones.covers(hitbox.bone))
                {
                    continue;
                }
                const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hitbox.index);
                if ((hitbox_parts & simulation::aim_part_of(hitbox, hitgroup)) == 0)
                {
                    continue;
                }
                const auto capsule =
                    game::place_hitbox(player.bones.bones[static_cast<std::size_t>(hitbox.bone)], hitbox);
                const auto to = capsule.center() - eye_pos;
                const auto distance = to.length();
                if (!(distance > 1.0f) || distance >= max_range)
                {
                    continue;
                }
                // How far off the view the hitbox centre is, as the tangent of the angle: no arccosine per hitbox.
                const auto along = to.x * direction.x + to.y * direction.y + to.z * direction.z;
                if (along <= 0.0f)
                {
                    continue;
                }
                const auto lateral = (to - direction * along).length();
                const auto offset = lateral / along;
                if (found && offset >= nearest_offset)
                {
                    continue;
                }
                found = true;
                nearest_offset = offset;
                best = {&player, player.bones, distance, hitbox.index, hitgroup};
                shot_direction = to / distance;
            }
        }
    }

    const auto mark_best = [&](const trigger_debug::reason why, const bool on_target)
    {
        if (!trace_enabled || !best.player)
            return;
        trace->on_target = on_target;
        trace->pawn = best.player->pawn;
        trace->hitbox = best.hitbox;
        trace->hitgroup = best.hitgroup;
        for (std::size_t index = 0; index < trace->target_count; ++index)
        {
            auto& target = trace->targets[index];
            if (target.pawn != best.player->pawn)
                continue;
            target.why = why;
            target.on_target = on_target;
            target.hitbox = best.hitbox;
            target.hitgroup = best.hitgroup;
            target.hit_distance = best.distance;
            target.damage = trace->damage;
            trace->target_age_ms = target.target_age_ms;
            break;
        }
    };

    if (!best.player || best.hitbox < 0 || best.distance >= max_range)
    {
        mark_best(trigger_debug::reason::not_visible, false);
        return {};
    }

    trigger_result result{};
    result.player = best.player;
    result.bones = best.bones;
    result.hitbox = best.hitbox;
    result.hitgroup = best.hitgroup;
    result.point = eye_pos + shot_direction * best.distance;
    if (simulation::collision().valid())
    {
        ballistics_t::penetration::result passage{};
        if (!simulation::ballistics().pen().run_seed(
                eye_pos, shot_direction, *best.player, best.bones, hitbox_parts,
                cfg.checks.walls == config::combat_profile::wall_policy::penetration, 1.0f, best.hitbox, passage))
        {
            mark_best(trigger_debug::reason::not_visible, false);
            return {};
        }
        result.damage = passage.damage;
        result.penetrated = passage.penetrated;
    }
    else
    {
        const auto& weapon = simulation::ballistics().pen().get_weapon_data();
        if (weapon.range <= best.distance || weapon.damage <= 0.0f)
        {
            mark_best(trigger_debug::reason::not_visible, false);
            return {};
        }
        result.damage = simulation::ballistics().pen().get_max_damage(best.hitgroup, best.player->armor,
                                                                      best.player->has_helmet, best.player->team) *
                        std::pow(std::clamp(weapon.range_modifier, 0.0f, 1.0f), best.distance / 500.0f);
        result.penetrated = false;
    }

    if (cfg.checks.smoke && line_through_smoke(eye_pos, result.point))
    {
        mark_best(trigger_debug::reason::smoke, false);
        return {};
    }
    if (trace)
        trace->damage = result.damage;
    mark_best(trigger_debug::reason::fire, true);
    return result;
}

void aimbot_t::triggerbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                          const std::vector<game::player_snapshot>& players,
                          const config::combat_profile::triggerbot& cfg)
{
    VESTA_PERF_SCOPE(aim_trigger);
    auto now = std::chrono::steady_clock::now();
    // The trace is a pass-local object. The scope guard publishes it on every exit, including early returns, so a
    // check that forgets to publish cannot leave the HUD showing an older decision.
    trigger_debug::frame trace{};
    trace.enabled = runtime_config()->general.trigger_debug_overlay;
    const foundation::scope_exit publish_trace([this, &trace]() noexcept { publish_trigger_debug(trace); });
    const auto block = [&trace](const trigger_debug::reason why)
    {
        trace.why = why;
        trace.state = trigger_debug::is_waiting(why) ? trigger_debug::decision::waiting : trigger_debug::decision::blocked;
        // Keep the selected enemy's label on the same reason as the panel. The trace can have reported FIRE before a
        // later check (damage, hitchance, input) rejected the shot.
        if (trace.pawn == 0)
            return;
        for (std::size_t index = 0; index < trace.target_count; ++index)
            if (trace.targets[index].pawn == trace.pawn)
                trace.targets[index].why = why;
    };
    const auto cancel_scheduled = [this]
    {
        m_trigger_shot_scheduled = false;
        m_trigger_fire_time = {};
        m_trigger_target_valid = false;
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
    };
    // Autospray owns the attack button and releases it itself; the per-shot path must not touch that press.
    if (m_spray_state != spray_state::idle)
        return;
    if (!g_walk_autofire.load(std::memory_order_acquire) &&
        !config::combat_profile::activation_active(cfg.activation_mode, cfg.key))
    {
        block(trigger_debug::reason::key_not_held);
        trace.trigger_active = false;
        cancel_scheduled();
        return;
    }
    trace.trigger_active = true;
    // The walk bot's autofire follows the hitboxes its own aim picked, so a body shot it lines up can fire even when
    // the triggerbot's own selection is head only. The part, not a point, is enough: the walk bot's multipoint
    // candidates all lie on the same hitbox capsule this trace tests, so a cap or side point is accepted as soon as
    // the view ray crosses it.
    const auto walk_autofire = g_walk_autofire.load(std::memory_order_acquire);
    const auto walk_parts = walk_autofire ? g_walk_autofire_parts.load(std::memory_order_acquire) : 0;
    const auto target_parts = walk_parts ? walk_parts : cfg.hitbox_parts;
    trace.required_parts = target_parts;
    // The pawn the game reports under the crosshair. For the walk bot's autofire that is the enemy its own hand is on,
    // and it is what the trace falls back to when the camera ray sits a fraction of a degree off the bone. The
    // standalone triggerbot stays strict: it may only fire at a hitbox the ray actually crosses.
    const auto crosshair = walk_parts != 0 ? game::local_player().crosshair_id() : 0;
    const auto focus_pawn = crosshair > 0 && crosshair < 0x7fff
                                ? game::entity_index().lookup_index(static_cast<std::uint32_t>(crosshair))
                                : std::uintptr_t{};

    const auto& ctx = ballistics().ctx();
    trace.weapon_ready = ctx.weapon_ready;
    trace.reloading = ctx.is_reloading;
    trace.clip = ctx.clip;
    trace.walk_autofire = walk_autofire;
    // CViewRender is already the final, punch-applied camera. Trace it as it is: recoil control moves that camera
    // back onto the target, so adding a second compensation offset here would aim above the enemy.
    // Journal why a trigger that should fire does not (at most 4 times a second): code = reason << 24 | a << 8 | b.
    // 10: the game reports something under the crosshair but our own trace hits no enemy hitbox;
    // 11/12: hit but blocked by minimum damage / lethal only; 20: waiting for the planned time (a = ms left);
    // 22: Windows did not accept the press (a = error); 23: a previous press is still held (a = ms overdue);
    // 24: hit chance too low (a = percent reached, b = percent wanted); 25: the target left the crosshair.
    const auto note = [&](const std::uint32_t reason, const float a, const float b)
    {
        if (!m_trigger_note.allow(now))
            return;
        diagnostics::emit(diagnostics::event::trigger_waiting, diagnostics::severity::information,
                          diagnostics::code_domain::mode,
                          (reason << 24) | (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 65535.0f)) << 8) |
                              static_cast<std::uint32_t>(std::clamp(b, 0.0f, 255.0f)));
    };

    if (m_trigger_held)
    {
        VESTA_SEED_COUNT(held_skip);
        trace.pawn = m_trigger_target.pawn;
        // m_trigger_target_valid is cleared after every press, but the cached target is still the pawn under the
        // crosshair. Keep it in the trace so a release latch is diagnosed even though the shot itself is complete.
        trace.on_target = m_trigger_target.pawn != 0;
        if (trace.on_target && trace.target_count == 0)
        {
            trigger_debug::target cached{};
            cached.pawn = m_trigger_target.pawn;
            cached.label = m_trigger_target.origin +
                           foundation::vec3{0.0f, 0.0f, m_trigger_target.is_ducked ? 46.0f : 64.0f};
            cached.why = trigger_debug::reason::held_pending;
            cached.health = m_trigger_target.health;
            cached.on_target = true;
            trace.add_target(cached);
        }
        block(trigger_debug::reason::held_pending);
        trace.state = trigger_debug::decision::waiting;
        trace.delay_ms = std::chrono::duration<float, std::milli>(m_trigger_release_time - now).count();
        note(23, std::chrono::duration<float, std::milli>(now - m_trigger_release_time).count(), 0.0f);
        return;
    }

    if (now < m_trigger_cooldown_until)
    {
        block(trigger_debug::reason::cooldown);
        trace.cooldown_ms = std::chrono::duration<float, std::milli>(m_trigger_cooldown_until - now).count();
        cancel_scheduled();
        return;
    }
    constexpr std::uint16_t revolver_id{64};
    const auto revolver = ctx.item_def_idx == revolver_id;
    if (ctx.is_reloading)
    {
        block(trigger_debug::reason::reloading);
        cancel_scheduled();
        return;
    }
    if ((!ctx.weapon_ready && !revolver) || ctx.clip == 0)
    {
        block(trigger_debug::reason::weapon_not_ready);
        cancel_scheduled();
        return;
    }

    const auto pawn = game::local_player().pawn();
    if (!m_trigger_shot_scheduled)
    {
        // This pass owns target publication: a stale target from an earlier pass must not keep auto-crouch latched
        // when the crosshair has left the enemy, a check blocks combat or the weapon is not ready.
        m_trigger_target_valid = false;
        if (local_checks_blocked(cfg.checks))
        {
            block(trigger_debug::reason::legit_check);
            features::visuals::event_log().push_throttled(0x54524301u, "Trigger blocked: legit check",
                                                          features::visuals::event_kind::blocked,
                                                          std::chrono::milliseconds(750));
            return;
        }

        // The game builds a shot from v_angle plus the bullet-scale punch. The Walk Bot's own aim closes its loop on
        // that impact point, so its autofire has to trace the same direction; tracing only CViewRender would miss
        // whenever the rendered view carries a different fraction of the punch (the Follow Recoil offset). The
        // standalone trigger keeps its camera ray, which is the existing behavior outside this spray path.
        auto trace_angles = view_angles;
        if (walk_autofire)
        {
            foundation::vec3 base{};
            const auto local_pawn = game::local_player().pawn();
            if (local_pawn && game::bound_memory().process.copy(local_pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id),
                                                               &base, sizeof(base)) &&
                std::isfinite(base.x) && std::isfinite(base.y) && std::isfinite(base.z))
            {
                if (const auto punch = simulation::read_cached_punch(
                        game::bound_memory().process, local_pawn,
                        SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id)))
                {
                    trace_angles = base + *punch;
                    trace_angles.x = std::clamp(trace_angles.x, -89.0f, 89.0f);
                    trace_angles.y = foundation::wrap_yaw(trace_angles.y);
                }
            }
        }
        foundation::vec3 forward{};
        trace_angles.to_directions(&forward, nullptr, nullptr);
        // Bone selection is strict: autofire may only fire at the parts the walk bot's own aim selected. Falling
        // back to every part here would shoot the head while the user has it unchecked.
        auto result = trace_direction(eye_pos, forward, players, cfg, target_parts, focus_pawn, &trace);
        if (!result.player)
        {
            auto why = trace.target_count > 0 ? trigger_debug::reason::not_visible : trigger_debug::reason::no_target;
            for (std::size_t index = 0; index < trace.target_count; ++index)
            {
                if (trace.targets[index].why == trigger_debug::reason::outside_hitbox)
                    why = trigger_debug::reason::outside_hitbox;
            }
            block(why);
            const auto id = game::local_player().crosshair_id();
            if (id > 0 && id < 0x7fff)
                note(10, static_cast<float>(id), 0.0f);
            return;
        }
        trace.pawn = result.player->pawn;
        trace.hitbox = result.hitbox;
        trace.hitgroup = result.hitgroup;
        trace.damage = result.damage;
        trace.on_target = true;
        trace.target_health = result.player->health;
        trace.min_damage = cfg.min_damage;
        for (std::size_t index = 0; index < trace.target_count; ++index)
        {
            if (trace.targets[index].pawn == result.player->pawn)
            {
                trace.targets[index].health = result.player->health;
                trace.targets[index].damage = result.damage;
                break;
            }
        }
        if (result.penetrated && result.damage < cfg.min_damage)
        {
            block(trigger_debug::reason::min_damage);
            note(11, result.damage, cfg.min_damage);
            return;
        }

        if (cfg.lethal_only && result.hitgroup != game::rules::hitgroup::head &&
            result.damage + 0.001f < static_cast<float>(result.player->health))
        {
            block(trigger_debug::reason::lethal_only);
            note(12, result.damage, static_cast<float>(result.player->health));
            return;
        }

        now = std::chrono::steady_clock::now();
        m_trigger_shot_scheduled = true;
        const auto jitter = cfg.randomize_ms > 0
                                ? static_cast<int>(std::round(m_rng.uniform(-static_cast<float>(cfg.randomize_ms),
                                                                            static_cast<float>(cfg.randomize_ms))))
                                : 0;
        const auto outlier = m_rng.uniform(0.0f, 100.0f) < std::clamp(cfg.outlier_chance, 0.0f, 100.0f)
                                 ? std::max(cfg.outlier_delay_ms, 0)
                                 : 0;
        const auto committed_delay = std::max(0, cfg.delay + jitter + outlier);
        m_trigger_fire_time = now + std::chrono::milliseconds(committed_delay);
        m_trigger_target = *result.player;
        m_trigger_target_bones = result.bones;
        m_trigger_target_eye = eye_pos;
        m_trigger_target_angles = trace_angles;
        m_trigger_target_parts = target_parts;
        m_trigger_target_valid = true;
        const auto weapon_max_speed = ctx.fire_mode ? ctx.debug.max_speed.second : ctx.debug.max_speed.first;
        const auto required_speed =
            std::max(0.5f, (std::isfinite(weapon_max_speed) && weapon_max_speed > 0.0f ? weapon_max_speed : 250.0f) *
                               runtime_config()->general.m_auto_stop.required_shoot_speed * 0.01f);
        features::misc::auto_stop().request_stop(features::misc::auto_stop_source::advanced_trigger,
                                                 m_trigger_fire_time, required_speed);
    }

    if (now < m_trigger_fire_time)
    {
        trace.pawn = m_trigger_target.pawn;
        trace.on_target = true;
        block(trigger_debug::reason::delay_pending);
        trace.delay_ms = std::chrono::duration<float, std::milli>(m_trigger_fire_time - now).count();
        note(20, std::chrono::duration<float, std::milli>(m_trigger_fire_time - now).count(), 0.0f);
        return;
    }

    if (!features::misc::auto_stop().ready_to_fire(features::misc::auto_stop_source::advanced_trigger))
    {
        // Auto Stop is a helper, not a veto. If it cannot settle the player (a ladder, a moving platform, a
        // movement key another feature holds), do not leave the trigger aiming forever without firing.
        constexpr auto max_stop_wait = std::chrono::milliseconds(300);
        if (now - m_trigger_fire_time < max_stop_wait)
        {
            block(trigger_debug::reason::auto_stop_wait);
            trace.delay_ms = std::chrono::duration<float, std::milli>(max_stop_wait - (now - m_trigger_fire_time)).count();
            trace.pawn = m_trigger_target.pawn;
            trace.on_target = true;
            return;
        }
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
    }

    // The walk bot's autofire already selected a hitbox and its ray crossed the target; applying the triggerbot's
    // probability model on top of that was what left sniper rifles aiming without ever firing. The direct trace is
    // still the gate; hitchance stays in force for the standalone triggerbot.
    const auto hit_probability =
        cfg.hitchance > 0.0f && m_trigger_target_valid && !walk_autofire
            ? ballistics().estimate_hit_probability(m_trigger_target_eye, m_trigger_target_angles, m_trigger_target,
                                                    m_trigger_target_bones, target_parts)
            : 1.0f;
    if (hit_probability < cfg.hitchance / 100.0f)
    {
        trace.pawn = m_trigger_target.pawn;
        trace.on_target = true;
        block(trigger_debug::reason::hitchance);
        trace.hitchance = hit_probability * 100.0f;
        trace.required_hitchance = cfg.hitchance;
        note(24, hit_probability * 100.0f, cfg.hitchance);
        // The estimate must follow the aim, not the moment the shot was planned: the planning snapshot can
        // sit on the edge of a hitbox and never reach the wanted chance however well the crosshair is placed
        // now. Take the current aim and target again; if the crosshair left the enemy, start over.
        if (!players.empty())
        {
            foundation::vec3 forward{};
            view_angles.to_directions(&forward, nullptr, nullptr);
            const auto fresh = trace_direction(eye_pos, forward, players, cfg, target_parts, focus_pawn, &trace);
            if (!fresh.player)
            {
                block(trigger_debug::reason::target_lost);
                note(25, 0.0f, 0.0f);
                cancel_scheduled();
                return;
            }
            m_trigger_target = *fresh.player;
            m_trigger_target_bones = fresh.bones;
            m_trigger_target_eye = eye_pos;
            m_trigger_target_angles = view_angles;
        }
        block(trigger_debug::reason::hitchance);
        features::visuals::event_log().push_throttled(0x54524302u, "Trigger waiting: hitchance",
                                                      features::visuals::event_kind::blocked,
                                                      std::chrono::milliseconds(750));
        return;
    }

    // Once committed, do not consult the target again. A failed syscall is not a
    // shot: retain the schedule and retry on the next 250 Hz pass.
    const auto shots_before =
        pawn ? game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id))
             : -1;
    const auto press_delivered = m_revolver_pre_cock_down || platform::windows::bound_input().pointer(
                                                                 0, 0, platform::windows::pointer_action::primary_down);
    if (!press_delivered)
    {
        block(trigger_debug::reason::input_rejected);
        note(22, static_cast<float>(::GetLastError()), 0.0f);
        return;
    }
    m_revolver_pre_cock_down = false;

    features::misc::auto_stop().notify_shot(features::misc::auto_stop_source::advanced_trigger);

    features::visuals::event_log().begin_trigger_shot(revolver ? "R8 Trigger" : "Trigger", false);
    m_trigger_held = true;
    m_seed_press_active = false;
    m_revolver_committed = revolver;
    m_trigger_press_shots = shots_before;

    const auto hold_ms =
        std::clamp(static_cast<int>(std::ceil(2.0f * game::rules::simulation_step * 1000.0f)) + 4, 32, 48);
    m_trigger_release_time = now + std::chrono::milliseconds(revolver ? 1200 : hold_ms);
    m_trigger_cooldown_until = now + std::chrono::milliseconds(std::max(cfg.delay_after_ms, 0));

    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_target_valid = false;
    trace.state = trigger_debug::decision::fire;
    trace.why = trigger_debug::reason::fire;
    trace.pawn = m_trigger_target.pawn;
    trace.on_target = true;
    trace.shot_fired = true;
    for (std::size_t index = 0; index < trace.target_count; ++index)
        if (trace.targets[index].pawn == trace.pawn)
            trace.targets[index].why = trigger_debug::reason::fire;
}

void aimbot_t::publish_trigger_debug(const trigger_debug::frame& frame) noexcept
{
    if (!frame.enabled)
        return;

    try
    {
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock(m_trigger_debug_mutex);
        auto& debug = m_trigger_debug;
        const auto milliseconds = [](const std::chrono::steady_clock::duration value)
        { return std::chrono::duration<float, std::milli>(value).count(); };

        const auto target_changed = !debug.seen || debug.pawn != frame.pawn;
        const auto changing = target_changed || debug.state != frame.state || debug.why != frame.why;
        if (changing)
        {
            debug.state = frame.state;
            debug.why = frame.why;
            debug.pawn = frame.pawn;
            debug.hitbox = frame.hitbox;
            debug.state_since = now;
        }
        else
        {
            debug.why = frame.why;
            debug.pawn = frame.pawn;
            debug.hitbox = frame.hitbox;
        }

        if (frame.on_target)
        {
            if (!debug.on_target || target_changed)
                debug.on_target_since = now;
        }
        else
        {
            debug.on_target_since = {};
        }
        debug.on_target = frame.on_target;
        debug.hitgroup = frame.hitgroup;
        debug.required_parts = frame.required_parts;
        debug.damage = frame.damage;
        debug.delay_ms = frame.delay_ms;
        debug.cooldown_ms = frame.cooldown_ms;
        debug.target_age_ms = frame.target_age_ms;
        debug.hitchance = frame.hitchance;
        debug.required_hitchance = frame.required_hitchance;
        debug.min_damage = frame.min_damage;
        debug.target_health = frame.target_health;
        debug.targets = frame.targets;
        debug.target_count = frame.target_count;
        debug.state_ms = debug.state_since.time_since_epoch().count() == 0 ? 0.0f : milliseconds(now - debug.state_since);
        debug.on_target_ms = debug.on_target_since.time_since_epoch().count() == 0
                                 ? 0.0f
                                 : milliseconds(now - debug.on_target_since);
        if (frame.shot_fired)
            debug.last_shot = now;
        debug.last_update = now;
        debug.seen = true;

        // A legitimate wait has a bounded reason. These states do not: the crosshair is on an enemy, no shot is being
        // delivered, and the flag/timer that is supposed to release the trigger has outlived any normal duration.
        auto stuck = false;
        if (frame.on_target && !frame.shot_fired && frame.state != trigger_debug::decision::fire &&
            debug.on_target_ms >= 500.0f)
        {
            switch (frame.why)
            {
                case trigger_debug::reason::held_pending:
                    stuck = frame.delay_ms <= 0.0f;
                    break;
                case trigger_debug::reason::schedule_pending:
                case trigger_debug::reason::unspecified:
                    stuck = true;
                    break;
                case trigger_debug::reason::delay_pending:
                    stuck = frame.delay_ms <= 0.0f;
                    break;
                case trigger_debug::reason::cooldown:
                    stuck = frame.cooldown_ms <= 0.0f;
                    break;
                case trigger_debug::reason::input_rejected:
                    stuck = debug.on_target_ms >= 1500.0f;
                    break;
                case trigger_debug::reason::weapon_not_ready:
                case trigger_debug::reason::reloading:
                    stuck = debug.on_target_ms >= 5000.0f;
                    break;
                default:
                    break;
            }
        }
        const auto was_stuck = debug.stuck;
        debug.stuck = stuck;
        if (changing || stuck != was_stuck)
        {
            trigger_debug::decision_entry entry{};
            entry.at = now;
            entry.state = stuck ? trigger_debug::decision::stuck : frame.state;
            entry.why = frame.why;
            entry.pawn = frame.pawn;
            entry.hitbox = frame.hitbox;
            entry.delay_ms = frame.delay_ms;
            entry.cooldown_ms = frame.cooldown_ms;
            entry.target_age_ms = frame.target_age_ms;
            entry.stuck = stuck;
            debug.history.push_back(entry);
            while (debug.history.size() > 100)
                debug.history.pop_front();
        }

        const auto dump_due = stuck && (!was_stuck || debug.last_dump.time_since_epoch().count() == 0 ||
                                        now - debug.last_dump >= std::chrono::seconds(3));
        if (!dump_due)
            return;
        debug.last_dump = now;

        const auto directory = diagnostics::default_logs_directory();
        if (directory.empty())
            return;
        std::error_code error{};
        std::filesystem::create_directories(directory, error);
        std::ofstream log(directory / "trigger-stuck.log", std::ios::app);
        if (!log)
            return;

        log << "trigger stuck at_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                   .count()
            << "\n";
        log << " state=" << trigger_debug::name(frame.state) << " reason=" << trigger_debug::name(frame.why)
            << " stuck=" << stuck << " on_target=" << frame.on_target << " shot_fired=" << frame.shot_fired
            << " trigger_active=" << frame.trigger_active << "\n";
        log << " state_ms=" << debug.state_ms << " on_target_ms=" << debug.on_target_ms
            << " target_age_ms=" << frame.target_age_ms << " delay_ms=" << frame.delay_ms
            << " cooldown_ms=" << frame.cooldown_ms << "\n";
        log << " target=" << frame.pawn << " hitbox=" << frame.hitbox << " hitgroup=" << frame.hitgroup
            << " required_parts=" << frame.required_parts << " damage=" << frame.damage
            << " health=" << frame.target_health << " hitchance=" << frame.hitchance
            << " required_hitchance=" << frame.required_hitchance << " min_damage=" << frame.min_damage
            << "\n";
        log << " flags: walk_autofire=" << frame.walk_autofire << " weapon_ready=" << frame.weapon_ready
            << " reloading=" << frame.reloading << " clip=" << frame.clip << "\n";
        log << " internal: trigger_held=" << m_trigger_held << " trigger_shot_scheduled=" << m_trigger_shot_scheduled
            << " trigger_target_valid=" << m_trigger_target_valid << " trigger_target=" << m_trigger_target.pawn
            << " trigger_target_parts=0x" << std::hex << m_trigger_target_parts << std::dec
            << " trigger_press_shots=" << m_trigger_press_shots
            << " spray_state=" << (m_spray_state == spray_state::spraying ? "SPRAYING" : "IDLE")
            << " rcs_shots=" << m_rcs_shots
            << " revolver_committed=" << m_revolver_committed << " revolver_weapon=" << m_revolver_weapon
            << " seed_press_active=" << m_seed_press_active << "\n";
        log << " timers_ms: fire="
            << (m_trigger_fire_time.time_since_epoch().count() == 0
                    ? -1.0f
                    : milliseconds(m_trigger_fire_time - now))
            << " cooldown="
            << (m_trigger_cooldown_until.time_since_epoch().count() == 0
                    ? -1.0f
                    : milliseconds(m_trigger_cooldown_until - now))
            << " release="
            << (m_trigger_release_time.time_since_epoch().count() == 0
                    ? -1.0f
                    : milliseconds(m_trigger_release_time - now))
            << "\n";
        log << " targets=" << frame.target_count << "\n";
        for (std::size_t index = 0; index < frame.target_count; ++index)
        {
            const auto& target = frame.targets[index];
            log << "  target[" << index << "] pawn=" << target.pawn << " reason=" << trigger_debug::name(target.why)
                << " hitbox=" << target.hitbox << " hitgroup=" << target.hitgroup
                << " age_ms=" << target.target_age_ms << " damage=" << target.damage
                << " health=" << target.health << " on_target=" << target.on_target << "\n";
        }
        log << " history=" << debug.history.size() << "\n";
        const auto first = debug.history.size() > 20 ? debug.history.size() - 20 : std::size_t{};
        for (auto index = first; index < debug.history.size(); ++index)
        {
            const auto& entry = debug.history[index];
            log << "  history[" << index << "] at_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(entry.at.time_since_epoch()).count()
                << " state=" << trigger_debug::name(entry.state) << " reason=" << trigger_debug::name(entry.why)
                << " pawn=" << entry.pawn << " hitbox=" << entry.hitbox << " delay_ms=" << entry.delay_ms
                << " cooldown_ms=" << entry.cooldown_ms << " target_age_ms=" << entry.target_age_ms
                << " stuck=" << entry.stuck << "\n";
        }
        log << "\n";
    }
    catch (...)
    {
        // Diagnostics must never take the combat worker down.
    }
}

trigger_debug::snapshot aimbot_t::trigger_debug_snapshot() const
{
    trigger_debug::snapshot out{};
    out.enabled = runtime_config()->general.trigger_debug_overlay;
    if (!out.enabled)
        return out;

    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(m_trigger_debug_mutex);
    const auto& debug = m_trigger_debug;
    if (!debug.seen)
        return out;

    out.state = debug.stuck ? trigger_debug::decision::stuck : debug.state;
    out.why = debug.why;
    out.pawn = debug.pawn;
    out.hitbox = debug.hitbox;
    out.hitgroup = debug.hitgroup;
    out.required_parts = debug.required_parts;
    out.damage = debug.damage;
    out.delay_ms = debug.delay_ms;
    out.cooldown_ms = debug.cooldown_ms;
    out.target_age_ms = debug.target_age_ms;
    out.hitchance = debug.hitchance;
    out.required_hitchance = debug.required_hitchance;
    out.min_damage = debug.min_damage;
    out.target_health = debug.target_health;
    out.stuck = debug.stuck;
    out.targets = debug.targets;
    out.target_count = debug.target_count;
    out.state_ms = debug.state_since.time_since_epoch().count() == 0
                       ? 0.0f
                       : std::chrono::duration<float, std::milli>(now - debug.state_since).count();
    out.on_target_ms = debug.on_target_since.time_since_epoch().count() == 0
                           ? 0.0f
                           : std::chrono::duration<float, std::milli>(now - debug.on_target_since).count();
    out.last_shot_ms = debug.last_shot.time_since_epoch().count() == 0
                           ? -1.0f
                           : std::chrono::duration<float, std::milli>(now - debug.last_shot).count();
    out.update_age_ms = debug.last_update.time_since_epoch().count() == 0
                            ? 0.0f
                            : std::chrono::duration<float, std::milli>(now - debug.last_update).count();
    out.recent_count = std::min(debug.history.size(), out.recent.size());
    const auto first = debug.history.size() - out.recent_count;
    for (std::size_t index = 0; index < out.recent_count; ++index)
        out.recent[index] = debug.history[first + index];
    return out;
}


} // namespace features::aimbot

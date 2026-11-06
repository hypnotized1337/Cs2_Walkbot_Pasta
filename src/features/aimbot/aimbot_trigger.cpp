#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
aimbot_t::trigger_result aimbot_t::trace_direction(const foundation::vec3& eye_pos, const foundation::vec3& direction,
                                                   const std::vector<game::player_snapshot>& players,
                                                   const config::combat_profile::triggerbot& cfg,
                                                   const int hitbox_parts) const
{
    constexpr auto max_range{8192.0f};
    // Trigger prediction follows the sampled target state only. Network ping is not
    // an aim-ahead horizon; server lag compensation consumes the historical sample.
    const auto prediction_time =
        cfg.predictive ? std::clamp(static_cast<float>(cfg.delay) * 0.001f + game::rules::simulation_step, 0.0f, 0.12f)
                       : 0.0f;
    auto local_velocity = foundation::vec3{};
    if (cfg.predictive)
    {
        const auto pawn = game::local_player().pawn();
        if (pawn)
        {
            local_velocity = game::bound_memory().process.load<foundation::vec3>(
                pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
            if (!std::isfinite(local_velocity.x) || !std::isfinite(local_velocity.y) ||
                !std::isfinite(local_velocity.z))
            {
                local_velocity = {};
            }
        }
    }


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

        if (player.health <= 0 || player.invulnerable || player.hitboxes.count <= 0)
        {
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
            const auto offset = (target_velocity - local_velocity) * prediction_time;
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
                continue;
            }
            const auto& bone = bones.bones[hitbox.bone];
            const auto rotation_length = bone.rotation.x * bone.rotation.x + bone.rotation.y * bone.rotation.y +
                                         bone.rotation.z * bone.rotation.z + bone.rotation.w * bone.rotation.w;
            if (!std::isfinite(bone.position.x) || !std::isfinite(bone.position.y) || !std::isfinite(bone.position.z) ||
                !std::isfinite(rotation_length) || rotation_length < 0.5f || rotation_length > 1.5f ||
                bone.position.distance_sqr(player.origin) > 512.0f * 512.0f)
            {
                continue;
            }
            const auto capsule = game::place_hitbox(bone, hitbox);
            float distance{};
            if (foundation::ray_capsule_entry(eye_pos, direction, capsule.start, capsule.end, capsule.radius,
                                              distance) &&
                distance < best.distance)
            {
                best = {&player, bones, distance, hitbox.index, hitgroup};
            }
        }
    }

    if (!best.player || best.hitbox < 0 || best.distance >= max_range)
        return {};

    trigger_result result{};
    result.player = best.player;
    result.bones = best.bones;
    result.hitbox = best.hitbox;
    result.hitgroup = best.hitgroup;
    result.point = eye_pos + direction * best.distance;
    if (simulation::collision().valid())
    {
        ballistics_t::penetration::result passage{};
        if (!simulation::ballistics().pen().run_seed(
                eye_pos, direction, *best.player, best.bones, hitbox_parts,
                cfg.checks.walls == config::combat_profile::wall_policy::penetration, 1.0f, best.hitbox, passage))
            return {};
        result.damage = passage.damage;
        result.penetrated = passage.penetrated;
    }
    else
    {
        const auto& weapon = simulation::ballistics().pen().get_weapon_data();
        if (weapon.range <= best.distance || weapon.damage <= 0.0f)
            return {};
        result.damage = simulation::ballistics().pen().get_max_damage(best.hitgroup, best.player->armor,
                                                                      best.player->has_helmet, best.player->team) *
                        std::pow(std::clamp(weapon.range_modifier, 0.0f, 1.0f), best.distance / 500.0f);
        result.penetrated = false;
    }

    if (cfg.checks.smoke && line_through_smoke(eye_pos, result.point))
        return {};
    return result;
}

void aimbot_t::triggerbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                          const std::vector<game::player_snapshot>& players,
                          const config::combat_profile::triggerbot& cfg)
{
    VESTA_PERF_SCOPE(aim_trigger);
    auto now = std::chrono::steady_clock::now();
    const auto cancel_scheduled = [this]
    {
        m_trigger_shot_scheduled = false;
        m_trigger_fire_time = {};
        m_trigger_target_valid = false;
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
    };
    const auto release_spray_button = [this]
    {
        const auto& input = platform::windows::bound_input();
        // A failed up is only a reason to keep the session if the gateway still believes the button is down.
        // Otherwise the held flags are stale and would block every later press.
        if (!input.pointer(0, 0, platform::windows::pointer_action::primary_up) && input.injected_primary_down())
            return false;
        m_trigger_held = false;
        m_spray_button_down = false;
        m_trigger_release_time = {};
        return true;
    };
    const auto end_spray = [this, &release_spray_button]
    {
        if (!release_spray_button())
            return false;
        m_spray_held = false;
        m_spray_cooling = false;
        m_spray_burst_shots = 0;
        m_spray_last_clip = -1;
        m_spray_cooldown_until = {};
        m_spray_weapon = 0;
        m_spray_check_at = {};
        m_spray_off_target_since = {};
        m_trigger_press_shots = -1;
        m_trigger_target_valid = false;
        return true;
    };

    if (!g_walk_autofire.load(std::memory_order_acquire) &&
        !config::combat_profile::activation_active(cfg.activation_mode, cfg.key))
    {
        cancel_scheduled();
        return;
    }
    // The walk bot's autofire follows the hitboxes its own aim picked, so a body shot it lines up can fire even when
    // the triggerbot's own selection is head only.
    const auto walk_parts = g_walk_autofire.load(std::memory_order_acquire)
                                ? g_walk_autofire_parts.load(std::memory_order_acquire)
                                : 0;
    const auto target_parts = walk_parts ? walk_parts : cfg.hitbox_parts;

    const auto& ctx = ballistics().ctx();
    // The spray holds the trigger for one burst at a time; the game then cycles the weapon while the press is down.
    // Recoil control compensates the punch on the same camera. It is an SMG/rifle feature only: sniper rifles, pistols,
    // shotguns, machine guns and everything else keep their normal per-shot trigger path.
    const auto spray_weapon =
        ctx.weapon_type == game::rules::smg || ctx.weapon_type == game::rules::rifle;
    const auto walk_autofire = g_walk_autofire.load(std::memory_order_acquire);
    const auto spray_requested = walk_autofire && g_walk_autofire_spray.load(std::memory_order_acquire) &&
                                 spray_weapon;
    // The walk bot publishes the hitbox it is fighting for as long as the fight lasts. Spray follows that flag rather
    // than the per-shot trace: holding the button is the whole feature, and the flag drops the moment the enemy is dead
    // or the walk bot stops aiming at it.
    const auto spray_engaged = g_walk_autofire_parts.load(std::memory_order_acquire) != 0;
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

    const auto& spray_settings = runtime_config()->general.m_walk_bot;
    const auto burst_enabled = spray_settings.spray_burst;
    const auto burst_count = std::clamp(spray_settings.spray_burst_count, 3, 30);
    const auto burst_cooldown =
        std::chrono::milliseconds(std::clamp(spray_settings.spray_burst_cooldown_ms, 100, 1500));

    // Starts one held burst: traces the crosshair, presses the trigger and opens a spray fight. The game's own
    // full-auto cycle fires the bullets while the press is held; the burst ends when the magazine count reaches the
    // configured size.
    const auto start_spray_burst = [&]() -> bool
    {
        if (!ctx.valid || ctx.is_reloading || ctx.clip == 0 || local_checks_blocked(cfg.checks))
            return false;
        foundation::vec3 forward{};
        view_angles.to_directions(&forward, nullptr, nullptr);
        const auto start = trace_direction(eye_pos, forward, players, cfg, target_parts);
        if (!start.player)
            return false;
        cancel_scheduled();
        if (!platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_down))
        {
            note(22, static_cast<float>(::GetLastError()), 0.0f);
            return false;
        }
        features::visuals::event_log().begin_trigger_shot("Spray", false);
        m_trigger_target = *start.player;
        m_trigger_held = true;
        m_spray_held = true;
        m_spray_button_down = true;
        m_spray_cooling = false;
        m_spray_weapon = ctx.weapon;
        m_spray_check_at = now + std::chrono::milliseconds(25);
        m_spray_off_target_since = {};
        m_spray_burst_shots = 0;
        m_spray_last_clip = ctx.clip;
        m_spray_cooldown_until = {};
        m_seed_press_active = false;
        m_revolver_committed = false;
        m_trigger_press_shots = -1;
        m_trigger_release_time = now + std::chrono::hours(1);
        m_trigger_cooldown_until = {};
        m_trigger_shot_scheduled = false;
        m_trigger_fire_time = {};
        m_trigger_target_valid = false;
        return true;
    };

    if (m_spray_held)
    {
        // The fight owns the session. A weapon change, a reload, an empty magazine or the walk bot dropping the
        // target ends it, including a pause between bursts.
        if (!spray_requested || !spray_engaged || !ctx.valid || ctx.weapon != m_spray_weapon ||
            ctx.is_reloading || ctx.clip == 0)
        {
            (void)end_spray();
            cancel_scheduled();
            return;
        }

        // Between bursts the trigger is up. Wait out the recovery time, then start the next burst when the crosshair
        // is back on an enemy. If the hand has not settled yet, retry shortly instead of waiting the whole cooldown.
        if (m_spray_cooling || !m_spray_button_down)
        {
            if (m_spray_button_down && !release_spray_button())
                return;
            if (now < m_spray_cooldown_until)
            {
                note_view_engaged(m_trigger_target.pawn);
                return;
            }
            if (start_spray_burst())
                return;
            m_spray_cooling = true;
            m_spray_cooldown_until = now + std::chrono::milliseconds(25);
            return;
        }

        // Count the bullets this press has fired by the magazine. A jump back up is a reload or a fresh magazine.
        if (ctx.clip >= 0)
        {
            if (m_spray_last_clip < 0 || ctx.clip > m_spray_last_clip)
                m_spray_last_clip = ctx.clip;
            else if (ctx.clip < m_spray_last_clip)
            {
                m_spray_burst_shots += m_spray_last_clip - ctx.clip;
                m_spray_last_clip = ctx.clip;
            }
        }
        if (burst_enabled && m_spray_burst_shots >= burst_count)
        {
            // The burst is complete: let go and let the weapon recover before the next one.
            if (!release_spray_button())
                return;
            m_spray_cooling = true;
            m_spray_cooldown_until = now + burst_cooldown;
            return;
        }

        // The walk bot's fight flag says it is aiming at an enemy, not that the crosshair has arrived. A moving
        // target, a long recoil kick or a target switch can leave a blind hold firing into the wall. Check the
        // crosshair at 25 Hz and let go after a short grace; the start path presses again when it is back on an enemy.
        constexpr auto spray_check_period = std::chrono::milliseconds(25);
        constexpr auto spray_off_target_grace = std::chrono::milliseconds(100);
        if (now >= m_spray_check_at)
        {
            m_spray_check_at = now + spray_check_period;
            foundation::vec3 forward{};
            view_angles.to_directions(&forward, nullptr, nullptr);
            const auto on_target = trace_direction(eye_pos, forward, players, cfg, target_parts);
            if (on_target.player)
            {
                m_spray_off_target_since = {};
                m_trigger_target = *on_target.player;
            }
            else
            {
                if (m_spray_off_target_since.time_since_epoch().count() == 0)
                    m_spray_off_target_since = now;
                if (now - m_spray_off_target_since >= spray_off_target_grace)
                {
                    (void)end_spray();
                    cancel_scheduled();
                    return;
                }
            }
        }
        // Keep the fire button down. Another feature may have released the shared injected button between ticks;
        // if so, put it back down instead of waiting for the trigger's next scheduled shot.
        if (!platform::windows::bound_input().injected_primary_down() &&
            !platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_down))
        {
            note(22, static_cast<float>(::GetLastError()), 0.0f);
            return;
        }
        note_view_engaged(m_trigger_target.pawn);
        return;
    }

    if (m_trigger_held)
    {
        VESTA_SEED_COUNT(held_skip);
        note(23, std::chrono::duration<float, std::milli>(now - m_trigger_release_time).count(), 0.0f);
        return;
    }

    // Spray starts as soon as the walk bot says it is fighting on a hitbox. It deliberately skips the per-shot trace,
    // hitchance and delay: the walk bot's aim is already on the enemy and the game's own full-auto cycle fires the
    // burst. This also means a target that leaves and comes back cannot turn the spray into single shots.
    if (spray_requested && spray_engaged)
    {
        if (!start_spray_burst())
        {
            cancel_scheduled();
            return;
        }
        return;
    }
    if (now < m_trigger_cooldown_until)
    {
        cancel_scheduled();
        return;
    }
    constexpr std::uint16_t revolver_id{64};
    const auto revolver = ctx.item_def_idx == revolver_id;
    if ((!ctx.weapon_ready && !revolver) || ctx.is_reloading || ctx.clip == 0)
    {
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
            features::visuals::event_log().push_throttled(0x54524301u, "Trigger blocked: legit check",
                                                          features::visuals::event_kind::blocked,
                                                          std::chrono::milliseconds(750));
            return;
        }

        // CViewRender already contains the final rendered camera orientation. Adding
        // AimPunch here a second time makes the miss grow with target distance.
        const auto trace_angles = view_angles;
        foundation::vec3 forward{};
        trace_angles.to_directions(&forward, nullptr, nullptr);
        // Bone selection is strict: autofire may only fire at the parts the walk bot's own aim selected. Falling
        // back to every part here would shoot the head while the user has it unchecked.
        auto result = trace_direction(eye_pos, forward, players, cfg, target_parts);
        if (!result.player)
        {
            const auto id = game::local_player().crosshair_id();
            if (id > 0 && id < 0x7fff)
                note(10, static_cast<float>(id), 0.0f);
            return;
        }
        if (result.penetrated && result.damage < cfg.min_damage)
        {
            note(11, result.damage, cfg.min_damage);
            return;
        }

        if (cfg.lethal_only && result.hitgroup != game::rules::hitgroup::head &&
            result.damage + 0.001f < static_cast<float>(result.player->health))
        {
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
        note(20, std::chrono::duration<float, std::milli>(m_trigger_fire_time - now).count(), 0.0f);
        return;
    }

    if (!features::misc::auto_stop().ready_to_fire(features::misc::auto_stop_source::advanced_trigger))
    {
        // Auto Stop is a helper, not a veto. If it cannot settle the player (a ladder, a moving platform, a
        // movement key another feature holds), do not leave the trigger aiming forever without firing.
        constexpr auto max_stop_wait = std::chrono::milliseconds(300);
        if (now - m_trigger_fire_time < max_stop_wait)
            return;
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
    }

    // A spray is one burst with many chances: the per-shot hit chance would only keep the trigger from ever going
    // down. The walk bot's own autofire also already selected a hitbox and its ray crossed the target; applying the
    // triggerbot's probability model on top of that was what left sniper rifles aiming without ever firing. The direct
    // trace is still the gate; hitchance stays in force for the standalone triggerbot.
    const auto hit_probability =
        cfg.hitchance > 0.0f && m_trigger_target_valid && !spray_requested && !walk_autofire
            ? ballistics().estimate_hit_probability(m_trigger_target_eye, m_trigger_target_angles, m_trigger_target,
                                                    m_trigger_target_bones, target_parts)
            : 1.0f;
    if (hit_probability < cfg.hitchance / 100.0f)
    {
        note(24, hit_probability * 100.0f, cfg.hitchance);
        // The estimate must follow the aim, not the moment the shot was planned: the planning snapshot can
        // sit on the edge of a hitbox and never reach the wanted chance however well the crosshair is placed
        // now. Take the current aim and target again; if the crosshair left the enemy, start over.
        if (!players.empty())
        {
            foundation::vec3 forward{};
            view_angles.to_directions(&forward, nullptr, nullptr);
            const auto fresh = trace_direction(eye_pos, forward, players, cfg, target_parts);
            if (!fresh.player)
            {
                note(25, 0.0f, 0.0f);
                cancel_scheduled();
                return;
            }
            m_trigger_target = *fresh.player;
            m_trigger_target_bones = fresh.bones;
            m_trigger_target_eye = eye_pos;
            m_trigger_target_angles = view_angles;
        }
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
}


} // namespace features::aimbot

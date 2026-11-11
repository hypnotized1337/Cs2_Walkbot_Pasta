#include <stdafx.hpp>
#include <features/aimbot/aimbot_internal.hpp>
#include <features/multipoint_overlay.hpp>
#include <diagnostics/log_directory.hpp>
#include <platform/windows/storage.hpp>

#include <format>

namespace features::aimbot
{
namespace
{
// A lock and an aim sample published by the movement worker are only trusted while they are fresh. The walk bot
// publishes its lock every movement tick and its aim every view step (about 4 ms), so three missed hand updates must
// release the spray; a frozen worker must not leave the attack button held on a stale frame.
constexpr auto k_spray_sample_max_age = std::chrono::milliseconds(12);
// Hysteresis: the hand must be closer to start a spray than to keep one. A single threshold around the aim's normal
// tracking error chatters the attack button on and off every few ticks.
constexpr float k_spray_start_error = 1.5f;
constexpr float k_spray_keep_error = 3.0f;
} // namespace
void aimbot_t::reset()
{
    end_autospray();
    if (m_revolver_pre_cock_down)
    {
        platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_revolver_pre_cock_down = false;
    }
    if (m_trigger_held)
    {
        auto released = true;
        if (m_seed_held_secondary)
        {
            platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::secondary_up);
        }
        else if (m_seed_held_proxy)
        {
            platform::windows::bound_input().key(VK_F24, false);
        }
        else
        {
            released = platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        }

        if (released)
            m_trigger_held = false;
    }
    m_spray_weapon = 0;
    m_trigger_press_shots = -1;
    m_trigger_release_time = {};
    m_seed_press_active = false;
    m_seed_held_secondary = false;
    m_seed_held_proxy = false;
    m_revolver_committed = false;
    m_revolver_weapon = 0;

    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_cooldown_until = {};
    m_trigger_target_valid = false;
    m_seed_last_shots = -1;
    m_seed_pending_target_tick = -1;
    m_seed_pending_time = {};
    m_seed_phase_receipt = {};
    m_seed_memo_sequence = -1;
    m_seed_memo_pitch = {};
    m_seed_memo_yaw = {};
    m_last_time = 0.0f;
    m_aim_pawn = 0;
    m_current_engaged_target.store(0, std::memory_order_release);
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_selection_view = {};
    m_aim_selection_weapon = 0;
    m_aim_selection_at = {};
    m_aim_last_bone = -1;
    m_aim_last_offset = {};
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_last_camera_angles = {};
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_virtual_angles = {};
    m_aim_virtual_angles_valid = false;
    m_aim_degrees_per_pixel = {};
    m_aim_degrees_candidate = {};
    m_aim_degrees_confirmations = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_previous_relative_velocity = {};
    m_aim_relative_acceleration = {};
    m_aim_velocity_valid = false;
    m_aim_simulation_tick = -1;
    m_aim_simulation_time = 0.0f;
    m_aim_velocity_samples = 0;
    m_rcs_applied = {};
    m_rcs_mouse_error = {};
    m_rcs_pending_mouse = {};
    m_rcs_last_call = {};
    m_rcs_last_input_sequence = -1;
    m_rcs_last_input_time = {};
    m_rcs_weapon = 0;
    m_rcs_shots = 0;
    m_rcs_last_fired = -1;
    m_rcs_active = false;

    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

// Autospray state machine. The walk bot's aim owns target selection and publishes the hitbox it is fighting
// (g_walk_autofire_parts); the spray only follows that lock. Two states, one transition each:
//   IDLE -> SPRAYING: lock and a ready weapon, then press attack.
//   SPRAYING -> IDLE: lock, weapon, menu or focus gone, then release and forget the session.
bool aimbot_t::update_autospray(const ballistics_t::context& ctx)
{
    const auto requested = g_walk_autofire.load(std::memory_order_acquire) &&
                           g_walk_autofire_spray.load(std::memory_order_acquire);
    const auto parts = g_walk_autofire_parts.load(std::memory_order_acquire);
    const auto now_ticks = steady_ticks();
    const auto fresh = [now_ticks](const std::int64_t published_at) noexcept
    {
        if (published_at == 0)
            return false;
        const auto limit = std::chrono::duration_cast<std::chrono::steady_clock::duration>(k_spray_sample_max_age);
        return now_ticks - published_at <= limit.count();
    };
    const auto lock_fresh = fresh(g_walk_autofire_parts_at.load(std::memory_order_acquire));
    const auto aim_fresh = fresh(g_walk_autofire_aim_at.load(std::memory_order_acquire));
    const auto aim_valid = g_walk_autofire_aim_valid.load(std::memory_order_acquire);
    const auto aim_error = g_walk_autofire_aim_error.load(std::memory_order_relaxed);
    const auto error_limit = m_spray_state == spray_state::spraying ? k_spray_keep_error : k_spray_start_error;
    // The walk bot's aim error is the only on-target test this mode uses: no extra trace, FOV or visibility pass. A
    // lock whose hand is not steering, or whose error grew past the keep threshold, releases on this tick.
    const auto on_target =
        aim_valid && aim_fresh && std::isfinite(aim_error) && aim_error <= error_limit;
    const auto locked = requested && parts != 0 && lock_fresh && on_target;
    const auto spray_weapon = ctx.valid && game::rules::is_spray_weapon(ctx.weapon_type);
    const auto weapon_ready = spray_weapon && !ctx.is_reloading && ctx.clip != 0;
    const auto input_ready = render::combat_input_ready() && !render::menu_open();
    auto& input = platform::windows::bound_input();

    if (m_spray_state == spray_state::idle)
    {
        if (!locked || !weapon_ready || !input_ready)
            return false;
        if (!input.pointer(0, 0, platform::windows::pointer_action::primary_down))
            return false;
        m_spray_state = spray_state::spraying;
        m_spray_weapon = ctx.weapon;
        begin_spray_log();
        m_trigger_held = true;
        // The spray is not a per-shot press: keep every trigger/seed latch out of its way.
        m_trigger_press_shots = -1;
        m_trigger_release_time = {};
        m_trigger_shot_scheduled = false;
        m_trigger_fire_time = {};
        m_trigger_target_valid = false;
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
        return true;
    }

    if (!locked || !weapon_ready || ctx.weapon != m_spray_weapon || !input_ready)
    {
        end_autospray();
        return false;
    }
    // Another feature may have lifted the shared injected button between ticks; put it back down, and end the session
    // when even that fails rather than leave the state machine claiming a press that is not there.
    if (!input.injected_primary_down() && !input.pointer(0, 0, platform::windows::pointer_action::primary_down))
    {
        end_autospray();
        return false;
    }
    return true;
}

void aimbot_t::end_autospray() noexcept
{
    if (m_spray_state == spray_state::idle)
        return;
    m_spray_state = spray_state::idle;
    m_spray_weapon = 0;
    // Nothing from this spray may survive into the next one: a pending pixel flushed after the release would move the
    // view with no button down, and a stale applied angle would be read as aim error by the next burst.
    m_rcs_applied = {};
    m_rcs_mouse_error = {};
    m_rcs_pending_mouse = {};
    m_rcs_last_call = {};
    m_rcs_last_input_sequence = -1;
    m_rcs_last_input_time = {};
    m_rcs_weapon = 0;
    m_rcs_shots = 0;
    m_rcs_last_fired = -1;
    m_rcs_active = false;
    // The standalone aim's virtual camera still contains the last RCS offset; drop it with the burst so the first aim
    // pass after the release reconciles against the real camera instead of correcting a correction.
    m_aim_error = {};
    m_aim_virtual_angles_valid = false;
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_last_input_sequence = -1;
    m_spray_log_rcs_delta = {};
    m_spray_log_rcs_pixels = {};
    auto& input = platform::windows::bound_input();
    // A failed up is only a reason to keep the held flag when the gateway still believes the button is down; otherwise
    // the flag is stale and would block every later press. The per-shot release path retries the up if it stuck.
    if (input.pointer(0, 0, platform::windows::pointer_action::primary_up) || !input.injected_primary_down())
        m_trigger_held = false;
    m_trigger_press_shots = -1;
    m_trigger_release_time = {};
    // One final row with attack_held = 0 proves the release happened on this tick, not in the gap after the file.
    try
    {
        capture_spray_log(true);
    }
    catch (...)
    {
    }
    write_spray_log();
}

void aimbot_t::begin_spray_log()
{
    m_spray_log.clear();
    m_spray_log_rcs_delta = {};
    m_spray_log_rcs_pixels = {};
    const auto snapshot = config::get_runtime_snapshot();
    m_spray_log_active = snapshot && snapshot->general.m_walk_bot.spray_log;
    m_spray_log_start = std::chrono::steady_clock::now();
    m_spray_log_last = m_spray_log_start;
}

void aimbot_t::capture_spray_log(const bool release_row)
{
    if (!m_spray_log_active || (m_spray_state != spray_state::spraying && !release_row))
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto& cfg = runtime_config()->combat.get(game::local_player().weapon_type());
    const auto age_ms = [&](const std::int64_t published_at) noexcept
    {
        if (published_at == 0)
            return -1.0f;
        const auto published = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(published_at));
        return std::chrono::duration<float, std::milli>(now - published).count();
    };

    spray_log_row row{};
    row.t_ms = std::chrono::duration<float, std::milli>(now - m_spray_log_start).count();
    row.dt_ms = std::chrono::duration<float, std::milli>(now - m_spray_log_last).count();
    row.spraying = m_spray_state == spray_state::spraying ? 1 : 0;
    row.attack_held = m_trigger_held ? 1 : 0;
    row.lock_age_ms = age_ms(g_walk_autofire_parts_at.load(std::memory_order_acquire));
    row.aim_error = g_walk_autofire_aim_error.load(std::memory_order_relaxed);
    row.aim_age_ms = age_ms(g_walk_autofire_aim_at.load(std::memory_order_acquire));
    row.aim_valid = g_walk_autofire_aim_valid.load(std::memory_order_acquire) ? 1 : 0;
    foundation::vec3 eye{};
    foundation::vec3 view{};
    foundation::vec3 base{};
    if (game::camera().sample(eye, view))
    {
        row.view_pitch = view.x;
        row.view_yaw = view.y;
    }
    // The shot starts from the client's base view angle, not the rendered camera, and adds the game's bullet-scale
    // punch. Logging both makes a camera/punch mismatch visible instead of turning it into a silent aim offset.
    if (const auto pawn = game::local_player().pawn(); pawn)
    {
        row.shots_fired =
            game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
        static_cast<void>(game::bound_memory().process.copy(pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id), &base,
                                                            sizeof(base)));
        row.base_pitch = base.x;
        row.base_yaw = base.y;
        row.punch_scale = 2.0f; // AimPunchServices::BulletScale
        if (const auto punch = simulation::read_cached_punch(
                game::bound_memory().process, pawn, SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id)))
        {
            // read_cached_punch already applies the game's 2x bullet scale. Keep the file's punch columns in raw
            // aim-punch degrees and record the scale separately so the impact point can be checked by hand.
            row.punch_valid = 1;
            row.punch_x = punch->x * 0.5f;
            row.punch_y = punch->y * 0.5f;
            auto impact = base + *punch;
            impact.x = std::clamp(impact.x, -89.0f, 89.0f);
            impact.y = foundation::wrap_yaw(impact.y);
            row.impact_pitch = impact.x;
            row.impact_yaw = impact.y;
        }
        else
        {
            row.impact_pitch = base.x;
            row.impact_yaw = base.y;
        }
    }
    row.impact_error = row.aim_error; // the walk bot now measures its fight error from this impact point
    row.rcs_pitch = m_spray_log_rcs_delta.x;
    row.rcs_yaw = m_spray_log_rcs_delta.y;
    row.pending_x = m_rcs_pending_mouse.x;
    row.pending_y = m_rcs_pending_mouse.y;
    row.rcs_px_x = static_cast<int>(m_spray_log_rcs_pixels.x);
    row.rcs_px_y = static_cast<int>(m_spray_log_rcs_pixels.y);
    row.aim_px_x = g_walk_autofire_aim_px_x.load(std::memory_order_relaxed);
    row.aim_px_y = g_walk_autofire_aim_px_y.load(std::memory_order_relaxed);
    row.aim_delta = g_walk_autofire_aim_delta.load(std::memory_order_relaxed);
    // In aimbot(), m_rcs_applied is added to the sampled camera before the target error is measured. During a walk bot
    // spray RCS stands down, so 0 records that no RCS correction took part in this impact error.
    row.rcs_order = walk_aim_owns_recoil() ? 0 : 1;
    row.rcs_owner = walk_aim_owns_recoil() ? 1 : 0;
    row.rcs_enabled = cfg.aimbot.rcs.enabled ? 1 : 0;
    row.rcs_start_bullet = cfg.aimbot.rcs.start_bullet;
    row.rcs_strength_pitch = cfg.aimbot.rcs.pitch;
    row.rcs_strength_yaw = cfg.aimbot.rcs.yaw;
    row.rcs_response_ms = cfg.aimbot.rcs.response_ms;
    try
    {
        // Keep the newest rows: the release (and any overspray before it) is the interesting end of a long spray.
        if (m_spray_log.size() >= 300)
            m_spray_log.erase(m_spray_log.begin());
        m_spray_log.push_back(row);
    }
    catch (...)
    {
        m_spray_log_active = false; // diagnostics must not take the combat worker down
    }
    m_spray_log_last = now;
    m_spray_log_rcs_delta = {};
    m_spray_log_rcs_pixels = {};
}

void aimbot_t::write_spray_log() noexcept
{
    if (!m_spray_log_active)
        return;
    try
    {
        std::string csv;
        csv.reserve((m_spray_log.size() + 1) * 200);
        csv += "t_ms,dt_ms,spraying,attack_held,lock_age_ms,view_pitch,view_yaw,base_pitch,base_yaw,"
               "punch_valid,punch_x,punch_y,punch_scale,impact_pitch,impact_yaw,impact_error_deg,"
               "aim_error_deg,aim_age_ms,aim_valid,shots,rcs_pitch_deg,rcs_yaw_deg,pending_px_x,pending_px_y,"
               "rcs_px_x,rcs_px_y,aim_px_x,aim_px_y,aim_delta_deg,rcs_order,rcs_owner,rcs_enabled,start_bullet,"
               "strength_pitch,strength_yaw,response_ms\n";
        for (const auto& row : m_spray_log)
        {
            csv += std::format("{:.2f},{:.2f},{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{:.3f},{:.3f},{:.1f},"
                               "{:.3f},{:.3f},{:.3f},{:.3f},{:.2f},{},{},{:.3f},{:.3f},{:.2f},{:.2f},"
                               "{},{},{},{},{:.3f},{},{},{},{},{:.1f},{:.1f},{:.1f}\n",
                               row.t_ms, row.dt_ms, row.spraying, row.attack_held, row.lock_age_ms, row.view_pitch,
                               row.view_yaw, row.base_pitch, row.base_yaw, row.punch_valid, row.punch_x, row.punch_y,
                               row.punch_scale, row.impact_pitch, row.impact_yaw, row.impact_error, row.aim_error,
                               row.aim_age_ms, row.aim_valid, row.shots_fired, row.rcs_pitch, row.rcs_yaw, row.pending_x,
                               row.pending_y, row.rcs_px_x, row.rcs_px_y, row.aim_px_x, row.aim_px_y, row.aim_delta,
                               row.rcs_order, row.rcs_owner, row.rcs_enabled, row.rcs_start_bullet,
                               row.rcs_strength_pitch, row.rcs_strength_yaw, row.rcs_response_ms);
        }
        const auto stamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        const auto path = diagnostics::default_logs_directory() / std::format("spray-{}.csv", stamp);
        static_cast<void>(platform::windows::write_file_atomically(path, csv));
    }
    catch (...)
    {
        // A diagnostics write must never interfere with releasing the attack button.
    }
    m_spray_log_active = false;
    m_spray_log.clear();
}

void aimbot_t::on_render(render::draw::draw_list& draw_list)
{
    refresh_runtime_config();
    const auto eye_pos = game::camera().origin();
    const auto view_angles = game::camera().angles();

    const auto& ctx = ballistics().ctx();
    const auto cfg = runtime_config()->combat.get(ctx.weapon_type);

    if (!ctx.valid)
    {
        return;
    }

    if (cfg.aimbot.enabled && cfg.aimbot.multipoint && cfg.aimbot.multipoint_config.display)
    {
        const auto players = game::world().players();
        if (players)
            features::multipoint_overlay::draw(draw_list, *players, cfg.aimbot.hitbox_parts,
                                               cfg.aimbot.multipoint, cfg.aimbot.multipoint_config);
    }

    const auto valid_weapon = game::rules::is_firearm(ctx.weapon_type);

    if (valid_weapon && cfg.other.penetration_crosshair)
    {
        const auto global_vars = game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().global_vars);
        const auto current_time = global_vars ? game::bound_memory().process.load<float>(global_vars + 0x30) : 0.0f;
        draw_penetration_crosshair(draw_list, eye_pos, view_angles, cfg, current_time);
    }

    const auto draw_aim_visualization = cfg.aimbot.enabled && cfg.aimbot.draw_fov;
    if (!draw_aim_visualization)
    {
        m_fov_alpha.snap(0.0f);
        return;
    }
    m_fov_alpha.set_target(valid_weapon && draw_aim_visualization ? 1.0f : 0.0f);
    m_fov_alpha.update(render::draw::get_delta_time());

    if (m_fov_alpha.value() <= 0.01f)
    {
        return;
    }

    draw_fov_ring(draw_list, eye_pos, view_angles, cfg.aimbot);
}

// Revolver pre-cock: holds the trigger while the hammer is cocking and lets go shortly before it would fire, so the
// trigger bot's own press fires at once.
void aimbot_t::update_revolver_pre_cock(const ballistics_t::context& ctx, const config::combat_profile::triggerbot& cfg)
{
    constexpr std::uint16_t revolver_id{64};
    auto& input = platform::windows::bound_input();
    const auto revolver_available = ctx.valid && ctx.item_def_idx == revolver_id && cfg.enabled &&
                                    cfg.revolver_pre_cock && !ctx.is_reloading && ctx.clip != 0;
    if (m_revolver_pre_cock_down && (!revolver_available || m_revolver_weapon != ctx.weapon))
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    if (!revolver_available || m_trigger_held)
        return;

    m_revolver_weapon = ctx.weapon;
    const auto ready_tick = ctx.postpone_fire_ready_tick;
    const auto ticks_left = ready_tick > 0 ? ready_tick - ctx.player_tick : std::numeric_limits<int>::max();
    const auto ready_fraction = std::isfinite(ctx.postpone_fire_ready_fraction)
                                    ? std::clamp(ctx.postpone_fire_ready_fraction, 0.0f, 1.0f)
                                    : 0.0f;
    const auto milliseconds_left =
        ticks_left == std::numeric_limits<int>::max()
            ? std::numeric_limits<float>::max()
            : (static_cast<float>(ticks_left) + ready_fraction) * game::rules::simulation_step * 1000.0f;
    const auto release_margin = static_cast<float>(std::clamp(cfg.revolver_release_margin_ms, 1, 60));
    if (m_revolver_pre_cock_down && ready_tick > 0 && milliseconds_left <= release_margin)
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    else if (!m_revolver_pre_cock_down && (ready_tick <= 0 || ready_tick <= ctx.player_tick))
    {
        m_revolver_pre_cock_down = input.pointer(0, 0, platform::windows::pointer_action::primary_down);
    }
}

void aimbot_t::cancel_scheduled_trigger()
{
    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_target_valid = false;
    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

void aimbot_t::reset_aim_tracking()
{
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_virtual_angles_valid = false;
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_pawn = 0;
    m_current_engaged_target.store(0, std::memory_order_release);
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_last_offset = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_velocity_valid = false;
}

// Ends a trigger press once the game consumed the shot (a newer command tick for a seed press, or a higher shot
// count), once the press timed out, or when a seed press lost its weapon context. A seed press is released
// unconditionally; a normal press only when the release went through.
void aimbot_t::release_finished_press(const ballistics_t::context& ctx, const std::chrono::steady_clock::time_point now)
{
    // A spray owns its own hold and release (update_autospray); the latches below belong to the per-shot trigger and
    // the seed press.
    if (m_spray_state != spray_state::idle)
        return;
    const auto shots_fired = [](const std::uintptr_t pawn)
    {
        return pawn ? game::bound_memory().process.load<std::int32_t>(pawn +
                                                                      SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id))
                    : -1;
    };
    bool command_consumed = false;
    if (ctx.valid && m_seed_press_active && m_seed_pending_target_tick >= 0)
    {
        const auto pawn = game::local_player().pawn();
        const auto command_tick = read_seed_tick();
        const auto shots = shots_fired(pawn);
        command_consumed =
            command_tick > m_seed_pending_target_tick || (m_seed_last_shots >= 0 && shots > m_seed_last_shots);
    }
    else if (!m_seed_press_active && m_trigger_press_shots >= 0)
    {
        command_consumed = shots_fired(game::local_player().pawn()) > m_trigger_press_shots;
    }

    const auto context_lost = !ctx.valid && m_seed_press_active;
    if (!context_lost && !command_consumed && now < m_trigger_release_time)
        return;
    if (command_consumed)
        features::visuals::event_log().mark_latest_trigger_consumed();
    auto& input = platform::windows::bound_input();
    if (m_seed_press_active)
    {
        input.pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_trigger_held = false;
        m_seed_press_active = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
    else if (input.pointer(0, 0, platform::windows::pointer_action::primary_up) ||
             !input.injected_primary_down())
    {
        // A failed release with the gateway no longer holding the button down leaves no real press to preserve;
        // keeping m_trigger_held would stall every later shot until a manual click reset the state.
        m_trigger_held = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
    else if (!m_seed_press_active && m_trigger_release_time.time_since_epoch().count() != 0 &&
             now - m_trigger_release_time >= std::chrono::milliseconds(750))
    {
        // The release has been overdue for 750 ms and the gateway still believes the button is down. At this point
        // the pending press is no longer a shot being delivered; it is a latch that blocks every future trigger
        // pass. Send one last up and clear the state. The next press re-sends the down, and reset() still owns the
        // final input cleanup on disable/death.
        input.pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_trigger_held = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
        m_trigger_release_time = {};
        cancel_scheduled_trigger();
    }
}

// A shot count past the one recorded for the pending seed press means that press already fired.
void aimbot_t::observe_shots_fired(const std::uintptr_t pawn)
{
    if (!pawn)
        return;
    const auto shots_fired =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
    if (m_seed_last_shots >= 0 && shots_fired > m_seed_last_shots && m_seed_pending_target_tick >= 0)
        m_seed_pending_target_tick = -1;
    m_seed_last_shots = shots_fired;
}

// Remembers the target the FOV indicator follows (drawn by on_render).
void aimbot_t::publish_indicator(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                                 const target& picked)
{
    if (!picked.player)
        return;
    auto point = picked.aim_point;
    if (picked.bone >= 0 && picked.bone < 128 && picked.bones.is_valid())
    {
        const auto& bone = picked.bones.bones[picked.bone];
        point = bone.to_world(picked.aim_offset);
    }
    std::scoped_lock lock(m_indicator_mutex);
    m_indicator_point = point;
    m_indicator_fov = selection_fov(cfg, eye, point);
    m_indicator_time = std::chrono::steady_clock::now();
    m_indicator_pawn = picked.player->pawn;
    m_indicator_bone_cache = picked.player->bone_cache;
    m_indicator_bone = picked.bone;
    m_indicator_offset = picked.aim_offset;
}

// Chooses the aim target, reusing the last choice for the same world sample within 16 ms, and steers towards it.
// Returns false when the aimbot's local checks block this frame.
bool aimbot_t::select_and_aim(const combat_frame& frame, const config::combat_profile::resolved_config& cfg,
                              const bool visual_scan_requested, const bool visual_scan_due)
{
    if (local_checks_blocked(cfg.aimbot.checks))
    {
        m_aim_pawn = 0;
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
        return false;
    }
    constexpr auto reuse_window = std::chrono::milliseconds(16);
    constexpr auto indicator_period = std::chrono::milliseconds(33);
    const auto selection_now = std::chrono::steady_clock::now();
    const auto view_delta = std::hypot(frame.view_angles.x - m_aim_selection_view.x,
                                       foundation::wrap_yaw(frame.view_angles.y - m_aim_selection_view.y));
    const auto can_reuse = m_aim_selection_world == frame.targets && m_aim_selection_weapon == frame.weapon.weapon &&
                           selection_now - m_aim_selection_at < reuse_window &&
                           (m_aim_selection_target.player || view_delta < 0.5f);
    auto selected_target =
        can_reuse ? m_aim_selection_target : choose_target(frame.eye, frame.view_angles, *frame.targets, cfg);
    if (selected_target.player)
    {
        selected_target.fov = get_fov(frame.view_angles, frame.eye, selected_target.aim_point);
        if (selected_target.fov > selection_fov(cfg.aimbot, frame.eye, selected_target.aim_point))
            selected_target = {};
    }
    if (!can_reuse)
    {
        m_aim_selection_world = frame.targets;
        m_aim_selection_target = selected_target;
        m_aim_selection_view = frame.view_angles;
        m_aim_selection_weapon = frame.weapon.weapon;
        m_aim_selection_at = std::chrono::steady_clock::now();
    }
    if (selected_target.player && visual_scan_requested)
    {
        publish_indicator(cfg.aimbot, frame.eye, selected_target);
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    else if (!selected_target.player && visual_scan_due &&
             cfg.aimbot.fov_config.selection == config::combat_profile::fov_settings::target_distance)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(cfg.aimbot, frame.eye,
                          choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg, false));
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    if (selected_target.player)
    {
        note_view_engaged(selected_target.player->pawn);
        m_current_engaged_target.store(selected_target.player->pawn, std::memory_order_release);
        aimbot(frame.eye, frame.view_angles, selected_target, cfg.aimbot);
    }
    return true;
}

void aimbot_t::tick()
{
    refresh_runtime_config();
    // Capture the tick that just ran while the spray is still marked active. update_autospray below is what starts or
    // ends the session, and end_autospray writes the file.
    capture_spray_log();
    m_current_engaged_target.store(0, std::memory_order_release);
    const auto ctx = ballistics().ctx();
    if (ctx.valid)
    {
        // The game time went backwards: a new match or map.
        if (ctx.current_time < m_last_time - 0.5f)
            reset();
        m_last_time = ctx.current_time;
    }

    if (!m_rng_seeded)
    {
        m_rng.seed(static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff));
        m_rng_seeded = true;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto requested_cfg = runtime_config()->combat.get(game::local_player().weapon_type());
    const auto walk_autofire = g_walk_autofire.load(std::memory_order_acquire);
    const auto trigger_activation_active =
        walk_autofire || (requested_cfg.triggerbot.enabled &&
                          config::combat_profile::activation_active(requested_cfg.triggerbot.activation_mode,
                                                                    requested_cfg.triggerbot.key));

    update_revolver_pre_cock(ctx, requested_cfg.triggerbot);
    const auto spray_holds = update_autospray(ctx);

    // The trigger key was let go during a normal press: release and forget it now. A spray is released by
    // update_autospray, which ran above.
    if (m_trigger_held && !m_seed_press_active && !trigger_activation_active)
    {
        const auto& input = platform::windows::bound_input();
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up) ||
            !input.injected_primary_down())
        {
            m_trigger_held = false;
            m_trigger_press_shots = -1;
            m_trigger_release_time = {};
            m_revolver_committed = false;
        }
        cancel_scheduled_trigger();
    }
    if (m_trigger_held)
        release_finished_press(ctx, now);

    const auto aimbot_requested =
        requested_cfg.aimbot.enabled &&
        config::combat_profile::activation_active(requested_cfg.aimbot.activation_mode, requested_cfg.aimbot.key);
    if (!aimbot_requested)
    {
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
    }
    // With an independent seed mode the seed thread presses; this trigger bot stays idle. The walk bot's autofire is
    // its own trigger: it fires through this path even when a seed mode is configured, so a seed press that waits for
    // its activation key cannot leave the walk bot aiming at a sniper target without shooting. Its spray already
    // overrides the seed for the same reason.
    const auto independent_seed =
        requested_cfg.triggerbot.seed_type != config::combat_profile::seed_mode::none && !walk_autofire;
    const auto trigger_requested =
        !independent_seed &&
        (walk_autofire || (requested_cfg.triggerbot.enabled &&
                           config::combat_profile::activation_active(requested_cfg.triggerbot.activation_mode,
                                                                     requested_cfg.triggerbot.key)));
    const auto dynamic_fov_visualization_requested =
        requested_cfg.aimbot.enabled && requested_cfg.aimbot.draw_fov &&
        requested_cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;

    // Nothing else refreshes the weapon context while only the FOV ring needs it.
    if (dynamic_fov_visualization_requested && now >= m_next_visual_scan && !aimbot_requested && !trigger_requested)
        simulation::ballistics().tick();

    recoil_control(requested_cfg.aimbot, aimbot_requested && ctx.valid);
    if (!aimbot_requested && !trigger_requested && !m_trigger_held && !dynamic_fov_visualization_requested)
    {
        reset_aim_tracking();
        cancel_scheduled_trigger();
        return;
    }

    // A shot scheduled by the trigger bot fires on time even without a fresh target list.
    if (trigger_requested && m_trigger_shot_scheduled && !spray_holds)
    {
        static const std::vector<game::player_snapshot> no_targets{};
        triggerbot({}, {}, no_targets, requested_cfg.triggerbot);
    }

    if (!ctx.valid)
        return;

    const auto cfg = runtime_config()->combat.get(ctx.weapon_type);
    const auto pawn = game::local_player().pawn();
    observe_shots_fired(pawn);
#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
    research::impact_probe_poll(pawn, now);
    research::seed_stats_flush(now);
#endif

    if (!game::rules::is_firearm(ctx.weapon_type))
        return;
    const auto visualization_only =
        dynamic_fov_visualization_requested && !aimbot_requested && !trigger_requested && !m_trigger_held;
    if (visualization_only && now < m_next_visual_scan)
        return;

    combat_frame frame{};
    frame.sequence = ++m_combat_sequence;
    frame.timestamp = now;
    frame.camera = game::camera().matrix();
    frame.eye = game::camera().origin();
    frame.view_angles = game::camera().angles();
    static_cast<void>(game::camera().sample(frame.eye, frame.view_angles));
    frame.local_pawn = pawn;
    frame.flash_alpha = game::local_player().flash_alpha();
    frame.weapon = ctx;
    frame.targets = game::world().players();
    if (!frame.targets)
    {
        flush_recoil_input();
        return;
    }

    const auto visual_scan_requested = cfg.aimbot.enabled && cfg.aimbot.draw_fov &&
                                       cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;
    const auto visual_scan_due = visual_scan_requested && now >= m_next_visual_scan;
    const auto aim_can_select = aimbot_requested && cfg.aimbot.enabled &&
                                config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key) &&
                                !m_trigger_held && !ctx.is_reloading && ctx.clip != 0 &&
                                (!visual_scan_due || !local_checks_blocked(cfg.aimbot.checks));
    // The FOV indicator follows the would-be target even while the aimbot is not selecting.
    if (visual_scan_due && !aim_can_select)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(
            cfg.aimbot, frame.eye,
            choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg,
                          cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::target_distance));
        m_next_visual_scan = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
    }
    if (visualization_only)
        return;
    if (ctx.is_reloading || ctx.clip == 0)
    {
        cancel_scheduled_trigger();
        flush_recoil_input();
        return;
    }

    if (!spray_holds && (cfg.triggerbot.enabled || walk_autofire) &&
        (walk_autofire || cfg.triggerbot.seed_type == config::combat_profile::seed_mode::none))
        triggerbot(frame.eye, frame.view_angles, *frame.targets, cfg.triggerbot);

    if (m_trigger_held)
        note_view_engaged(m_trigger_target_valid ? m_trigger_target.pawn : 0);
    if (cfg.aimbot.enabled && !m_trigger_held)
    {
        if (!config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key))
            reset_aim_tracking();
        else if (!select_and_aim(frame, cfg, visual_scan_requested, visual_scan_due))
        {
            flush_recoil_input();
            return;
        }
    }
    // Publish the trigger target even between bursts: auto-crouch keeps its duck held for the whole engagement,
    // not only while the trigger button happens to be down.
    if (trigger_requested && m_trigger_target_valid)
        m_current_engaged_target.store(m_trigger_target.pawn, std::memory_order_release);
    flush_recoil_input();
}


} // namespace features::aimbot

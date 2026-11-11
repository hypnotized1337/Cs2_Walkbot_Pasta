#pragma once

#include <game/memory/remote_pointer.hpp>
#include <foundation/identity.hpp>
#include <game/state/services.hpp>
#include <foundation/math/ray_capsule.hpp>
#include <features/aimbot/aimbot.hpp>
#include <diagnostics/journal.hpp>
#include <features/aimbot/aim_control.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/visuals/event_log.hpp>
#include <game/input/bindings.hpp>
#include <game/input/action_state.hpp>
#include <simulation/aim_parts.hpp>
#include <simulation/multipoint.hpp>
#include <simulation/punch_snapshot.hpp>
#include <simulation/grenade.hpp>
#include <simulation/seed_timing.hpp>
#include <simulation/seed_evaluation.hpp>
#include <simulation/shot_clock_evidence.hpp>
#include <simulation/native_velocity.hpp>
#include <simulation/shot_state.hpp>
#include <simulation/shot_trace.hpp>
#include <features/aimbot/seed_state.hpp>

#include <features/aimbot/seed_research.hpp>

namespace features::aimbot
{
using simulation::ballistics;
using simulation::ballistics_t;

// Helpers shared by the aimbot translation units (selection, trigger, recoil, seed). The aim-point builder is in
// simulation/multipoint.hpp so the Aimbot and the Walk Bot's own aim use the same cap/side geometry.

using point_buffer = simulation::aim_point_buffer;
using simulation::build_aim_points;

// Whether the local player hosts the session (listen server: ping 0); nothing if the controller is unreadable.
[[nodiscard]] std::optional<bool> is_host_seed_session(std::uintptr_t controller);
// The tick the next shot is simulated at: the host tick, or the pawn simulation tick on a remote server.
[[nodiscard]] int read_seed_tick(std::uintptr_t controller, std::uintptr_t pawn, bool host_session);
// read_seed_tick for the local player.
[[nodiscard]] int read_seed_tick();
// Whether a line crosses an active smoke cloud (or the projectile list is unavailable).
[[nodiscard]] bool line_through_smoke(const foundation::vec3& start, const foundation::vec3& end);
// Why the local player may not shoot now under the legit checks (airborne, flashed), or none.
[[nodiscard]] combat_block_reason local_block_reason(const config::combat_profile::legit_checks& checks);
[[nodiscard]] bool local_checks_blocked(const config::combat_profile::legit_checks& checks);
// Spawn immunity read live; the snapshot value is used when the read fails.
[[nodiscard]] bool live_target_invulnerable(std::uintptr_t pawn, const bool snapshot_value);
// The selection field of view for a target at this distance (log-interpolated between near and far).
[[nodiscard]] float distance_fov(const config::combat_profile::aimbot& cfg, float distance_units);
[[nodiscard]] float selection_fov(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                                  const foundation::vec3& target);

} // namespace features::aimbot

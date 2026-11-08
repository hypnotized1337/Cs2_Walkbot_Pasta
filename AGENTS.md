# AGENTS.md

Guidance for coding agents working in this repository. Read this before building or changing behavior.

## What this is

Vesta is a Windows x64 C++23 overlay/assist for Counter-Strike 2. It attaches to the game process as an ordinary user
process (no elevation, no UIAccess) and reads game memory read-only. Dear ImGui renders the UI. The build is MSVC +
CMake with the Visual Studio generator.

## Build

CMake is not on `PATH` on this machine. Use the Visual Studio 18 CMake:

Git Bash:

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --preset debug-local
"$CMAKE" --build --preset debug-local --target vesta -- -m
```

Existing configured tree (no reconfigure):

```bash
"$CMAKE" --build build/debug --config Debug --target vesta -- -m
```

PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/debug --config Debug --target vesta -- -m
```

- Use `-m`, not `/m`, in Git Bash: MSYS rewrites `/m` to `M:/`.
- Close `pasta.exe` before linking. If the link fails with `LNK1168`, the old process still owns `build/bin/pasta.exe`.
- Output: `build/bin/pasta.exe` (plus `pasta.pdb`). Build artifacts are ignored.
- `pasta_loader` is a separate launcher target: `build/bin/pasta_loader.exe` requests administrator rights, opens
  an idle window with a **Load** button, starts Steam/CS2, waits briefly for the game modules, then runs `pasta.exe`.
  It does not inject; build it with `--target pasta_loader` when needed.
- Pushing to `main` runs `.github/workflows/release.yml`, which builds and publishes both executables as a new release.
  `.github/workflows/build.yml` builds pull requests without publishing.
- `debug-local` is the local Visual Studio 18 preset. It reuses `build/debug`. The `debug`, `release` and `profile`
  presets target Visual Studio 17 2022 for CI; do not change them to fix a local build.
- Configure embeds `configs/legit.cfg` as the built-in default profile. If that file is absent, CMake warns and falls
  back to an empty profile. Restore the file and reconfigure to re-embed the product default.
- Debug is `/Od`; use Release or the `profile` preset for performance measurements.

## Runtime and config layout

- Live config: `%TEMP%\vesta\configs\current.cfg` (read at startup, written by the app when settings change).
- Named configs: the folder in `HKCU\Software\Vesta\ConfigFolder` (currently `C:\Users\ivan\Documents\VESTA`),
  `*.cfg`. Loading a named config copies it into the live cache; saving writes the live settings to the named file.
- Startup applies the embedded default profile, then overlays `current.cfg`. Missing or new keys inherit the
  embedded profile, which is why `configs/legit.cfg` matters.
- Data root: `%TEMP%\vesta` (`configs`, `logs`, `cache`, `nav`, `walkbot`).
- Walk-bot recordings: `%TEMP%\vesta\walkbot\<map>\<t|ct>\*.route`.
- Logs/journal: `%TEMP%\vesta\logs`.
- Trace, when `VESTA_SEED_TRACE_DIR` is set: `config-at-launch.json`, `schema.json`, `scene.jsonl` in its parent.
- Edit the live cache only while `pasta.exe` is closed; the app writes it from the running process. Do not edit a
  user's named config unless the task asks for it.

## Threads and shared state

Workers are started in `src/app/runtime_session.cpp`; the loops are in `src/app/workers.cpp`:

- game state: 64 Hz; owns map/match transitions and calls `walk_bot().request_reset()`.
- movement: 1 ms active / 4 ms idle; runs auto-stop and `walk_bot().tick()`.
- combat: 250 Hz; aim and ballistics.
- seed trigger; render thread (overlay); diagnostics.

Walk-bot invariants:

- `walk_bot_t::tick()` runs on the movement worker and is the only writer of its movement/route/combat state.
- `walk_bot_t::on_render()` runs on the render thread. It must not mutate movement state. It may touch only its
  render-only caches (`m_edge_visible`, `m_edge_cursor`, `m_edge_graph`).
- `m_route_mutex` guards route state shared with render: `m_graph`, `m_path`, `m_destination`,
  `m_has_destination`, `m_walking`, `m_recording_active`, `m_recordings_shown`, `m_last_origin`. Copy
  `shared_ptr`s under the lock, then release it before drawing or planning. Keep lock scopes short.
- `m_knife_watch_mutex` guards `m_knife_watch`. The movement worker writes it; the render eligibility overlay copies
  it under the lock. Every write and clear must lock it.
- `m_reset_requested` is `std::atomic<bool>`. Any thread may call `request_reset()`; only the movement worker
  consumes it, at the top of `tick()`. `match_flow` uses the same pattern.
- `m_progress` and `m_render_obstacle` are atomics for the render thread; preserve their acquire/release pairing.
- Config is immutable per snapshot. `config::publish_runtime_snapshot()` publishes a
  `shared_ptr<const runtime_snapshot>`; workers read `config::get_runtime_snapshot()` each tick. Do not read
  `config::general_settings` directly from a worker.
- Input is owned. `input_lifecycle::owned_control` sends only transitions and must be released on disable, death,
  reset or focus loss. The menu, aimbot, trigger and the player's own mouse win over walk-bot input. Never send
  input from the render thread. Gate on `render::menu_open()`, `render::combat_input_ready()`,
  `game::input_bindings().text_entry_active()` and `game_window_in_front()`.
- `g_walk_autofire`, `g_walk_autofire_parts` and `g_walk_autofire_spray` are atomics shared with the aim/trigger
  code.

## Conventions

- C++23, MSVC x64, static CRT; Windows-only.
- Match the surrounding style: 4 spaces, Allman braces, `#pragma once`, `#include <stdafx.hpp>` first in `.cpp` files
  (never in headers), lowercase snake_case functions and types (`walk_bot_t`), `m_` members, `k_` constants,
  `[[nodiscard]]` on getters and pure functions, `const auto` locals.
- Prefer `std::scoped_lock`, `std::atomic` with explicit memory orders, `std::clamp` and `std::isfinite`.
- Comments explain why and invariants, not what. Keep them; this codebase is intentionally comment-heavy.
- Do not add third-party dependencies without asking. ImGui is pinned to a verified commit; do not bump it casually.
- Process access is external and read-only. Vesta must stay `asInvoker` (no elevation, no UIAccess). The
  `pasta_loader` is the exception: it is a separate launcher and must request administrator rights.
- Config fields: add the field in `src/config/misc.hpp` (or `combat.hpp`/`visuals.hpp`), serialize and deserialize
  it in `src/config/settings.cpp` with clamps, then add the UI row in the matching `src/ui/menu/*` page. Keep the UI
  range and the JSON clamp in sync. New fields must survive missing keys and round-trip.
- Do not commit `build/`, PDBs, logs or `.env` files. Do not edit generated files under `build/`.
- There is no unit-test target yet. Verify with a compile and, when possible, a live run. State clearly when a
  behavior was only compiled, not exercised in-game.
- Work only in the repo. Do not rewrite user configs or system settings unless the task asks for it.

## Verification

- Incremental Debug build: the command above. A change to `src/stdafx.hpp` triggers a full PCH rebuild.
- If the game is running, the link fails with `LNK1168`; close it first.
- For runtime issues, capture `%TEMP%\vesta\logs`, the relevant live config, and the map/scenario.
- The agent cannot see the game or overlay. Ask for a screenshot or a short clip for visual issues.

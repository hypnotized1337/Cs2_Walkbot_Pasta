# PASTA

PASTA is a Windows x64 external assist for Counter-Strike 2, built from the
open-source [Vesta](https://github.com/Read1dno/vesta) project. It attaches to
the game as an ordinary user process, reads game memory read-only, and draws its
menu and visuals in a separate DirectX 11 overlay. There is no DLL injection, no
kernel driver, and no elevation.

This fork keeps Vesta's combat, visuals, radar and Walk Bot, removes the Lua
runtime and the grenade/movement modules, and turns the automation features into
a first-class part of the interface.

> Version 1.2.3 · Windows 10/11 x64 · C++23 · Visual Studio 2022 or newer
>
> Educational and research use only. See [License and credit](#license-and-credit).

## Features

### Combat

- Aimbot with always/hold/toggle activation, fixed, distance-scaled and
  target-attached FOV, configurable hitboxes and multipoints, prediction,
  smoothing and a humanizer.
- Visibility, smoke, flash, airborne, immunity, penetration and lethal checks.
- Independent recoil control that also works together with the active aim
  solution.
- Per-weapon-group overrides for every combat setting.
- Triggerbot with conventional and seed-based firing, pre-shot delay, post-shot
  lockout, timing randomization, hitbox groups, prediction, penetration, minimum
  damage and lethal logic.
- Auto Stop on the Triggerbot page: prepares weapon accuracy before a requested
  shot.

### Visuals and information

- Player ESP and chams: boxes, skeleton, head marker, view line, name, health,
  armor, weapon, ammo, distance, money, ping and information flags.
- Separate visible and occluded chams materials, on-hit poses, death effects and
  a drag-and-drop ESP editor.
- Off-screen arrows, sound ESP, spectator list, hitboxes, bloom and Legit Sync
  for visibility, radar, sound and smoke.
- World information: dropped weapons and items, projectile timers and paths,
  grenade radii, inferno bounds, planted-bomb marker, bomb timer/defuse panel and
  a calculated bomb safe zone.
- Bullet tracers, hitmarkers, hit sounds, damage popups, event log, reconstructed
  crosshair, watermark and keybind list.
- Radar with players, view direction, health, armor, weapons, money, kits,
  grenades, the bomb, dropped items, projectiles and inferno bounds.

### Walk Bot

The Walk Bot has its own top-level page instead of living inside a Misc tab.

- Routes over the map's navigation mesh, either to random points or along routes
  you recorded yourself. Jumps, crouches and ladders are kept in recordings.
- Per-map and per-side route storage under `%TEMP%\vesta\walkbot`.
- Hunt enemies, turn the camera, break vents and boards, and open doors on the
  way.
- Route display and navigation-mesh overlay with configurable colour and
  thickness.
- Its own combat layer: a player-like aim hand, autofire using the Triggerbot
  settings, spray mode, rear defense, reaction to unseen hits, and automatic
  scope handling.
- Auto buy with per-game-mode presets and an editable item list, plus auto
  reload.
- Owned input only: the menu, the player's mouse, the aimbot and the trigger
  always win, and every held key is released on disable, death, focus loss or
  reset.

### Automation

A dedicated Automation page, shown as a child of Walk Bot, collects the round
and match automation:

- **Match automation** — Auto Accept, Auto Rejoin (game mode and team) and Auto
  Disconnect (score, minutes on server, randomized extra time).
- **Buy automation** — default, Premier, Competitive, Wingman, Casual and
  Deathmatch presets, or a custom item list with buy codes, side, money rules and
  a "one random item" option.
- **Combat automation** — Auto Crouch, Auto Scope, Auto Bonus Weapon, Auto Knife,
  Auto Health Shot and Auto Reload.

Auto Crouch holds duck while any hitbox of the engaged enemy is still reachable.
It releases only when the target dies or is fully behind cover (with a short
release grace), instead of standing up mid-fight because the crouched angle
temporarily looks worse. Auto Health Shot waits for a quiet moment, draws the
syringe, lets the deploy animation finish, injects, then restores a firearm.

### Interface

- Six top-level pages: Aimbot, Triggerbot, Visuals, Walk Bot, Automation and
  Misc. The Automation row glides out of the Walk Bot section and retracts when
  another page is selected.
- Configs live under **Misc → Configs**, next to General and Overlay.
- English and Russian interface, DPI scaling, custom palette, movable panels and
  portable configs.
- Configurable menu and exit hotkeys, with movement keys released while the menu
  is open.

## How PASTA differs from Vesta

Vesta is the upstream project this fork is based on. Both are external,
read-only overlays with the same base combat and visual systems. The differences
are deliberate:

| Area | PASTA | Vesta |
| --- | --- | --- |
| Navigation | Six pages; Walk Bot and Automation are top-level workflows | Four pages; tools grouped into Misc tabs |
| Automation | Dedicated page for match, buy and combat automation | Auto Accept plus walk-bot buy/reload controls inside Misc |
| Walk Bot | Its own page with route, combat and recording controls | A tab inside Misc |
| Scripting | No scripting runtime | Sandboxed Lua 5.4 API, hot reload, scripts and Web Radar |
| Grenades | No grenade helper | Nade Helper lineups, grenade aim assist and trajectory preview |
| Movement | No Bunny Hop or Edge Jump (Auto Stop remains on Triggerbot) | Bunny Hop and Edge Jump in the Movement tab |
| Dependencies | ImGui, FreeType, meshoptimizer, LZ4, Zstandard, nlohmann/json | The same plus Lua 5.4 |
| Code size | About 16,000 lines removed with the dropped modules | Correspondingly larger surface |

### What this fork gains

1. **One place for automation.** Match, buy and combat automation are grouped on
   a single page instead of being scattered through Misc and the walker.
2. **A clearer Walk Bot workflow.** Route source, recording, navigation-mesh
   display and combat behaviour are on one page, with the Automation child
   sliding out only while the Walk Bot section is open.
3. **A crouch that stays down.** Auto Crouch now holds while any part of the
   target is reachable and only releases on death, target loss or full
   occlusion. The old rule that counted exposed hitboxes could stand the player
   up in the middle of a fight.
4. **A smaller trust surface.** There is no Lua sandbox, no third-party script
   execution, no browser radar and no local tunnel. Nothing outside the compiled
   binary can change settings or press game keys.
5. **Less code to keep working after a CS2 update.** Removing the scripting
   runtime and the grenade and movement modules drops dozens of files and one
   third-party dependency, which shortens the build and narrows the part of the
   code that touches game data.
6. **Configs where you expect them.** Profiles sit next to General and Overlay in
   Misc, so the sidebar stays short.

### What it gives up

These are real tradeoffs, not hidden wins:

- No Lua scripts or the Web Radar, so there is no script ecosystem and no
  browser radar.
- No grenade lineups, grenade aim assist or trajectory preview.
- No Bunny Hop or Edge Jump; Auto Stop is still available for the trigger.
- A fixed feature set is not an upgrade for someone who relies on any of the
  removed modules.

Like any external assist, PASTA offers no anti-cheat guarantee. Valve's
detection systems change independently of this project; use it at your own risk.

## Quick start

1. Start Counter-Strike 2.
2. Run `vesta.exe` (the binary keeps the upstream internal name).
3. Press `Insert` to open the menu. Press `End` to exit PASTA.

If those keys clash with something, change them in
`%TEMP%\vesta\configs\hotkeys.cfg` and restart. The menu and exit keys can also
be changed in the first-run guide or in **Misc → General**.

## Build

Requirements:

- Windows 10/11 x64.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake 3.20+ and Git.

Release build:

```powershell
cmake --preset release
cmake --build --preset release --parallel
```

The executable is written to `build/bin/vesta.exe`. CMake downloads the pinned
third-party dependencies during the first configure; the repository vendors
none of them. Dear ImGui is verified against its pinned commit at configure
time.

Local debug build on this machine (Visual Studio 18 preset, reuses
`build/debug`):

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --preset debug-local
"$CMAKE" --build --preset debug-local --target vesta -- -m
```

Use `-m`, not `/m`, in Git Bash: MSYS rewrites `/m` to `M:/`. Close `vesta.exe`
before linking, or the link fails with `LNK1168`.

## Data and configuration

PASTA keeps its runtime data outside the repository, in `%TEMP%\vesta`:

| Path | Contents |
| --- | --- |
| `configs/current.cfg` | Live settings cache, written by the running app |
| `configs/hotkeys.cfg` | Menu and exit keys |
| `logs/` | Logs and the crash journal |
| `cache/` | Parsed game and map data |
| `nav/` | Navigation-mesh caches |
| `walkbot/<map>/<side>/*.route` | Recorded Walk Bot routes |

Named configs live in the folder stored in
`HKCU\Software\Vesta\ConfigFolder` (by default `Documents\VESTA`). Loading a
named config copies it into the live cache; saving writes the live settings back
to the named file. The config format is unchanged from Vesta, so existing
profiles load, but settings for removed modules have no matching controls.

Edit the live cache only while `vesta.exe` is closed: the running process
rewrites it when settings change.

## Architecture

The process is split into workers started in `src/app/runtime_session.cpp`:

- **Game state** at 64 Hz, owning map and match transitions.
- **Movement** at 1 ms active / 4 ms idle, running Auto Stop and the Walk Bot.
- **Combat** at 250 Hz, running aim, ballistics and the trigger.
- **Seed trigger**, **render/overlay** and **diagnostics** workers.

The Walk Bot is the largest stateful subsystem. Its movement state is written
only by `walk_bot_t::tick()` on the movement worker; the render thread may draw
its route and cached navigation-mesh edges but never mutates movement state.
Route data shared with the render thread is copied under `m_route_mutex` before
drawing or planning. Configuration is immutable per snapshot: workers read a
published `shared_ptr<const runtime_snapshot>` instead of the live settings.

Input is owned and transition-based. Every held key is released on disable,
death, reset or focus loss, and the menu, player input, aimbot and trigger take
priority over automation.

## Verification

There is no unit-test target yet. Changes are verified with a compile and, when
possible, a live run; compiling alone does not exercise gameplay behaviour. For
runtime issues, include the contents of `%TEMP%\vesta\logs`, the active config
and the map/scenario. The overlay and the game are separate windows, so visual
problems need a screenshot or a short clip.

## License and credit

PASTA is a derivative of [Vesta](https://github.com/Read1dno/vesta) by Read1dno,
used under the Apache License 2.0. The original copyright and attribution are
retained in [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). Third-party components
and their licenses are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

The PASTA name and branding are local to this fork. Internal identifiers
(`vesta.exe`, `%TEMP%\vesta`, `HKCU\Software\Vesta`) are kept for compatibility
with existing configs and data directories.

This project is provided for educational and research purposes. You are
responsible for complying with the terms of service of any software you use it
with.

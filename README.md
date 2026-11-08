# PASTA

PASTA is a Windows x64 external assist for Counter-Strike 2, forked from the
open-source [Vesta](https://github.com/Read1dno/vesta) project. It reads game
memory read-only and draws in a separate DirectX 11 overlay: no injection, no
kernel driver, no elevation.

Comparison below is against Vesta `main` as of November 2026.

## What Vesta already provides

Vesta already ships the aimbot and RCS, the conventional and seed triggerbot,
Auto Stop, player ESP and chams, the radar, world/projectile/bomb visuals,
profiles, English/Russian localization and the base Walk Bot routing and combat
systems. PASTA keeps all of it. This page only covers what changed.

## Differences at a glance

| Area | Vesta | PASTA |
| --- | --- | --- |
| Sidebar | Aimbot, Triggerbot, Visuals, Misc | Adds Walk Bot and Automation as top-level pages |
| Automation | Auto Accept, rejoin/disconnect and buy/reload inside Misc | Dedicated page, plus new combat automation |
| Walk Bot | Tab inside Misc | Own page, with new combat controls |
| Scripting | Lua 5.4 sandbox, scripts and Web Radar | Removed entirely |
| Grenades | Nade Helper, aim assist, trajectory preview | Removed entirely |
| Movement | Bunny Hop and Edge Jump | Removed entirely |
| Dependencies | Seven third-party libraries | Six (Lua is gone) |
| Code size | Baseline | About 16,000 lines and 60 files smaller |

## Automation is now a first-class page

Upstream keeps match automation and the walker's buy/reload settings scattered
through Misc. PASTA gives them their own page and adds a combat automation layer
that upstream does not have.

### New combat automation

- **Auto Crouch** holds duck only while an aim or trigger engagement is active
  and the target is still reachable. It samples every hitbox from the camera and
  stays down while any of them is visible. The gate needs 100 ms of confirmed
  sight before ducking and 150 ms of confirmed occlusion before releasing, so a
  single frame cannot start a crouch/release cycle. It releases on target death,
  target loss or full cover, and fails open if collision data is unavailable
  rather than silently disabling itself. This is deliberately less clever than
  judging how much of the target the crouch exposes: the old approach stood the
  player up mid-fight whenever the lower angle looked worse.
- **Auto Health Shot** waits until health is below a threshold, no damage has
  been taken for a configurable window, no enemy is within a configurable
  distance, and combat is not aiming or firing. It then presses the draw bind,
  waits 600 ms for the deploy animation, holds primary attack until the syringe
  is consumed, and restores the primary or sidearm. Draw and restore attempts are
  bounded, failures go on a five-second cooldown, and an optional debug overlay
  shows the exact blocking reason.
- **Auto Knife** draws the knife inside 80–600 units, closes the distance and
  secondary-attacks. It can require the target to be stationary, respects a
  post-respawn grace period, and keeps the knife selected until the target dies
  or leaves its leash so the firearm cannot win back the slot mid-chase.
- **Auto Scope** taps secondary attack, verifies the scope is up, and after a
  kill switches to the knife to leave the scope and back to the primary for the
  next target.
- **Auto Bonus Weapon** taps the pickup key on a configurable interval, and
  pauses while the health shot owns the weapon slot.

### Automation that already existed upstream

Auto Accept, Auto Rejoin, Auto Disconnect, Auto Buy and Auto Reload are Vesta
features. PASTA surfaces them together and keeps the buy preset/item editor
(buy codes, side, money rules, one-random-item) on the same page rather than
inside the walker.

## Walk Bot moves out of Misc

Upstream's Walk Bot is a tab inside Misc. PASTA promotes it to its own sidebar
page and makes Automation a child of it, with the row animated in and out.

The routing engine is unchanged. The fork adds controls that upstream does not
expose there:

- **Rear Defense** turns onto an enemy behind the player and engages with its own
  distance, FOV, smoothing, hitbox and weapon settings.
- **Spray Mode** adds burst count and burst cooldown on top of the trigger-based
  autofire.
- **Auto Scope** and **Auto Knife** work through the walker's engagement state
  rather than as separate tools.
- **Route Display** exposes route colour/thickness and navigation-mesh drawing,
  with recordings kept per map and side under `%TEMP%\vesta\walkbot`.

Own Aim, Autofire and React To Hits remain the upstream walker features.

## Smaller surface

The fork removes three whole feature areas rather than hiding them:

- **Lua and Web Radar.** The runtime, sandbox, scripts page, API docs, bundled
  radar and its browser tunnel are gone. Nothing can load third-party code into
  the process, and Lua 5.4 is no longer a dependency.
- **Grenades.** Nade Helper, grenade aim assist and trajectory preview are gone.
  Radar projectile paths remain because the radar still uses them.
- **Movement.** Bunny Hop and Edge Jump are gone; Auto Stop stays on the
  Triggerbot page.

Together that removes 60 files and about 16,000 lines. The practical benefit is
not a benchmark: it is less code that can break after a CS2 update, one fewer
runtime dependency, and no script-trust boundary to reason about.

## What it gives up

- No Lua scripts or browser radar.
- No grenade lineups or trajectory preview.
- No Bunny Hop or Edge Jump.
- A fixed feature set is not an upgrade for anyone who relies on those.

Like any external assist, PASTA makes no anti-cheat guarantee. Valve's detection
systems change independently of this project; use it at your own risk.

## Build and run

Requirements: Windows 10/11 x64, Visual Studio 2022 with **Desktop development
with C++**, CMake 3.20+ and Git.

```powershell
cmake --preset release
cmake --build --preset release --parallel
```

The executable is written to `build/bin/vesta.exe`; the internal name is kept for
config and data compatibility. Start Counter-Strike 2, run the executable, press
`Insert` for the menu and `End` to exit. Change those keys in
`%TEMP%\vesta\configs\hotkeys.cfg` and restart if they clash. Debug builds use
the `debug` preset; close the running executable before linking.

## Data

- `%TEMP%\vesta\configs\current.cfg` — live settings cache.
- `%TEMP%\vesta\configs\hotkeys.cfg` — menu and exit keys.
- `%TEMP%\vesta\logs`, `cache`, `nav` — diagnostics, parsed data and nav meshes.
- `%TEMP%\vesta\walkbot\<map>\<side>\*.route` — recorded routes.
- Named configs: the folder in `HKCU\Software\Vesta\ConfigFolder` (default
  `Documents\VESTA`). **Misc → Configs** loads and saves them.

Edit `current.cfg` only while PASTA is closed; the running process rewrites it.

## Credit and license

PASTA is a derivative of [Vesta](https://github.com/Read1dno/vesta) by Read1dno,
under the Apache License 2.0. Copyright and attribution remain in
[`LICENSE`](LICENSE) and [`NOTICE`](NOTICE); third-party licenses are in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

The PASTA branding is local to this fork. Internal names (`vesta.exe`,
`%TEMP%\vesta`, `HKCU\Software\Vesta`) are kept for compatibility.

Provided for educational and research purposes. You are responsible for
complying with the terms of service of any software you use it with.

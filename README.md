# PASTA

PASTA is a Windows x64 external assist for Counter-Strike 2. It reads game memory read-only and draws a separate DirectX 11 overlay: no injection, no driver.

![PASTA menu hero](docs/images/hero.png)


## Features

| Area | What it covers |
| --- | --- |
| Aimbot | Target selection, FOV modes, humanizer, prediction, multi-point aiming, RCS, penetration, and per-weapon-group overrides. |
| Triggerbot | Hold, always-on, and toggle activation; seed modes; hitboxes; reaction time and hitchance; Auto Stop; penetration; predictive and lethal-only modes. |
| Visuals | Player ESP, box and bar styles, info flags, chams, skeleton and hitboxes, sound ESP, item and projectile ESP, bomb ESP, radar, tracers, hitmarkers, hit sounds, visual no-flash/no-smoke, and crosshair. |
| Interface | Astral-derived menu shell, icon navigation, accent-aware cards, shared HUD panels, and the config library. |
| Lua API | Sandboxed Lua 5.4 scripting, script packages, autoload and hot reload, per-script controls, and the verified Web Radar package. |
| Movement | Bunny Hop with a configurable activation key; requires CS2's jump action on a keyboard key. |
| Grenades | Lineup helper with 12 maps and 1,122 lineups, plus trajectory preview with bloom, bounce points, and an endpoint. |
| Walk Bot | Navigation-mesh routes and recordings, hunting, obstacle breaking, own aim, autofire, and the automation group. |
| Automation | Auto Accept, Auto Rejoin, and Auto Disconnect for match flow. |
| Misc | Game binds, language, DPI scale, interface colors, overlay widgets, performance limits, diagnostics, logs, movement, grenades, and Lua scripts. |
| Configs | Import, save, load, overwrite, rename, duplicate, and delete named configurations; choose the config folder. |
| Overlays | Watermark, active binds, spectator list, and bomb information/timer. |

Combat automation and Auto Buy live on the **Walk Bot** page. Their controls stay disabled while Walk Bot is off. Movement, grenades, and Lua scripts live in **Misc**.

## What PASTA adds

On top of Vesta's combat, visual, and walk-bot core:

- **Astral menu:** A rebuilt interface with top-level Aimbot, Triggerbot, Visuals, Walk Bot, Automation, Misc, and Configs pages, accent-aware cards, animated transitions, and Astral HUD panels.
- **Lua API and Web Radar:** A sandboxed Lua 5.4 runtime, script packages with autoload and hot reload, script-provided controls, and the restored verified Web Radar package.
- **Bunny Hop:** Movement assistance from Vesta, with a configurable activation key and a required keyboard jump bind.
- **Grenade lineup helper:** 12 maps and 1,122 lineups, stand and aim markers, throw details, and optional Mouse5 aim assist with automatic release.
- **Trajectory preview:** The held grenade's path, live grenade trails, bloom, bounce points, and the end point, with an optional local-only mode.
- **Walk Bot automation:** Spray Mode, Rear Defense, Auto Crouch, Auto Scope, Auto Bonus Weapon, Auto Knife, Auto Health Shot, Auto Buy, Auto Accept, Auto Rejoin, and Auto Disconnect.

## Screenshots

| | |
| --- | --- |
| **Aimbot**<br>[![Aimbot tab](docs/images/menu-aimbot.png)](docs/images/menu-aimbot.png) | **Triggerbot**<br>[![Triggerbot tab](docs/images/menu-triggerbot.png)](docs/images/menu-triggerbot.png) |
| **Visuals**<br>[![Visuals tab](docs/images/menu-visuals.png)](docs/images/menu-visuals.png) | **Walk Bot**<br>[![Walk Bot tab](docs/images/menu-walkbot.png)](docs/images/menu-walkbot.png) |
| **Automation**<br>[![Automation tab](docs/images/menu-automations.png)](docs/images/menu-automations.png) | **Misc**<br>[![Misc tab](docs/images/menu-misc.png)](docs/images/menu-misc.png) |
| **Configs**<br>[![Configs tab](docs/images/menu-configs.png)](docs/images/menu-configs.png) | **Overlays**<br>[![Watermark, active binds, spectator list, and bomb timer](docs/images/overlays.png)](docs/images/overlays.png) |

## Quick start

### Requirements

- Windows 10 or Windows 11, x64.
- Counter-Strike 2.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake 3.25+ and Git.

### Build

From the repository root:

```powershell
cmake --preset release
cmake --build --preset release --parallel
```

The main output is `build/bin/pasta.exe`. The build also produces `build/bin/pasta_loader.exe`.

### Run

Start Counter-Strike 2, then run `pasta.exe`. Press `Insert` to open or close the menu and `End` to exit PASTA.

The menu key and exit key are saved in `Documents\PASTA\configs\hotkeys.cfg` by default, or in the config folder selected on the Configs page.

### Loader

`pasta_loader.exe` installs and launches `pasta.exe`. It requests administrator rights, checks the latest public GitHub release, and installs a newer `pasta.exe` in `Documents\PASTA` when needed. It uses the Windows Documents folder, including OneDrive redirection. It starts Steam and Counter-Strike 2 with `-applaunch 730` if the game is not running. It does not inject a DLL or modify the game.

## Lua API

Open **Misc → Lua API** to manage scripts. PASTA scans `Documents\PASTA\scripts`; use **Open folder** to add a script or package, then **Refresh**.

- **Script controls:** Enable, autoload, and hot reload each script. A script can expose text, buttons, toggles, sliders, selects, inputs, colors, and keybinds in its tile.
- **Sandbox:** Scripts can read game state, draw overlays, handle events, and store per-script settings. They can press only game keys, and only while CS2 is focused; Windows, Alt, Escape, Enter, console, application, and browser keys are refused.
- **Verified Web Radar:** The bundled **Vesta Web Radar** is the trusted package. It can launch its helper process and use network access; every other script stays in the sandbox.
- **Examples:** The Lua API smoke test is `docs/lua/pasta_api_test.lua`. The bundled Web Radar source is `lua/scripts/Vesta Web Radar.lua`.

## Movement

Bunny Hop is under **Misc → Movement**. Enable it and bind an activation key. It requires CS2's jump action to be bound to a keyboard key; a mouse jump bind is rejected. It is off by default, pauses while Walk Bot owns movement, and stays out of a grenade lineup's crouch/run/jump sequence.

Edge Jump is not included.

## Grenades

Open **Misc → Grenades**.

- **Lineup Helper:** Draws map-specific lineups with stand and aim markers, throw type, and distance. It covers 12 maps and 1,122 lineups, is off by default, and works with the game's own grenade binds.
- **Lineup Aim Assist:** Uses Mouse5 by default. It can settle the aim on the selected lineup and release the throw automatically.
- **Trajectory Preview:** Shows the held grenade's path, live grenade trails, line and bloom styling, bounce points, and the end point. **Local Prediction Only** keeps the preview to the local simulation. It is off by default.

## Customization

- **Accent color:** Misc → Overlay → Interface Colors → Accent.
- **DPI scale:** Misc → Overlay → Interface → DPI Scale (50% to 150%).
- **Configs:** choose a folder on the Configs page, then save, load, import, overwrite, rename, duplicate, or delete `.cfg` files. Use **Open folder** to copy a config out; there is no separate Export button.
- **Live settings:** `Documents\PASTA\configs\current.cfg` by default. Edit it only while PASTA is closed.
- **Named configs:** `Documents\PASTA\configs` by default, or the folder selected on the Configs page. The choice is stored at `HKCU\Software\Vesta\ConfigFolder`.
- **Lua scripts:** `Documents\PASTA\scripts`, managed from Misc → Lua API.
- **Logs:** `Documents\PASTA\logs`.

## Notes and limitations

- PASTA is an external program. It reads game memory and draws an overlay; it does not inject or use a driver.
- Use is at your own risk. You are responsible for following the terms of the games and services you use it with. No feature or configuration guarantees protection from VAC, VAC Live, cooldowns, or bans.
- Auto Crouch acts only while an Aimbot or Triggerbot engagement is active.
- The automation controls on the Walk Bot page are disabled while Walk Bot is off.
- Edge Jump is not included.
- The bundled Web Radar is the only trusted Lua package and can start a helper process; other scripts remain sandboxed.
- Windows only.

## Project structure

```text
src/app/          startup, runtime session, workers, trace support
src/config/       settings, serialization, named-config library
src/features/     aimbot, visuals, walk bot, automation, chams, grenades
src/game/         process access, schemas, entities, input bindings
src/simulation/   ballistics, collision, nav mesh, penetration
src/scripting/    Lua runtime, sandbox, trusted Web Radar
src/ui/           Astral menu, legacy menu pages, HUD, overlay
lua/scripts/      bundled trusted Web Radar script
docs/lua/         Lua API smoke test
loader/           pasta_loader installer and launcher
docs/images/      README screenshots
```

## Credits

PASTA is a derivative of [Vesta](https://github.com/Read1dno/vesta) by Read1dno, under the Apache License 2.0.

The menu presentation is adapted from **Astral-Remake** by kdt255, via the [hypnotized1337/Astral-Remake](https://github.com/hypnotized1337/Astral-Remake) fork. Astral first-party code and assets are covered by the MIT + Commons Clause license in [`third_party/astral/LICENSE`](third_party/astral/LICENSE); see [`third_party/astral/THIRD_PARTY_NOTICES.md`](third_party/astral/THIRD_PARTY_NOTICES.md).

Third-party components include Dear ImGui, nlohmann/json, xorstr, FreeType, meshoptimizer, LZ4, Zstandard, ValveResourceFormat-derived KV3 parsing, cs2-dumper-derived schema traversal, Kenney interface sounds, and Inter. Full licenses and links are in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

PASTA is licensed under the Apache License 2.0. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

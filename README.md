# PASTA

PASTA is a Windows x64 external assist for Counter-Strike 2, forked from
[Vesta](https://github.com/Read1dno/vesta). It reads game memory read-only and
draws in a separate DirectX 11 overlay: no injection, no driver, no elevation.

Vesta already ships the aimbot/RCS, triggerbot and Auto Stop, ESP/chams, radar
and world visuals, Walk Bot routing, configs and localization. PASTA keeps all
of that; only the differences are below. Compared with Vesta `main`, November
2026.

## Differences

| Area | Vesta | PASTA |
| --- | --- | --- |
| Sidebar | Aimbot, Triggerbot, Visuals, Misc | Adds Walk Bot and Automation |
| Automation | Auto Accept, rejoin/disconnect and buy/reload inside Misc | Dedicated page plus new combat automation |
| Walk Bot | Misc tab | Own page with new combat controls |
| Scripting | Lua 5.4 and Web Radar | Removed |
| Grenades | Nade Helper, aim assist, trajectory preview | Removed |
| Movement | Bunny Hop and Edge Jump | Removed |
| Dependencies | Seven third-party libraries | Six; Lua is gone |
| Code size | Baseline | 60 files and about 16,000 lines smaller |

## New automation

Upstream's automation is spread through Misc. PASTA puts it on one page and adds
combat tools Vesta does not have:

- **Auto Crouch** holds duck while an aim or trigger engagement has any reachable
  hitbox. It needs 100 ms of confirmed sight to crouch and 150 ms of confirmed
  occlusion to stand, so one frame cannot toggle it. It releases on death, target
  loss or full cover, and fails open without collision data rather than disabling
  itself. Holding through a tight angle is deliberate: standing up mid-fight
  looks worse than crouching through it.
- **Auto Health Shot** requires low health, a quiet damage window, no nearby
  enemy and no active aim/trigger/autofire. It draws the syringe, waits 600 ms
  for the deploy, holds attack until it is consumed, then restores a firearm.
  Retries are bounded and failures cool down for five seconds.
- **Auto Knife** draws the knife within 80–600 units, closes distance and
  secondary-attacks. It can require a stationary target, respects a post-respawn
  grace period, and holds the knife until the kill or leash ends.
- **Auto Scope** scopes in, exits to the knife after a kill, then redraws the
  primary for the next target.
- **Auto Bonus Weapon** taps the pickup key on an interval and yields while the
  health shot owns the slot.

Auto Accept, Rejoin, Disconnect, Buy and Reload already exist in Vesta; PASTA
groups them here and keeps the buy preset/item editor (codes, side, money rules,
one random item).

## Walk Bot page

Vesta keeps Walk Bot in a Misc tab. PASTA gives it a sidebar page and nests
Automation under it with an animated row.

Routing is unchanged. The page adds Rear Defense (turn and engage an enemy
behind with separate distance/FOV/hitbox/weapon settings), Spray Mode (burst
count and cooldown), and Auto Scope/Auto Knife integration. It also exposes route
and nav-mesh drawing plus per-map, per-side recordings. Own Aim, Autofire and
React To Hits are upstream features.

## Removed

The Lua runtime, sandbox, scripts page, API docs, Web Radar and its browser
tunnel are gone, along with the Lua dependency. Nade Helper, grenade aim assist
and trajectory preview are gone; radar projectile paths remain. Bunny Hop and
Edge Jump are gone; Auto Stop stays on Triggerbot.

That removes 60 files and roughly 16,000 lines: less code to repair after a CS2
update and no script-trust boundary. The cost is no Lua/Web Radar, grenade
lineups or Bunny Hop/Edge Jump, which makes this a bad fit if you use them.

PASTA makes no anti-cheat guarantee. Use it at your own risk.

## Build and run

Requires Windows 10/11 x64, Visual Studio 2022 with **Desktop development with
C++**, CMake 3.20+ and Git.

```powershell
cmake --preset release
cmake --build --preset release --parallel
```

Output is `build/bin/vesta.exe`; the internal name is kept for config and data
compatibility. Start CS2, run the executable, press `Insert` for the menu and
`End` to exit. Change those keys in `%TEMP%\vesta\configs\hotkeys.cfg` and
restart if needed.

## Data and license

- `%TEMP%\vesta` — configs, logs, cache, nav meshes and recorded routes.
- `%TEMP%\vesta\configs\current.cfg` — live settings; edit only while closed.
- `HKCU\Software\Vesta\ConfigFolder` — named configs (`Documents\VESTA` by
  default), managed from **Misc → Configs**.

PASTA is a derivative of [Vesta](https://github.com/Read1dno/vesta) by Read1dno,
under the Apache License 2.0. Copyright and attribution remain in
[`LICENSE`](LICENSE) and [`NOTICE`](NOTICE); third-party licenses are in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). The PASTA branding is local
to this fork; internal `vesta` names are kept for compatibility.

Provided for educational and research purposes.

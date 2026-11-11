# Proposed Astral transplant layout — approval required

This is a code/asset transplant of Astral's title bar, horizontal tabs, panel layout, theme,
fonts, icons, widgets and animations. It is not a new visual design and not a recolor of
Vesta's existing menu. Keep Vesta's renderer/input entry points and serialized settings.

## Top-level tabs and pages

- **Aimbot:** Global / Pistol / SMG / Rifle / Shotgun / Sniper / Heavy. Keep targeting,
  FOV/humanizer, checks, multipoint, prediction, RCS and penetration in Astral panels.
- **Triggerbot:** same weapon-group navigation; activation, checks, seed/timing,
  hitboxes, auto-stop, penetration and debug controls. Add real saved-but-unexposed
  trigger settings only as itemized in the inventory and approved decisions.
- **Visuals:** Players / Items / Projectiles / Bomb / Radar / Effects / Crosshair.
  Preserve nested player bar, name, weapon/ammo, flags, material and layout editors.
  Use Astral popups for nested settings rather than deleting advanced options.
- **Walkbot:** route/navigation and route display, combat/defense, combat automation,
  and auto buy all on this tab. Keep disabled dependencies and the required tooltip.
  Dynamic auto-buy preset/item rows remain editable, not replaced by static toggles.
- **Automation:** match acceptance, rejoin (mode/team), disconnect thresholds/randomization.
- **Misc:** General (game binds/support/guide/unload) / Interface & Overlays
  (language, live DPI, palette/accent, performance and all overlay options).
- **Configs:** dedicated tab retaining every existing library operation and list state.
  Preserve the duplicate Misc/Configs navigation route as an alias rather than removing
  a current navigation control. Explicit export behavior is listed for owner decision.

The first-run guide and existing overlays remain available. The `PASTA(retard)` branding
must be retained in the transplanted window chrome (currently drawn as two text runs).
No Astral-only cheat/script/skin functions gain fake controls. See ASTRAL_AUDIT.md and
FEATURE_MAPPING.csv for what is reusable presentation and what is reference-only content.

## First visible vertical slice

**Automation** is the first completed page: a small page with real existing bindings,
master toggles, settings popups, combo options, integer sliders and conditional rows.
It can be completely compared against its inventory without hiding unfinished controls.

The slice also ports the Astral window/title bar, horizontal navigation, palette binding,
fonts/icons, spacing, animations and the full required widget API. Widget types unused
by Automation will be implemented as reusable widgets, not exposed as fake test controls.
Until later pages are fully ported, retain access to their existing functional pages in
the isolated test build; those temporary legacy pages are explicitly NOT claimed as done.

After the owner compiles and views this slice, port page groups with their own checkpoints:
1. Aimbot + Triggerbot; all global/per-class controls and debug settings.
2. Walkbot + auto-buy and dependencies.
3. Visuals Players plus the full editor/popups/materials.
4. Remaining Visuals pages and effects.
5. Misc, overlays, Configs and guide; then full inventory/reverse-binding check.

No page is complete merely because its container exists. Every control binding is checked
against controls.csv and config_keys.csv before a page checkpoint is reported complete.

## Integration constraints

- Retain the current ImGui pin and Win32/DX11 integration; adapt reference internal APIs.
- User-selected accent must drive every interaction state, including Configs and animations.
- Menu and watermark/spectator/binds DPI must update live with sharp text.
- No gameplay feature implementation or config-key renaming/removal in this UI transplant.
- Unknown config keys still ignored; copy-only round-trip verification is performed by owner.
- Stage 2 starts only after the owner accepts Stage 1 and resolves blockers below.

# UI remake progress

## Stage 2 approved — Automation vertical slice (2026-11-12)

- Astral is a C++17 standalone Win32/DX11 Dear ImGui 1.92.9b showcase; Vesta uses C++23,
  its own Win32/DX11 overlay lifecycle, and pinned ImGui 1.92.3. Adapt presentation APIs,
  not the host/backend or config serialization; retain the pinned library.
- Structure: separate theme/drawing, Inter SemiBold/Bold fonts, icon atlas, window chrome,
  horizontal tabs, panel/column layout and animated widgets, adapted to repository naming.
- Widget set: toggles, integer/float sliders, single/multi-select, color picker, buttons,
  text inputs, key capture, settings popups, lists, tooltips and disabled states.
- Theme: graphite panels, accent-aware highlights/gradients and smooth hover/page/popup
  animations. Existing user accent and live DPI remain authoritative.
- Tabs: Aimbot, Triggerbot, Visuals, Walkbot, Automation, Misc, Configs; existing subtabs
  and config binding semantics remain. No reference-only features or invented backend APIs.
- Slice: Automation's 8 existing controls (accept, rejoin with mode/team, disconnect with
  score/minutes/randomization), with every top-level tab visible. No other page is ported yet.
- Fallback: a single compile-time CMake switch selects Astral or the unchanged legacy menu;
  retain access to functional legacy pages while migrating. Preserve Walkbot disabled rules,
  overlay input/DPI behavior and Configs isolation/accent behavior.
- Owner approved all 33 additions for their later page checkpoints, bound to existing keys.
  Three saved options still lack runtime consumers; UI approval does not add gameplay logic.
- Keep legacy/internal keys unchanged and list them in `hidden_keys.md`; omit/reference-list
  showcase-only functionality in `astral_only.md`. Carry license and asset notices with port.
- Commit this slice and stop for owner Windows compile/screenshots before porting other pages.

## Stage 2 implementation checkpoint — stop for owner compile

The Astral shell and Automation slice are implemented. All seven top-level icon tabs are
visible (localized tooltips); Automation opens first. Other pages still use their original
widgets and bindings inside the new shell. No other page has been migrated and none of the
33 later-page additions is claimed as implemented here.

Automation uses the existing eight values: Auto Accept Match, Auto Rejoin, Game Mode, Team,
Auto Disconnect, Score (0–2000), Minutes On Server (0–60), Random Extra Time (0–100%).
Rejoin and disconnect settings open from their gear buttons. The shell adds the reference
header, ribbons/stars, Inter typography, icon tabs, cards, footer and widget transitions.
Astral's standalone host, persistence, search, arbitrary binds and showcase overlays are not
wired in. Existing guide, visual editor, HUD, input ownership and config paths remain.
Fonts/icons join the existing renderer lifecycle; the new menu and its popups participate
in the existing menu draw-list transform. The legacy menu is retained behind one switch.

### Build / fallback

Reconfigure the isolated test checkout before building (new source files were added):

```text
cmake --preset debug-local -DVESTA_ASTRAL_MENU=ON
cmake --build --preset debug-local --target vesta -- -m
```

Use the existing Visual Studio CMake path from `AGENTS.md` if `cmake` is not on PATH.
For the unchanged legacy menu, reconfigure with `-DVESTA_ASTRAL_MENU=OFF` and rebuild.
Use only the test executable and `%TEMP%\vesta_uitest`; do not replace the production binary.

### Verification and required owner feedback

- All six new translation units pass portable C++23 syntax checks against the pinned
  ImGui 1.92.3 headers. Windows-only host interfaces are stubbed for this check, not compiled.
- A temporary headless harness using the actual pinned ImGui implementation exercises
  shell rendering, all seven tab selections, all three toggles, both settings popups,
  both combo selections and all three sliders against the Automation value bindings.
  This is not a Windows renderer, persistence round-trip or in-game test.
- The hidden-key cross-check finds exactly 70 remaining keys after accounting for all
  33 approved proposals (54 serialized leaves); the copied Astral license matches the reference.
- Native MSVC builds (both switch settings), Windows appearance, input/DPI behavior,
  config save/reload, device recovery and live overlays remain unverified here.

Owner check: compile ON, send Automation and both settings-popup screenshots; check tab
visibility, header drag, live scale/accent changes, Configs save/load in the test root,
Walkbot's disabled automation rows, guide/editor/overlays, and OFF fallback compilation.
Stop at this checkpoint; do not port another page until compile and visual feedback arrives.

### Exact changed / added files

- Build / notices: `CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`.
- Host integration: `src/ui/menu/menu.hpp`, `src/ui/menu/menu.cpp`,
  `src/ui/menu/internal.hpp`, `src/ui/menu/layout.cpp`, `src/ui/overlay/overlay.cpp`.
- New shell / page: `src/ui/astral/menu.cpp`, `src/ui/astral/automation.cpp`.
- New widget layer: `src/ui/astral/widgets.hpp`, `src/ui/astral/widgets.cpp`,
  `src/ui/astral/draw.hpp`, `src/ui/astral/draw.cpp`, `src/ui/astral/theme.hpp`,
  `src/ui/astral/types.hpp`.
- New assets: `src/ui/astral/fonts.hpp`, `src/ui/astral/fonts.cpp`,
  `src/ui/astral/fonts/inter.hpp`, `src/ui/astral/icons.hpp`, `src/ui/astral/icons.cpp`.
- Documentation: `docs/ui_remake/PROGRESS.md`, `docs/ui_remake/NEEDS_DECISION.md`,
  `docs/ui_remake/hidden_keys.md`, `docs/ui_remake/astral_only.md`.
- Licenses: `third_party/astral/LICENSE`, `third_party/astral/THIRD_PARTY_NOTICES.md`,
  `third_party/inter/OFL.txt`.

## Stage 1 checkpoint (historical; superseded by Stage 2 approval above)

Test branch: `ui-remake-test`. Base revision: `83b616b`.
Stage 0 is accepted; Stage 1 inventory and reference audit are complete for review.
**Stage 2 has not started.** No skin, widget, page or gameplay implementation was changed
in Stage 1. The original checkout remains clean.

The owner reported a successful Windows compile and creation of `vesta_uitest` without
changing `vesta` on 2026-11-11. That is owner-reported Stage 0 verification, not a local
build or proof of a future UI. Storage isolation remains as documented in `ISOLATION.md`.

## Review files

- `controls.csv`: **768 rows = 735 existing + 33 proposed**.
  Proposed rows are labeled `new control (no old UI)`; 3 are blocked inactive options,
  30 are proposals for already-consumed settings. Nothing proposed is implemented yet.
- `config_keys.csv`: **982 serialized JSON leaf/template paths**: 106 global combat,
  79 combat override, 565 ESP, 232 Misc. **858** have old-menu bindings; **124** do not.
- `external_settings.csv`: **2** separately persisted menu/exit hotkeys.
- `schema/no_menu_controls.md`, `schema_notes.md`, `parts/*_unexposed.*`: complete
  unexposed-leaf list, current/legacy schema distinctions and runtime classifications.
- `other_surfaces.md`: HUDs, guide, notices, popups, input, native chooser and non-JSON state.
- `ASTRAL_AUDIT.md`, `FEATURE_MAPPING.csv` (**39 feature-family rows**),
  `feature_mapping.md`: reference compatibility, exclusions and old-to-proposed placement.
- `PROPOSED_LAYOUT.md`, `NEEDS_DECISION.md`: transplant plan and explicit behavior gaps.
- `README.md`: counting conventions and regeneration instructions.

### Existing control rows by page

| Page | Existing rows |
|---|---:|
| Aimbot | 141 |
| Automation | 8 |
| Configs | 15 |
| Guide | 8 |
| Misc/General | 17 |
| Misc/Overlay | 37 |
| Navigation | 11 |
| Triggerbot | 52 |
| Visuals/Bomb | 16 |
| Visuals/Crosshair | 16 |
| Visuals/Effects | 48 |
| Visuals/Items | 11 |
| Visuals/Players | 242 |
| Visuals/Projectiles | 16 |
| Visuals/Radar | 29 |
| Walkbot | 68 |
| **Total** | **735** |

Visuals totals **378** existing rows; Misc General/Overlay totals **54**. Counts include
navigation, popup entries and shared/dynamic templates as explained in `README.md`; they
are not independent feature counts or a count of every runtime-generated instance.
`source_calls.csv` contains **484 syntactic call sites**, not 484 controls.

## Proposed presentation and first slice

Transplant Astral's window/title bar, horizontal tabs, panel geometry, widgets, Inter
SemiBold/Bold fonts, icons and animations—not just its colors. Keep Vesta's existing
renderer/input lifecycle, pinned ImGui, config keys, accent/DPI behavior, debug defaults,
`PASTA(retard)` branding and all functional controls/surfaces.

Top-level tabs: **Aimbot, Triggerbot, Visuals, Walkbot, Automation, Misc, Configs**.
First vertical slice: **Automation**, plus the reusable Astral chrome/widget layer.
Keep the unported legacy pages reachable until their replacements pass their checkpoints.
Reference-only Skins/Scripts and showcase behavior do not become fake product features.

## Stage 1 decision record (superseded where approved above)

1. Implement spectator avatars and the two inactive Auto Stop options in a separately
   approved behavior task, or retain their keys without misleading controls?
2. When Walkbot is disabled, preserve stored automation preferences (current behavior)
   or clear them? No forced-clearing change is authorized yet.
3. Add explicit Configs Export? Import/save/folder operations exist; export does not.
4. Accept the documented compatibility/derived/inactive-field policy and review the
   proposed numeric ranges before affected pages are ported. Keep all saved keys.
5. Preserve reference notices/credit and review Commons Clause restrictions before
   distributing a derived product.

## Stage 1 verification

- Schema extraction and inventory assembly pass: no unmatched saved control bindings,
  duplicate exact control rows or duplicate saved key paths; existing source references
  and their line ranges validate. External hotkey persistence is checked separately.
- Scanner, extractor and assembler run twice with identical output hashes for all three
  generated CSVs. `git diff --check` passes.
- Source-backed reviews cover page bindings, popup helpers, runtime consumers for the
  unexposed setting families, reference APIs and license notices.
- These are static inventory checks, **not** proof that every future feature works.
  No Windows build, new-menu screenshot, interaction or config round trip ran here.
  Stage 1 only changes documentation and inventory scripts; no new compile is needed.

## Stage 1 handoff (historical)

Inventory groups are committed locally on `ui-remake-test`. Only the previously authorized
isolation commit was pushed; this documentation work has not been pushed or merged.
No original checkout or production branch changes were made.

**Next action: owner review/approval of Stage 1 and the layout/slice, with the decisions
above resolved or explicitly deferred. Stop here until approval.** Then provide the exact
source-file list at each implementation checkpoint and wait for the owner's Windows compile
feedback. Do not run the production loader or replace the working executable.

## Shared-widget restyle — Step 1

The Astral-enabled menu now applies the fork's shared presentation to legacy cards,
section headers, tabs, sliders, toggles, selectors, inputs, keybinds, buttons, color
previews and popups. Config-library controls use the same live palette. Existing
bindings and input paths remain; the compile-time legacy fallback remains available.
The Walkbot disabled section and its tooltip are unchanged.

Only Automation has the new page structure. Aimbot, Triggerbot, Visuals, Walkbot,
Misc and Configs retain their layouts; the Guide and Visual Editor still contain
specialized drawing. See `raw_widget_audit.md` for the remaining custom surfaces.

`astral_mapping.csv` audits 37 Legit controls: 4 bound, 15 with no matching feature,
and 18 requiring an owner decision. This is a mapping audit, not a port. No Legit
page implementation or unit/range/scale conversion is authorized by this checkpoint.

Portable ImGui checks passed for shared cards and integer/float sliders, row bounds,
disabled input, slider numeric entry, live accent changes and scoped-style restoration.
The extracted library-widget implementation also passed syntax checks with Astral on
and off. These checks use temporary platform stubs and do not replace a Windows/MSVC build.
Native screenshots and live overlay interaction remain unverified in this Linux
workspace. Stop for owner compile/visual feedback before another page group.

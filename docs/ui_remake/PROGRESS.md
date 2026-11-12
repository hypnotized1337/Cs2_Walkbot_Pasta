# UI remake progress — Stage 1 ready for approval

## Checkpoint

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

## Decisions still required

1. Implement spectator avatars and the two inactive Auto Stop options in a separately
   approved behavior task, or retain their keys without misleading controls?
2. When Walkbot is disabled, preserve stored automation preferences (current behavior)
   or clear them? No forced-clearing change is authorized yet.
3. Add explicit Configs Export? Import/save/folder operations exist; export does not.
4. Accept the documented compatibility/derived/inactive-field policy and review the
   proposed numeric ranges before affected pages are ported. Keep all saved keys.
5. Preserve reference notices/credit and review Commons Clause restrictions before
   distributing a derived product.

## Verification

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

## Handoff and publication

Inventory groups are committed locally on `ui-remake-test`. Only the previously authorized
isolation commit was pushed; this documentation work has not been pushed or merged.
No original checkout or production branch changes were made.

**Next action: owner review/approval of Stage 1 and the layout/slice, with the decisions
above resolved or explicitly deferred. Stop here until approval.** Then provide the exact
source-file list at each implementation checkpoint and wait for the owner's Windows compile
feedback. Do not run the production loader or replace the working executable.

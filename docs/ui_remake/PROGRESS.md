# UI remake progress

## Current checkpoint

Test branch: `ui-remake-test`. Base revision: `83b616b`.
The owner reported a successful Windows compile on 2026-11-11, with `vesta_uitest` created
and the original `vesta` untouched. This is owner-reported verification; no local Windows build ran.
Stage 0 is accepted. Stage 1 inventory is in progress; no skin/widgets/pages have been ported.
Commit message for this checkpoint: `isolate test config path`.
See [ISOLATION.md](ISOLATION.md) for all changed source files/lines, exact paths and manual checks.

## Done

- Separate test worktree and local reference clone; original checkout source is unchanged.
- Runtime/migration/log roots changed to `%TEMP%\vesta_uitest`.
- Named-config registry preference isolated, with a default test-only named-config folder.
- Named-config operations and explicit diagnostic paths fenced to the test data tree.
- Resolver rejects a test-root junction/symlink redirected elsewhere, including for default logs.
- Production trace environment variables no longer influence test tracing.
- Diagnostic report saves to test logs; displayed paths/comments updated.
- Static source checks completed; owner subsequently confirmed Windows compile and isolation.
- Added repeatable UI call-site scanner and `source_calls.csv` (466 syntactic sites, not control count).
- Proposed Astral tab layout/Automation slice and initial decision list written; final inventory in progress.

## Owner action now

Compilation checkpoint passed per owner report. Next action: review the completed Stage 1
inventory, decision list and proposed layout, then approve the Astral vertical slice.
Copy your old config to `%TEMP%\vesta_uitest\configs\current.cfg` with the app closed,
or place a named copy in `%TEMP%\vesta_uitest\configs\named`.
Do not run the separate production loader. Do not replace your working executable.

## Next

1. Finish Stage 1 as files, one page/settings group at a time with commits:
   `controls.csv`, `config_keys.csv`, `other_surfaces.md`, reference compatibility/mapping,
   proposed page layout, and the explicit needs-decision list.
2. Add repeatable extraction and completeness checking; no zero-missing claim until validated.
3. Obtain the owner's approval of the full inventory, decisions and page layout before Stage 2.
4. Port skin, then widgets, then complete pages, with a compile checkpoint after each stage.
5. Owner performs visual/in-game checks and copied-config round trip.

## Inventory row counts

No CSV files have been written yet: 0 recorded rows for each of Aimbot, Triggerbot, Visuals,
Walkbot, Automation, Misc and Configs; 0 recorded saved-key rows. These are pending counts,
NOT a count of actual product controls. The earlier chat summaries are incomplete source notes,
not the required final inventory.

## Decisions already made

- Continue without local Windows build; do not install/emulate MSVC.
- Work only in the test worktree. The owner authorized pushing the isolation commit to
  `origin/ui-remake-test`; it is published. Never push or merge into the production branch.
- Keep existing config keys and gameplay behavior; port visual layer only after Stage 1 approval.
- Add controls in the NEW menu for saved user-facing values missing old UI; label inventory rows
  `new control (no old UI)`. Confirmed requested examples: item ammo, shotgun/heavy filters,
  projectile default color, spectator avatars, auto-stop options.
- Do not expose internal/legacy settings without the owner's decision.
- Preserve the required debug defaults/labels, accent/DPI behavior and Walkbot dependency rules.

## Needs owner's decision

No additional decision is needed for storage isolation. The complete internal/legacy-key list is
pending the key-by-key audit; do not treat earlier no-control summaries as definitive classifications.
Existing code appears to retain Walkbot automation values when disabled rather than clearing them;
resolve the exact behavior against the user's forced-off requirement during Stage 1.
Configs export was requested but no explicit existing export widget was found in the preliminary audit;
record the actual behavior and proposed control in Stage 1 before page work.

## Verification limits

The reference uses different ImGui internals/version; keep Vesta's pinned version unless approved.
No baseline build ran in this Linux sandbox. The owner confirmed the Windows build and root
isolation. No new-menu visual/gameplay/round-trip check has run.
The owner explicitly replaced that build gate with per-stage Windows compilation on their PC.

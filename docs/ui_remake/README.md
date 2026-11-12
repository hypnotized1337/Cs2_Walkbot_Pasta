# Stage 1 review pack

This folder inventories the existing menu and saved settings before any Astral UI port.
Everything is confined to the `ui-remake-test` worktree. No Stage 2 implementation is included.

## Read first

- `PROGRESS.md`: checkpoint, final counts, verification and handoff.
- `controls.csv`: reviewed existing controls plus explicitly marked proposed additions.
- `config_keys.csv`: serialized JSON leaf paths, declared defaults, and old-menu crosswalk.
- `external_settings.csv`: menu/exit hotkeys saved separately in `hotkeys.cfg`.
- `parts/*_notes.md`: conditional controls, dynamic rows, aliases and page-specific caveats.
- `schema/no_menu_controls.md`: complete list of saved leaves without an old-menu binding.
- `other_surfaces.md`: windows/overlays outside the main pages.
- `PROPOSED_LAYOUT.md` and `NEEDS_DECISION.md`: proposed transplant and approval blockers.

## What a row means

A control row is a reviewed UI entry, not necessarily one clickable primitive. Some widgets
combine a toggle, color swatch and popup; other rows expand shared popup controls or color-picker
interactions. Shared controls instantiated on Global/Override or editor surfaces can appear more
than once. Weapon override and buy-item arrays use templates instead of multiplying rows by the
number of runtime instances. Counts are for inventory comparison, not feature totals.

`config_key` uses actual serialized JSON paths, not necessarily C++ member names. Semicolons
separate multiple bindings. `@ui.*` means transient UI state and `@action.*` means an operation,
not a saved setting; `@state.*` describes other state such as the registry-backed folder.
The two `hotkeys.*` bindings are separately persisted and listed in `external_settings.csv`.
Source locations refer to the existing UI; proposed rows are marked
`new control (no old UI)` and intentionally have no old-menu location.

Saved-key rows expand JSON objects to leaves, including RGBA channels. `[*]` represents array
elements. A color control covers its channel leaves; a compound editor may cover multiple keys.
`has_menu_control` describes the OLD menu only: proposed rows never make it `yes`. A `no` does
not by itself mean a missing feature: it can be migration metadata, derived layout state, a
compatibility field, an unused setting, or a user-facing value needing a new control.

Defaults are source-declared defaults, not a captured user profile or a verified startup config.
The embedded `configs/legit.cfg` is absent from this checkout. UI ranges can differ from load-time
clamps; page notes call out known differences. Do not change either during a presentation port.

## Rebuild the inventories

From the test worktree root, using Python 3:

```sh
python3 docs/ui_remake/scripts/scan_ui_calls.py
python3 docs/ui_remake/scripts/extract_config_keys.py
python3 docs/ui_remake/scripts/assemble_inventory.py
git diff --check
```

The scanner is a syntactic review aid, not a C++ semantic parser or a control total. The schema
extractor follows the current serializers; the assembler applies the reviewed control fragments,
checks source files/line ranges, duplicate rows/keys and unmatched saved bindings, and writes the
final crosswalk and no-control list. Rebuild in that order: extraction alone is not the final
reviewed crosswalk. Source ranges prove location validity, not semantic correctness; the page
notes document manual binding review.

These are static documentation checks, not Windows compilation, UI interaction, visual proof or
config round-trip proof. Later stages require the owner's Windows compile/run at each checkpoint.

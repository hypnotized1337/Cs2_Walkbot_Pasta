# Decisions required before porting affected controls

**Stage 2 update (2026-11-12):** The owner approved all 33 proposed UI controls for their
later page checkpoints, including the three dormant settings. This does not authorize new
runtime behavior. Keep the existing Walkbot-disabled behavior and all serialized keys.
The exact remaining internal/legacy paths are listed in `hidden_keys.md`; Astral-only
showcase exclusions are in `astral_only.md`. Notices now accompany the Automation slice.
The sections below preserve the Stage 1 rationale, not an outstanding blocker on this slice.

This file distinguishes saved fields that can receive a working UI control from fields
whose runtime behavior is missing, derived or legacy. It is not permission to omit keys.
All saved keys remain serialized and keep their existing names.

## 1. Requested avatar and auto-stop options currently have no consumer

These fields are declared, saved and loaded, but a repository-wide identifier search
finds no runtime/UI reader outside their struct and serializer:

- `misc.m_spectator_list.show_avatars` — `src/config/misc.hpp:101`,
  `src/config/settings.cpp:1654,1667`.
- `misc.m_auto_stop.physical_fire` and `misc.m_auto_stop.predictive_trigger` —
  `src/config/misc.hpp:256-257`, `src/config/settings.cpp:1987-1997`.

Adding toggles would save values but not change behavior. That contradicts the instruction
not to add dead controls. **Decision:** authorize a separate behavior implementation task,
or explicitly keep these persisted inactive settings without menu controls. Until decided,
record them as proposed `new control (no old UI)` with a blocked condition, not working features.

## 2. Walkbot disabled-state semantics

`src/ui/menu/walk_bot_page.cpp:117-128` explicitly preserves stored automation switch values
while disabling/greying the controls; its hover tooltip is the requested text at line 221.
The original requirement also asks that these settings be forced off. **Decision:** retain
stored preferences while effective automation is off, or clear the saved toggles on disabling
Walkbot? The latter changes existing config behavior and must be explicit. No change made yet.

## 3. Explicit config export

The old Configs page/library exposes load, save/create, overwrite, rename, duplicate,
delete, import, folder selection/opening and list interaction, with automatic refresh. No explicit
export action was found. **Decision:** approve an Export operation that copies a selected
config to a user-chosen destination within the isolated test data tree, without changing
its contents. Keep existing save/import behavior. No placeholder export button will be added.

## 4. Legacy/derived/internal saved keys

The complete unexposed leaf list is `schema/no_menu_controls.md`. Classification and
source evidence are in `schema_notes.md` and `parts/{combat,visuals,misc}_unexposed.md`. Legacy aliases must not become misleading duplicate controls. The recommended
policy is to retain serialized compatibility fields, expose the canonical active setting,
and document derived/position/version values instead of treating them as ordinary toggles.
Approval of that policy does not authorize dropping keys or undocumented omissions.

Concrete cases needing that policy:
- Keep combat wall/prediction/RCS/Auto Stop compatibility values and duplicate flash aliases
  hidden; expose only their canonical active controls. FOV migration/derived fields stay hidden.
- Preserve HUD layout/version/legacy-position fields and watermark orientation through existing
  drag, hover-wheel, edge-orientation and migration behavior, not duplicate numeric controls.
- Additional inactive visual settings: chams glow layers, item-name color, player hitbox group,
  offscreen-arrow thickness, and threat-module maximum distance. Retain keys without dead
  controls unless a separate behavior task is approved.
- Active unexposed visual thickness/length/outline settings lack decoder clamps. Their proposed
  rows are recorded, but numeric widget ranges require review before the affected page is ported;
  do not silently add new loading clamps or change stored values.



## 5. Reference distribution conditions

The reference is MIT + Commons Clause, with kdt255 credit and separate Inter/third-party
notices. Preserve these with any copied code/assets. Its no-sale condition needs review
before distributing a derived product; no reference code or assets have been copied yet.
See `ASTRAL_AUDIT.md`. This is a licensing constraint, not a gameplay change request.

## Approval scope

Approve the proposed seven-tab layout and Automation-first slice separately from any new
behavior. Approving a UI port does not authorize implementing inactive settings, deleting
compatibility keys, clearing stored preferences, or adding an export operation. Those
remain explicit decisions above. Proposed rows do not count as implemented controls.

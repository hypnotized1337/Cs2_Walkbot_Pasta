# Decisions required before porting affected controls

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
delete, refresh, import, folder selection/opening, search and list interaction. No explicit
export action was found. **Decision:** approve an Export operation that copies a selected
config to a user-chosen destination within the isolated test data tree, without changing
its contents. Keep existing save/import behavior. No placeholder export button will be added.

## 4. Legacy/derived/internal saved keys

The final key-by-key classification is appended after the serialized-key crosswalk is
assembled. Legacy aliases must not become misleading duplicate controls. The recommended
policy is to retain serialized compatibility fields, expose the canonical active setting,
and document derived/position/version values instead of treating them as ordinary toggles.
Approval of that policy does not authorize dropping keys or undocumented omissions.

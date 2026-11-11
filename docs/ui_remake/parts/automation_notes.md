# Walkbot and Automation inventory notes

## Scope and counts

Inventory source: `automation_controls.csv`, traced against `src/ui/menu/walk_bot_page.cpp`, `src/ui/menu/automation_page.cpp`, the called helpers in `src/ui/menu/combat_widgets.cpp`, `src/ui/menu/widgets.cpp`, and `src/ui/menu/popups.cpp`, plus `src/config/misc.hpp` and `src/config/settings.cpp`.

| Page | Section | CSV rows |
|---|---|---:|
| Walkbot | WALK BOT | 5 |
| Walkbot | COMBAT | 25 |
| Walkbot | ROUTE DISPLAY | 9 |
| Walkbot | WALK BOT AUTOMATION | 28 |
| **Walkbot total** |  | **67** |
| Automation | MATCH AUTOMATION | 8 |
| **Total** |  | **75** |

Counts are inventory entries, not the runtime number of expanded controls. Six rows describe one dynamic buy-item template (`buy_presets[*].items[*]`); the selected preset renders that template once per existing item (up to 10 items), with Add available below the limit. `[*]` means an array element, not a literal config key. The source field records the UI call site; helper-owned child rows cite their helper implementation too.

## Config and UI behavior

- Serialized paths use the actual profile JSON root: `misc.m_walk_bot.*`, `misc.auto_crouch`, `misc.auto_accept`, and `misc.m_auto_bonus.*`. The `m_walk_bot` serializer is nested under `misc`; the buy presets serialize as `buy_presets[]` objects with `custom`, `random_one`, and `items[]` (`enabled`, `code`, `side`, `money_rule`, `money`). Item `count` is runtime bookkeeping, not a serialized key; save writes the first `count` entries, while load discards entries with an empty `code` (including a newly added but unfilled item).
- Defaults in the CSV come from the `misc.hpp` initializers (and the add-item initializer at `walk_bot_page.cpp:208`); numeric bounds come from the row widget calls, not broader JSON clamps. For example, disconnect score is UI-limited to 0–2000 though config loading permits up to 10000.
- Popup children are reached by opening the row's settings button; a parent toggle being off does **not** itself hide/disable that row's popup. For example, `Own Aim` and `Auto Buy` parameters can be edited with their respective switches off.
- The entire `WALK BOT AUTOMATION` card is wrapped in `BeginDisabled()` and dimmed when `misc.m_walk_bot.enabled` is false. This also locks Auto Health Shot controls even though the config comment calls health shot standalone and says it does not need Walk Bot. Values are preserved while disabled; retain this existing mismatch unless the owner decides otherwise.
- `keybind_row` captures a key after the initiating input is released; `keyboard_only=true` excludes mouse buttons and `Escape` clears the binding. The three bindings here (record route, pickup, use bind) are keyboard-only. Calls pass `game_control=false` (the health-shot call relies on the default); no lifecycle-key filtering is applied.
- `Folder` is an action, not a saved setting: it opens `runtime_storage::paths().root / L"walkbot"`. Buy Add/Remove are actions over the selected preset's item list; removal compacts following entries. `@action.*` marks these actions; `@ui.item_index` is the current per-item loop index used by Remove, not a saved JSON key. Popup-open state is ImGui transient state and is not represented as config.
- `Routes` stores `0=random_points`, `1=recorded`. `Hitboxes` is a bitmask multiselect (Head=1, Body=2, Arms=4, Legs=8). Multipoint children come from the shared `combat::multipoint_row` helper and use the `aim_multipoint_config.*` object. Preserve its seven child rows and their 0.05 slider steps.
- `Auto Buy` has six selectable presets: Default, Premier, Competitive, Wingman, Casual, Deathmatch. Non-Default presets expose `Own Preset`; item editing and `One Random Item` are available for Default or a selected custom preset. Empty presets start with no items. Add initializes enabled=true, empty code, side=Both, money rule=Any, amount=2000.

## Coverage and additions for parent

All serialized `m_walk_bot` fields are represented by a control or dynamic control template in this scoped inventory; `misc.auto_accept`, `misc.auto_crouch`, and all three `misc.m_auto_bonus` fields used by these pages are also represented. No additional missing saved-key row is proposed for these two pages. Do not infer that unrelated pages are covered by this result: the requested ESP/item filters, projectile color, and spectator avatars belong in their owning page inventories and remain outside this scoped CSV. Auto-stop is on Triggerbot, not either audited page. The existing Triggerbot UI shows `Auto Stop`, `Stop Speed`, and `Shoot Speed`, but config fields `misc.m_auto_stop.physical_fire` and `misc.m_auto_stop.predictive_trigger` (both default `true`) have no UI or runtime references outside their declaration and JSON serialization. Propose these two as candidate missing saved-key rows on the Triggerbot inventory, but have the parent decide whether to expose or retire them because they currently appear inert.

## Completeness cautions

Keep the debug labels exact: `Show Eligibility Debug` and `Show Debug`. Preserve the stored-but-disabled Walk Bot automation settings, including the standalone-health-shot mismatch above. `Auto Accept Match`, `Auto Rejoin`, and `Auto Disconnect` are the Automation page's complete visible match-automation set; the rejoin selectors and disconnect sliders are popup children.

# Misc, Configs, Guide, and Navigation audit

## Inventory coverage

Control rows: Misc/General **17**, Misc/Overlay **37**, Configs **15**, Guide **8**, Navigation **11** (88 total).

`misc_controls.csv` records each rendered control on Misc/General (including all game binds and support actions), Misc/Overlay (including nested settings popup openers and each popup child), Configs, Guide, and Navigation. `Configs` is the same `draw_config_library()` used by the Misc > Configs subpage; it is inventoried once to avoid double-counting an identical widget set.

Defaults and constraints are from `src/config/misc.hpp`, `src/config/game_controls.hpp`, and `src/config/visuals.hpp`; serialized names are checked against `src/config/settings.cpp`. Guide helper source locations are attached to their caller rows (shared button helper, key-selector button and option buttons). The Guide key selectors use the separately persisted `hotkeys.menu` and `hotkeys.exit` settings in `hotkeys.cfg` (defaults `VK_INSERT` and `VK_END`), not JSON config keys. UI bounds for controls are taken from the helper call sites in `misc_page.cpp`.

## Misc findings / genuine UI gap

- `m_spectator_list.show_avatars` exists in the config structure, defaults to `true`, and is serialized, but no Misc page control exposes it. The current Spectators row only toggles `misc.m_spectator_list.enabled`; avatars need a new menu control during the transplant.
- The four palette popup openers (Interface Colors, Typography, Surfaces, Interaction) are listed separately from the nine RGBA color pickers nested within them. FPS, watermark, event log, and active-bind settings use popup rows; their child controls are listed separately with the popup condition.
- Event filtering is one multiselect with six independently serialized booleans; all six default to true. Show Bomb Info is serialized under `esp.m_bomb.show_info_panel`, although its toggle is visually grouped on Misc/Overlay.

## Configs operations, dynamic list, and dialogs

- Existing operations: save current settings as a named config, import a config, open the configured folder, choose/change the folder, load a healthy config, overwrite a healthy config with current settings, rename, duplicate, and delete (including broken entries). The list refreshes as it is drawn and re-scans after mutations. Healthy entries expose Load plus a healthy action-menu opener; broken entries expose a separate Delete-only action-menu opener. Rename is inline, submits on Enter, and cancels on Escape or focus loss. Configs are not exported by this page/library; the only export call nearby is the separate Diagnostic Report action on Misc/General. There is no Configs Export control to preserve.
- Import and folder selection are delegated to the overlay's native file-request handling (`import_config` and `choose_config_folder`). Import/open-folder are disabled when there is no selected folder; Change folder stays available. The empty-folder state offers Choose folder. No folder path or status text is itself interactive.
- Save accepts Enter from the name field or the Save button; the button requires a selected folder and non-empty name. Successful save clears the entered name. Operation feedback appears temporarily; otherwise the configured folder path is displayed as non-interactive state (`@state.registry.ConfigFolder`), persisted under `HKCU\Software\VestaUITest\ConfigFolder`.

## Guide controls and navigation

- The four Guide steps are Welcome to PASTA (language cards), Hotkeys (menu/exit key selectors), Good to know (data-folder action and current key readout), and Performance (tips). Navigation includes Back after the first step, Next/Finish, and Skip. Progress dots are drawn as indicators, not clickable controls.
- The top-level shell has seven tabs (Aimbot, Triggerbot, Visuals, Walk Bot, Automation, Misc, Configs); Misc has General, Overlay, and Configs sub-tabs. The separate header Unload action is visible on every Misc subpage, in addition to the General support-row Unload action.

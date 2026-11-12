# Misc saved fields without a main-menu control

This classifies the `misc.*` portion of `schema/no_menu_controls.md`. Preserve all saved keys.
No additional functional toggle is justified by these gaps alone.

- `misc.m_spectator_list.show_avatars` and `misc.m_auto_stop.physical_fire` /
  `predictive_trigger`: declared and serialized, with no runtime identifier consumer found.
  Requested proposed rows are explicitly blocked in `proposed_controls.csv`; see
  `NEEDS_DECISION.md`. Do not imply that toggling them changes behavior.
- `misc.m_{watermark,spectator_list,event_log,keybind_list,bomb_info}.layout`:
  anchor and offsets are maintained by existing HUD placement/drag code; scale is edited
  by the existing hover-and-wheel interaction (0.55–2.0, step 0.08); version is migration
  metadata, not a user feature. Preserve these interactions rather than inventing numeric
  fields for each serialized leaf. Evidence: `src/ui/overlay/hud.cpp:437-470,592-641`,
  watermark placement `:816-855`, spectator placement `:1026-1033`, shared compact-panel
  layout used for binds `:1074-1135,1474`, event log `:1149-1167`, bomb `:1499-1520`.
- `misc.m_{watermark,spectator_list,bomb_info}.position_x/y`: conditional legacy-layout
  output, retained for migration. Do not expose a second position UI alongside anchored
  layout. See the conditional serializers and decoder branches in `src/config/settings.cpp`.
- `misc.m_watermark.vertical`: existing drag placement selects orientation near screen edges
  and writes it back (`src/ui/overlay/hud.cpp:823-854`). This is existing presentation state,
  not a missing gameplay switch. Preserve automatic orientation and the current value.

Menu/exit hotkeys are not JSON leaves; they are listed in `external_settings.csv`.
The first-run marker and config-folder registry preference are covered in `other_surfaces.md`.

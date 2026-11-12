# Serialized keys without menu controls

These are the exact serialized leaf paths marked `has_menu_control=no` in `config_keys.csv` that are not covered by an existing menu control or one of the 33 approved proposed controls. The 124 no-menu leaves are reduced by 54 leaves covered by the proposals (26 direct paths plus seven RGBA color controls covering four channels each), leaving **70 hidden keys**. They remain serialized and must continue loading and saving unchanged; this list is not a request to remove or rewrite them.

Three approved proposed settings are dormant according to the Stage 1 notes, but are not hidden here because they are among the approved additions: `misc.m_auto_stop.physical_fire`, `misc.m_auto_stop.predictive_trigger`, and `misc.m_spectator_list.show_avatars`. The audit records no runtime consumer for them; listing them as approved controls does not assert behavior that the runtime does not provide.

## Combat compatibility and migration fields (20)

These flat paths are compatibility aliases, migration inputs, or derived values. Current controls use nested checks/profile settings; Auto Stop is controlled by its existing Misc setting. Do not add duplicate controls.

| Serialized path | Reason |
| --- | --- |
| `combat_global.aimbot_autowall` | Legacy flat wall-policy fallback; nested aimbot checks are canonical. |
| `combat_overrides[*].aimbot_autowall` | Legacy flat wall-policy fallback; nested aimbot checks are canonical. |
| `combat_global.triggerbot_autowall` | Legacy flat wall-policy fallback; nested triggerbot checks are canonical. |
| `combat_overrides[*].triggerbot_autowall` | Legacy flat wall-policy fallback; nested triggerbot checks are canonical. |
| `combat_global.aimbot_visible_only` | Legacy flat visibility setting; current controls edit nested aimbot wall checks. |
| `combat_overrides[*].aimbot_visible_only` | Legacy flat visibility setting; current controls edit nested aimbot wall checks. |
| `combat_global.aimbot_predictive` | Flat migration fallback; nested profile setting is canonical. |
| `combat_overrides[*].aimbot_predictive` | Flat migration fallback; nested profile setting is canonical. |
| `combat_global.aimbot_recoil_sync` | Flat migration fallback; nested profile setting is canonical. |
| `combat_overrides[*].aimbot_recoil_sync` | Flat migration fallback; nested profile setting is canonical. |
| `combat_global.triggerbot_autostop` | Migration input for the existing `misc.m_auto_stop.enabled` control. |
| `combat_overrides[*].triggerbot_autostop` | Migration input for the existing `misc.m_auto_stop.enabled` control. |
| `combat_global.aimbot_fov_config.draw_target_point` | Derived FOV display value, not an independent setting. |
| `combat_overrides[*].aimbot_fov_config.draw_target_point` | Derived FOV display value, not an independent setting. |
| `combat_global.aimbot_fov_config.visualization` | Retained for legacy FOV migration. |
| `combat_overrides[*].aimbot_fov_config.visualization` | Retained for legacy FOV migration. |
| `combat_global.aimbot_checks.flash_threshold` | Compatibility alias for the approved `flash_threshold_percent` control. |
| `combat_overrides[*].aimbot_checks.flash_threshold` | Compatibility alias for the approved `flash_threshold_percent` control. |
| `combat_global.triggerbot_checks.flash_threshold` | Compatibility alias for the approved `flash_threshold_percent` control. |
| `combat_overrides[*].triggerbot_checks.flash_threshold` | Compatibility alias for the approved `flash_threshold_percent` control. |

## Unused visual settings (18)

Stage 1 found no runtime consumer for these saved fields. Keep them persisted, but do not surface dead controls.

| Serialized path | Reason |
| --- | --- |
| `esp.m_chams.glow_effect.layers` | Unused saved glow-layer setting; no runtime consumer. |
| `esp.m_item.m_name.color.r` | Unused saved item-name color; no runtime consumer. |
| `esp.m_item.m_name.color.g` | Unused saved item-name color; no runtime consumer. |
| `esp.m_item.m_name.color.b` | Unused saved item-name color; no runtime consumer. |
| `esp.m_item.m_name.color.a` | Unused saved item-name color; no runtime consumer. |
| `esp.m_player.m_hitboxes.enabled` | Unused saved player-hitboxes group; no runtime consumer. |
| `esp.m_player.m_hitboxes.fill` | Unused saved player-hitboxes group; no runtime consumer. |
| `esp.m_player.m_hitboxes.outline` | Unused saved player-hitboxes group; no runtime consumer. |
| `esp.m_player.m_hitboxes.visible_color.r` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.visible_color.g` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.visible_color.b` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.visible_color.a` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.occluded_color.r` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.occluded_color.g` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.occluded_color.b` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_hitboxes.occluded_color.a` | Unused saved player-hitboxes color; no runtime consumer. |
| `esp.m_player.m_offscreen_arrows.thickness` | Unused saved thickness setting; no runtime consumer. |
| `esp.m_player.m_threat_module.max_distance` | Unused saved range setting; no runtime consumer. |

## HUD placement and migration state (32)

These values are overlay state, not standalone menu settings. Preserve existing HUD drag/wheel placement, scale, and orientation interactions and their serialization; layout versions and legacy positions remain migration state.

| Serialized path | Reason |
| --- | --- |
| `misc.m_bomb_info.layout.anchor` | HUD placement/editor state; retain drag/wheel interaction and migration metadata. |
| `misc.m_bomb_info.layout.offset_x` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_bomb_info.layout.offset_y` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_bomb_info.layout.scale` | HUD placement/editor state; retain existing scale interaction. |
| `misc.m_bomb_info.layout.version` | HUD layout migration metadata, not a user-facing control. |
| `misc.m_bomb_info.position_x` | Legacy HUD position retained for layout migration. |
| `misc.m_bomb_info.position_y` | Legacy HUD position retained for layout migration. |
| `misc.m_event_log.layout.anchor` | HUD placement/editor state; retain drag/wheel interaction and migration metadata. |
| `misc.m_event_log.layout.offset_x` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_event_log.layout.offset_y` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_event_log.layout.scale` | HUD placement/editor state; retain existing scale interaction. |
| `misc.m_event_log.layout.version` | HUD layout migration metadata, not a user-facing control. |
| `misc.m_keybind_list.layout.anchor` | HUD placement/editor state; retain drag/wheel interaction and migration metadata. |
| `misc.m_keybind_list.layout.offset_x` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_keybind_list.layout.offset_y` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_keybind_list.layout.scale` | HUD placement/editor state; retain existing scale interaction. |
| `misc.m_keybind_list.layout.version` | HUD layout migration metadata, not a user-facing control. |
| `misc.m_spectator_list.layout.anchor` | HUD placement/editor state; retain drag/wheel interaction and migration metadata. |
| `misc.m_spectator_list.layout.offset_x` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_spectator_list.layout.offset_y` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_spectator_list.layout.scale` | HUD placement/editor state; retain existing scale interaction. |
| `misc.m_spectator_list.layout.version` | HUD layout migration metadata, not a user-facing control. |
| `misc.m_spectator_list.position_x` | Legacy HUD position retained for layout migration. |
| `misc.m_spectator_list.position_y` | Legacy HUD position retained for layout migration. |
| `misc.m_watermark.layout.anchor` | HUD placement/editor state; retain drag/wheel interaction and migration metadata. |
| `misc.m_watermark.layout.offset_x` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_watermark.layout.offset_y` | HUD placement/editor state; retain drag/wheel interaction. |
| `misc.m_watermark.layout.scale` | HUD placement/editor state; retain existing scale interaction. |
| `misc.m_watermark.layout.version` | HUD layout migration metadata, not a user-facing control. |
| `misc.m_watermark.position_x` | Legacy HUD position retained for layout migration. |
| `misc.m_watermark.position_y` | Legacy HUD position retained for layout migration. |
| `misc.m_watermark.vertical` | Existing watermark orientation/placement state; retain its current interaction and persistence. |

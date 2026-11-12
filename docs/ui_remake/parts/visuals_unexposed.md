# Visuals no-old-UI key audit

Scope is limited to the `esp.*` leaves currently listed in `schema/no_menu_controls.md` (52 serialized leaves). Classification is based on actual runtime reads and rendering behavior. The generated no-menu file also includes rows that the parent has already proposed; those settings are classified here but not duplicated in the visuals proposal fragment.

| Current key family | Classification | Runtime evidence and decision |
|---|---|---|
| `esp.m_chams.glow_effect.layers` | Unused | Deserialization clamps it to 1–6 (`src/config/settings.cpp:1441-1465`), but no runtime consumer reads the field. The renderer's glow path uses enabled, radius, color, and strength (`src/features/chams/renderer.cpp:3370-3405`). No control proposed. |
| `esp.m_chams.on_shot.appearance.enabled` | Active user-facing | The on-shot feature is gated by `on_shot.enabled`, then copies the appearance material into the draw path (`renderer.cpp:3545-3556`); generic `draw_material` independently skips disabled materials (`renderer.cpp:2336`). Proposed as a nested appearance gate, separate from enabling on-hit event capture. |
| `esp.m_item.m_ammo.*`, `esp.m_item.m_filters.heavy`, `.shotguns` | Active user-facing; parent proposal exists | Item rendering uses group filters, ammo enable, and filled/empty colors (`src/features/visuals/item.cpp:126-162`). The parent proposal already includes these controls; omitted from `visuals_proposed_controls.csv`. |
| `esp.m_item.m_name.color.*` | Unused | Item-name rendering uses a compile-time `label_color` (`item.cpp:150-155`); the serialized color has no runtime read. No control proposed until rendering honors this setting. |
| `esp.m_player.m_box.outline` | Active user-facing | `paint_box` tests `cfg.outline` for both full and cornered box outlines (`src/features/visuals/player.cpp:614-634`). Proposed as a checkbox under Bounding Box, gated by the existing Show Box setting. |
| `esp.m_player.m_head_circle.thickness` | Active user-facing | `paint_head_marker` passes `cfg.thickness` to the circle draw (`player.cpp:707-713`). Proposed; no decoder clamp found, so range is an owner decision. |
| `esp.m_player.m_hitboxes.*` | Unused | The config group is declared/serialized, but no runtime reads of `m_player.m_hitboxes` were found. Player capsules currently render through the separate threat module using `player.hitboxes` (`player.cpp:429-593`). No controls proposed for fields that do not affect rendering. |
| `esp.m_player.m_offscreen_arrows.thickness` | Unused | `paint_offscreen_markers` consumes size, radius, color, and bloom fields (`player.cpp:295-420`), but not thickness. The field is persisted without a runtime consumer; no control proposed. |
| `esp.m_player.m_skeleton.thickness`, `.occluded_color.*` | Active user-facing | `paint_skeleton` chooses visible/occluded color and passes thickness to each line (`player.cpp:638-654`). The visible color is already exposed; occluded color and thickness are proposed. No decoder clamp found for thickness. |
| `esp.m_player.m_threat_module.head_hitbox`, `.body_hitbox`, `.limb_hitbox` | Active user-facing | Threat rendering conditionally draws each bone group (`player.cpp:581-593`). Head/body colors and fill alpha are already exposed; the three toggles are proposed. |
| `esp.m_player.m_threat_module.limb_color.*` | Active user-facing | Limb capsules use `cfg.limb_color` (`player.cpp:591-593`). Proposed with 0–255 RGBA controls, as represented by the schema's 8-bit color channels. |
| `esp.m_player.m_threat_module.max_distance` | Unused | The threat renderer reads hitbox selection, colors, and outlines but never reads `max_distance` (`player.cpp:429-593`). No control proposed. |
| `esp.m_player.m_threat_module.outline_alpha`, `.outline_thickness` | Active user-facing | Threat outlines scale alpha from the configured value and draw when thickness is positive (`player.cpp:527-543`, `574-576`). Proposed; no decoder clamps found, so ranges are owner decisions. |
| `esp.m_player.m_view_line.length`, `.thickness` | Active user-facing | `paint_view_direction` uses length for the endpoint and thickness for the line (`player.cpp:716-736`). Proposed; ranges are owner decisions because the decoder does not clamp them. |
| `esp.m_projectile.default_color.*` | Active user-facing; parent proposal exists | The projectile palette fallback reads this color (`src/features/visuals/projectile.cpp:64`). It is already in the parent proposal; omitted from the visuals proposal fragment. |

## Proposed controls

`visuals_proposed_controls.csv` contains only active settings from this current list that are not already exposed in Visuals and are not in `parts/proposed_controls.csv`: on-hit appearance enable; box outline; skeleton thickness and occluded color; head-circle thickness; view-line length and thickness; threat head/body/limb toggles, limb color, and outline alpha/thickness. Each condition contains the exact marker `new control (no old UI)`, and source fields are blank because these controls do not exist in the old UI.

The proposed defaults match `config_keys.csv`. Color channels use 0–255. Numeric UI ranges are left blank with an owner-decision note where no decoder clamp exists. This audit changes no product behavior or config defaults.

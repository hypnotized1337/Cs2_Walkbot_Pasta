# Visuals inventory notes

## Scope and mapping

`visuals_controls.csv` inventories the existing Visuals page and its shared settings/editor controls. The pass followed `visuals_page.cpp` callsites, expanded widget helpers in `internal.hpp` and `widgets.cpp`, traced the editor interactions in `visual_editor.cpp` and its popup implementation, then matched fields to the `visual_profile`/`general_profile` serializers in `config/settings.cpp`. Config roots are exactly `esp.*` and `misc.*`; non-persisted navigation/view/action controls use `@ui.*` or `@action.*` keys. Source lines point to actual existing UI/helper callsites, not placeholders.

The `default` column writes colors as `[r,g,b,a]` for readability; serialized color values are JSON objects `{"r":…, "g":…, "b":…, "a":…}`. A color control maps to the complete color-object path. Combined toggle/color controls list both paths separated by semicolons. `min`/`max` are the UI bounds (not a claim about every JSON clamp); conditions identify popup, mode, material, and editor visibility gates. Editor drag defaults list x;y coordinates; editor scale values are the declared layout defaults. The editor's right-click settings rows are separately inventoried because the editor creates a second control instance; the editor reuses the same widget helpers as the page.

## Row counts

Counts include each subtab navigation button and each editor settings control/interaction.

| Page | Rows |
| --- | ---: |
| Visuals/Players | 240 |
| Visuals/Items | 11 |
| Visuals/Projectiles | 16 |
| Visuals/Bomb | 16 |
| Visuals/Radar | 29 |
| Visuals/Effects | 48 |
| Visuals/Crosshair | 16 |
| **Total** | **376** |

## Saved settings with no current menu control

These are existing serialized settings, not proposed defaults or newly implemented controls. They need inclusion in the later transplant, with parent ownership of control design:

- Player box/skeleton/head/view/arrows: `esp.m_player.m_box.outline`; `esp.m_player.m_skeleton.thickness`; `esp.m_player.m_skeleton.occluded_color`; `esp.m_player.m_head_circle.thickness`; `esp.m_player.m_view_line.length`; `esp.m_player.m_view_line.thickness`; `esp.m_player.m_offscreen_arrows.thickness`.
- Player hitbox/threat details: `esp.m_player.m_hitboxes.enabled`; `esp.m_player.m_hitboxes.visible_color`; `esp.m_player.m_hitboxes.occluded_color`; `esp.m_player.m_hitboxes.fill`; `esp.m_player.m_hitboxes.outline`; `esp.m_player.m_threat_module.max_distance`; `esp.m_player.m_threat_module.head_hitbox`; `esp.m_player.m_threat_module.body_hitbox`; `esp.m_player.m_threat_module.limb_hitbox`; `esp.m_player.m_threat_module.outline_alpha`; `esp.m_player.m_threat_module.outline_thickness`; `esp.m_player.m_threat_module.limb_color`.
- Player chams/item/projectile details: `esp.m_chams.glow_effect.layers`; `esp.m_item.m_name.color`; `esp.m_item.m_ammo.enabled`; `esp.m_item.m_ammo.color`; `esp.m_item.m_ammo.empty_color`; `esp.m_item.m_filters.shotguns`; `esp.m_item.m_filters.heavy`; `esp.m_projectile.default_color`.
- Other requested surfaces outside this Visuals page: `misc.m_spectator_list.show_avatars`; `misc.m_auto_stop.enabled`; `misc.m_auto_stop.physical_fire`; `misc.m_auto_stop.predictive_trigger`; `misc.m_auto_stop.stop_speed`; `misc.m_auto_stop.required_shoot_speed`.

These omissions are deliberate in this inventory: no synthetic menu row is added for a control absent from the current UI. The list identifies persisted keys for the parent to place on the appropriate page in the port. The spectator-avatar field currently has no runtime consumer; its placement is a decision, and adding a dead toggle is not recommended.

## Serialized paths and config-key validation

The CSV binds serialized JSON paths, not C++ member names. `no_flash` and `no_smoke` use the C++ member `wireframe_color`, but `settings.cpp` serializes it as `line_color`; rows therefore use `esp.m_no_flash.line_color.r/g/b/a` and `esp.m_no_smoke.line_color.r/g/b/a`. Name and Chams paths follow their serializer fields. Colors are represented by RGBA component paths separated by semicolons.

Info Flags have no `<selected>` placeholders: Money, Armor, Defuse Kit, Scoped, Defusing, Flashed, Ping, Distance, and Bomb Damage each have explicit `.color.r/g/b/a` and `.scale` paths, conditioned on Edit Flag selection. Material defaults are separately recorded for Visible (Glow Outline, type 3; `[136,60,255,184]`; exponent `5.099999905`, falloff `0.050000001`, Fresnel fill `0.719999969`), Occluded (Water Flow, type 5; `[60,71,255,200]`; speed `1.200000048`), and On Hit Appearance (Glow Outline, type 3; `[255,185,80,150]`; exponent `3.0`, falloff `0.18`, Fresnel fill `0.22`).

Validated against the current `config_keys.csv` leaf catalog: all serialized Visuals CSV bindings match a saved key; source locations resolve to existing lines. The avatar field `misc.m_spectator_list.show_avatars` has no runtime consumer and remains a decision item, not a proposed control.

## Source-backed behavior worth preserving

- The page has Players, Items, Projectiles, Bomb, Radar, Effects, and Crosshair subtabs. The player ESP and radar activation keybinds appear only for Hold/Toggle modes.
- Chams material parameters are conditional on selected material: Shaded (roughness/metalness), Glow (exponent), Glow Outline (exponent/falloff/fresnel fill), Iridescent (strength/roughness), Water Flow (speed), and Glossy (exponent/fresnel fill/falloff/tint). Other values remain saved when hidden.
- Player bar settings include Position, Thickness, Outline, Gradient, Segments, Show Value, and Background Color; nested popup values are in the CSV. Segment count is enabled when greater than one; turning segmentation on sets an initial count of four. Dragging a bar to a preview dock changes both layout coordinates and bar position.
- The editor previews Name, Weapon, Health Bar, Armor Bar, and Info Flags. These elements are shown only when their relevant settings are enabled (Info Flags additionally needs a selected flag); drag is clamped to the preview and wheel scale is `0.55–2.0` by `0.08` per wheel unit. Right-click opens the shared settings popup. Dragging the live model orbits it; orbit is unavailable when the live model preview is absent.
- Projectile `default_color` is serialized but has no editor on the current page; per-type projectile colors are separate controls. Item ammo and the shotgun/heavy filter fields are also serialized but unexposed here.

This is a Vesta-source inventory only; it does not assess the reference fork's UI or renderer compatibility.

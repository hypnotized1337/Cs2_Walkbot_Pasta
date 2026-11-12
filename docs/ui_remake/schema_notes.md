# Config schema inventory notes

`config_keys.csv` is an inventory of the JSON leaves produced by the current config serializers in `src/config/settings.cpp` and `build_config_json()`, paired with defaults from the corresponding declarations under `src/config/*.hpp`. The extractor is intentionally repository-specific: run `python3 docs/ui_remake/scripts/extract_config_keys.py` from any directory to regenerate the raw CSV, then run `scripts/assemble_inventory.py` to apply the reviewed crosswalk and regenerate `schema/no_menu_controls.md`.

The current inventory has 982 unique leaf paths: 106 `combat_global`, 79 `combat_overrides[*]`, 565 `esp`, and 232 `misc`. `[*]` marks serialized array elements, not a literal JSON key. Combat overrides have six fixed group slots. Walk-bot buy presets have six mode slots; each preset writes only its active `items` entries (up to ten). Their internal `count` is not a JSON field. The single string-valued leaf is the buy-item `code`; the remaining leaves are booleans or numbers. Enum defaults are recorded as their serialized integer values, and character/Windows virtual-key defaults as their integer values.

`has_menu_control` in the final CSV comes from the reviewed control fragments, including
shared helpers, compound widgets, and editor instances. The extraction-only heuristic is
not the final crosswalk. After assembly, 858 paths have an old-menu binding and 124 do not.
`no` does not mean obsolete or safe to remove: the complete list is in
`schema/no_menu_controls.md`, with family classifications in
`parts/{combat,visuals,misc}_unexposed.md`. Proposed new controls do not change this old-menu
coverage flag. Keep all keys loadable and round-trippable regardless of presentation.


## Decoder-only legacy aliases

These are accepted during deserialization but are not current serialized leaf paths. They are compatibility inputs, not additional fields to emit in a new config:

- `combat_global.triggerbot_early_autostop`, `combat_global.triggerbot_mode`, `combat_global.triggerbot_seed`, `combat_global.triggerbot_simple_delay`; the same four legacy paths under `combat_overrides[*]`.
- `esp.m_player.bomb_damage`, `esp.m_player.color`, `esp.m_player.direct_visible`, `esp.m_player.m_ammo_bar`, `esp.m_player.m_bomb_damage`, `esp.m_player.pulse_max_opacity`, `esp.m_player.pulse_min_opacity`, `esp.m_player.pulse_period`, `esp.m_player.radar`, `esp.m_player.radar_hold`, `esp.m_player.sound`, `esp.m_player.sound_distance`, `esp.m_player.sound_hold`.
- `esp.m_player.m_info_flags.armor_color`, `.bomb_damage_color`, `.defusing_color`, `.distance_color`, `.flashed_color`, `.kit_color`, `.money_color`, `.ping_color`, `.scoped_color`; these legacy color leaves are used when the corresponding current style object (for example `money`) is absent.
- `esp.m_player.m_weapon.empty_color` and `esp.m_player.m_weapon.show_count`; current serialization nests these under `m_weapon.ammo`.
- `esp.m_player.m_offscreen_arrows.pulse`, `esp.m_player.m_offscreen_arrows.pulse_speed`, and `esp.m_player.m_layout.ammo`.
- `esp.m_projectile.bar_background`, `esp.m_projectile.show_timer_bar`.
- `esp.m_chams.appearance`, `.color`, `.count`, `.duration`, `.layers`, `.radius`, `.size`, `.strength`, `.wireframe`; these are legacy flattened chams inputs, while current output uses nested effects/materials.
- `misc.m_walk_bot.mode` is the legacy route-mode input. Current output uses `misc.m_walk_bot.route`; old mode `1` also migrates to `hunt=true`, and old mode `2` selects the recorded route.
- `misc.accent`, `misc.background`, `misc.border`, `misc.card`, `misc.hover`, `misc.muted_text`, `misc.panel`, `misc.popup`, `misc.text` (legacy flat palette inputs; current output stores the palette under `misc.palette`).

## Internal fields and path caveats

- `misc.m_walk_bot.buy_presets[*].count` is an in-memory count derived from the serialized `items` array and is deliberately not emitted.
- `*.layout.legacy_position_x` and `*.layout.legacy_position_y` are migration state, not current members of the nested `layout` JSON object. Conditional flat output paths `misc.m_watermark.position_x/y`, `misc.m_spectator_list.position_x/y`, and `misc.m_bomb_info.position_x/y` are emitted only by the legacy layout-version branch; they are included in the CSV because that branch can still serialize them.
- `combat_profile::groups[*]` is runtime group configuration; saved per-group overrides use `combat_overrides[*]` instead.
- Hidden, editor-only, and no-control settings remain present in the inventory. The CSV is a preservation checklist, not an approval to expose or remove a field.

## Provenance and limits

The extractor follows `to_json` serializer key/value expressions, nested serializers, inline JSON objects, conditional `j["key"]` assignments, fixed arrays, and the custom walk-bot buy-preset serializer. It derives direct menu labels from the first user-facing widget label and its bound setting expression. This is a source inventory, not a runtime round-trip test; it does not prove that a running Windows build can read or write each path. Review any future custom serializer or decoder branch against these notes when changing the config schema.

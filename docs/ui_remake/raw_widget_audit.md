# Raw widget audit (Step 1)

Interactive raw ImGui calls in the legacy page sources were checked. Shared page controls remain in their existing helpers; the custom page interactions below now use the shared library interaction helpers, which use Astral button styling and retain the legacy ImGui behavior when `VESTA_ASTRAL_MENU` is disabled; invisible hit regions preserve their original item bounds and behavior.

## Routed through shared helpers

- `[62](src/ui/menu/guide_page.cpp)` — const bool pressed = detail::styled_button(label, {width, 34.0f});
- `[79](src/ui/menu/guide_page.cpp)` — detail::hit_region("##key", {width, control_height});
- `[141](src/ui/menu/guide_page.cpp)` — detail::hit_region("##option",
- `[289](src/ui/menu/guide_page.cpp)` — const bool clicked = detail::hit_region("##language", {card_width, card_height});
- `[602](src/ui/menu/guide_page.cpp)` — detail::hit_region("##skip", {size.x + 8.0f, size.y + 8.0f});
- `[585](src/ui/menu/visual_editor.cpp)` — detail::hit_region("##preview_element", hit_max - hit_min);

## Intentionally remaining custom drawing / raw interactions

- `src/ui/menu/guide_page.cpp`, `key_selector`: its `BeginPopup("##keys")` and per-key option drawing remain specialized to the lifecycle-key picker. The popup's option hit targets are routed through `hit_region`; the custom popup layout and selected/hovered option rendering remain because no generic combo preserves its dynamic above/below placement, seven-row cap, and key-binding semantics.
- `src/ui/menu/guide_page.cpp`, onboarding cards and step illustrations: custom card backgrounds, language-card visuals, text placement, progress marks, and tutorial artwork remain presentation drawing rather than shared menu widgets.
- `src/ui/menu/visual_editor.cpp`, preview renderer: model/stock-image, bones/hitboxes, element geometry, and preview selection overlays remain custom preview drawing. Only the preview element hit region is routed through `hit_region`.
- `src/ui/menu/walk_bot_page.cpp`, disabled automation section: `BeginDisabled`/`EndDisabled` and the surrounding rectangle hit-test plus `SetTooltip` remain unchanged to preserve the existing Walkbot greying and the exact tooltip trigger and text.
- `src/ui/menu/configs_page.cpp`, `misc_page.cpp`, `combat_page.cpp`, and `visuals_page.cpp`: raw `BeginChild` calls are page/layout containers; raw text calls display status or explanatory copy. Their interactive controls already use shared menu helpers.

No preview drawing or gameplay logic was changed.

## Font overrides

- `guide_page.cpp:263,304` draws guide headings at 1.6× the scoped ImGui font (24px with Astral Inter 15); the rest inherits the scoped font.
- `visual_editor.cpp:843` passes `menu_source_18` through `push_menu_font`, which maps to Astral Inter 15. `menu_regular_12`, `menu_semibold_13`, and `weapons_15` are intentionally retained for preview labels and glyphs only.
- Config entry labels and library card titles use the scoped Inter 15 and do not push a page-local font.

# Astral UI audit for the Vesta transplant

## Scope and source

Inspected the read-only reference/ checkout of hypnotized1337/Astral-Remake: README, build script, license/notices, host, all menu/src/astral UI sources, and bundled dependency headers. This is an audit of the reference implementation, not product integration. Astral describes itself as a standalone recreation with no game interaction (reference/README.md); its controls and previews must not be treated as a working backend.

## Stack and compatibility

| | Vesta test fork | Astral reference | Port consequence |
|---|---|---|---|
| Language / host | C++23; existing Windows overlay and Win32/DX11 ImGui backends | C++17 standalone Win32/D3D11 host (reference/README.md, reference/menu/src/main.cpp) | Port presentation into Vesta's existing overlay; do not bring Astral's host, message loop, swap chain, device lifecycle, or a second ImGui context. |
| Dear ImGui | CMake pins commit bf75bfec48fc00f532af8926130b70c0e26eb099, expected v1.92.3 (CMakeLists.txt) | Bundled ImGui v1.92.9b / IMGUI_VERSION_NUM 19291 (reference/third_party/imgui/imgui.h) | Compile against Vesta's pin. Audit every copied call, especially icon/user-texture APIs; do not replace or upgrade Vesta's pin as part of the visual port. |
| Font rendering | Existing Vesta ImGui font setup and FreeType integration | FreeType 2.15.0 and embedded Inter font data (reference/README.md, reference/THIRD_PARTY_NOTICES.md, reference/menu/src/astral/fonts/inter.h) | Reuse Vesta's atlas lifecycle. Adapt font data/atlas setup, not the reference renderer's startup/shutdown code. |
| Rendering and input | Vesta owns frame creation, capture/focus policy, input mapping, DPI and presentation (src/ui/overlay/overlay.cpp, src/ui/overlay/input.cpp) | Astral widgets draw into ImGui; its host owns frame/input lifecycle | Preserve Vesta's frame/input policy, renderer callbacks, key ownership, and clipping. Port draw/widget behavior as an in-process layer. |

Vesta's pinned ImGui is close enough for a visual transplant but is not the exact version Astral was built against. Ordinary public widget and draw-list operations are portable; version-sensitive texture-atlas calls and ImGui-internal item lifecycle code need compile-time adaptation. Astral's icon atlas explicitly relies on the Dear ImGui 1.92 user-texture API (reference/menu/src/astral/icons.h), the clearest compatibility risk. Do not copy its bundled ImGui, FreeType binaries, Win32/DX11 backend, or dependency versions into Vesta.

## Architecture, theme, and interaction

- **Structure:** menu/src/astral/menu.cpp owns window, horizontal tabs, page dispatch and transitions; pages.cpp implements seven showcase pages; ui.h/.cpp provide stateful widgets/layout; draw.h/.cpp provide palette-aware drawing; icons.h/.cpp provide the icon atlas; fonts/inter.h embeds fonts; preview.h/.cpp draws the ESP-style preview; persist.h/.cpp handles standalone config/script files. Dispatch is at reference/menu/src/astral/menu.cpp:240-246.
- **Window/layout:** fixed design-space geometry is scaled through ui::Px(); reference window design size is 600×620. It uses a header, horizontal tabs, card/page area with columns and panels, accessory/control rows, and footer. It supports drag/resize, menu scale, and animated tab/page transitions. Adapt geometry to Vesta display/DPI and retain Vesta window lifecycle.
- **Palette:** dark graphite surfaces, warm coral/orange accent, restrained gradient/glow. Presets are Salmon, Crimson, Sunset, and Custom; the reference palette includes Crimson #E5101B→#FF8A1C, Sunset #F2581C→#FFAA22, Salmon #FF8A84→#FFB27C (reference/menu/src/astral/spec.h). Use the intended default appearance; palette customization is not a new Vesta setting.
- **Fonts/icons:** embedded Inter SemiBold/Bold faces provide body/heading hierarchy. A custom icon set is rasterized to an SDF-style atlas and bound as an ImGui user texture. Keep the look, but bridge registration to Vesta's pinned ImGui and existing font/device lifecycle.
- **Animation:** frame-time easing drives open/close (~0.22 s), search reveal (~0.18 s), tab/hover states, checkbox/slider fills, popup reveal, and ambient particles (menu.cpp, ui.cpp, draw.cpp). These are presentation effects; setting writes remain direct and deterministic.

## Widget/API coverage to transplant

The reusable surface is broader than toggles. ui.h/.cpp and pages.cpp provide:

| Surface | Reference behavior | Vesta adaptation |
|---|---|---|
| Layout | SetScale, SetAccent, BeginFrame/Shutdown; scaled pixels; columns; fit/fill panels; headings, captions, separators, spacing | Call from Vesta's existing frame. Keep layout state local to Vesta and compose with its scale/DPI. |
| Basic controls | Animated checkbox/toggle; integer/float sliders; single/multi-option combos; CardCombo; color rows/picker; buttons; text input; list rows | Bind each widget to existing fields, conditions, ranges and config. Do not copy showcase defaults or add controls without live bindings. |
| Secondary controls | Settings/accessory buttons and popups, color picker, tooltips, disabled/hover states, selectable rows, search/filter | Reuse Vesta popup, localization, disabled-state and setting behavior where semantics differ. |
| Bind controls | Bind editor/capture, bind modes (Hold, Toggle, Always), bind kinds, active-binds overlay | Preserve Vesta input lifecycle; expose only bindings Vesta supports. Do not adopt arbitrary showcase-widget binds. |
| Drawing | Panel/card surfaces, rounded borders, shadows, gradients, accent glow, icon/text layout, preview drawing | Use Vesta draw-list/backend ownership, clipping, alpha and DPI; do not take reference device/texture ownership. |

Widget API names in the reference header are: `SetScale`, `SetAccent`, `BeginFrame`, `Shutdown`; `BeginColumns`, `NextColumn`, `EndColumns`, `BeginPanel`, `EndPanel`; `Checkbox`, `Slider`, `SliderInt`, `Combo`, `MultiCombo`, `Caption`, `ColorRow`, `Button`, `InputText`, `ListRow`, `Separator`, `Tooltip`, `ColorDot`, `BeginSettings`, `EndSettings`, `CardCombo`, and `DrawBindsOverlay` (`reference/menu/src/astral/ui.h`). The palette-aware drawing layer also exposes `ShadeLinear`, `RectGradient`, `Glow`, `Star`, `Text`, `TextCapCentered`, `TextCapTop`, `Chevron`, `Plus`, `Minus`, `Check`, `HueStrip`, and `Checkerboard` (`draw.h`). Bind these APIs to Vesta-owned settings and renderer state; the reference `persist.h` Save/Load/Delete/Import/Autoload functions are intentionally not part of the transplant.

## Reference-only functionality to omit

The tab names are a showcase, not a Vesta feature contract (reference/menu/src/astral/menu.cpp:240-246; implementations in pages.cpp). Port visual language and appropriate widget patterns only.

1. **Legit/Rage showcase state:** sample aim controls are not Vesta combat bindings/runtime. Vesta Aimbot and Triggerbot remain authoritative.
2. **Skins:** omit the reference skin page and cosmetic selection controls; do not add a Vesta Skins tab or fake controls.
3. **Misc/Interface extras:** accent preset/freeform controls, reference scale/search preferences, and arbitrary widget-bind editing are not new Vesta settings.
4. **Reference Configs storage:** its standalone config-file operations/schema are not Vesta config semantics or test-root fences. Reuse layout only.
5. **Scripts:** PageScripts lists/filters files and shows a **read-only source preview** (reference/menu/src/astral/pages.cpp:804-865); there is no Lua execution backend. Omit Scripts, reload/autoload controls and source preview.
6. **ESP preview:** static showcase, not live game/player data. Do not replace Vesta's functional ESP/world overlays or visual editor.

Do not add these as disabled/fake controls. Keep Vesta-only pages/settings, as mapped in FEATURE_MAPPING.csv.

## License and distribution note

The reference README and reference/THIRD_PARTY_NOTICES.md identify first-party menu code as MIT + Commons Clause and require kdt255 credit; the Commons Clause forbids selling the software and products/services whose value derives substantially from it. Carry reference/LICENSE and notices with copied code/assets. Dear ImGui, FreeType, and Inter retain separate licenses (MIT, FTL, SIL OFL 1.1; see notices). Review distribution obligations before shipping a derived binary; this is not legal advice. No reference code or asset was copied into product sources in Stage 1.

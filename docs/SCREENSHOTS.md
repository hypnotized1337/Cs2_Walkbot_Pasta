# Screenshot capture guide

These are the real-menu capture instructions for the README. The files already in `docs/images/` are clearly labelled placeholders, not mockups. Replace each placeholder with the matching real capture and keep the exact filename.

## Capture setup

Use the same setup for every image so the set looks consistent:

- Windows display scaling: **100%**.
- Game resolution: 1920x1080 or 2560x1440, **Fullscreen Windowed** or **Borderless**.
- Menu **DPI Scale: 100%** (Misc → Overlay → Interface).
- Accent: default **RGB 180, 180, 208** (Misc → Overlay → Interface Colors).
- Overlay opacity: default **86%**.
- Turn off HDR, Windows night light, Steam overlay, Discord overlay, recording indicators, and the mouse cursor.
- Use the same map, lighting, and background for the menu shots. A local practice map is enough.
- Use a neutral, empty config folder created only for captures. Do not show a personal folder path, account name, avatar, chat, or server browser.
- For the overlay shot, enable **Show Watermark**, **Show Spectators**, **Show Active Binds**, and **Show Bomb Info** in Misc → Overlay.
- Use sample binds for the active-binds overlay, for example Aimbot on `C` and Triggerbot on `V`. Keep the menu itself closed for `overlays.png`.
- The spectator panel must contain a real spectator. Use a local test client or a cooperative second player; do not fabricate names or panels.

## Capture method

1. Start CS2 in borderless/windowed mode and run PASTA.
2. Open the menu with `Insert`, select the page, close any popups, and wait for the page transition to finish.
3. Press `Win+Shift+S`, choose **Rectangular snip**, and capture the menu window with a small even margin. Snipping Tool saves PNG; use **Save as** and choose PNG if needed.
4. For `overlays.png`, close the menu and capture the full 16:9 game frame with the overlays visible.
5. Crop to 16:9, then export at **1600x900 PNG**, sRGB. Do not upscale. Keep text sharp and avoid JPEG compression.
6. Check the image for personal paths, names, avatars, chat, or notifications before saving it into `docs/images/`.

## Shot list

| File | Screen or state | Settings to show | Crop and size |
| --- | --- | --- | --- |
| `hero.png` | Menu open on **Aimbot → Global**, no popup, clean practice-map background. | Default accent, DPI 100%, Skeleton preview off, no active route. | Menu plus an even background margin; 1600x900 PNG. |
| `menu-aimbot.png` | **Aimbot → Global**. | Default values; Global selected; no popup open. | Menu window with a small margin; 1600x900 PNG. |
| `menu-triggerbot.png` | **Triggerbot → Global**. | Default values; Global selected; no popup open. | Menu window with a small margin; 1600x900 PNG. |
| `menu-visuals.png` | **Visuals → Players**. | Default player ESP; Visual Editor closed; no preview popup. | Menu window with a small margin; 1600x900 PNG. |
| `menu-walkbot.png` | **Walk Bot** page. | Walk Bot enabled so the automation rows are visible, but no route running. Keep Auto Crouch and Auto Buy off. | Menu window with a small margin; 1600x900 PNG. |
| `menu-automations.png` | **Automation** page. | Auto Accept on; no rejoin or disconnect popup open. | Menu window with a small margin; 1600x900 PNG. |
| `menu-misc.png` | **Misc → Overlay**. | Default accent, DPI 100%, watermark/spectators/active binds/bomb info visible in the options. | Menu window with a small margin; 1600x900 PNG. |
| `menu-configs.png` | **Configs** page with two neutral sample configs, `Default` and `Practice`. | No personal folder path or file names; action menu closed. | Menu window with a small margin; 1600x900 PNG. |
| `overlays.png` | In-game, menu closed. | Watermark, active binds with sample binds, spectator list with a real spectator, bomb info/timer. | Full 16:9 frame; 1600x900 PNG. |

## Notes

- The final delivery size is 1600x900. Capture at native 2560x1440 when possible and downscale once; do not capture at 1600x900 and upscale.
- For `menu-configs.png`, create the sample configs in the capture folder and do not open a file browser that shows the rest of the disk.
- If a required state (a spectator or a planted bomb) is not available, leave that image as a placeholder rather than editing a fake panel into it.

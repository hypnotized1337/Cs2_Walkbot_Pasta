# Astral-only showcase features excluded from the port

Astral is a presentation reference only. These showcase features are not part of the Vesta menu; do not add placeholder controls, static demo data, or separate persistence for them.

| Astral showcase feature | Port decision |
| --- | --- |
| Skins page | Exclude the page and its showcase controls; no corresponding Vesta destination is part of the approved layout. |
| Scripts page and source-preview workflow | Exclude script browsing/source previews and script functionality; the Astral showcase page is not a Vesta backend. |
| Static ESP/player preview | Exclude the demo preview; keep Vesta's existing live visual data, renderer, and editor behavior. |
| Standalone Astral config/script persistence | Exclude Astral's separate configuration and script storage semantics; keep Vesta's existing Configs operations and config format. |
| Custom palette/accent controls | Exclude Astral's palette presets/editor; retain the Vesta accent-color rule. |
| Astral UI-scale setting | Exclude a second independent scale control; retain Vesta's DPI scaling behavior. |
| Search workflow | Exclude Astral's showcase search UI and search-specific behavior. |
| Arbitrary bind editing | Exclude editing arbitrary binds; preserve Vesta's existing configured key bindings and supported interactions. |

These exclusions follow `ASTRAL_AUDIT.md`, `FEATURE_MAPPING.csv`, and `feature_mapping.md`. Reusable styling and widget patterns may be adapted only when connected to existing Vesta settings and operations.

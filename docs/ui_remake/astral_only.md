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

## Legit-page controls with no matching Vesta feature

These Astral controls have no equivalent in Vesta's current combat settings and should not be ported as dead toggles: per-weapon selection (Vesta exposes global and weapon-group profiles, not individual weapon profiles); backtrack and its maximum-time setting; maximum aim time; target-swap delay; trigger overshoot and amount; spread adjustment, mode, and limited option; trigger hitbox extension; Passive/Aggressive assist mode; mouse redirection and strength; and aim-assist Quick stop (Vesta's triggerbot Auto Stop is a separate behavior).

The mapping CSV records the other Legit controls as direct matches or decision-required candidates. In particular, Astral has seven `LegitWeapon w[group]` profiles, one per group; its weapon selector is UI state and does not select a separate per-weapon settings object. Vesta stores global settings and weapon-group overrides. Decisions are also required for different group taxonomies, boolean wall penetration versus Vesta's three wall policies, Astral's extra Feet hitbox, and the documented range/unit/scale differences; no conversion is assumed. These rows are an audit only: the Legit page has not been ported.

# Test storage isolation — compile checkpoint

Only the `ui-remake-test` worktree is changed. No push or remote repository was created.

## Exact paths

- Test worktree: `/tmp/hoplite/workspace-ui-test` (the sandbox path, not a Windows path).
- Windows build output: `<your ui-remake-test checkout>\build\bin\pasta.exe`.
- Data root: `%TEMP%\vesta_uitest`.
- Live config: `%TEMP%\vesta_uitest\configs\current.cfg`.
- Default named configs: `%TEMP%\vesta_uitest\configs\named\*.cfg`.
- Logs, crash journal, spray logs and exported diagnostic report: `%TEMP%\vesta_uitest\logs`.
- Report file: `%TEMP%\vesta_uitest\logs\vesta-report.txt` (no Desktop overwrite).
- Cache: `%TEMP%\vesta_uitest\cache`; mesh cache: `cache\chams` beneath the data root.
- Nav and Walkbot recordings continue using `nav` and `walkbot` beneath the isolated data root.
- Research logs: `%TEMP%\vesta_uitest\logs\research`.
- Default seed shot trace output: `%TEMP%\vesta_uitest\logs\research\shots`.
- Named-folder preference: `HKCU\Software\VestaUITest\ConfigFolder`.

## Isolation behavior

Both runtime resolution and legacy migration use the test root. Startup does not import the production
`%TEMP%\vesta` cache or reuse the production named-folder registry value. Existing config JSON keys,
format markers and feature defaults are unchanged. The journal uses its own writer mutex.

A shared path check rejects named-config folder selections, config reads/writes/deletes/imports, and
explicit diagnostic destinations outside the test root. It compares canonical path components
case-insensitively rather than using a string prefix, so `vesta_uitest_other` is not inside the test root.
The resolver refuses a pre-existing test-root junction/symlink redirected elsewhere; default logs use
that same resolver rather than bypassing the check. Copies to import must first be placed inside `%TEMP%\vesta_uitest`.

Tracing now uses `VESTA_UITEST_SEED_TRACE=1` and optional `VESTA_UITEST_SEED_TRACE_DIR`;
the original `VESTA_SEED_TRACE` variables are ignored. The optional directory AND its parent must
be within the test root, because config/schema/scene dumps and the STOP file use that parent.
Command-line diagnostic report output paths must likewise be absolute and within the test root.
This is application-level path isolation, not an OS sandbox against deliberate filesystem races/hard links.

All ordinary config load/save/list/create/rename/duplicate/delete/import operations share the config
library and the runtime storage root. No separate config-export widget was found during the preliminary
audit; final inventory must distinguish an existing operation from a requested new control.
ImGui INI persistence is disabled by the existing overlay host.

## Compile and manual checks

Build ONLY target `vesta` in your separate test checkout; do not build/run `pasta_loader`, which remains
an unrelated production updater. Do not copy this executable over your working installation.

1. Configure/build the test checkout using the existing Windows presets. Send compiler errors before
   the next stage. No MSVC installation/emulation or build was attempted here, per your instruction.
2. With the app closed, copy (do not move) your old config to the live path above, or put a named
   copy under `configs\named` to load through Configs. Keep the originals untouched.
3. Verify test config changes persist only under the test root; test all Configs operations on copies.
4. Verify selecting/importing outside the test root is refused and copied files inside it still work.
5. Check Logs/Open and Diagnostic Report/Save go to test logs; verify traces if you use them.
6. Check the original cache, named configs, registry preference and logs have not changed.

Static checks performed: diff whitespace; storage-root/old trace-env searches; new helper declaration,
definition and include/caller checks; original checkout status/diff; reference exclusion.
Windows compilation and runtime path behavior remain unverified until your check.

## Files and changed line locations

Locations below are for this isolation patch, before later menu work. Header/comment and displayed
path updates are included. This is the compile-checkpoint touched-source-file list.

- `src/app/diagnostic_reports.cpp` — lines 10, 144
- `src/app/main.cpp` — lines 32
- `src/app/trace_session.cpp` — lines 12, 34, 38
- `src/config/library.cpp` — lines 41, 79, 219, 231, 278, 291, 329, 348, 364
- `src/config/misc.hpp` — lines 170
- `src/config/storage.hpp` — lines 11
- `src/diagnostics/crash_record.hpp` — lines 11
- `src/diagnostics/journal.hpp` — lines 15, 18
- `src/diagnostics/log_directory.cpp` — lines 2, 8, 11
- `src/diagnostics/log_directory.hpp` — lines 6
- `src/platform/windows/data_migration.cpp` — lines 229, 252
- `src/platform/windows/data_paths.cpp` — lines 56, 71, 92
- `src/platform/windows/data_paths.hpp` — lines 28, 31
- `src/platform/windows/user_settings.cpp` — lines 3, 11, 31, 45
- `src/platform/windows/user_settings.hpp` — lines 9
- `src/render/text/localization.cpp` — lines 118, 120
- `src/simulation/shot_trace.cpp` — lines 6, 694, 699, 705, 711, 714, 742
- `src/ui/menu/guide_page.cpp` — lines 393
- `src/ui/menu/misc_page.cpp` — lines 89, 131

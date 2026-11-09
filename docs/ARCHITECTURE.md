# Architecture

A map of the source tree for contributors. [Contributing](../CONTRIBUTING.md) covers
building and testing.

## Processes

| Program | Source | Role |
| --- | --- | --- |
| `omakade` | `src/app/main.cpp`, `qml/` | The window: library, details, settings, Game Mode and the in-game guide host |
| `omakade-sessiond` | `src/sessiond/main.cpp` | User service that records play sessions and hosts the resident in-game guide and highlight recorder, with or without the window open |
| `omakade-guide-button` | `src/guidebutton/` | User service that reads controllers for the Home button and runs the Game Mode shortcut |
| `omakade-guide-guard` | `src/guide/guard.cpp` | Pauses a game while the in-game guide is open and resumes it if Omakade exits or crashes |
| Omarchy shell plugin | `omarchy-plugin/` | The in-game guide card drawn by the Omarchy shell; see its [README](../omarchy-plugin/README.md) |

All of them run as the desktop user. Omakade reads launcher data and never writes it.
Its own data lives in `~/.local/share/omakade/` (`library.sqlite3` holds the library
cache, organization and play history), settings in `~/.config/omakade/config.toml`, and
downloaded art in `~/.cache/omakade/`. Credentials go to Secret Service.

## `src/`

| Directory | Owns |
| --- | --- |
| `app/` | Start-up and wiring in `main.cpp`, settings (`AppSettings`), single-instance commands, card export, the idle inhibitor, and the UI self-test harness |
| `sources/` | One folder per launcher or emulator. A scanner finds its roots and reads the launcher's files into plain records; caching and the UI live in `library/` |
| `library/` | One model per source (`*GameModel`), the merged `UnifiedGameModel`, filtering and sorting (`LibraryFilterModel`), consoles, Home, stats, Repair Library and the library database |
| `launch/` | Building and running the launch command for every source, launch setups, Steam URLs and RetroArch's home |
| `metadata/` | Game identity and matching, IGDB, ProtonDB and regional titles |
| `artwork/` | Cover image provider and readers for covers inside Switch, Wii U and TGA files |
| `achievements/` | Steam, Xbox (through Xodus Nexus) and RetroAchievements |
| `tracking/` | Play session recording shared with `omakade-sessiond`: process matching, Hyprland windows, the session database, Discord status, stopping games |
| `highlights/` | The replay buffer, clips and Steam achievement clips |
| `gamemode/` | Entering and leaving Game Mode, display and audio changes, launch hold and watch, the Game Mode shortcut |
| `guide/` | The in-game guide: payloads, input, pausing and the plugin connection |
| `guidebutton/` | Controller Home button detection for `omakade-guide-button` |
| `input/` | SDL controller input, focus and the couch cursor |
| `saves/` | Emulator save protection and backups |
| `backup/` | Library backup archives, restore and recovery |
| `streaming/` | Sunshine app list export |
| `theme/` | Reading the active Omarchy theme |

The build splits these into static libraries in `CMakeLists.txt`. `omakade_tracking`
holds the recording code shared with the recorder, `omakade_guide` and
`omakade_guide_runtime` hold the guide button and guide, and `omakade_core` holds
the rest. The `omakade` executable adds `main.cpp`, the QML module and the bundled
images.

## `qml/`

- `Main.qml` is the window. It owns navigation between screens, the library toolbar
  and its menus, and keyboard and controller routing.
- `screens/` holds full-page views: game details, Home, Stats, the editors
  (artwork, backup, bulk organization, manual games, saved filters) and the restore
  start-up screen.
- `components/` holds the pieces they share: cards, the desktop and Game Mode library
  views, Settings, Repair Library, launch setup, menus and themed controls.
  `UiMetrics.qml` and `TextEntry.qml` are singletons.

C++ objects reach QML as context properties set in `src/app/main.cpp`, such as
`Library`, `Preferences`, `Launcher` and one `<Source>Library` per source. The colours
come from `Theme`, which follows the Omarchy theme.

## Other folders

| Folder | Holds |
| --- | --- |
| `tests/` | Qt Test suites (`*Tests.cpp`), Python tests (`*Tests.py`) and fixtures, all registered in `tests/CMakeLists.txt` |
| `resources/` | Icons, console art, the native ports catalog and the recorder's process profiles |
| `packaging/` | Desktop entry, AppStream metadata, systemd user units and the Arch `PKGBUILD` template |
| `tools/` | CI helpers, audit programs and guide preview tools |
| `scripts/` | SBOM generation and the native ports catalog updater |
| `skills/` | The optional TV gaming agent skill |
| `docs/` | User guides and this map |

## Tests

`ctest` runs every suite. Most source parsing, models and storage are covered in
`tests/CoreTests.cpp`, which builds launcher folders in a temporary directory and
scans them. Larger features have their own executables, such as `GameModeTests`,
`BackupRecoveryTests`, `SaveSetTests` and `XodusTests`.

UI behaviour is tested by running `omakade` itself with a test flag, for example
`omakade --couch-navigation-test` or `--smoke-test`. The test registrations set
`QT_QPA_PLATFORM=offscreen` and the software Qt Quick backend, so the suite runs
without a display, as it does in CI. Python tests cover Game Mode start-up, the guide recorder, the TV launcher
helper, the native ports catalog and the SBOM generator.

## Add a launcher source

Use an existing source of the same kind as the model. Faugus is a small launcher
source; Kyty and native ports reuse the generic `LauncherGameModel`.

1. **Scanner.** Add `src/sources/<name>/<Name>Scanner.{h,cpp}` with a function that
   returns the native and Flatpak roots, and a `scan` function that reads them into
   records with roots, warnings and an `incomplete` flag. Keep it read-only, limit
   the size of every file it reads, and report a root that could not be read as
   incomplete rather than as empty.
2. **Model.** Add `src/library/<Name>GameModel` or a `LauncherGameModel::Definition`.
   Scan on a worker thread, cache the games in the library database, keep the cached
   list when a scan is incomplete, and expose `statusText`, `errorText`,
   `detectedPaths` and `lastScan` for Settings.
3. **Build.** List the new files in `omakade_core` in `CMakeLists.txt`.
4. **Wiring.** In `src/app/main.cpp`, create the model, add it to `UnifiedGameModel`,
   apply its enabled setting, set its context property and schedule its start-up scan.
   Add the `<name>Enabled` setting to `AppSettings`.
5. **Launching.** Add the launch route to `GameLauncher`, and its process names to
   `GameStopService` and to the launcher list in `GameModeDesktop` if it runs its own
   client.
6. **UI.** Add the source to the Settings source list and its on/off and rescan
   handlers in `SettingsPanel.qml`, to the **Sources** menu in `Main.qml`, to the
   Game Mode source list in `CouchLibraryView.qml`, and to **Manage in** in
   `GameDetails.qml` if the launcher can be opened.
7. **Backup.** Add its cache table and setting to `BackupSnapshot`, `BackupDatabase`
   and `BackupArchive` so backups and restores carry it.
8. **Play time.** If `omakade-sessiond` should recognize its games, add a profile to
   `resources/sessiond-profiles.json`.
9. **Tests.** Add a fixture and scanner and model tests to `tests/CoreTests.cpp`:
   native and Flatpak roots, a missing root, a malformed file, and launch commands.
10. **Docs.** Update the source list in the README, [Guide](GUIDE.md),
    [Compatibility](COMPATIBILITY.md), the paths table in
    [Troubleshooting](TROUBLESHOOTING.md), the bug report template, and
    [Privacy](../PRIVACY.md) if it reads anything new or contacts a server.

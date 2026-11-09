# Architecture

A map of the source tree for contributors. [Contributing](../CONTRIBUTING.md) covers
building and testing.

## Processes

| Program | Source | Role |
| --- | --- | --- |
| `omakade` | `src/app/`, `qml/` | The window: library, details, settings, Game Mode and the in-game guide host |
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
| `app/` | Start-up and wiring in `OmakadeApp.cpp`, settings (`AppSettings`), single-instance commands, card export, the idle inhibitor, and the self-tests not yet moved to `selftest/` |
| `selftest/` | `omakade-uitest`, the app the UI tests run: fixtures, render paths and self-tests, which the shipped `omakade` leaves out |
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
the rest. `omakade_app` holds the window's wiring (`runOmakade` in `OmakadeApp.cpp`), the
QML module and the bundled images. The `omakade` executable is `omakade_app` and
`main.cpp`. With testing on, `omakade-uitest` links the same `omakade_app` and adds
`src/selftest/`, which `runOmakade` calls at each point where a fixture, render path or
self-test changes what it builds. It is never installed.

## `qml/`

- `Main.qml` is the window. It owns navigation between screens, the library toolbar
  and its menus, and keyboard and controller routing.
- `screens/` holds full-page views: game details, Home, Stats, the editors
  (artwork, backup, bulk organization, manual games, saved filters) and the restore
  start-up screen.
- `components/` holds the pieces they share: cards, the desktop and Game Mode library
  views, Settings, Repair Library, launch setup, menus and themed controls.
  `UiMetrics.qml` and `TextEntry.qml` are singletons.

C++ objects reach QML as context properties set in `src/app/OmakadeApp.cpp`, such as
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

UI behaviour is tested by running the app with a test flag. Most UI tests run
`omakade-uitest`, the app plus `src/selftest/`, for example
`omakade-uitest --render-overlay=settings --render-screenshot=settings.png` or
`omakade-uitest --backup-editor-test`. A few still run `omakade` itself, such as
`omakade --startup-navigation-test`, and `omakade --smoke-test` checks the shipped
binary. Only `omakade-uitest` reads `--render-size`, so a render run at a set
window size needs it rather than `omakade`. The test registrations set `QT_QPA_PLATFORM=offscreen` and the software Qt
Quick backend, so the suite runs without a display, as it does in CI. Python tests cover Game Mode start-up, the guide recorder, the TV launcher
helper, the native ports catalog and the SBOM generator.

### In-game guide

Run graphical guide checks in an isolated test desktop such as omabox, with the
matching binaries and plugin. Link `omarchy-plugin/` into that desktop's plugin
directory and enable it there. `tools/guide-i1/` seeds a private library and a
fullscreen test game; `tools/guide-i2/latency.py` sends synthetic controller events
through the native translator. Fixtures in `tools/guide-overlay-preview/` cover
layout and unavailable states. These preview tools are only in a git checkout;
the release source tarball leaves them out.

Unit tests cover pause ownership and recovery, parked identities, payload deltas,
real-reading validation, input arbitration, process termination and plugin setup.
A box covers shell IPC, focus, pause signals, capture exclusion and rendering.
Physical controller delivery, grabs, hidraw/Steam Input, audio, UPower and actual
recording encoders require their corresponding devices and services.

### Game Mode on real hardware

`tools/game-mode-acceptance.sh` exercises Hyprland, notifications, and a private
PipeWire server with virtual displays and null sound outputs. It does not prove
physical display behavior, audible sound, or real game behavior. The following
checks can also be driven automatically on a real machine; record physical
observations separately:

1. One display: start Game Mode with no game, navigate to details and scroll, then
   toggle to desktop and back three times. The same page, selection, focus and scroll
   return; the previous desktop workspace and window layout return on each park.
   End Game Mode explicitly and verify the journal and temporary owner are cleared.
2. A second display that is normally on: Game Mode opens there, and leaving restores the
   workspace it was showing and returns focus to the first display.
3. A display that is disabled in `monitors.lua`: it turns on, and turns off when leaving.
4. A chosen sound output: game audio plays there, and the previous output returns.
5. Launch a Steam game, an emulator and one Heroic or Lutris game. Each opens on the
   Game Mode display. Return and resume with the shortcut, then return and quit the
   game while parked. The library session remains and resumes without dead game/audio
   records. Stop Games and Leave still asks for confirmation and ends the session.
6. Kill Omakade during Game Mode, then start it again. The display, sound and
   notifications are put back.
7. Unplug or disable the display during Game Mode. Game Mode ends and sound returns.
8. Controller Home button, with Steam open and Steam Input on: with no game a short
   press starts and ends Game Mode in place, and over a game it opens and closes the
   guide. A long hold starts, returns and resumes Game Mode on the Game Mode display.
   A Home button chord does nothing, and the game and Steam still receive ordinary input.
9. With the display powered off, start Game Mode. If Hyprland cannot enable its
   output, it reports the failure and rolls back. If the display still advertises
   an active connection, Game Mode may start; verify this separately from the
   automated timeout test.
10. With a TV and its sound output chosen, press F11 on the desk display. Game Mode opens
    on the desk display, the TV stays off, sound and notifications do not change, and
    LEAVE with no game running returns to the desktop library. Start a game there and
    let it idle: the screen stays awake. Browse without a game: the screen may sleep.
11. Start a Moonlight stream of Omakade. Game Mode opens on the streamed display and the
    stream's sound keeps working. Ending the stream closes Omakade and clears the session.

## In-game guide protocol

The initial `omarchy-shell shell summon omakade.guide JSON` payload contains
version 1, output, pad, scale, backend socket/token and data. Data includes the
game title, source, pause state, known session time, art, achievements and
available frame readings. Achievement items contain title, description,
unlocked, when, icon, rarity and hidden; secret descriptions remain absent until
unlocked.

After opening, newline-delimited socket messages keep the shape
`{type: "update", payload: {...}}`. `payload.delta: true` marks changed fields in
`data`: absent fields retain their previous value, null removes a field, arrays
replace as a whole. Unchanged payloads send nothing. Static achievements and art
are sent on open and again only when their content changes. The integrated
plugin merges a delta before validating the complete version 1 data.

The user-only socket accepts `{version: 1, token, action, value}`. Controller input
and updates share that ordered connection. Lifecycle commands are serialized;
an open-only heartbeat closes ownership if the shell disappears. Test event
injection is available only with `--guide-input-test`.

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
4. **Wiring.** In `src/app/OmakadeApp.cpp`, create the model, add it to `UnifiedGameModel`,
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

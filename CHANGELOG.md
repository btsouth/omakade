# Changelog

## 1.16.1

- Record clip in the in-game guide now uses its own recorder. A screen recording you
  start from Omarchy keeps running on its own: the guide no longer shows it as its
  clip or stops it, and Omarchy's stop leaves the guide's clip alone.
- The in-game guide shows GPU load and temperature on NVIDIA cards too.
- Controller navigation is fixed across the app. A press could move two steps, which made
  some buttons and Settings sections hard or impossible to reach. Every screen is now
  tested from every control in every direction, in couch and desktop layouts.
- Home: the top buttons stay on one row, Down moves straight between shelves, and X adds
  a game to Up next. Going back up to the top buttons shows the top of the page.
- Settings: Back from Settings opened on Home returns to Home. Sliders change only with
  Left and Right, dropdowns no longer trap the controller, and Left from a page goes to
  the section list.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.16.1)

## 1.16.0

Press Home or Super + Ctrl + G over a running game to open the in-game guide, a
panel at the edge of the screen in your Omarchy theme. The game pauses while it is
open. Resume, take a screenshot or record a clip, change the volume or sound output,
check performance, or quit the game.

- **Desktop** (in Game Mode) takes you to the desktop with the game hidden and muted.
  Press Home to come back to the game, paused, with the guide open.
- Hold Home for half a second to go to the desktop and back without the guide, as
  Home did in 1.15.
- RetroArch started by Omakade: Home opens only the guide. Click both sticks
  (L3 + R3) for RetroArch's own menu. Your `retroarch.cfg` is left as it was.
- The press that closes the guide never reaches the game.
- Game Mode opens straight onto its own workspace at full size, and the library
  stays behind your game.
- Prefer the old Home button? Set **Home button in games** to **Game Mode** in
  Settings. The guide comes with the package; `omarchy plugin disable
  omakade.guide` turns it off.

Fixes

- Sunshine apps exported by Omakade launch from Moonlight again.
- Heroic libraries with an empty Epic or Amazon cache no longer block GOG imports.
- OmaStore installs record playtime.
- x86_64 packages are built against Omarchy stable, so they start on stable, RC and edge.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.16.0) ·
[Guide documentation](https://github.com/btsouth/omakade/blob/v1.16.0/docs/GAME-GUIDE.md)

## 1.15.0

Press the Xbox, PlayStation or Home button to start Game Mode, return to the desktop
and resume.

- New **Controller Home button** setting in Settings → Controls, off by default. A short
  press on any controller does what Super + Ctrl + G does, from inside a game or with
  Omakade closed.
- It runs as an optional user service, `omakade-guide-button`, that only reads
  controllers. Nothing is grabbed, remapped or emulated, and Steam Input keeps working.
- Long holds, chords with other buttons or triggers, the press that switches a
  controller on and duplicate reports from Steam Input's pad are ignored. Presses while the screen is
  locked are ignored.
- With Steam open, turn off "Guide button focuses Steam" and "Enable Guide Button
  Chords for controllers" in Steam's controller settings.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.15.0) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.15.0/docs/GAME-MODE.md)

## 1.14.2

Game Mode remembers your place. Return to the desktop, then resume your library
or running game with the same shortcut.

- Super + Ctrl + G returns to the desktop and resumes the complete Game Mode session,
  with or without a game. Details, selection, focus and scroll stay in place until
  End Game Mode, Stop Games and Leave, or closing Omakade.
- Running games stay on their workspace with attributable game audio muted while away.
  A game ending releases its game/audio records and keeps the library session available.
- Prepare the Game Mode layout and fullscreen state before showing the window,
  avoiding the windowed-to-fullscreen flash when entering or resuming.
- Keep Resume available during background session checks and prevent late focus
  requests from pulling you away from the restored desktop.
- Restore the desktop even when game audio recovery fails, retaining the recovery
  record for a retry.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.2) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.2/docs/GAME-MODE.md)

## 1.14.1

Fixes the Game Mode shortcut leaving a new Omakade window on your desktop.
When the shortcut launches Omakade, pressing it again leaves Game Mode and closes
Omakade after restoring the desktop. An already-open window stays open in its
previous layout.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.1) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.1/docs/GAME-MODE.md)

## 1.14.0

Game Mode brings Omakade to the couch. Choose a display and sound output, then
start from Settings or `omakade --game-mode`. Omakade opens fullscreen, silences
notifications and keeps the screen awake. Leaving restores the settings it changed.

- Add Super + Ctrl + G from Settings with one click. The same key leaves Game Mode,
  or opens the Game Mode controls over a game that is still running.
- Return Omakade to the exact spot it held in a tiled layout.
- Return to your library, leave games running, or confirm Stop Games and Leave. A game
  left running comes back to your desktop.
- Keep controls responsive with large libraries and when an emulator hangs.
- Recover session settings after an unexpected exit.
- Find setup instructions in the shorter README and new feature guide.
- Sort installed games first, then alphabetically, and dim uninstalled covers in
  desktop and Couch Mode. Other sort choices remain available.
- Import owned Epic, GOG and Amazon games from Heroic, including uninstalled
  titles. Heroic handles installation and launch; refresh its libraries before rescanning.
- Keep ROM filename details accessible when metadata and descriptions are missing.
- Add `layer-shell-qt` as a required dependency for the in-game overlay and
  declare `libpulse` as optional for Game Mode sound switching.
- Show wide box art whole on the details page, name consoles in full, and keep
  setup hints and playtime bookkeeping off the TV.

Game Mode switches desktop audio as a whole. Gamescope, HDR and per-game audio
routing are outside this release.

[Install Omakade](https://github.com/btsouth/omakade/releases/tag/v1.14.0) ·
[Game Mode guide](https://github.com/btsouth/omakade/blob/v1.14.0/docs/GAME-MODE.md)

## 1.13.0

Clearer browsing, library repair and save relocation, with controller and keyboard
navigation checks built into the test suite.

- Rework Home, Library, Details and Settings layouts. Keep actions reachable in
  narrow windows, theme native controls, and use shared spacing and the Omarchy font.
- Make Couch navigation follow the visible rows. Up and Down reach adjacent toolbar
  rows, including wrapped filters, and return between games and their entry control.
  Search, Filters and Details restore focus when closed. Tab and Shift+Tab follow
  the visible controls and games.
- Give keyboard and controller input usable focus on a cold launch, including
  empty and delayed libraries. Opening Omakade no longer needs a mouse click first.
  Clear Filters also clears search stored in the library model.
- Add five navigation contracts covering 4,320 directional links and 16 cold-start
  cases to normal CI. Check keyboard, D-pad and analog input through production
  controller polling, both Couch layouts, empty results and wrapped rows.
- Match Nintendo controller actions to their button prompts. Show Stop Game only
  for attributable running targets and repair focus when actions change.
- Improve Stats Overview, Play patterns and Library snapshot, with clearer labels
  for recorded periods and imported lifetime totals.
- Clarify repair, artwork, launch setup and save actions. Evaluate library review
  availability in the background and keep review and undo controls navigable.
  Recheck superseded background work, group disconnected folders correctly, and
  keep relocation dialog focus inside its visible controls. Removing a search
  chip clears Couch searches too; relocation accepts real filenames containing `#`.
- Copy verified save backups when relocating a game's path. Preserve the original
  backups and copies on undo, reuse existing verified copies on repeated repairs,
  and refuse destination conflicts, incomplete recovery or unverified save mappings.
- Fix Home shelf scrolling and a cached-artwork geometry binding loop.

## 1.12.0

Play history and local statistics, safer game stopping, durable recording, and new Nintendo DS,
PlayStation 3, and PSP sources.

### Play history and statistics

- Open Stats for recorded play, top games, hourly patterns, streaks, achievements,
  and backlog habits. Export a local PNG card with the recording period clearly labelled.
  Imported lifetime totals stay separate. Desktop and Couch Mode are supported.
- See running sessions on Home, browse older history, and delete finished sessions
  with confirmation. Deletion preserves imported playtime and later recorded play.
- Stop one game or all attributable games. Previews explain shared Wine/Flatpak scopes;
  idle installations and documents open in editors are excluded. Recognize Steam Proton
  games whose process paths use Steam's container drive, and stop their verified prefix
  processes when no system wineserver is installed. Wait briefly for a forced process to
  exit before reporting the result.
- Record supported file-picker loads on Hyprland. Optionally pause recording when the
  emulator loses focus, or show the current game through Discord Rich Presence.
- Fix imported/recorded playtime overlap, recovery timestamps, stale title matching,
  Discord reconnects, and partial history deletion on storage failure.
- Keep Stats totals and charts consistent across midnight, New Year and DST. Paused
  historical sessions use labelled timing estimates. Storage errors prevent card export.
- Remember the selected Stats period, count linked installations consistently, and fit
  longer notes inside the exported card. Headless demo exports require an explicit fixture flag.

### Recording reliability and emulator support

- Make refused session writes durable. Sessions get a stable identity before their first
  write, and an insert, close or active-session checkpoint the database refuses is journaled
  to a bounded file beside the database and replayed, both after a recorder restart and
  during ordinary polling. Replay is idempotent, so a crash between the database commit and
  the journal acknowledgment cannot double a session. A torn final append keeps the verified
  prefix, and a partial header is repaired rather than swallowing the next record. A replayed
  checkpoint keeps its observation time as the heartbeat and never lowers recorded progress.
  A game-wide clear, a single-session deletion and a database restore each invalidate only the
  records they should, checked in the same transaction as the write, and a temporary database
  read failure retries instead of discarding. An unresolved identity remains retryable under
  a durable journal-owner binding, while Replace restore invalidates and removes prior
  journal work. A recovered session whose game has exited or changed is closed at the last
  observation, and a surviving same-game process is adopted under one stable key. A failed
  final close remains retryable, and checkpoint replacement reports write, flush, sync,
  rename and directory-persistence failures instead of a false acknowledgement. Repeated
  checkpoints replace rather than fill the journal, and a full or damaged journal is shown in
  recorder status instead of dropping accepted records. This replaces the earlier in-memory
  retry limitation.

- Preserve artwork when a Steam app has no published capsule. Metadata keys with their embedded
  source/runner/app-id separators are now decoded losslessly when the library reopens, and a
  Steam preload whose capsule request fails falls back to its identified IGDB cover. A real
  Steam capsule still takes priority whenever one exists.

- Attribute a game Dolphin loaded from its own file picker. Dolphin rewrites a playtime file
  while emulation runs, keyed by disc id, so it is read for a live Dolphin process and a game
  whose total has advanced is recorded against the right game. A total that has not advanced
  attributes nothing, a disc id the library does not know or one that matches two games is
  refused rather than guessed at, and two games advancing between two polls is refused for that
  poll. A record that stops advancing keeps the game it last confirmed instead of ending the
  session, which is what a paused game looks like. A session first attributed from Dolphin's
  window title ends when the record takes over, so the same play is never billed twice. Evidence
  from a command line still wins over the record, and stop actions still use verified command
  line evidence only. No other supported emulator writes a record that proves the loaded game,
  so their file-picker loads keep the window-title path.

- Add native melonDS discovery, launching, recording, title-index and save protection for
  Nintendo DS and DSi dumps. Omakade scans the ROM folders the user marked as DS, reads the
  same game-code and header fields melonDS reads, groups the games under a Nintendo DS console
  entry, and launches one ROM as a structured argument. The default battery save beside the ROM
  and a `SaveFilePath` override from `melonDS.toml` are protected as a shared ROM-folder save
  set; save states and relocated `<state>.sav` files are not treated as in-game saves. The
  native source was accepted with the MIT-licensed DS-Craft beta 1.7.1 homebrew release under
  melonDS 1.1. The Flatpak route launched the same homebrew and closed one 14-second session.
  melonDS still has no current-session evidence for a game loaded inside its own picker.

- Add RPCS3 discovery, PS3 grouping, launching, recording profile, title-index support and
  PARAM.SFO-aware per-title save resolution. Omakade reads `games.yml`, installed
  `dev_hdd0/game` directories, the `vfs.yml` auto-detection folder and explicit PS3 ROM
  folders, validates categories rather than importing updates or save data, and uses the
  verified `rpcs3 --no-gui <path>`/Flatpak argv. The save resolver reads each candidate save
  directory's `PARAM.SFO` instead of trusting a title-id prefix. Native execution was accepted
  after installing supplied PS3 firmware 4.93 in the isolated RPCS3 root: the GPL-2.0 iPSX3
  homebrew booted as `IPSX30001` and closed one 79-second session.

- Add PPSSPP discovery, PSP grouping, launching, recording and save protection. Omakade reads
  PARAM.SFO from ISO and PBP images, derives `DISC_ID`, `DISC_VERSION` and region, accepts
  homebrew ELF entries with a stable path identity, and scans explicit PSP folders plus the
  bounded Recent and PinnedPaths entries in `ppsspp.ini`. The native source was accepted with
  the MIT-licensed 2048PSP release under PPSSPP 1.20.4 and closed one 59-second session over a
  60-second wall span. A real-title run with `God of War: Ghost of Sparta` (`NPUG80508`) under
  PPSSPP 1.20.4 closed one 79-second attributed session. Save protection selects only
  `PSP/SAVEDATA` folders prefixed by the disc ID, keeps the shared-container warning, and never
  treats `PPSSPP_STATE` files as game saves.

- Read PCSX2's live `logs/emulog.txt` to identify the serial of the game actually loaded, including
  titles started inside PCSX2's own file picker. The adapter tracks appended data, handles log
  truncation and disc changes, keeps the last confirmed game while the log is quiet, and refuses
  an unknown serial instead of falling back to the previous game. A real `Black (USA)` run
  identified `SLUS-21376` and closed one 75-second attributed session. Recorder Settings now
  states which sources have verified live-game evidence and which remain command-line/title only.

- Persist observed activity intervals for each session, including pause spans, through the
  database and the durable recovery journal. Existing sessions retain aggregate provenance; new
  interval-backed sessions are marked observed. The existing proportional Stats allocator is
  intentionally unchanged in 1.12.

## 1.10.0

Xenia (Xbox 360) support and a TV setup for the couch.

- Xbox 360 games from Xenia Canary: discovery from Xenia's recent-games list and storage
  root, save backup, playtime recording, and launching through the `xenia_canary` binary.
  Thanks to @Salt-555 for the Xenia source work.
- Xbox 360 titles identified with IGDB and shown under their own console card, including
  names Xenia writes with trademark marks.
- Xenia pinned to X11 on Wayland, where its window otherwise stayed grey while audio kept
  playing.
- An optional TV gaming agent skill with a Gamescope launcher that checks the TV output,
  workspace, and audio sink before starting a game. It stays inert unless you install it.
  Thanks to @LucasOl1337 for the guide and helper.

## 1.9.2

- Add an optional ProtonDB tier badge on library cards, off by default. Turn on both
  community reports and card badges in Settings to see the tier beside playtime and
  rating. Fixes #39.

## 1.9.1

This patch fixes RomM catalog refresh for large libraries.

- Ask RomM to omit the result-set index and filter data it repeats on every page
  by default, which is what pushed large catalogs past the response limit.
- Raise the whole-refresh total while keeping each page bounded in memory, so
  libraries with tens of thousands of entries finish loading. A failed refresh
  still keeps the existing offline catalog.

Fixes #46.

## 1.9.0

- Connect an optional read-only RomM library with secure credentials, locally mounted
  games, and a catalog that remains available offline.
- Save per-installation emulator/core choices, inspect launch diagnostics, and repair
  missing paths. Keep explicit choices consistent across launch entry points.
- Protect supported emulator saves before launch, including memory cards and clock data.
  Restore and undo with shared-storage warnings and interrupted-restore recovery.
- Review save coverage, configure custom file layouts and retention, and preview backup
  cleanup. Refresh the selected backup list after automatic capture.
- Work through a persistent library repair queue with source/reason filters, selected
  retries, and separate identity and artwork undo.
- Improve ROM identification and artwork recovery while preserving manual choices,
  editions, and edits in searches. Prevent cached cover loading loops.
- Keep archive entries on RetroArch and report missing archives, cores, or runtimes
  without silently replacing a selected setup.
- Add optional ProtonDB community reports in Steam game details, with report counts
  and cache dates. Keep library captions uncluttered and launching independent of the service.
- Recognize Cemu `.wua` games in session recording.
- Clarify empty library views, source errors, and repository versus direct package installs.

## 1.8.0

- Add optional Home, persistent Up Next, and suggestions from the local library.
  Improve Home wheel scrolling during background metadata updates.
  Put game shelves before shortcuts and add direct Play beside Details.
- Filter by genre, decade, and platform, including saved filters.
- Show regional release dates, title evidence, genres, credits, and descriptions.
  Preserve manual identity choices and leave ambiguous matches correctable.
- Bring matching and cover selection together under Game & Artwork. Preserve
  existing portraits during refresh and cache maintenance, and recover covers
  through verified aliases. Keep Done visible while the panel scrolls.
- Prevent overlapping desktop library captions after returning from a game.
  Keep existing cards stable during unchanged startup console scans.
- Improve popup keyboard navigation, controller focus, and narrow details layouts.
  Restore the original Home action after closing details. Show immediate launch
  feedback, suppress repeated presses briefly, and keep launch errors visible.
  Limit the rating-count tooltip to the rating and put credits before regional details.
- Add optional local session recording for configured emulator process profiles.
  Show recorder status and separate imported time from recorded time. New installs
  opt in; existing recording preferences and history are preserved.
  Attribution requires a recognizable game path in process arguments. Internal
  emulator game changes and wrapper handoffs still need adapter-specific testing.
- Back up explicit metadata choices, recorded sessions, baselines, and preferences
  in archive format 2. Format 1 remains readable. Emulator saves are excluded.
- Report persistence failures and protect referenced artwork during cache cleanup.
- Keep ROM Folders on the Sources overview. Fixes #40.

## 1.7.0

Omakade 1.7 brings console libraries, more ways to organize your games, and a
reworked controller keyboard.

### Console libraries

- Discover Dolphin, Cemu, and shadPS4 games alongside existing sources. Launch
  Dreamcast games through Flycast and scan ROM folders and EmuDeck layouts.
- Browse console cards or individual games, pin games outside their console
  card, and choose a layout for each system.
- Read titles and icons from supported Switch dumps using locally installed
  keys, and artwork from Wii U archives. Filter out Switch updates and DLC.

### Artwork and settings

- Identify games with IGDB, display ratings, and sort by rating or popularity.
  Matching handles common region, revision, and translation tags in ROM names.
- Add optional SteamGridDB portrait covers, or choose your own cover, hero,
  and logo images. Each artwork slot can be reset separately.
- Browse settings by Sources, Library, Connections, Controls & streaming, and
  About & storage. Adjust desktop and couch cover sizes independently.

### Organization and backups

- Add native games and desktop entries with custom arguments and a working
  directory. Removing an entry leaves its game files alone.
- Choose a preferred installation for linked games and add extra GOG folders.
- Bulk-edit games, save named filters, and pick a random game from your results.
- Export personal library settings and artwork to a local backup. Preview,
  merge, or replace data, with recovery and undo for interrupted restores.
- Preserve console pins and favorite/hidden choices for the new emulator sources
  in backups.

### Controller and reliability fixes

- Use a QWERTY on-screen keyboard with A to select, B to cancel, X to delete,
  Y for space, and Start to finish. Confirm stays on the bottom face button,
  including controllers connected in Switch mode.
- Reach search and text fields with a controller, keep keyboard input inside
  its dialog, and return to the field after editing.
- Keep focus on Show, Source, and View while changing them. Source cycles through
  choices; Filters opens the full filter panel.
- Fix details-page navigation back to the title, status alignment, clipped
  collection controls, and narrow-window editor and settings layouts.
- Fix disappearing filtered libraries and preserve cached Dolphin and Cemu games
  when their configuration cannot be read.
- Track manually added games through launch and exit. Request Wayland idle
  inhibition while a tracked game process is running.
- Handle unavailable crypto when reading Switch artwork.

### Upgrading

Existing IGDB credentials carry over. SteamGridDB requires its own optional API
key in Settings → Connections. Use Update Ratings & Portraits to fetch metadata.

Backups containing the new console-pin data may not open in older Omakade
versions. Keep an older backup if you plan to downgrade.

## 1.6.1

- Launch, manage, and install Steam games through the Steam client itself,
  native first and then Flatpak, and only fall back to the desktop `steam://`
  URL handler when neither is available. Steam packages that register no
  handler sent Play to the web browser. Thanks @radiohost-cloud for the report
  and the Apple Silicon test.
- Stop matching the Omakade desktop entry when searching for "Steam" or
  "RetroArch" in the app launcher. Thanks @gmickel for the report.
- Remember the library sort order between launches.
- Show every game's cover at the same compact size on the details screen
  instead of letting portrait covers render larger than landscape ones.
- Share QML role-name definitions across nine game models without changing
  their role IDs, names, or behavior.

## 1.6.0

### Couch Mode

- Browse your library in detail or grid view with controller navigation,
  search, filters, and an on-screen keyboard.
- Open Couch Mode with F11, controller Start, or `omakade --couch`. Sunshine
  sessions open it automatically, and you can make it your startup view.
- Hold the stick or directional pad to move through games. Selection stays on
  the same game when switching layouts.
- Use a controller throughout Settings and game organization, including text
  entry, case, and symbols.
- Let games keep controller focus after launching; ignore controller input while
  Omakade is in the background.
- Hide the cursor during keyboard and controller navigation and restore it
  when the mouse moves.

### GOG and compatibility

- Discover and launch direct GOG installations. Native Linux games launch
  directly; Windows games use UMU with a separate prefix for each game.
- Keep Heroic-managed GOG games launching through Heroic with their existing
  settings.
- Remove uninstalled direct GOG games on rescan and preserve cached entries when
  a Heroic GOG inventory cannot be read.
- Fix Proton detection and launching for Omarchy Battle.net prefixes. Thanks
  @TheAirick for the report.
- Add aarch64 packages alongside x86_64, with checksums and provenance.

## 1.5.0

### RetroAchievements

- Added optional RetroAchievements support for RetroArch games, including
  compatible ROM hashing, achievement progress, unlock details, rarity, and
  account-aware caching.
- Kept network, hashing, and database work off the interface thread and handled
  sign-out, stale data, unsupported systems, and malformed responses safely.

### Battle.net

- Added Battle.net as a library source. Omakade finds the Windows Battle.net
  client in Wine, Proton, and Bottles prefixes, imports installed games from
  `product.db`, and launches them through Battle.net.
- Downloads missing Battle.net covers and banners from Lutris's public artwork
  hosts, including Heroes of the Storm.

### PCSX2 and Ryujinx

- Added PCSX2 as a game source: imports disc-based games from the current
  gamelist cache (v34) for native and Flatpak installs, with cover
  art, playtime, last-played, and region metadata, and delegated launching
  through the owning PCSX2 install. Sources are discovered automatically and
  appear once the emulator is detected.
- Added Ryujinx as a game source: discovers XCI, NSP, and NRO games from the
  configured game directories for native and Flatpak installs, with custom
  titles, playtime, and last-played metadata, and delegated launching.
- Added per-source filter chips, status rows, and rescan controls for both
  emulators in Settings.

### Steam

- Imported non-Steam shortcuts from `shortcuts.vdf`, including Wine/Proton
  games added to Steam, and launched them with the 64-bit shortcut ID Steam
  expects.
- Kept cached shortcuts available when `shortcuts.vdf` is temporarily
  unreadable.

### Interface and reliability

- Improved game-details layouts across narrow, standard, and ultrawide windows,
  including cover sizing, action widths, and the insights grid.
- Fixed keyboard and controller movement between Play, Favorite, Manage, and
  Hide in both two-column and four-column layouts.
- Preserved cached launcher games when an optional source is unavailable and
  expanded automated coverage for the new integrations and navigation paths.
- Updated project, support, download, and package links after the repository
  account rename.

Thanks to @karem505 for PCSX2 and Ryujinx, @HowieDuhzit for
RetroAchievements, @Nitemaeric for Battle.net, @Aweiward for Steam non-Steam
shortcuts, and @jeanmrx1 for the responsive game-details improvements.

## 1.4.0

### Sunshine and Moonlight

- Added optional Sunshine app export for Omakade and individual installed games, including
  cover art, while preserving existing Sunshine apps and keeping a one-time backup.
- Added a Restart Sunshine action in Settings.
- Added `omakade --play Source:runner:id` and `omakade --quit` for Sunshine app entries and
  other integrations.
- Used the installed Omakade executable for native Sunshine entries and waited for a fresh
  library scan when a game starts before the cache is ready.
- Opened Omakade fullscreen on Sunshine's streamed display for Moonlight sessions and used
  each game's normal launcher.

### Library and organization

- Replaced the Status, Collection, and Tag filter cycles with picker lists that open on the
  current value and work with keyboard, mouse, and controller.
- Kept the grid and details on the correct game during unchanged Steam rescans, filtered
  edits, and cover changes.
- Preserved unfinished text in tags and credential fields when background refreshes finish.
- Reported private Steam profiles correctly and showed scanning state for every library
  source.

### Navigation and interface

- Made keyboard arrows use the same spatial navigation as controllers in game details,
  Settings, and dialogs, without taking arrow keys from text fields.
- Moved focus to Clear Filters when the last visible game leaves a filtered grid and returned
  focus to the collection button when its editor closes.
- Kept focus in place when the window is reactivated and added accessible names to text fields.
- Rendered titles as plain text, kept toasts inside the window, shortened long card subtitles,
  and avoided unnecessary cover reloads while resizing.

### Performance and reliability

- Sped up linked-game searches, Steam artwork scans, controller detection, and theme updates.
- Pruned unused covers first, retried covers removed by the cache limit, and remembered IGDB
  misses instead of requesting them repeatedly.
- Delayed keyring access until credentials are configured and reduced unnecessary database
  and cover work during unchanged scans.
- Kept running when the single-instance socket is unavailable and restored normal SIGTERM,
  logout, and service-stop behavior.

## 1.3.0

### Steam

- Added optional owned Steam library sync, installed and ready-to-install
  filters, and Steam installation handoff.
- Loads owned-game covers as they enter the visible library instead of fetching
  an entire account at once.
- Kept the Steam library when a configured library path is missing or a manifest is
  unreadable instead of showing an empty or frozen library.
- Skipped unusable entries and Steam tools during owned-library sync instead of failing
  the whole sync.
- Remembered games without Steam achievements instead of re-requesting them on every
  visit, and reported that state plainly.
- Required a 17-digit Steam ID, reported when Steam is still busy, and stopped
  re-requesting covers Steam does not have.

### Heroic, RetroArch, and Lutris

- Added games sideloaded into Heroic, plus Heroic playtime and last-played activity.
- Resolved the RetroArch Flatpak's sandbox paths so its playlists, thumbnails, and
  playtime logs are found, and matched playtime logs by the core's short name and
  archived content name.
- Ignored a leftover Lutris database whose native or Flatpak launcher is no longer
  installed, and checked Flatpak launchers without blocking the interface.

### Navigation and library

- Added controller navigation across library modes, source filters,
  organization controls, Settings, and game details.
- Moved keyboard and controller Up from the top row of games into the filters and
  toolbar, with arrow keys between those controls and Down back into the grid.
- Kept detail-page controller movement in content order below collections.
- Added controller and keyboard scrolling through Steam achievement cards.
- Kept the highlighted card and the open game details on the same game when a
  background rescan rebuilds the library.
- Named the search or organization filter behind an empty library and offered a Clear
  Filters action, and made the Collection and Tag filters say how to create the first
  one instead of doing nothing.
- Added a visible whole-library Rescan action.
- Made Escape close the new-collection editor before closing game details.
- Made Return, Enter, and the controller confirm button press every button on desktops
  whose Qt platform theme does not map them.
- Clarified that automatic closing after launch is an opt-in setting.

### Fixes and housekeeping

- Fixed narrow game-details layouts and prerelease owned-library cache upgrades.
- Reused the IGDB access token within a session and declared the Qt SVG and image
  format plugins the package needs.
- Prevented space-separated screenshot options from creating an invisible main instance.
- Pinned third-party workflow actions and enabled monthly dependency updates.
- Added end-to-end navigation coverage and owned-library regression tests.

Thanks to @destx0 for the stale Steam library fix, and to @8uff3r, @bscott, and
@Zedster07 for the reports that shaped this release.

## 1.2.3

- Restored controller navigation on game details.
- Kept controller focus inside the game grid at the top library row.
- Added an end-to-end controller navigation test for the library and details.

## 1.2.2

- Fixed controller Up and Down navigation in the game library.
- Made controller focus movement follow the actual screen direction on details
  and overlay screens.
- Fixed absent launchers reporting database errors in Settings.
- Reflowed Settings actions so they remain visible in tiled windows.

## 1.2.1

- Improved keyboard and controller navigation across game details, settings,
  filters, and dialogs.
- Preserved favorites and hidden state when launcher games disappear and return.
- Moved Lutris, Heroic, and Faugus scans off the interface thread.
- Fixed stale Steam achievement rows and controller repeat after disconnecting.
- Added stricter limits for artwork, achievement caches, and Steam metadata parsing.

## 1.2.0

- Added native and Flatpak RetroArch playlist discovery and launch delegation.
- Added RetroArch box art, screenshots, core names, playtime, and recent activity.
- Added safe handling for archived ROM paths and missing core associations.
- Added RetroArch source filters, diagnostics, and settings.

## 1.1.1

- Refreshes stale Steam achievements automatically when game details open.
- Hides the new collection field behind a compact action.
- Moves achievement sorting into the Achievements header.
- Uses the selected Steam installation for linked-game achievements and insights.

## 1.1.0

- Added native and Flatpak Faugus library discovery, artwork, playtime, filters, and settings.
- Added safe launch delegation and management handoff to Faugus.
- Preferred high-resolution Steam hero artwork over small store headers.
- Fixed missing achievement icons from Steam's legacy image CDN.
- Added achievement sorting by status or unlock date.
- Increased smooth mouse-wheel travel and added proportional touchpad scrolling.
- Added project and issue links to Settings.

## 1.0.2

- Fixed Steam Web API keys being rejected by the wrong API host.
- Added a visible Settings button to the library header.
- Made mouse-wheel library scrolling smooth and row-based.

## 1.0.1

- Added a distinct Omakade launcher icon and matching in-app brand mark.
- Made the library scrollbar larger and easier to drag with a mouse.
- Added a persistent accent outline to the selected game card.
- Improved the new-collection layout and clarified Twitch setup for IGDB.
- Hid unavailable game-insight metrics and reflowed the remaining cards.
- Reserved scroll gutters so tiled layouts never place content under a scrollbar.

## 1.0.0

- Added persistent launch activity across Steam, Lutris, and Heroic.
- Made Recently Played sort by exact activity time instead of a yes or no flag.
- Added optional IGDB critic aggregates and game-length estimates with an
  offline cache and Secret Service credential storage.
- Added custom collections, tags, completion states, card badges, and smart
  organization filters.
- Added runtime source controls with persisted scan times, detected locations,
  and per-source errors.
- Added stale-install checks, Flatpak launcher verification, actionable launch
  errors, and ProtonDB and PCGamingWiki links.
- Added an opt-in close-after-launch setting and visible version diagnostics.

## 0.5.0

- Added Steam, Lutris, and Heroic libraries in one view.
- Added explicit, reversible linking for duplicate installations.
- Added a source selector so every linked installation remains launchable.
- Added user-selected cover artwork with reset support.
- Made window transparency follow the active Omarchy shell theme.
- Changed game cards to open with one click.

## 0.4.0

- Added Heroic support for installed Epic, GOG, and Amazon games.
- Added local Heroic artwork and native or Flatpak launch delegation.

## 0.3.0

- Added Lutris installed-game import and launch delegation.

## 0.2.0

- Added local and connected Steam achievements.

## 0.1.0

- Added the first Steam library preview.

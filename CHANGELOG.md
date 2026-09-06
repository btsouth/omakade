# Changelog

## 1.7.0

Omakade brings console libraries together with your PC games, with simpler settings
and more control over how your library looks.

### Emulators and console libraries

- Add Dolphin for GameCube and Wii, Cemu for Wii U, and shadPS4 for dumped PS4
  games. Dreamcast dumps can launch through Flycast.
- Group console games behind one card per system, or show every game in the
  library. Switch views from the desktop or Couch Mode toolbar. Set per-system
  overrides and pin individual games outside their console card.
- Scan ROM folders and EmuDeck layouts without requiring RetroArch playlists.
- Selecting a source with one console opens its games directly. Sources with
  multiple systems keep console grouping.
- Filter all emulators with Emulated, or combine individual source filters.
  Couch Mode Browse now opens on sources.
- Read titles and icons from Switch dumps using Ryujinx's installed keys, and
  icons from Wii U archives. Skip Switch updates and DLC as separate games.

### Ratings, covers, and settings

- Identify games with IGDB and sort the library by rating or popularity. Ratings
  appear on library cards beside the source and the playtime.
- Match ROM dumps automatically. Region, revision, release and translation tags
  are understood, so a shelf of tagged dumps identifies itself instead of asking
  you to confirm every game. What cannot be matched confidently is counted at the
  end and can be corrected from game details.
- Add optional SteamGridDB portrait covers using the same game identification,
  for Switch, Wii U, PS4 and PC games. Retro consoles keep their own box art
  instead. Choose another portrait manually and keep artwork available offline.
- Replace the long settings sheet with Sources, Library, Connections,
  Controls & streaming, and About & storage.
- Adjust cover size, with separate desktop and Couch Mode preferences. Restore
  centered Couch Mode grids across cover sizes and window dimensions.
- Add console cover artwork and improve handling of portrait, square, and wide art.

### Library organization and backups

- Add native games and desktop entries manually, with editable launch arguments.
- Choose a preferred installation for linked games and add extra GOG folders.
- Customize covers, hero images, and logos with independent resets.
- Organize several games at once and save named library filters.
- Pick a game from the current results, then decide whether to play it.
- Back up personal library choices, settings, and artwork. Preview and merge or
  replace a backup, with recovery if restoring is interrupted.
- Use the new controls in Desktop and Couch Mode, including controller text entry.

### Fixes and improvements

- Keep the screensaver and lock screen away while a launched game runs. Controllers
  do not count as input for the compositor's idle timer, and unlike Steam most
  emulators never say they are busy, so Omakade holds a Wayland idle inhibitor
  from launch until the game process exits.
- Fix the library disappearing after scrolling a filtered source list.
- Keep other sources out of console views and reduce stalls when opening large
  ROM collections. Cover downloads no longer rebuild the whole library grid.
- Improve source selection, focus indicators, toolbar navigation, and console
  back navigation with a controller.

Existing IGDB credentials carry over. SteamGridDB covers require a separate API
key in Settings → Connections. Choose Update Ratings & Portraits to fetch metadata;
these connections are optional.

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

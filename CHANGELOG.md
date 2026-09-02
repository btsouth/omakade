# Changelog

## Unreleased

### Battle.net

- Added Battle.net as a library source. Omakade finds the Windows Battle.net
  client in Wine, Proton, and Bottles prefixes, imports installed games from
  `product.db`, and launches them through Battle.net.
- Downloads missing Battle.net covers and banners from Lutris's public artwork
  hosts, including Heroes of the Storm.

## 1.3.1

- Left the library, database, and cover requests alone when a Steam rescan finds the same
  games, so Steam downloads no longer reset the grid every few seconds.
- Reported a private Steam profile as private instead of as an invalid API key.
- Fixed the details page acting on a different game after un-favoriting or changing a
  cover while a filter was active.
- Kept typed tags, Steam ID, and IGDB client ID text when a background refresh finished
  mid-edit.
- Moved keyboard arrows through game details, Settings, and dialogs the way controller
  directions already did.
- Gave keyboard focus a landing spot when the last visible game leaves the grid, and
  returned focus to the collection button when its editor closes.
- Rendered game and achievement titles as plain text so markup-looking names cannot change
  formatting.
- Kept toasts inside the window and above the settings panel, elided long card subtitles,
  hid the empty year separator, and stopped covers reloading on every window resize step.
- Ran normally when the single-instance socket cannot be created instead of exiting
  without a window.
- Stopped walking the achievement icon cache every time game details open.
- Let SIGTERM, logout, and service stops close Omakade; SDL had been swallowing the signals.

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

# Changelog

## Unreleased

- Added owned Steam games to the library through the Steam Web API, marked
  NOT INSTALLED and installable through Steam.
- Added an INSTALLED ONLY setting to hide games that are owned but not installed.

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

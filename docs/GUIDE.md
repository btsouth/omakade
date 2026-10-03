# Omakade guide

How each source is discovered and how the main features work. The
[README](../README.md) covers installation and first steps.

- [Sources](#sources)
- [Consoles and emulators](#consoles-and-emulators)
- [Include uninstalled Heroic games](#include-uninstalled-heroic-games)
- [Include uninstalled Steam games](#include-uninstalled-steam-games)
- [Non-Steam games, Proton, and Steam Input](#non-steam-games-proton-and-steam-input)
- [Add a native game manually](#add-a-native-game-manually)
- [Keyboard and Couch Mode](#keyboard-and-couch-mode)
- [Game Mode](#game-mode)
- [Organize several games at once](#organize-several-games-at-once)
- [Save a library view](#save-a-library-view)
- [Pick something to play](#pick-something-to-play)
- [Customize game artwork](#customize-game-artwork)
- [Back up your library](#back-up-your-library)
- [Track play sessions](#track-play-sessions)
- [Stream with Sunshine and Moonlight](#stream-with-sunshine-and-moonlight)
- [Use a dedicated TV alongside agent work](#use-a-dedicated-tv-alongside-agent-work)
- [ARM64](#arm64)

## Sources

Omakade reads launcher data without modifying it. The owning launchers remain
responsible for games, accounts, updates, cloud saves, DRM, and compatibility tools.

Direct GOG discovery checks `~/GOG Games`, `~/Games/GOG`, `~/Games/Heroic`,
and immediate game folders under `~/Games`. In Settings, use **Extra GOG Folders**
to add library folders by path or through the desktop folder picker. Saved folders
are scanned alongside the standard locations. `OMAKADE_GOG_LIBRARY_PATHS` remains
an additive, colon-separated list of extra roots. Missing folders keep their
cached games while available folders refresh. Removing a saved folder does not
delete its game files or personal library choices. Native Linux builds
launch directly; Windows game builds run on Linux through `umu-run` with an
isolated per-game prefix. Omakade itself does not run on Windows.
GOG games installed through Heroic continue to launch through Heroic.

Battle.net games come from the Battle.net Agent database inside a Wine, Proton,
or Bottles prefix. Omakade launches each title through that prefix's Battle.net
client. Wine, umu-launcher, or Bottles must be installed to play.

## Consoles and emulators

Emulated games can sit behind one console card per system (Super Nintendo,
Nintendo 64, Sega Dreamcast, and so on) instead of filling the library. Each
system's layout is chosen in Settings → Library, and any game can be
pinned to the main library from its details page. ROM folders without a
RetroArch playlist are scanned when they follow the EmuDeck layout
(`~/Emulation/roms`, `/data/Emulation/Games`) or are added under ROM Folders in
Settings → Sources. Switch dumps show the icon and name stored inside the NSP or XCI when
Ryujinx's `prod.keys` is present; Wii U `.wua` archives and PS4 dumps carry
their own icons. GameCube and Wii discs come from Dolphin's game folders or a
`GameCube`/`Wii` folder, launch through Dolphin's batch mode, and take their
covers from Dolphin's cache or GameTDB.

Nintendo DS games come from ROM folders marked as DS. Omakade reads the game code and title
from the ROM header melonDS reads, groups the games under Nintendo DS, and launches the native
`melonDS` binary or the Flatpak app. The battery save beside the ROM and a configured
`SaveFilePath` can be protected; save states are kept separate.

PlayStation 3 games come from RPCS3's installed-game index, its automatic-disc folder, or ROM
folders marked as PS3. Omakade reads `PARAM.SFO` for identity and category, rejects updates,
save data and media entries, and protects only save directories whose own `PARAM.SFO` identifies
the selected title.

PlayStation Portable games come from PPSSPP's remembered paths or ROM folders marked as PSP.
Omakade reads `DISC_ID`, `DISC_VERSION` and the title from PARAM.SFO inside ISO and PBP images,
supports homebrew ELF entries with a stable path identity, and protects matching folders in the
shared `PSP/SAVEDATA` container without including save states.

Xbox 360 games come from Xenia Canary. Omakade reads Xenia's recent-games list and
scans its storage root for `default.xex` dumps and ISO, XEX, or ZAR images, then
launches each title through the `xenia_canary` binary on your PATH.

RetroArch games come from its configured playlists. Omakade uses local
RetroArch thumbnails and runtime logs, then launches each game with its assigned
core. Entries without a core association remain visible and explain how to fix
launching after you press Play.

## Include uninstalled Heroic games

Omakade reads the Epic, GOG, and Amazon libraries cached by Heroic, including
owned games that are not installed. Refresh your library in Heroic first, then
rescan Omakade and choose **All Games** or **Ready to Install** in the filters.
Installed games remain the default view. Choose **Open in Heroic** on an
uninstalled game to install it there, then rescan Omakade.

Native and Flatpak Heroic libraries are supported. Omakade does not sign in to
stores or refresh Heroic's account libraries itself. Missing or unreadable caches
keep previously imported owned games available until a complete rescan.

## Include uninstalled Steam games

Omakade shows installed games by default. To include the rest of your Steam
library, open Settings, save your Steam ID and Web API key, then select **Sync
owned Steam library**. Your Steam Game Details must be public. After syncing,
use **All Games** or **Ready to Install** in the library. Installation is handed
off to Steam. With **All Games**, choose **Sort: Installed** to group installed
games first, then sort each group by title. Sources that do not report install
state are treated as installed. Uninstalled covers are dimmed in the desktop
library and both Couch layouts, while the selected card keeps its focus border.

## Non-Steam games, Proton, and Steam Input

Games added to Steam as non-Steam shortcuts appear in Omakade's Steam source.
Omakade launches those shortcuts through Steam, leaving their compatibility
and controller settings with Steam. Configure and test the shortcut in Steam
first, then refresh Omakade and launch its Steam entry. If linked to another
installation, choose the Steam installation or make it the default.

A game launched through Heroic, Lutris, Faugus, or the Manual source uses that
source's launch path. Omakade does not automatically route it through Steam or
choose Steam's Proton. Steam Input and overlay behavior still depend on Steam,
the shortcut, and the game; importing a shortcut does not verify controller
compatibility. Omakade does not create or modify Steam shortcuts.

See [Steam's shortcut instructions](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC).

## Add a native game manually

Open Settings and choose **Add a Game**. Select a native executable or `.desktop`
file, review its title, executable, working folder, and arguments, then save.
Each argument has its own field, including arguments containing spaces. Omakade
stores a copy of the launch details; importing does not modify the desktop entry.
Use **Edit Manual Game** in details to repair paths or remove the library entry.
Removing it never deletes the game files. Manual games support the existing
artwork, favorites, tags, collections, linking, launch history, and streaming flows.

Couch Mode supports typing and editing launch details with the on-screen keyboard;
file browsing is available in Desktop Mode. Desktop entries requiring a terminal
are not supported. Windows games should be configured in Steam, Heroic, Lutris,
or Faugus so those applications retain responsibility for their runners.

## Keyboard and Couch Mode

Use `Ctrl+F` to search, arrow keys to navigate, Enter to open details, Escape
to return, and F11 to enter or leave Couch Mode. The controller Start button
does the same. Couch Mode offers detail and grid library views and remembers
the preferred launch mode. Its cursor hides during controller or keyboard use,
returns on mouse movement, and remains visible in Desktop Mode. `Ctrl+M`
toggles reduced motion and `Ctrl+D` opens settings and source diagnostics.

## Game Mode

Choose a display and sound output in **Settings → Controls**, then select
**Start Game Mode**. `omakade --game-mode` also starts it, even when Omakade is
already open. Game Mode uses Couch Mode on its own workspace, silences notifications,
and keeps the screen awake. Leaving restores the changes it made.

Start, F11, and the Couch Leave button open the Game Mode controls. Choose
**Leave Game Mode** to return, or run `omakade --game-mode-exit`. Sound switching
affects the whole desktop. Gamescope and HDR remain launcher settings.

See [Game Mode](GAME-MODE.md) for disabled displays, recovery, and limitations.

## Organize several games at once

Open **Organize** above the desktop library or in Couch Mode's **Browse** panel.
Select individual games or choose **Select Results** for the current filtered
library. Actions explicitly favorite, unfavorite, hide, unhide, set completion
status, add/remove tags, or add/remove collection membership for the selection.
Adding to a new collection creates it. Other tags and collections are retained.

Each batch is atomic: a failed write changes none of the selected games.
Selection follows game identities through sorting and rescans; if a selected
entry disappears, clear and reselect before applying changes. A successful
batch clears selection. **Clear** or **Done** cancels the selection without edits.
Bulk choices persist locally and also apply to linked installations. Tag limits
are 20 per game and 32 characters per tag.

## Save a library view

Open **Saved Filters** above the desktop library or from Couch Mode's **Browse**
panel. Enter a name and choose **Save Current** to keep the current search,
source, availability, favorites/recent/hidden mode, status, collection, tag, and
sort order. Select a saved view to rename or delete it, or choose **Apply**.
Deletion requires confirmation and leaves your current filters unchanged.

Saved filters are dynamic queries, so newly matching games appear automatically.
If a referenced collection or tag is missing, Omakade keeps that criterion and
shows a message instead of silently broadening the results. Recreating it makes
the view usable again. Saved views stay local and persist across restarts.

## Pick something to play

Choose **Pick a Game** above the desktop library or in Couch Mode's **Browse**
panel. Omakade picks an available game from the current results and opens its
details for review. **Pick Another** chooses again; **Play** launches only when
you choose it. Search, source, favorites, status, collection, and tag filters
still apply. Linked installations count as one game, and the last choice is
avoided when another eligible game exists.

## Customize game artwork

Open a game's details and choose **Artwork** to change its cover, wide hero
background, or logo. Select a PNG, JPEG, or WebP file up to 32 MB. Transparent
logos are supported. Each image is copied into Omakade's data folder, so moving
the original or clearing downloaded caches does not remove your choice.
Use **Reset** beside an artwork type to restore its provider fallback without
changing the other images. Desktop Mode includes a file picker; Couch Mode can
enter an image path with the on-screen keyboard.

## Back up your library

Open **Settings → Backup & Restore**. **Save As** writes a local
`.omakade-backup` archive of your favorites, hidden choices, organization, links,
preferred installations, manual entries, custom artwork, saved filters, launch
activity, and core preferences. Game files, launcher databases, account
credentials, and downloaded caches are excluded.

Use **Open** to preview a backup, or enter a path and choose **Preview Path**.
The preview lists counts, preferences, missing paths, and saved-filter name
conflicts. **Merge** imports matching choices and keeps unrelated personal data.
**Replace** clears personal library choices before importing, while keeping game
files and cached source records. Both require confirmation. Close and reopen
Omakade to apply the queued restore; it saves a recovery copy before changing
anything. An interrupted restore can finish or be undone before the library opens.

Account-service identifiers and Sunshine publishing choices stay local. Missing
manual paths remain repairable, and disconnected game identities stay stored.
Restoring never launches imported games. In Couch Mode, confirm the path field
to use the on-screen keyboard, and use the arrows to scroll the preview.

## Track play sessions

Every emulator keeps its own playtime in its own format, and some keep none at
all. Omakade ships an optional recorder. Turn on **Record Playtime** in Settings,
then enable its service:

```bash
systemctl --user enable --now omakade-sessiond
```

The recorder watches supported emulator processes. A recognizable game path on the
command line identifies a session; on Hyprland, a confident match against a known game
window title also covers file-picker loads. Profiles include RetroArch, Dolphin, PCSX2,
Cemu, Ryujinx, shadPS4, Xenia, and yuzu-family forks such as Eden. Wrapper handoffs still
need adapter-specific validation; profile coverage is not runtime acceptance.

Imported emulator counters and recorded time are reconciled without counting known
overlap twice. Recovery preserves committed time and excludes unobserved downtime.
Existing history is not rewritten automatically. Settings can optionally pause recording
when the emulator loses focus on Hyprland; an emulator's own pause screen still counts
while it remains focused.

New installations require opting in. Existing saved choices are preserved, and
older configuration files without this setting retain their previous enabled
default. Settings reports whether the recorder is running separately from whether
recording is enabled. The recorder continues after Omakade closes. Switching recording
off preserves history and displays imported time.
Game details separates imported emulator time from Omakade's recorded total and provides
paged history with confirmed deletion. Stats remembers the chosen period and exports a
local Year in Review image. Imported lifetime totals remain separate from dated recordings.
Optional Discord presence is off by default; see [Privacy](../PRIVACY.md) for what it shares.

File-picker loads can be recorded through a known window title on Hyprland or
Dolphin's live playtime record. Other internal loads may remain unattributed. See
the [recording coverage notes](RECORDING-COVERAGE.md) for validation limits.

## Stream with Sunshine and Moonlight

Omarchy installs Sunshine from its menu and ships Moonlight. Once Sunshine is
running, open Omakade's Settings and enable **Omakade in Moonlight** to add
Omakade to Sunshine's app list next to Steam Big Picture, or **One app per
installed game** to add every installed game with its cover. Sunshine reads the
list when it starts, so press **Restart Sunshine** after a change. Omakade
leaves the other Sunshine apps alone and keeps a one-time backup next to
`apps.json`.

Starting Omakade from Moonlight opens it in Couch Mode on the streamed display.
Starting a game from Moonlight launches it through its own launcher, the same
as pressing Play. The same entry points work from a terminal or a keybinding:

```bash
omakade --play Steam::620          # Source:runner:id, the runner is often empty
omakade --play Heroic:legendary:Sugar
omakade --couch                     # Start directly in Couch Mode
omakade --quit
```

## Use a dedicated TV alongside agent work

The optional [TV gaming skill](../skills/omakade-tv-gaming/SKILL.md) guides coding
agents through reserving a TV workspace, preserving work displays and input,
routing game audio, and verifying session shutdown. It includes a configurable
Gamescope launcher that checks the TV and audio sink before opening a game.
It is a setup guide and helper, not a built-in TV session manager.

Packages install it under `/usr/share/omakade/skills/omakade-tv-gaming`. Add a
symlink in the skill directory your agent discovers, or copy the folder if you
want to customize it independently of package updates. For Codex, for example:

```sh
mkdir -p ~/.codex/skills
ln -s /usr/share/omakade/skills/omakade-tv-gaming ~/.codex/skills/omakade-tv-gaming
```

From a source checkout, use the absolute path to `skills/omakade-tv-gaming`
instead. For agents using a shared skill hub, link it there and add a pointer
to that agent's local instructions: when setting up, starting or ending a TV
gaming session, or performing desktop input during a game, read this skill.
Installing Omakade does not automatically modify agent configuration.

The helper's optional dependencies and machine setup are described in the
[session guide](../skills/omakade-tv-gaming/references/session.md). Workspaces
separate windows; CPU, RAM and GPU resources remain shared with desktop work.

## ARM64

ARM64 packages pass automated build and lifecycle checks. Testing reported in
[issue #13](https://github.com/btsouth/omakade/issues/13) confirmed installation,
discovery, and Couch Mode on Apple Silicon with Asahi Linux. The `fex-steam`
wrapper that provides `/usr/bin/steam` can still fail to start games from any
`steam://` request, including Steam's own client. That is a wrapper limitation,
not something Omakade can work around; see issue #13 for details and workarounds.

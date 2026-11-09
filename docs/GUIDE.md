# Omakade guide

How each source is discovered and how the main features work. The
[README](../README.md) covers installation and first steps.

Menu paths use the labels shown in the app. **Settings** has these categories:
Sources, Library & launching, Appearance, Controls, Highlights, Connections,
Streaming, Backup & storage, and About & help. Above the desktop library, **More**
holds the library actions. In the Game Mode library, **Filters** holds Organize,
Saved Filters and Pick a Game.

## User guides

- [Game Mode](GAME-MODE.md): fullscreen play, a dedicated gaming display, and recovery
- [In-game guide](GAME-GUIDE.md): the overlay that opens over a running game
- [Highlights](HIGHLIGHTS.md): save clips of the last moments of a game
- [Emulator save protection](SAVE-SETS.md): automatic save copies and restores
- [Native ports](NATIVE-PORTS.md): native PC ports of console games
- [Compatibility](COMPATIBILITY.md): what each source supports, and test reports
- [Troubleshooting](TROUBLESHOOTING.md): missing games, sources that are not found,
  launch errors, covers, and logs

## In this guide

- [Sources](#sources)
- [Consoles and emulators](#consoles-and-emulators)
- [Kyty and native ports](#kyty-and-native-ports)
- [RomM](#romm)
- [Include uninstalled Heroic games](#include-uninstalled-heroic-games)
- [Include uninstalled Steam games](#include-uninstalled-steam-games)
- [Non-Steam games, Proton, and Steam Input](#non-steam-games-proton-and-steam-input)
- [Add a native game manually](#add-a-native-game-manually)
- [Home and Up next](#home-and-up-next)
- [Keyboard and Game Mode](#keyboard-and-game-mode)
- [Game Mode](#game-mode)
- [Organize several games at once](#organize-several-games-at-once)
- [Save a library view](#save-a-library-view)
- [Pick something to play](#pick-something-to-play)
- [Customize game artwork](#customize-game-artwork)
- [Connections and achievements](#connections-and-achievements)
- [Back up your library](#back-up-your-library)
- [Track play sessions](#track-play-sessions)
- [Stats and Year in Review](#stats-and-year-in-review)
- [Stream with Sunshine and Moonlight](#stream-with-sunshine-and-moonlight)
- [Command line](#command-line)
- [Use a dedicated TV alongside agent work](#use-a-dedicated-tv-alongside-agent-work)
- [ARM64](#arm64)

## Sources

Omakade reads launcher data without modifying it. The owning launchers remain
responsible for games, accounts, updates, cloud saves, DRM, and compatibility tools.

Direct GOG discovery checks `~/GOG Games`, `~/Games/GOG`, `~/Games/Heroic`,
and immediate game folders under `~/Games`. In **Settings → Sources**, use
**Extra GOG Folders** to add library folders by path or through the desktop folder
picker. Saved folders are scanned alongside the standard locations. `OMAKADE_GOG_LIBRARY_PATHS` remains
an additive, colon-separated list of extra roots. Missing folders keep their
cached games while available folders refresh. Removing a saved folder does not
delete its game files or personal library choices. Native Linux builds
launch directly; Windows game builds run on Linux through `umu-run` with an
isolated per-game prefix. Omakade itself does not run on Windows.
GOG games installed through Heroic continue to launch through Heroic.

Battle.net games come from the Battle.net Agent database inside a Wine, Proton,
or Bottles prefix. Omakade launches each title through that prefix's Battle.net
client. Wine, umu-launcher, or Bottles must be installed to play.

Microsoft Store and PC Game Pass games come from
[Xodus Nexus](https://github.com/domsvoboda/xodus-nexus). Omakade lists the games
Nexus shows as ready to play, with posters and banners from the Microsoft Store, and
starts them through Nexus's own shortcut, so sign-in, licences and the runtime stay with
Nexus. Open Nexus once after installing or updating it so that shortcut exists. Whether
a game runs, and online play for games with anti-cheat, depends on Nexus; see its
compatibility list.

## Consoles and emulators

Emulated games can sit behind one console card per system (Super Nintendo,
Nintendo 64, Sega Dreamcast, and so on) instead of filling the library. Each
system's layout is chosen under **Console Layout** in **Settings → Library &
launching**, and a single game can also appear in the main library with **Manage →
Show Beside Console** on its details page. ROM folders without a RetroArch playlist
are scanned when they follow the EmuDeck layout (`~/Emulation/roms`,
`/data/Emulation/Games`) or are added under **ROM Folders** in **Settings → Sources**. Switch dumps show the icon and name stored inside the NSP or XCI when
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

## Kyty and native ports

Kyty and Native Ports appear in **Settings → Sources** when detected. You can turn
either source off or rescan it there. Kyty reads `Kyty.ini` game folders, the usual PS5 folders
(`~/Emulation/roms/ps5`, `~/Emulation/Games/PS5`, `/data/Emulation/Games/PS5`),
and dumps named by your desktop launchers. It reads titles and artwork from
`sce_sys/param.json` and `sce_sys`, and prefers your matching per-game launcher
so tuned settings stay in use. Without a wrapper, it starts `kyty_emulator`
with the dump and fullscreen arguments. Omakade does not install Kyty or change
its configuration.

Native Ports reads application entries and AppImages in the usual folders,
matching them against a bundled catalog. Games such as SpartanPort, bbport,
OpenCE, GoldenEye and Lost Odyssey appear under their original console. When a
matching dump is already in the library, the port joins it as another
installation on one card and is preferred for a new pair. You can choose a
different installation or unlink the card; an unlinked pair stays separate.

For a dump with a known port you have not installed, game details offers a link
to the project's repository. Windows-only projects are labelled. Omakade does
not download ports or game files, and a link does not promise compatibility.
The catalog is curated and does not cover every project. See
[Native ports](NATIVE-PORTS.md) for discovery paths and catalog maintenance.

## RomM

Omakade can list the games on a [RomM](https://github.com/rommapp/romm) server and
launch them with your local emulators. It never downloads or changes files on the
server, so the game files must already be on this computer, for example through a
network share or a synced folder.

1. In RomM, create a Client API Token with `roms.read` access.
2. Open **Settings → Connections → RomM Library**.
3. Enter the server address, choose the local folder that holds the same files as
   RomM's library folder, paste the token, and choose **Save and Connect**.

RomM games then appear under the **RomM** source filter. They launch through the
emulator that matches each platform, with the same launch setups, save protection
and play recording as other emulated games. **Test / Refresh** reloads the catalog,
and the last catalog stays available while the server is offline. **Disconnect**
stops requests and keeps the token; **Forget Token** removes it too. The token is
kept in Secret Service; see [Privacy](../PRIVACY.md#optional-romm-library).

Use an `https` address when the server supports it. Below the address, Settings shows
whether the connection is encrypted. A plain `http` address on your local network
connects without asking; for any other `http` server, Omakade asks before it sends
the token unencrypted.

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
library, open **Settings → Connections → Steam Library Information**, save your
Steam ID and Web API key, then select **Sync Owned Steam Library**. Your Steam Game Details must be public. After syncing,
use **All Games** or **Ready to Install** under **Filters → Availability**. Installation is handed
off to Steam. With **All Games**, choose **Sort: Installed** to group installed
games first, then sort each group by title. Sources that do not report install
state are treated as installed. Uninstalled covers are dimmed in the desktop
library and both Game Mode layouts, while the selected card keeps its focus border.

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

Choose **More → Add a Game** above the library, or **Add a Game** in **Settings →
Sources**. Select a native executable or `.desktop` file with **Choose File**,
review its title, executable, working folder, and arguments, then choose **Save Game**.
Each argument has its own field, including arguments containing spaces. Omakade
stores a copy of the launch details; importing does not modify the desktop entry.
Use **Manage → Edit Manual Game** in details to repair paths or remove the library entry.
Removing it never deletes the game files. Manual games support the existing
artwork, favorites, tags, collections, linking, launch history, and streaming flows.

Game Mode supports typing and editing launch details with the on-screen keyboard;
file browsing is available on the desktop. Desktop entries requiring a terminal
are not supported. Windows games should be configured in Steam, Heroic, Lutris,
or Faugus so those applications retain responsibility for their runners.

## Home and Up next

**Home** gathers what you are likely to play next. The desktop window opens on the
library; choose **Home** in the header to open it. Game Mode always starts on Home.

- **Jump back in** shows the game you played last, with **Play**, **Details** and
  **+ Up Next**.
- **Continue playing** lists other recent games; **View All** opens them in the library.
- **Up next** is your own shortlist of up to 100 games. Add a game with **+ Up Next**
  on a Home tile or **Add to Up Next** on its details page. On a queued game, **Queue
  Actions** offers **Move Earlier**, **Move Later** and **Remove**. In a wide window the
  first queued game also appears beside Jump back in.
- **Find your next game** suggests games from your library: your backlog first, then
  favorites, games like the ones you played recently, and games you have not played in
  Omakade yet. Completed, abandoned and hidden games are left out.
- **Quick access** opens All Games, Favorites, Backlog, your collections, consoles and
  sources, and your saved filters.

## Keyboard and Game Mode

Use `Ctrl+F` to search, arrow keys to navigate, Enter to open details, Escape
to return, and F11 to enter or leave fullscreen Game Mode. The controller Start
button and the **GAME MODE** button in the header do the same. Game Mode opens
on Home, with one header for Home and the library, offers detail and grid
library views, and Omakade remembers whether it was last fullscreen. Its cursor
hides during controller or keyboard use, returns on mouse movement, and remains
visible on the desktop. `Ctrl+M` toggles reduced motion and `Ctrl+D` opens
settings and source diagnostics.

## Game Mode

Fullscreen is always Game Mode. F11, Start, the header button and
`omakade --couch` open it on the display you are using, leave sound and
notifications alone, and keep the screen awake only while a game runs.

For a TV or another dedicated display, choose a display and sound output in
**Settings → Controls**, then select **Start Game Mode**, press Super + Ctrl + G
once you add it under **Keyboard Shortcut**, or run `omakade --game-mode`, even when Omakade is already
open. That Game Mode uses its own workspace on the chosen display, silences
notifications, and keeps the screen awake. Leaving restores the changes it made.

With no game running, LEAVE, Start and F11 end Game Mode and return to the
desktop library. With a game running they open the Game Mode controls, where
**End Game Mode** or **Stop Games and Leave…** ends it; `omakade --game-mode-exit`
ends it too. Sound switching affects the whole desktop. Gamescope and HDR remain
launcher settings.

**Controller Home button** in the same section lets the Xbox, PlayStation or Home button
open the in-game guide over a game and do what Start does otherwise. Holding it does the
same as the keyboard shortcut.
See [Game Mode](GAME-MODE.md) for disabled displays, recovery, and limitations.

## Organize several games at once

Choose **More → Organize** above the desktop library, or **Filters → Organize** in
the Game Mode library. Select individual games or choose **Select Results** for the current filtered
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

Choose **More → Saved Filters** above the desktop library, or **Filters → Saved
Filters** in the Game Mode library. Enter a name and choose **Save Current** to keep the current search,
source, availability, favorites/recent/hidden mode, status, collection, tag, and
sort order. Select a saved view to rename it with **Rename Selected**, delete it,
or choose **Apply**.
Deletion requires confirmation and leaves your current filters unchanged.

Saved filters are dynamic queries, so newly matching games appear automatically.
If a referenced collection or tag is missing, Omakade keeps that criterion and
shows a message instead of silently broadening the results. Recreating it makes
the view usable again. Saved views stay local and persist across restarts.

## Pick something to play

Choose **More → Pick a Game** above the desktop library, or **Filters → Pick a Game**
in the Game Mode library. Omakade picks an available game from the current results and opens its
details for review. **Pick Another** chooses again; **Play** launches only when
you choose it. Search, source, favorites, status, collection, and tag filters
still apply. Linked installations count as one game, and the last choice is
avoided when another eligible game exists.

## Customize game artwork

Open a game's details and choose **Game & Artwork → Custom Images…** to change
its **Cover**, its wide **Background**, or, in Game Mode, its **Couch Logo**. Before
a game is identified, the same button reads **Identify Game** or **Find Cover**.
Choose **Browse** or enter a path, then **Apply**. Select a PNG, JPEG, or WebP file
up to 32 MB. Transparent logos are supported. Each image is copied into Omakade's
data folder, so moving the original or clearing downloaded caches does not remove
your choice. **Use Automatic** beside an artwork type restores the provider's image
without changing the other images. Game Mode can enter an image path with the
on-screen keyboard.

The same **Game & Artwork** panel can change which game a title is matched to with
**Change Match** and, with SteamGridDB connected, choose another portrait cover.

## Connections and achievements

Everything in **Settings → Connections** is optional. Installed games, local
artwork and launching work without any of them.

| Connection | What it adds | What you need |
| --- | --- | --- |
| ProtonDB Reports | Community compatibility tiers in Steam game details, and optionally on cards | Nothing |
| SteamGridDB Portrait Covers | Portrait covers for games without one | A SteamGridDB API key |
| Steam Library Information | Uninstalled owned games, account playtime, and achievement rarity | Your Steam ID and a Steam Web API key |
| RetroAchievements | Achievement progress for supported cartridge systems | Your RetroAchievements username and Web API key |
| Ratings and Game Information · IGDB | Critic scores, play time estimates and game matching | A Twitch developer client ID and secret |
| RomM Library | Games from a RomM server; see [RomM](#romm) | A RomM Client API Token |

**Update Ratings & Portraits** fills in ratings and covers for the whole library.
While IGDB or SteamGridDB is connected, Omakade also does this on its own in the
background. Turn off **Identify Library Automatically** to look games up only when
you select Update or open a game's details.
Keys and tokens are stored in Secret Service, never in Omakade's settings file, and
each connection can be removed from the same page. [Privacy](../PRIVACY.md) lists
what each one sends.

Achievements appear on a game's details page, on its card and in the
[in-game guide](GAME-GUIDE.md):

- **Steam** achievements are read from the Steam client's own files without any key.
  A Steam Web API key adds rarity and refreshes them when details open.
- **Xbox** achievements for Xodus Nexus games come from Nexus, which reads them from
  the Microsoft account it is signed in to.
- **RetroAchievements** match your ROMs by hash on this computer, then load your
  progress for the matched game. Disc-based systems are not matched yet.


## Back up your library

Open **Settings → Backup & storage → Backup & Restore**. **Save As…** writes a local
`.omakade-backup` archive of your favorites, hidden choices, organization, links,
preferred installations, manual entries, custom artwork, saved filters, launch
activity, and core preferences. Game files, launcher databases, account
credentials, and downloaded caches are excluded.

Use **Open…** to preview a backup, or enter a path and choose **Preview Path**.
The preview lists counts, preferences, missing paths, and saved-filter name
conflicts. **Merge…** imports matching choices and keeps unrelated personal data.
**Replace…** clears personal library choices before importing, while keeping game
files and cached source records. Both require confirmation. Omakade saves a
recovery copy before changing anything. An interrupted restore can finish or be
undone before the library opens.

A backup that includes play history can only be restored while the play recorder
is stopped. Closing Omakade or turning **Record Playtime** off does not stop it, so:

1. Confirm **Merge…** or **Replace…**, then choose **Close Omakade**.
2. Stop the recorder:

   ```bash
   systemctl --user stop omakade-sessiond
   ```

3. Open Omakade. It applies the restore before the library opens.
4. Start the recorder again. On Omarchy, Omakade does this when it opens; elsewhere,
   run `systemctl --user start omakade-sessiond`.

If the recorder is still running, the restore stops with "Stop the play-session
recorder before restoring, then retry." Choose **Stop Recorder and Restore** to stop
`omakade-sessiond` and apply the backup, or stop it yourself and retry.

A backup can also repair a library database that is damaged and cannot be read.
The preview compares against an empty library, and the restore keeps an exact copy
of the damaged database in the recovery folder before it builds a new one. Undo
puts the damaged copy back, along with any play sessions the recorder was still
holding for it.

Account-service identifiers and Sunshine publishing choices stay local. Missing
manual paths remain repairable, and disconnected game identities stay stored.
Restoring never launches imported games. In Game Mode, confirm the path field
to use the on-screen keyboard, and use the arrows to scroll the preview.

## Track play sessions

Steam, Heroic, Lutris, Faugus, RetroArch, PCSX2 and Ryujinx keep their own play
time, and Omakade shows it without any setup. Omakade can also record play
sessions itself, for emulators that keep no time and for the dated history that
Stats uses.

To turn recording on:

1. Open **Settings → Library & launching** and set **Record Playtime** to On.
   It is off on new installations.
2. Make sure the recorder, `omakade-sessiond`, is running. The first time Omakade opens
   it sets the recorder to start at login, and on Omarchy it also starts it whenever it
   opens. To start it yourself, or keep it running from login:

   ```bash
   systemctl --user enable --now omakade-sessiond
   ```

The same page shows whether the recorder is running, separately from whether
recording is on.

**What is recorded.** While a game runs in RetroArch, Dolphin, PCSX2, RPCS3, PPSSPP,
Ryujinx, Eden, Cemu, shadPS4, melonDS, mGBA, xemu or Xenia, from Xodus Nexus, or from
Steam, the recorder saves when it started, when it ended and how long it ran. It knows
which game is running from the file the emulator was started with, so games started
from Omakade, another launcher or a terminal all count. On Hyprland it also recognizes a
game you load from inside the emulator when the window title names a game in your
library. Dolphin reports the game it is running, and so does PCSX2 with file logging on.
A Steam game counts from about ten seconds after Steam starts it, whether you start it
from Omakade or from Steam, as long as the Steam source is on and lists it as installed.

**What is not recorded.** A game loaded from inside an emulator that none of those
checks can identify, non-Steam shortcuts you added to Steam, and Steam games Omakade
does not list as installed, or any Steam game while the Steam source is off. Heroic, Lutris, Faugus, Battle.net, direct GOG and manual
games are not recorded; their time comes from the launcher, where it keeps one.

**How time is counted.** Time keeps counting while the emulator is open, including on
its own pause screen. On Hyprland, **Pause When Unfocused** stops counting while the
game window is behind other windows; it is off by default. A launcher's own total
and Omakade's recorded time are combined without counting the same session twice.
A Steam game's total stays Steam's own; its recorded sessions add the dates that Stats
and Play History use.

**Where it goes.** History stays in Omakade's library database on this computer. The
recorder reads the process list and, on Hyprland, window titles; it does not change
emulators or games. For Steam it reads the command line Steam starts each game with,
which names the game's app id. It keeps running after Omakade closes. Turning
**Record Playtime** off keeps your history and shows the launchers' own time. The
service itself stays running because the in-game guide and Highlights also use it.
You can stop it with `systemctl --user disable --now omakade-sessiond`, but on
Omarchy, Omakade starts it again each time it opens. Turning **Record Playtime** off
is what stops recording.

A game's details page shows the launcher's time and Omakade's recorded total
separately. **Play History** pages through sessions, and **Delete All Recorded
Sessions…** clears them after confirmation.

**Discord Status** on the same Settings page shows the game you are playing in
Discord, including Steam games. It is off by default and sends only the game name and
source; see
[Privacy](../PRIVACY.md#statistics-and-discord-presence).

The [recording coverage notes](RECORDING-COVERAGE.md) list what has been tested for
each emulator and for Steam.

## Stats and Year in Review

Choose **Stats** in the header. Stats is built from recorded sessions, so turn on
[recording](#track-play-sessions) first. While it is off, Stats says so and offers
**Turn On Recording**; once it is on, Stats says nothing has been recorded until you play.

- **This Year** and **All Time** choose the period, and Omakade remembers your choice.
- **Overview** shows recorded play, days played, games played and sessions, your most
  played games, a weekly pattern, and achievements unlocked in the period.
- **Play Patterns** shows when you play by hour, session length, streaks, and habits
  such as first-time plays and games you came back to.
- **Library Snapshot** shows your current library and completion marks, lifetime
  totals including the launchers' own time, and the systems, sources and genres you
  played most.

**Make a Card** opens **Your Card**, a Year in Review image of the chosen period.
**Save Image** writes it to `~/Pictures/Omakade`, and **Open Folder** shows it there.
The card is made on this computer and is not uploaded.

## Stream with Sunshine and Moonlight

Omarchy installs Sunshine from its menu and ships Moonlight. Once Sunshine is
running, open **Settings → Streaming** and enable **Omakade in Moonlight** to add
Omakade to Sunshine's app list next to Steam Big Picture, or **One app per
installed game** to add every installed game with its cover. Sunshine reads the
list when it starts, so press **Restart Sunshine** after a change. Omakade
leaves the other Sunshine apps alone and keeps a one-time backup next to
`apps.json`.

Starting Omakade from Moonlight opens Game Mode on the streamed display and leaves
the stream's sound alone. Ending the stream closes Omakade, which ends Game Mode.
Starting a game from Moonlight launches it through its own launcher, the same
as pressing Play. The same entry points work from a terminal or a key binding; see
[Command line](#command-line).

## Command line

Commands that open Omakade reuse the running window when there is one.

| Command | What it does |
| --- | --- |
| `omakade` | Open Omakade |
| `omakade --demo` | Open a fictional library to try Omakade |
| `omakade --couch` | Open fullscreen in Game Mode on the current display |
| `omakade --reduced-motion` | Open with reduced motion turned on and saved, as `Ctrl+M` does |
| `omakade --play <key>` | Launch one game, for example `--play Steam::620` |
| `omakade --quit` | Close the running Omakade window |
| `omakade --game-mode` | Start Game Mode on the display and sound chosen in **Settings → Controls** |
| `omakade --game-mode-toggle` | What Super + Ctrl + G runs: over a game, open the in-game guide; otherwise start Game Mode, return to the desktop, or resume |
| `omakade --game-mode-desktop` | Return to the desktop and keep Game Mode ready to resume |
| `omakade --game-mode-return` | Resume Game Mode after returning to the desktop |
| `omakade --game-mode-exit` | End Game Mode, or undo what an interrupted session left changed |
| `omakade --clip` | Save a [highlight](HIGHLIGHTS.md) of the running game |

A `--play` key is `Source:runner:id`, for example `Heroic:legendary:Sugar`. The
runner is often empty, as in `Steam::620`. Sunshine's app entries use `--play` and
`--quit`.

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
discovery, and the fullscreen controller layout on Apple Silicon with Asahi Linux. The `fex-steam`
wrapper that provides `/usr/bin/steam` can still fail to start games from any
`steam://` request, including Steam's own client. That is a wrapper limitation,
not something Omakade can work around; see issue #13 for details and workarounds.

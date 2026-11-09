# Troubleshooting

Find the symptom, then work down its steps. Most answers start in
**Settings → Sources**; `Ctrl+D` opens Settings from the library. Menu paths use the
labels shown in the app. If nothing here helps, [Support](../SUPPORT.md) lists what to
include in a report.

- [A game is missing from the library](#a-game-is-missing-from-the-library)
- [Read a source's details](#read-a-sources-details)
- [Where Omakade looks for each launcher](#where-omakade-looks-for-each-launcher)
- [Steam games on another drive are missing](#steam-games-on-another-drive-are-missing)
- [ROMs are missing or under the wrong console](#roms-are-missing-or-under-the-wrong-console)
- [A RetroArch game will not start](#a-retroarch-game-will-not-start)
- [Xodus Nexus says "Could not update Xodus Nexus games"](#xodus-nexus-says-could-not-update-xodus-nexus-games)
- [Battle.net games are missing](#battlenet-games-are-missing)
- [A game has no cover](#a-game-has-no-cover)
- [Fix several games at once with Repair Library](#fix-several-games-at-once-with-repair-library)
- [Collect logs for a report](#collect-logs-for-a-report)

## A game is missing from the library

Check these in order. The first four hide games that Omakade already knows about.

1. **Filters.** Active filters show as chips in a row under the library toolbar, for
   example `Search: souls ×`. Select a chip to remove it, or **Clear All** to remove
   them all. An empty library with filters on shows **Clear Filters** in the middle.
   **All**, **Favorites** and **Recent** above the search field also narrow the list.
2. **Console cards.** Emulated games can sit behind one card per console. Open the
   console's card, or choose **View → Console View: Games** to list every game
   separately. Each console's own layout is under **Console Layout** in
   **Settings → Library & launching**.
3. **Hidden games.** **Filters → Hidden Games** lists the games you hid. Open one and
   choose **Manage → Unhide**.
4. **Not installed.** The library shows installed games by default. Owned games
   that are not installed (Steam with a synced library, Heroic) appear under
   **Filters → Availability → All Games** or **Ready to Install**.
5. **Source turned off.** **Settings → Sources** has two tabs. **In Use** lists the
   sources that are on; **Available** lists the rest. Turn a source **On** and it scans
   straight away. Turning a source off also removes it from the library's
   **Sources** filter.
6. **Source not found.** If the source's status says it was not found, or **Details**
   lists no folders, Omakade is not looking where your launcher keeps its data. Compare
   with [Where Omakade looks for each launcher](#where-omakade-looks-for-each-launcher).
7. **Launcher not refreshed.** Omakade reads what the launcher has already saved. Open
   the launcher once so it records the game, then choose **Rescan Source** in the
   source's **Details** or **Rescan Enabled Sources** at the top of
   **Settings → Sources**.

For a game no launcher reports, use **More → Add a Game**; see
[Add a native game manually](GUIDE.md#add-a-native-game-manually).

## Read a source's details

In **Settings → Sources**, each source shows a status line. **Details** opens a page
with:

- the status and the time of the last scan
- every folder or prefix the source found, one per line
- warnings in yellow, such as a file that could not be read or a library folder that
  is not there
- **Rescan Source**, which scans only this source

**Back to Sources** returns to the list. Common statuses:

| Status | What it means | What to do |
| --- | --- | --- |
| Imported or found *N* games | The scan worked | Nothing, unless a game is still missing; check the filters above |
| *Source* was not found, No *source* games were found, or Not detected | None of the usual folders exist | Compare with the paths below, open the launcher once, and rescan |
| Scan interrupted; kept the cached library | A folder or file could not be read, so the previous list was kept | Read the yellow warning, reconnect the drive or fix the file, then rescan |
| Imported *N* installed game(s) with warnings | The scan worked but skipped something | Read the yellow warning |
| Could not update *source* games, or *source* cache unavailable | Omakade could not save the scan to its library database | See [the Xodus Nexus entry](#xodus-nexus-says-could-not-update-xodus-nexus-games); the same steps apply to every source |

## Where Omakade looks for each launcher

Omakade checks both the native and the Flatpak locations. It launches a game through
the copy whose data it found, so a Flatpak library opens with `flatpak run`. When both
copies of Lutris have a database, Omakade keeps only the one whose launcher is still
installed.

| Source | Native | Flatpak |
| --- | --- | --- |
| Steam | `~/.local/share/Steam` or `~/.steam/steam` | `~/.var/app/com.valvesoftware.Steam/data/Steam` |
| Heroic (Epic, GOG, Amazon) | `~/.config/heroic` | `~/.var/app/com.heroicgameslauncher.hgl/config/heroic` |
| GOG without Heroic | `~/GOG Games`, `~/Games/GOG`, `~/Games/Heroic`, `~/Games`, plus **Extra GOG Folders** | |
| Lutris | `~/.local/share/lutris/pga.db` | `~/.var/app/net.lutris.Lutris/data/lutris/pga.db` |
| Faugus | `~/.local/share/faugus-launcher/games.json` | `~/.var/app/io.github.Faugus.faugus-launcher/data/faugus-launcher` |
| Battle.net | Wine, Proton and Bottles prefixes; see [Battle.net](#battlenet-games-are-missing) | Bottles and Steam Flatpak prefixes |
| Xodus Nexus | `~/.config/xodus-nexus/launcher-state.json` | |
| RetroArch | `~/.config/retroarch` | `~/.var/app/org.libretro.RetroArch/config/retroarch` |
| PCSX2 | `~/.config/PCSX2` | `~/.var/app/net.pcsx2.PCSX2/config/PCSX2` |
| RPCS3 | `~/.config/rpcs3` | `~/.var/app/net.rpcs3.RPCS3/config/rpcs3` |
| PPSSPP | `~/.config/ppsspp` | `~/.var/app/org.ppsspp.PPSSPP/config/ppsspp` |
| Ryujinx | `~/.config/Ryujinx` | `io.github.ryubing.Ryujinx` or `org.ryujinx.Ryujinx` under `~/.var/app` |
| shadPS4 | `~/.config/shadps4` or `~/.local/share/shadPS4` | `~/.var/app/net.shadps4.shadPS4` |
| Cemu | `~/.config/Cemu` or `~/.local/share/Cemu` | `~/.var/app/info.cemu.Cemu` |
| melonDS | `~/.config/melonDS` | `~/.var/app/net.kuribo64.melonDS/config/melonDS` |
| Dolphin | `~/.config/dolphin-emu` | `~/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu` |
| Xenia Canary | `~/Documents/Xenia`, `~/.local/share/Xenia` or `~/Xenia` | |

Paths under `~/.config` and `~/.local/share` follow `XDG_CONFIG_HOME` and
`XDG_DATA_HOME` when you set them. Kyty and native ports have their own folders; see
[Kyty and native ports](GUIDE.md#kyty-and-native-ports) and
[Native ports](NATIVE-PORTS.md).

A Flatpak emulator can only open games in folders its sandbox can read. If a game
is listed but will not start, grant the Flatpak access to that folder, for example
with Flatseal, or move the game into a folder it can already read.

## Steam games on another drive are missing

Omakade reads every library folder in Steam's `libraryfolders.vdf`. When one of
those folders is not there, for example because its drive is not mounted, the
**Steam** details show `Steam library is unavailable, skipping:` and the path. Games
in that folder leave the installed list until it is back.

1. Mount the drive at the same path Steam uses.
2. Choose **Rescan Source** in Steam's **Details**.

If you moved or removed the library on purpose, remove it in Steam's **Storage**
settings too so it stops appearing in the warning.

## ROMs are missing or under the wrong console

RetroArch games come from RetroArch's playlists. ROMs without a playlist need a
**ROM folder**, which tells Omakade which console the files belong to.

Folders named after a console are found automatically inside `~/Emulation/roms`,
`~/Emulation/Games`, `/data/Emulation/roms` and `/data/Emulation/Games`, the EmuDeck
layout:

```text
~/Emulation/roms/
├── snes/      Super Nintendo (also sfc)
├── nes/       NES (also fc)
├── gb/  gbc/  gba/
├── n64/       Nintendo 64
├── genesis/   Sega Genesis (also megadrive, md)
├── psx/       PlayStation (also ps1)
└── dreamcast/ Sega Dreamcast (also dc)
```

For any other folder, open **Settings → Sources → ROM Folders**. The first button
shows a console name; select it until it shows the right console, then choose
**Add Folder** and pick the folder. Files inside it and its subfolders are added
when their extension fits that console. The folder is listed with its console, for
example `SNES · /path`. To change a folder's console, remove it and add it again.

Nintendo DS, PlayStation Portable and PlayStation 3 folders are never detected
automatically. Add them under **ROM Folders** with that console, and their games
go to melonDS, PPSSPP and RPCS3. Switch, Wii U, PS2 and PS4 games stay with their
own emulators' libraries, and GameCube and Wii games come through Dolphin.

If a ROM appears under the wrong console, its folder is marked with the wrong
console. If it does not appear at all, check that its extension is one that
console's emulators open, then rescan.

A cover image with the same name as the ROM, such as `Chrono Trigger (USA).png`
next to `Chrono Trigger (USA).sfc`, is used as its cover.

## A RetroArch game will not start

A playlist entry can have no core assigned, and a ROM folder game never has one.
Omakade then picks a core for the game's console from RetroArch's cores folder
(`~/.config/retroarch/cores`, the Flatpak's cores folder, or `/usr/lib/libretro`),
or a standalone emulator it knows for that console. When it finds neither, Play
shows `No available emulator or RetroArch core was found for` and the console.

1. In RetroArch, install a core for that console with **Online Updater → Core
   Downloader**, or install the distribution's libretro package.
2. Or open the game's details, choose **Manage → Launch Setup**, choose **Core**
   and select a core file, then **Save Setup**. **Reset to Automatic** undoes it.
3. If the message is `Omakade cannot determine this ROM's console`, add its folder
   under **ROM Folders** with the right console, as above.

`The configured RetroArch core is missing or unreadable` means a core chosen
earlier has been removed. Reinstall it or choose **Reset to Automatic** in
**Launch Setup**. **Copy Launch Details** in the same panel copies the route,
program and status for a report.

## Xodus Nexus says "Could not update Xodus Nexus games"

This status means Omakade could not save the scan to its library database. The
games from the last good scan stay in the library. In current versions it can also
show when Xodus Nexus is not installed at all.

1. If you do not use Xodus Nexus, turn **Xodus Nexus** off in **Settings → Sources**.
2. If you do, open its **Details**. The yellow text is the database error.
3. Choose **Rescan Source**. A one-off failure clears on the next good scan.
4. If it stays, check that `~/.local/share/omakade` has free space and is writable,
   then report it with the yellow text.

Omakade lists only the games Nexus shows as ready to play. A game that is still
installing appears after Nexus finishes it. Open Nexus once after installing or
updating it, so the shortcut Omakade launches through exists.

## Battle.net games are missing

Omakade reads the Battle.net Agent database at
`drive_c/ProgramData/Battle.net/Agent/product.db` inside a Wine prefix. It looks for
prefixes in:

- `~/.wine`
- each folder inside `~/.local/share/wineprefixes`
- each bottle inside `~/.local/share/bottles/bottles` and the Bottles Flatpak's
  `~/.var/app/com.usebottles.bottles/data/bottles/bottles`
- each folder inside `~/Games`
- each Proton prefix under `steamapps/compatdata` in your Steam libraries

If **Battle.net was not found**:

1. Start Battle.net once in its prefix and let it finish installing or verifying the
   game, so its Agent writes `product.db`.
2. If the prefix lives somewhere else, move it into one of the folders above.
3. Choose **Rescan Source** in Battle.net's **Details**. The found prefixes are
   listed there.

To play, Wine, `umu-launcher` or Bottles must be installed, matching the prefix.

## A game has no cover

Covers come from the launcher first: Steam's own cache or the Steam CDN, Heroic's
image cache, Lutris cover art, Dolphin's cache, RetroArch thumbnails, the image
stored in Switch, Wii U and PS4 dumps, or an image next to a ROM.

- Rescan the source after the launcher has downloaded its art.
- Add a SteamGridDB API key in **Settings → Connections** for portrait covers, and
  IGDB for game matching. **Update Ratings & Portraits** fills in the whole library.
- Choose a different match or image in the game's **Game & Artwork** panel; see
  [Customize game artwork](GUIDE.md#customize-game-artwork).
- **Settings → Backup & storage → Clear Downloaded Portraits** removes downloaded
  portraits so they are fetched again.
- **Filters → Needs Review** and **Repair Library** list every game missing art.

Without keys or a network, a game with no local art shows its title on a plain card.

## Fix several games at once with Repair Library

**More → Repair Library** steps through games that need attention, one at a time. Filter
by source, and by reason:

| Reason | Meaning | Actions |
| --- | --- | --- |
| Needs identification | No confident match to a known game | **Correct Identity**, or **Retry This Game** to look it up again |
| Missing artwork | No cover | **Choose Artwork** |
| Game file moved or missing | The file Omakade knew is gone | Emulated games: **Locate File**. Manual games: **Edit Game**. Launcher games: fix it in the launcher, then **Recheck** |
| Drive or folder disconnected | Several games under one missing folder | Reconnect the drive, then **Recheck** |
| Emulator or core unavailable | The emulator, core or launcher is missing | **Launch Setup** |
| Source scan failed | The source's last scan failed | **Retry Source**, then read the source's **Details** |
| Duplicate suggestions | Probably the same game twice | **Launch Setup / Link** |

Use **Previous** and **Next** to move. **Select for Retry** collects up to 100 games for
**Retry Selected**. Identity, artwork and relocation changes each have an
**Undo**. With **Locate File**, identity, organization and play history follow the
game to its new path and eligible save backups are copied there. Game files are
never moved.

## Collect logs for a report

1. Note the source's status and yellow warning from its **Details**.
2. For a game that will not start, open **Manage → Launch Setup → Copy Launch
   Details** and paste it into the report. It replaces your home folder with `~`.
3. **Settings → About & help** shows the version, the database path and the
   controller in use.
4. To capture Omakade's own messages, close it with `omakade --quit`, start it from a
   terminal and reproduce the problem:

   ```bash
   omakade 2>&1 | tee ~/omakade.log
   ```

5. For play time recording or the controller Home button, read their services:

   ```bash
   journalctl --user -u omakade-sessiond -b
   journalctl --user -u omakade-guide-button -b
   ```

Read logs before sharing them. Remove paths, account names and anything private.
Never post API keys, tokens or your configuration folder.

# Emulator save protection

Omakade copies existing in-game saves before launching an emulator. It uses the current
emulator selection without a setup questionnaire. Games without saves get their first copy
on a later launch. Unchanged saves do not create duplicate versions.

Open **Manage Game > Save Backups** to inspect coverage and restore a dated version.
Restoring first protects the current save set, including files added or deleted since the
selected snapshot. Shared storage is labeled in the list and confirmation because restoring
it also replaces progress for other games or profiles in that storage. Cancel receives focus.
Save states and cloud synchronization are outside this feature.

## Coverage

| Emulator | Save coverage |
| --- | --- |
| RetroArch | Every core in Omakade's console catalog: Snes9x, bsnes, Nestopia, FCEUmm, Gambatte, SameBoy, mGBA, Mupen64Plus-Next, ParaLLEl N64, Genesis Plus GX, PicoDrive, PCSX-ReARMed, SwanStation, Flycast |
| RetroArch save formats | Frontend SRAM and RTC together; Nestopia FDS save patches; Genesis CD backup RAM; PS1 memory cards; shared and per-content Dreamcast VMUs |
| Dolphin | GameCube GCI folders and raw cards, including region/size variants; Wii title saves; shared save-bank fallback when a compressed disc has no readable identity |
| PCSX2 | File and folder memory cards, configured card directories, external cards, and per-game/multitap card selections |
| Cemu | Complete title save folders, including profiles and save metadata; configured MLC roots; title IDs from the library, RPX metadata, WUA directories, or title cache |
| shadPS4 | Title save folders across profiles, including configured save roots and modern home layouts |
| Ryujinx | The user save bank, save metadata, and shared save index together |
| Native cartridge fallbacks | Snes9x, bsnes, Nestopia, FCEUX, Mednafen, SameBoy, mGBA, Mupen64Plus, BlastEm, Gens, DuckStation, Flycast |
| Flatpak | Isolated config/data roots for the dedicated sources and RetroArch |
| Portable/custom wrappers | Explicit local layouts can point at the actual save folders without changing the launch command |

Configured RetroArch save directories, content-directory/core sorting, and readable
core/content-directory/game overrides are respected. Archive-entry paths use the inner ROM's
basename. Configuration that cannot be interpreted, unsafe paths, and unknown third-party
cores produce an unavailable-layout message rather than a guessed restore target.

These are save-layout adapters, not a claim that every emulator version, game, custom wrapper,
or filesystem provider has been tested in gameplay. Every catalog emulator has an adapter.
Nonstandard layouts can use the local configuration below. Symbolic links and non-file storage
providers require an explicit real filesystem location; the store never follows them during restore.

## Restore and recovery

A version contains a manifest, hashes, source context, allowed save locations, and all member
files. Publication of a snapshot follows a complete readback and source recheck. The old
single-file snapshot format remains readable.

Multi-file restore writes a persistent journal containing both the previous and requested
sets before changing live files. Each file uses an atomic write. Interrupted restores roll
back on recovery; a committed journal only needs completion/cleanup. Recovery refuses to
overwrite unexpected external changes and keeps its copies for investigation. Omakade pauses
emulator launches while a restore journal remains unresolved. **Retry Save Recovery** appears
in Save Backups when needed. Close emulators before retrying. Do not delete the journal to
bypass recovery.

Process checks cover the supported emulators, including Eden used by local wrappers.
An emulator started outside Omakade during restore can interrupt the operation; its subsequent
changes are preserved for explicit recovery rather than overwritten. Automated interruption
tests exercise process interruption, not a physical power-loss test.

The store keeps ten versions per save layout, with one shared history for identical shared
banks across games. Save sets are bounded to 512 MiB and 20,000 files; committed set storage
is limited to 2 GiB. Restore temporarily needs room for recovery copies as well. The earlier
single-file store retains its separate 256 MiB limit. Storage failure preserves existing
versions, reports the failure, and does not overwrite live progress.

Save protection is on by default and can be disabled in **Settings > Backup & storage**.
The existing machine-local preference is retained. Existing versions remain restorable when
copying is disabled. Save copies and portable layouts are separate from organization backups.

## Local portable layouts

`$XDG_CONFIG_HOME/omakade/save-layouts.json` is an optional machine-local override. A rule
matches the exact source and library install path. Omitted `flatpak` means native. Files and
trees are absolute paths or start with `~/`. An optional anchored `relativePattern` selects
files within a tree, such as one title across emulator profiles. Unmatched rules do not affect
other games. Changing a layout does not silently migrate or retarget old backups.

```json
{
  "format": 1,
  "layouts": [{
    "source": "shadPS4",
    "game": "/games/CUSA00900/eboot.bin",
    "trees": ["/portable/shadps4/user/home/1000/savedata/CUSA00900"],
    "description": "Portable Bloodborne saves",
    "shared": false
  }]
}
```

The local candidate includes explicit mappings for the existing Bloodborne and Eden wrappers.
Those mappings live in user configuration; machine-specific game paths are not shipped in code.

## Validation

Regression tests cover multi-file restore/undo, deleted folders and entirely deleted sets,
empty files, corruption, redirected paths, changed layouts, shared-history deduplication,
retention, interrupted restores, refusal to overwrite external progress, native/Flatpak
layouts, and launch-before-write ordering for every dedicated emulator source. Desktop and
couch fixtures exercise ordinary and shared-storage confirmations with disposable saves.

Private build evidence, live read-only inventory, original hashes, and installation manifests
are under `build/save-complete-20260909/`. All 228 automated tests and the staged smoke test
passed. Desktop and AppStream validation passed, and the shared-storage desktop and couch
confirmations were visually checked. Read-only inventory found 220 unique save files.
Live backup verification was deferred because Ryujinx was running; the running-game guard
left its saves alone. Automation never restores real saves. Maintainer gameplay and
physical-controller acceptance of the exact candidate are still required before publication.

## Format references

- [RetroArch path and override behavior](https://github.com/libretro/RetroArch/blob/master/runloop.c)
  and [SRAM/RTC files](https://github.com/libretro/RetroArch/blob/master/save.c)
- [mGBA memory exports](https://github.com/mgba-emu/mgba/blob/master/src/platform/libretro/libretro.c)
- [Genesis Plus GX CD backup RAM](https://github.com/libretro/Genesis-Plus-GX/blob/master/libretro/libretro.c)
- [PCSX-ReARMed memory cards](https://github.com/libretro/pcsx_rearmed/blob/master/frontend/libretro.c)
  and [SwanStation memory cards](https://github.com/libretro/swanstation/blob/master/src/libretro/libretro_host_interface.cpp)
- [Flycast VMU paths](https://github.com/flyinghead/flycast/blob/master/core/oslib/oslib.cpp)
  and [libretro VMU paths](https://github.com/libretro/flycast/blob/master/core/stdclass.cpp)
- [Dolphin card paths](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/Config/MainSettings.cpp)
- [PCSX2 memory cards](https://pcsx2.net/docs/configuration/memcards/)
- [shadPS4 title saves](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/save_data/save_instance.cpp)
- [Cemu MLC configuration](https://github.com/cemu-project/Cemu/blob/main/src/config/CemuConfig.cpp)

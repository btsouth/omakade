# Local RetroArch save protection

Historical first candidate. Current coverage and recovery behavior: [Emulator save protection](SAVE-SETS.md).

This candidate adds versioned local save copies to the existing launch path. It uses the
current emulator/core selection and adds no setup wizard, dependency, or background service.

## User behavior

Supported native RetroArch launches copy the existing in-game save immediately before the
emulator starts. This protects the previous completed session even when Omakade closes after
launch. New games without a save get their first snapshot on a later launch. No save is copied
while a detected RetroArch process runs. Unchanged saves do not create duplicate versions.

Games with snapshots gain **Manage Game > Save Backups**. Selecting a dated version opens a
confirmation with Cancel focused. Restore requires RetroArch to be closed, verifies the saved
bytes and current location, and protects the current save first. A deleted save can be restored
when its original directory and game still exist. The restore itself uses an atomic file write.
Start from an in-game save afterward; a separately loaded save state may supersede it.

**Settings > Backup & storage > Save Protection** switches automatic copying off or on.
It defaults on and is local to the machine, excluded from organization-backup preference restore.
Existing versions remain available when automatic copying is off. A copy failure does not block
launch; it remains visible and prevents automatic Omakade closure on that failed-protection launch.

## Supported layouts

- Native Snes9x (`.sfc`/`.smc`), Nestopia (`.nes`), Mupen64Plus-Next (`.n64`/`.z64`/`.v64`),
  mGBA Game Boy Advance (`.gba`), and Genesis Plus GX cartridges (`.md`/`.gen`/`.smd`/`.sms`/`.gg`).
- One existing `.srm` file, at most 8 MiB. Configured save folders, core-name sorting, and saves
  beside content are supported. Configuration values must be explicit and interpretable.
- Save-location overrides, content-directory sorting, include directives, redirected/symlinked
  paths, archives, Flatpak, save states, extra save files, other cores and emulators are skipped.
  Empty, unreadable, oversized, and ambiguously located saves are not overwritten.
- No emulator configuration, controller mapping, performance setting, or save conversion changes.
- Backups are tied to the exact ROM path and original core/save location. Renaming or moving files
  and changing save layouts does not trigger an automatic migration.

RetroArch already implements per-game/core overrides. Extending that into universal performance
profiles would require title, emulator-version, hardware, and save-continuity evidence we do not
have. Existing local routing is retained while this independently useful protection is delivered.
The format and override boundaries are informed by the official [Snes9x core documentation](https://docs.libretro.com/library/snes9x/),
[RetroArch overrides guide](https://docs.libretro.com/guides/overrides/), and
[directory configuration guide](https://docs.libretro.com/guides/change-directories/).

## Storage and recovery

Versions live in `$XDG_DATA_HOME/omakade/save-backups` (normally
`~/.local/share/omakade/save-backups`), with private directories/files and a shared writer lock.
A SHA-256 key of the exact ROM path separates games. Each committed version contains
`save.srm` and `manifest.json`, including its source, core, timestamp, size, and SHA-256 digest.
Temporary versions become visible only after both files have been written and checked.

Keep the newest 10 versions per game. A 256 MiB total ceiling stops further snapshots with a
warning instead of removing other games' backups. A damaged manifest cannot suppress a fresh
copy of the current save. Restore never proceeds if that current copy cannot be protected.
Copies are local and are not included in Omakade organization archives. They do not protect
against losing this disk, and do not synchronize or replace any emulator cloud-save service.

## Validation and local acceptance

The Release build, all 225 isolated CTests, staged startup smoke, desktop-file validation,
and AppStream validation passed. Desktop and couch screenshots were inspected.
Evidence is in `build/save-protection-20260909/`. Regression tests exercise ordinary launch
ordering, duplicate suppression, retention, deleted-save recovery, restore/undo, damaged data
and manifests, changed paths, running-emulator refusal, storage exhaustion, symlink skipping,
and preference persistence. Desktop and couch renders exercise the actual confirmation and
restore flow against disposable data, including Cancel focus.

The live read-only audit identified five existing saves totaling 907,264 bytes. Their hashes
were recorded before any copies. All live restore testing remains deferred; automated restores
use disposable files. Exact final checks, installed paths, and copied-save verification are
recorded in the candidate manifests and installation update below.

Before publication, test a normal supported game launch with its existing progress, return to
Omakade, and confirm the new backup appears. Check physical-controller navigation in the restore
menu without confirming a live restore unless intentionally testing recovery. No new title-specific
performance profile, first-class Eden/Xenia integration, or broad save synchronization is included.

## Installation update

Installed source candidate `a44a535ea31edc6b00af485c1db0eb8528a783e2` at
`/home/bts/.local/lib/omakade/a44a535ea31e/`. Installed smoke and binary/link checks passed.
Both commands, the desktop entry, and the recorder override select this candidate. Recorder
PID 136485 remains on its prior, byte-identical binary; no restart was needed. Omakade was not
reopened automatically. The next normal start uses this installation.

Five existing supported saves were copied and checked against both their originals and the
manifest SHA-256 values. All originals and the four game/emulator wrappers remain unchanged.
Saved Omakade configuration was preserved. The new save-protection preference defaults on
when its key is absent. The live copies make Save Backups available for these existing games.

Previous app: `/home/bts/.local/lib/omakade/c9ea15ddd21c/`.
Installation rollback: `/home/bts/.local/state/omakade/local-install-a44a535ea31e-20260909-183600/`.
Restore the saved command links, desktop entry, and service override to roll back the binary,
then reload the user service manager. Leave the live library and save files in place. Save
backup copies are independent and can remain when using the older app. No push, tag, or release.

## Cartridge coverage extension

The next local candidate adds mGBA Game Boy Advance and Genesis Plus GX cartridge saves
using the existing atomic single-file snapshot and restore path. No migration is needed:
existing snapshots remain readable. Both adapters honor the same configured save folders,
override checks, process guard, integrity checks, and storage limits.

This is a partial coverage expansion. Complete save sets, Gambatte, mGBA Game Boy/Color,
Flycast, Dolphin, and PCSX2 are not implemented in this candidate. Other standalone sources
also remain unsupported. Launch support does not imply save-protection support.

The upstream [mGBA libretro implementation](https://github.com/mgba-emu/mgba/blob/master/src/platform/libretro/libretro.c)
exposes Game Boy RTC data separately from SRAM. That requires coordinated restore and is
excluded even when only an SRAM file currently exists. The [Genesis Plus GX implementation](https://github.com/libretro/Genesis-Plus-GX/blob/master/libretro/libretro.c)
exposes cartridge SRAM through the frontend, while CD backup storage follows separate rules.
CD formats and ambiguous `.bin` inputs are excluded.

Before adding these remaining layouts, implement a save-set manifest, interrupted-restore
recovery, and rollback tests. Shared memory-card restores must identify that other games on
the card are affected. Discover files from emulator configuration and game identity, without
copying entire emulator data folders or changing the user's launch setup.

Extension regression cases cover discovery and restore for each added cartridge extension,
protection of current progress before restore, changed save overrides, and rejection of
clock-dependent, archive, disc, and ambiguous content. Tests use disposable files.

Extension validation: Release build, all 225 isolated CTests, staged startup smoke, desktop-file
and AppStream checks passed. Six existing save copies totaling 1,038,336 bytes were verified,
including the newly covered FireRed GBA save. Original save and emulator-wrapper hashes were
unchanged. Evidence and exact candidate hashes are in `build/save-sets-20260909/`. Live restore
and gameplay acceptance remain unperformed. Nothing has been pushed or released.

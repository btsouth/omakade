# Automatic launch audit

September 9, 2026. Local candidate only; no push, tag, or release.

## Installed candidate

Library-review candidate `96acfabe6d2a822ae0bc32bbcd654a40fdd8b194` is installed.
See [installation and rollback](LOCAL-INSTALL-REVIEW-2026-09-09.md).
The launch changes described below are built and staged separately, not installed.
Metroid Dread and its existing recorder process remained running throughout the work.

## Launch behavior

Play now passes the library's console identity to the ROM launch resolver. Shared formats
such as `.chd` no longer select whichever console happens to occur first in the catalog.
Without a declared console, only an unambiguous extension can select a system.

Automatic selection checks runtime availability before selecting RetroArch. An installed
compatible standalone emulator can be used when no core is pinned and RetroArch is absent.
An explicit core stays selected; missing runtime or unreadable native core produces an
error instead of silently changing emulator and save setup. No setup questionnaire was added.

This preserves existing catalog priorities; it does not claim to benchmark or choose optimal
settings for every game. The shared-format defect was tested with fixtures. The two current
Dreamcast entries use `.gdi`, so this was not an observed failure in those games.

## Read-only library results

The production planner resolved all 1,414 ROM records without an error. Content paths exist.
All 1,386 explicit core selections remained unchanged.

| Console | Entries | Selected RetroArch core |
| --- | ---: | --- |
| SNES | 1,387 | snes9x |
| NES | 15 | nestopia |
| Nintendo 64 | 9 | mupen64plus_next |
| Dreamcast | 2 | flycast |
| Game Boy Advance | 1 | mgba |

Active dedicated-source entries also have existing paths: 29 Ryujinx, 1 shadPS4, 1 Cemu,
and 5 Dolphin. Inactive historical source rows were excluded from that claim.
The Ryujinx/Eden and Bloodborne wrapper hashes are unchanged. No emulator settings or saves
were modified. Private library snapshots and detailed results remain under
`build/launch-audit-20260909/`, outside tracked documentation.

## Validation and remaining acceptance

- Release build and all 219 isolated CTests passed.
- Focused tests cover declared-console routing through the UI-callable method and headless
  PlayRequest, runtime fallback, pinned cores, and missing cores using fake executables.
- Staged app smoke test, desktop-file validation, and AppStream validation passed.
- This is command-resolution evidence, not gameplay or performance acceptance.
- Live Flatpak and ARM64 validation remain unperformed. Flatpak is not installed here;
  this change does not redesign existing core discovery across native and Flatpak paths.

After the current game session ends, the next step is local installation of this launch
candidate and a normal SNES, N64, and Dreamcast launch, checking controller input, existing
saves, return to Omakade, and recorded playtime. Keep publication deferred until the exact
candidate has been tested and explicitly approved. Do not remove real cores to test errors;
those cases already run against isolated fixtures.

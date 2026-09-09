# Everyday launch and library polish

September 9, 2026. Local candidate, with no push, tag, or release.

This builds on [automatic console-aware launching](AUTOMATIC-LAUNCH-AUDIT-2026-09-09.md).
Play still launches immediately using existing choices, with no first-launch questionnaire.

## Changes

- Archived ROM entry targets such as `collection.zip#game.sfc` stay with RetroArch,
  even when standalone emulators are preferred. Other emulators are not handed RetroArch's
  archive-entry syntax. If the needed runtime/core is absent, the error explains recovery.
- Archive preflight requires a readable file container and a nonempty entry name. A directory
  or an unrelated existing prefix before `#` cannot hide a missing target. Actual filenames
  containing `#` still work, and archive syntax applies only to the RetroArch source.
- Empty Favorites, Recent, and Needs Review views identify the view. Multiple selected sources
  no longer produce a missing-source title for only the first selection, and unrelated Steam
  errors do not appear when Steam is excluded.

The archive checks do not inspect or extract archive contents. RetroArch still validates the
entry itself. No automatic extraction, emulator configuration, or save migration was added.
The current 1,414-record local ROM snapshot contains no archive-entry targets; the archive
regressions use isolated fixtures, not a claim of a reported local game failure.

## Validation

Evidence is under `build/launch-audit-20260909/`: `polish-build-final.log`,
`polish-ctest.log`, `polish-smoke.log`, and `polish-candidate.json`.
The Release build and all 220 isolated CTests passed, as did the staged app smoke test.
The final production audit preserves all 1,414 previously resolved launch plans.
The new archive tests use fake emulator executables and temporary files. The empty-view
render checks actual QML bindings at 640 by 600; the screenshot was inspected for clipping
and access to Clear Filters. Staging includes desktop and AppStream
validation. Exact final counts and installation paths are recorded in the candidate manifests.

## Local installation and acceptance

The installation helper verifies the clean candidate commit and staged hashes, backs up the
library and settings, and retains the previous installation for rollback. It may switch the
launcher while a game runs, but must preserve the recorder when its binary is byte-identical
and must not reopen Omakade over the game. The installed manifest records those observations
and the backup path. Publication remains separate from local installation.

Still needed: normal SNES, N64, and Dreamcast launches, existing saves and physical controller
input, return to Omakade, and recorded playtime. Live Flatpak and ARM64 validation remain
unperformed. Runtime/core detection is not a benchmark or proof of optimal game settings.

Next work should follow actual acceptance results: launch/return and controller reliability,
then slow or confusing library flows. Keep the established title-specific wrappers intact.

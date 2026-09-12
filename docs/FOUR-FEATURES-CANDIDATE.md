# Four-feature testing candidate

This document tracks the integrated implementation of
[NEXT-FOUR-FEATURES.md](NEXT-FOUR-FEATURES.md). It supersedes the older incremental
handoffs for this candidate. Release publication and maintainer acceptance are separate.

## Included workflows

### RomM library

Open Settings, Connections, RomM Library. Enter the server URL, locally mounted library
folder, and a Client API Token with `roms.read` access. Save and Connect stores the token
in the desktop keyring on a worker and loads supported local games into the library.
RomM participates in source filters, console cards, linking, favorites, hidden games,
metadata/artwork editing, Home, launch setup, and supported save protection.

Test / Refresh replaces a catalog only after a complete successful response. Offline
startup, failed authentication, and a missing mount keep the last successful catalog.
Unavailable files remain visible for repair. Disconnect disables the source; Forget Token
also removes the server's keyring entry. Neither action deletes server files or local games.
No remote game downloads are implemented. Manual/local artwork and existing cover providers
supply artwork. The source's token is not reused for artwork requests.

### Per-installation launch setup

Open a game's Details, Manage, Launch Setup, or expand Launch Setup on the details page.
Keep Automatic or select a supported emulator, an optional libretro core, native/Flatpak,
and an optional repaired game path. Save Setup persists that installation's choices.
Reset to Automatic removes them. Linked installation preference remains a separate choice.

Home, Details, library Play, and `--play` resolve the same saved setup. Direct launches check
files and runtime availability and track process start. Delegated platform requests do not
claim that the game process started. Copy Launch Details excludes credentials and environment
variables and shortens the home directory to `~`.

Launch setup is machine-local in `launch_setups` in the library database. It is excluded from
personal-data archives. Resetting it does not delete game files or saves.

### Save protection and recovery

Open Settings, Backup & Storage, Save Protection Overview. Search and select an installation
to inspect coverage, included files/folders, shared-storage scope, backup count, and the last
capture carrying a verification record. The overview counts shared backup payloads once.
Discovery alone is never presented as a verified backup.

Backups / Restore / Undo uses the same menu as Game Details. Restoration preserves the current
save as an undo version and explains shared impact before confirmation. Existing interrupted
recovery and running-emulator guards remain in force.

Custom layouts accept 1 to 32 explicitly selected regular save files for supported adapters.
Preview lists scope and size before applying. Broad directory selections and symbolic links
are refused. Existing hand-written layout rules are preserved except for the selected
installation's rule. Use Automatic Layout removes that rule, without changing live saves.

Retention accepts 2 to 50 versions; storage accepts 256 to 8192 MiB. Existing migration defaults
remain 10 versions, 2 GiB for save sets, and 256 MiB for legacy SRAM snapshots. Saving a storage
limit applies the chosen limit across the store. Lowering retention does not immediately delete
existing backups. Review Cleanup lists proposed older versions and requires a separate delete
action. Deletion changes backup history only.

### Guided library repair

Open library filters, Repair Library. Filter by source and review reason, inspect platform/path
and identity evidence, then correct identity, choose artwork, or open launch setup/linking.
The details page shows the same review reasons. Same-title games on different platforms are
not duplicate suggestions. Suggestions never merge or delete installations automatically.

Returning from Details resumes the queue. Previous and Next / Skip preserve unresolved entries;
resolved entries leave the queue while the current entry remains available until you continue.
Position and filters persist across restart. Select individual entries for a retry batch of up
to 100 games. Stop Retry cancels pending work without undoing completed corrections. A selected
retry does not restart the background pass over unselected games.

Identity and artwork have separate recovery points. Undo restores the corresponding choice
without replacing the other one. Recovery points persist in `review_undo`; referenced images
are retained under `artwork/review-undo` with a 512 MiB bound. Stop metadata work and let the
current request settle before making or undoing a correction. No source entries are deleted.

## Validation record

The implementation was reviewed against all four planned user journeys. Review fixes covered
credential cancellation ordering, repaired-path persistence, source-specific emulator routing,
legacy save-budget migration, archive validation, shared backup accounting, separate undo,
native fallback from Flatpak source entries, and controller text-entry/focus contracts.

The capped Release build passed with one build job. Focused routing and UI journeys passed,
including actual identity correction/undo and controller text-entry cancellation. Staged
startup, desktop-entry validation, and offline AppStream validation passed. Final full-suite
results and installation are recorded below.

Source candidate: `57721a28ceedb67fbba0ec9e3972a8551a7f61bb` on `codex/1.9-testing`.
The validation/installation documentation commit does not change the compiled sources.

| Binary | SHA-256 |
| --- | --- |
| `omakade` | `beb99fa19e9ab3a003e86f82a2e2b5b29c1a4664b43a661f9c6c0e7b4458aa35` |
| `omakade-sessiond` | `abc00b729d09b5dd728dc638b28f256ef25c07a2024af858dd618691294c2a67` |

## Final results and installation

- All **246 CTest checks passed** in **383.87 seconds** on the final source candidate.
- No QML TypeError, ReferenceError, binding-loop, or assignment errors were found in
  the full test log. The final build had no compiler warnings.
- Staged and installed startup smoke checks, desktop-entry validation, and offline
  AppStream validation passed. Binary hashes and the running recorder path were verified.
- Installed app and recorder: `~/.local/lib/omakade/57721a28ceed-1.9-four-features/`.
  `~/.local/bin` links and the desktop entry now select this candidate.
- The already-open app session was preserved. Close and reopen Omakade to use this build.
  The matching recorder service is active from the candidate directory.
- The candidate directory contains `candidate.json`, validation logs, and a private
  `rollback/` directory holding the previous launch paths, desktop/service configuration,
  Omakade settings, and a consistent SQLite backup. The backup integrity check passed.
- Previous installed build: `3e57cfac49ba-1.9-reviewed`. To restore its app and recorder,
  close Omakade and emulators, then run
  `python3 ~/.local/lib/omakade/57721a28ceed-1.9-four-features/rollback/restore-app.py`.
  This restores launch paths without replacing current library data. The database backup
  is retained separately for deliberate data recovery.
- Implementation and automated validation are complete. Maintainer acceptance and live
  RomM acceptance remain pending. No public release, tag, or main merge was made.

## Maintainer test checklist

1. Use the library normally with mouse/keyboard and controller. Open each new entry point,
   edit a text field in Couch Mode, back out, and confirm focus and selection return correctly.
2. For an emulator game, inspect Launch Setup, save an explicit supported choice, launch and
   exit, restart Omakade, and confirm the choice remains. Reset to Automatic afterward if desired.
3. Inspect Save Protection coverage. On disposable saves, create a backup, change the save,
   restore, undo, and review cleanup. Confirm the displayed shared scope matches expectations.
4. Correct one identity and one artwork choice through Repair Library, continue to another
   game, reopen/restart, then test the separate undo actions. Select a few entries for retry
   and stop the batch. Confirm previously completed choices remain.
5. With a real RomM server, connect and launch a supported game, verify recording/save coverage,
   restart offline, inspect the cached catalog, then reconnect and refresh.

Live RomM acceptance needs a configured server and token. Automated checks use local fixtures.
Physical-controller feel and actual gameplay remain maintainer acceptance checks. No ARM build,
release tag, main-branch merge, or public release is part of this local testing candidate.

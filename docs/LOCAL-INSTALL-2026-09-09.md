# Local install, September 9, 2026

The local candidate includes the fetched upstream main commit `5ba99e6` and the matching, artwork loading, manual-search, and corrupt imported-title fixes from the September 9 audit. It also includes the recent Home, navigation, session recording, and backup work already present in main. Nothing was pushed or released for this installation.

The normal command and desktop launcher point to the versioned local build. The recorder uses the same candidate through a user systemd override. The recording preference is preserved. Package-owned files are unchanged.

The exact source commit, executable paths, SHA-256 checksums, previous executable, and installation time are recorded in `/home/bts/.local/state/omakade/local-install-20260909/installed-candidate.json` and the installed build's `candidate.json`.

Application SHA-256: `a1d668f596b33bc4c78e170f29719ee1ced9ef3132d16b3673eda5abb1214e76`.

## Verification

- Fresh Release build and staged installation.
- 213/213 CTests in isolated config, data, cache, state, runtime, and temporary directories.
- Two SBOM generator tests; desktop and AppStream validation.
- Smoke test of the exact staged application, then installed executable/process verification.
- The full test log is `build/local-install-20260909/ctest.log` in the quality checkout.

## Workspace and recovery

The quality checkout owns the committed candidate. The primary `steam-launcher` checkout is on a local branch at the same commit. Both are clean. Other historical worktrees remain preserved.

The original checkout's earlier patches and quality-plan draft are preserved in stash `9c457db7e78cae47eb650d29b716974f0543ca7d`. A separate patch copy, the old 1.7.1 package archive, settings/desktop backups, and a consistent pre-install library backup are in `/home/bts/.local/state/omakade/local-install-20260909/` (owner-only directory).

The previous application remains at `/home/bts/.local/lib/omakade/5ba99e6/omakade`. To roll back, close Omakade, restore the application command symlink and saved desktop entry, remove the newly added `~/.local/bin/omakade-sessiond` symlink, remove only `~/.config/systemd/user/omakade-sessiond.service.d/local-build.conf`, reload user systemd, and restart the recorder. This returns the recorder to the packaged executable. Do not restore the database backup over newer play history merely to change application binaries.

## Next use

Open Settings → Connections → Update Ratings & Portraits to refresh the existing library. Matching rules and artwork-cache versions have been bumped so previous misses can be retried. First-time downloads still take time.

The audit found 68 recoverable unidentified SNES games and artwork routes for 191 of 243 missing covers. These are measured audit results, not a claim that all library downloads have completed. The remaining 52 covers and unresolved identities are listed in [the audit](matching-audit-2026-09-09/README.md).

Manual acceptance remains: scroll the real SNES library, verify the sample matches and custom-cover priority, and edit both manual search fields. Physical controller, real game/session, and ARM64 hardware acceptance remain separate from the automated checks. Publication still requires explicit approval of the exact candidate.

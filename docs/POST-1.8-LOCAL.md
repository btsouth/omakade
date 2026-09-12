# Post-1.8 local work

> Latest maintenance: [save safety review follow-up](SAVE-SAFETY-REVIEW-2026-09-12.md).
> Three confirmed save defects are fixed with regression coverage; local acceptance is next.

> Latest feature candidate: [emulator save protection](SAVE-SETS.md).

> Latest continuation: [everyday launch and library polish](EVERYDAY-POLISH-2026-09-09.md).

> Latest: [automatic launch audit](AUTOMATIC-LAUNCH-AUDIT-2026-09-09.md) records
> the installed library-review candidate and separately staged launch fixes.

> Later follow-up: [library review and local candidate](library-review-2026-09-09/README.md)
> records the refreshed audit, review filters, and automatic-launch product direction.

> Installation update: the tested candidate is now installed locally. See
> [LOCAL-INSTALL-PROTONDB-2026-09-09.md](LOCAL-INSTALL-PROTONDB-2026-09-09.md)
> for verified runtime paths, rollback and remaining manual acceptance. The
> preparation notes below describe the earlier pre-installation state.

September 9, 2026. All new work stays local. No push, public review, issue comment,
tracker mutation, tag or publication is authorized by this handoff.

## Maintenance candidate

`f8d00f713426626c785ad72c7acca417d2650bbb` is the installed matching/artwork
candidate, with 213 isolated Release tests and staged-install validation recorded
in LOCAL-INSTALL-2026-09-09.md. Its installed manifest was rechecked. Keep that
installation available for SNES matching, artwork priority, manual search editing,
controller and launch/session acceptance. The ProtonDB work is a separate local
continuation, not a replacement installation or a published maintenance release.

## Backlog reconciliation

- #38: the reporter confirmed on GitHub that adding the native wrapper as a
  Manual game, linking it and selecting Manual as preferred solved the problem,
  including restart persistence. No direct-execution override of Steam shortcuts
  is needed for that reported use case. Leave closure for an authorized public pass.
- #42: the report concerns the Omarchy repository shipping an older package.
  The README already provides checksummed direct release package installation.
  Clarify the difference locally; updating the external package repository remains
  a separate action, not a change to the launcher.
- #9: real native/Flatpak launcher and hardware coverage is still external acceptance.
- #44: reviewed and deferred; see reviews/XENIA-2026-09-09.md.
- #33: optional TV/Gamescope work stays deferred pending physical TV/audio testing.

## ProtonDB candidate

Optional cached badges for Steam store App IDs, on desktop cards, Couch Mode and
selected-installation details. The connection starts off. Non-Steam shortcuts,
manual games and emulator identifiers are excluded. No fuzzy title matching.
For linked games, the displayed installation determines whether a badge applies.

The public summary endpoint responded during development, but no stable versioned
provider contract was found. Treat it as best effort: reject unexpected payloads,
preserve stale ratings, back off failures and rate limits, and keep core launching
independent. The existing ProtonDB link remains available without enabling badges.

The provider returns community tiers, not an assurance that this machine or runner
will work. Details show report count and local cache date. Ratings cache for seven
days; missing reports retry after one day. Transient failures retry after one hour
in the running session. Downloads are serialized and normally paced at one second.
Cached summaries are disposable, excluded from backups, and the connection opt-in
is local to the machine rather than imported by library restore.

## Automated validation

- Fresh Release configure/build passed. The final isolated suite passed 215/215
  tests in 345.32 seconds, including the new provider and card-layout tests.
- Provider tests cover opt-in, Steam shortcut exclusion, response validation,
  persisted cache, offline/stale reuse, rate limits, cancellation and settings.
- Actual card renders at 128, 180 and 240 pixels were inspected. Badge bounds
  remain inside each card and switching off hides them.
- Empty staging installation, exact staged smoke test, desktop/AppStream checks,
  two SBOM generator tests and `git diff --check` passed.
- The first full run was invalidated by an overlapping rebuild and was discarded.
  The final run above used the completed build without concurrent compilation.

Evidence is in `build/post18-local/`: `ctest.log`, `staged-smoke.log`,
`sbom-tests.log`, `protondb-cards.png` and `candidate.json`. The manifest records
this local commit and hashes after committing. Build and staged app hashes differ
because installation changes RUNPATH to `$ORIGIN:$ORIGIN/../lib`.

The matching installation's app and recorder hashes were independently rechecked
against its manifest and are unchanged.

## Local acceptance checklist

The new binary is staged at
`build/post18-local/final-stage/usr/bin/omakade`. The installed app and recorder
remain at the matching candidate. Close the running app before testing a different
binary so single-instance activation does not bring the old build forward.

1. In Settings → Connections, enable ProtonDB badges. Browse known Steam store
   games and confirm the badge, report count, link and cache date in details.
2. Restart and disconnect networking. Cached badges should remain; an old cache
   is marked with an asterisk and explained in details. Disable the connection
   and confirm badges disappear.
3. Check desktop small covers and Couch Mode with a physical controller. Verify
   settings focus, details scrolling, Back and return after a game.
4. Inspect a linked Steam/Manual game using each installation. A Steam store
   installation may show its rating; a Manual installation or non-Steam shortcut
   must not inherit an unrelated rating.
5. Continue the existing SNES matching/artwork acceptance on the installed
   maintenance candidate before selecting a new default installation.

No ARM64 build or physical controller/emulator acceptance was performed for the
ProtonDB continuation. Xenia remains unmerged. No publication is scheduled.

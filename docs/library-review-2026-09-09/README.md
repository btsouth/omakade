# Library review, September 9, 2026

> Installation update: the tested candidate is now installed. See
> [installation and rollback](../LOCAL-INSTALL-REVIEW-2026-09-09.md). The preparation
> and validation notes below describe the candidate before installation.

A new read-only SQLite snapshot and image-decoding check confirms the installed matching
candidate has recovered all 68 previously tested identities and 191 previously missing SNES
covers. This check includes custom images, source covers, downloaded portraits, and IGDB
fallbacks. It does not judge whether every cover depicts the correct edition.

| SNES library | Earlier audit | Current snapshot |
| --- | ---: | ---: |
| Games | 1,387 | 1,387 |
| Needs identification | 180 | 115 |
| No usable artwork | 243 | 52 |

The 115 unidentified entries include 13 ambiguous matches. All 68 expected recoveries are
identified; the net improvement is 65 because three older identities now require an edition
choice: Dragon Ball Z: Hyper Dimension, Dragon Ball Z: Super Butouden, and Teenage Mutant
Ninja Turtles: Tournament Fighters. Their cached IDs are retained, not replaced by guesses.

There are 117 distinct SNES games needing either identification or artwork. See
[snes-needs-review.csv](snes-needs-review.csv). Fifty of the 52 missing covers belong to
unidentified games. The other two are Hercules and Hercules 2, whose saved metadata has no
IGDB cover URL. Outside SNES, the RetroArch snapshot also has ambiguous Contra and two
unidentified multicarts. No live identities, artwork choices, ROM files, or emulator settings
were changed by this audit.

## Local improvements

- Library filters now offer Needs Review: identification, missing artwork, or either.
  They combine with existing filters, respect hidden games and console navigation, update
  after a repair, and can be saved. Deliberately removed identities are excluded from the
  identification filter. Missing artwork means the library has no resolved cover path;
  remote image failures are not inferred from the URL.
- Manual game/artwork search starts with ROM dump tags removed. Editions and subtitles stay
  intact, and later typing remains literal and survives metadata updates.
- The metadata completion message includes ambiguous entries in its identification count.

Saved review filters use state version 3. Ordinary filters remain version 2; older builds
cannot apply review filters or restore archives containing them. See BACKUP-FORMAT.md.

## Launch direction

The maintainer wants immediate play with sensible automatic choices, not first-launch setup
questions. Existing source/core choices and preferred linked installations remain unchanged.
No launch-profile picker is included in this candidate.

Future launch work should preserve known-working choices, check installed runners and cores,
and provide a clear recovery action when a required component is unavailable. Game-specific
changes need real launch/gameplay/return and save-continuity evidence. Do not promise that a
catalog priority list, successful process start, or generated frame counter proves optimal
performance. Advanced overrides can remain optional.

Raw snapshots and the isolated check harness remain in `build/library-polish-20260909`.

## Validation and local acceptance

The fresh Release build passed all 218 CTests in 121.07 seconds using private configuration,
data, cache, runtime, and temporary directories. The targeted repair/persistence and search
editing checks passed; keyboard picker selection/clear checks passed at 640x600, 1280x900,
and in Couch Mode. Narrow desktop and Couch Mode renders were visually inspected.
The staged app passed `--smoke-test`; desktop and AppStream validation passed.

The first full test attempt was interrupted because its long private temporary path exceeded
Unix socket limits. Those IPC tests passed with a short private path, followed by the complete
successful suite above. No product source changes were needed for that environment correction.

The candidate is staged at `build/library-polish-20260909/stage/usr/bin/omakade`.
The installed app and recorder remain on the previously accepted `80457ed` build. Nothing from
this follow-up was pushed, tagged, released, or installed over it. Physical controller and
real-game acceptance, ARM64 validation, and the optional real-ROM probe remain outstanding.

When testing the candidate, close the installed app first so single-instance activation does
not bring the previous build forward. Check Filters > Needs Review, identify one ambiguous
game, add a custom cover, and verify each leaves the appropriate filtered view. Confirm manual
search starts without dump tags and keeps a changed query. Save/reopen the review filter and
check Back/controller focus. These checks do not require changing any emulator selection.

# Publication candidate

> Current work is the unpublished [integrated 1.9 testing candidate](1.9-TEST-CANDIDATE.md).
> The September 8 publication record below is historical and does not authorize publishing
> or pushing the 1.9 candidate.

The maintainer authorized pushing reviewed work through btsouth on September 8.
The candidate branch is `codex/feature-quality-local`, draft PR #43. Publication,
tags, release assets, and main remain subject to RELEASING.md and exact-candidate
maintainer acceptance. Version 1.8.0 package links remain prospective until assets
exist. Do not merge this branch before release assets are available.

## Scope and preservation

Home and Up Next, discovery filters, regional details, portrait preservation,
Game & Artwork, navigation fixes, session recording, and backup format 2 are in
this candidate. Follow-ups fix rating tooltips, overlapping Recent captions,
startup artwork refresh, and Home launch feedback.

Recorder settings distinguish the saved preference from a running daemon.
Details identify imported and recorded emulator time. New configurations require
opting in; existing preferences and historical totals are preserved. Full-queue
checks cover the 100-entry limit and navigation. See RECORDING-COVERAGE.md for
what automated recording fixtures establish and what still needs real hardware.

Other worktrees, package archives, checkpoint refs, and deferred PR #33 remain
preserved. The reconciled ancestry and previous evidence are retained in
[the candidate history](PUBLICATION-HISTORY-2026-09-08.md).

## Verification

Record the final commit, local checks, package workflow, and CI results in PR #43
and the local `build/quality-evidence/` manifest. Earlier candidate test counts do
not establish verification of subsequent edits.

Package scanning supplies `GRYPE_DISTRO=arch:rolling` so Arch advisories can be
matched using [Grype’s Arch support](https://oss.anchore.com/docs/capabilities/all-os/).
This is a baseline for ARM64, not complete Arch Linux ARM advisory
coverage. Inspect the scan diagnostics and database status for each candidate.
The optional GitHub AI findings job lacks a Copilot license; it is not a completed
review. Regular build, tests, and CodeQL are separate checks.

Local verification for this follow-up passed: release build, 213/213 isolated
CTest cases (87.34 seconds), two SBOM generator tests, final staged smoke,
desktop entry, and AppStream validation. Narrow and couch recorder renders were
inspected. The full queue fixture verifies traversal, directional movement,
reorder/removal focus, and the model's unavailable-source recovery. The existing
navigation implementation passed once the fixture waited for layout.

## Maintainer acceptance still needed

1. Home scrolling, full Up Next queue, and Play/Details focus return.
2. Keyboard and physical controller: Sources/Filters, Back, and launcher return.
3. Narrow and couch details, tooltips, Other Names, and Game & Artwork controls.
4. Recorder preference/status and imported versus recorded totals.
5. Real launcher return and short-session accounting when game tests resume.
6. Backup preview and restore using disposable data with its recorder stopped.
7. Exact ARM64 package on supported hardware.

Do not restart the paused Z-A diagnostics or change emulator settings as part of
release preparation. Process attribution cannot observe all internal game changes;
provider metadata and artwork coverage remain incomplete. Automated fixtures and
package lifecycle checks do not replace the manual gates above.

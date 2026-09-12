# Save safety review follow-up

The initial review covered the 11 commits after 1.8 through `caca579`, plus the pending WUA
recorder extension. Three save-protection defects were reproduced with disposable files:

- Unreadable subfolders were silently omitted from a successful snapshot. Restoring it after
  restoring folder access deleted the omitted save.
- Missing manifest file lists were converted to empty sets. Restore deleted active saves
  and reported success. The pre-restore undo copy survived.
- RetroArch's `default` directory sentinel was expanded to `/default`, missing real saves.

The fixes abort incomplete directory enumeration, validate snapshot and recovery schemas,
check individual and aggregate byte counts, and resolve RetroArch defaults before path expansion.
Native and Flatpak save roots, overrides, core sorting, and explicit content-directory saves
are covered. The standard Linux defaults follow [RetroArch's Unix frontend](https://github.com/libretro/RetroArch/blob/master/frontend/drivers/platform_unix.c)
and [configuration loader](https://github.com/libretro/RetroArch/blob/master/configuration.c).
Custom RetroArch builds with different defaults still need explicit layouts.

Regression cases cover unreadable roots and subfolders, malformed snapshots and recovery
journals, missing byte counts on empty members, unchanged current saves on refusal, and
successful recovery after repairing a journal. Existing empty-set restore/undo and legacy
single-file backups remain covered. Old snapshots cannot be retroactively certified complete;
the new checks protect future captures and reject structurally malformed existing versions.

The pending WUA recorder fix is committed separately with a test that loads the shipped
profiles and matches both `.wua` and `.WUA` Cemu processes, while ignoring an unrelated process
with the same path. This verifies process matching, not an hour of recorded gameplay.

Validation evidence is under `build/review-sol-20260912/`: the original reproduction,
focused save tests, full Release build and CTest logs, and staged startup/package checks.
On the post-1.8 branch, the Release build passed, all 228 CTest tests passed, and the
focused save suite passed 50 cases with zero skips. Staged startup, desktop entry, and
AppStream validation passed. Fixes are committed as `fcfa395` and `c6bd6bc`.
No real saves are restored by automated validation. The installed candidate is unchanged;
these commits and the staged build are local candidates, not a publication.

## Next acceptance and development

Before publication, test the exact candidate through normal launch, in-game save, exit,
backup listing, physical-controller navigation, and recorded playtime. Test restore only
with a disposable copy of a game/save setup and confirm the undo version works.

Next bug work should use that acceptance evidence, then verify remaining emulator adapters
against real configuration formats. Existing feature improvements should prioritize clear
save coverage and launch/return reliability. Additional emulator or streaming features stay
behind their existing runtime and hardware acceptance gates in `POST-1.8-LOCAL.md`.

## Integration into the newer 1.9 worktree

The initial review missed `/home/bts/Projects/omakade-1.9-testing`, which contained
11 more commits through `54e01d3`. The save fixes and WUA regression were carried
into that branch as `f1da4f3` and `2c7de50`. Its installed candidate remains
`ed79e44a1021fb02aa4bc3604d8a4bed50b37cb8`; do not replace it with the older branch's staged build.

The follow-up inspected backup management, session history, and RomM parser interactions.
It fixed misleading manual-backup success messages at the retention limit and rejected
RomM pages with invalid counts or no pagination progress. New regression cases are committed
with those fixes. A suspected normal session-completion refresh issue was ruled out because
`lastPlayedByPath` changes when the session ends.

Integrated 1.9 compilation and tests remain pending: Cemu was running, and the development
handoff prohibits compilation during gameplay. Static diff/format checks passed. The earlier
228-test result does not validate these newer additions or their integrated build.
After gameplay, use the resource-capped single-job build described in the 1.9 handoff,
run the save suites and RomM parser cases, then the full suite and staged startup/package checks.
Record the exact commit and results before installing or requesting publication approval.

Continue RomM with bounded same-origin requests, secure token storage, and retention of the
last complete catalog on refresh failure. Its parser foundation alone is not a working
RomM integration. Prioritize these correctness checks before expanding the feature surface.

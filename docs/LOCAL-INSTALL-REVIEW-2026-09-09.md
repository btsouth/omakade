# Local library-review candidate installation

Installed source candidate `96acfabe6d2a822ae0bc32bbcd654a40fdd8b194` after the maintainer
requested installation and the launch-selection audit. Its staged hashes matched the
218-test candidate manifest; the installed app also passed an isolated smoke test.

- App and recorder: `/home/bts/.local/lib/omakade/96acfabe6d2a/`
- Previous candidate retained: `/home/bts/.local/lib/omakade/80457ed4d7f7/`
- Rollback: `/home/bts/.local/state/omakade/local-install-96acfabe6d2a-20260909-174715/`

Both command symlinks, the user desktop entry, and recorder service override now select
this candidate. Saved configuration was unchanged. The consistent SQLite backup passed
`PRAGMA quick_check`. Package-owned files were not changed.

Metroid Dread was running during installation. Omakade closed gracefully, but the game and
recorder continued running. The recorder binary is byte-identical to the previous candidate,
so its process was left alone to preserve the active session. Its updated ExecStart takes
effect on the next service restart. The running recorder remained PID 136485 on the previous
path. The launcher was not reopened over the game; its next normal start uses the new binary.

Rollback restores `previous-links.json`, `application.desktop`, and `local-build.conf` from
the backup directory. Reload the user service manager afterward; restart the recorder only
when no game session is active. Binary rollback does not require restoring the library
backup, which would discard newer history.

The installed app smoke check is not physical-controller or gameplay acceptance. New launch
resolver changes are a separate local candidate and are not part of this installation.

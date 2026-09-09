# Local ProtonDB candidate installation

Installed and opened September 9, 2026 after the maintainer asked to continue
locally. Nothing was pushed, tagged or published.

Source candidate: `80457ed4d7f7e39e45f3fdbd62cc02892ac472bb`.

- App: `/home/bts/.local/lib/omakade/80457ed4d7f7/omakade`
- Recorder: `/home/bts/.local/lib/omakade/80457ed4d7f7/omakade-sessiond`
- App SHA-256: `34fac794e9fb375f4f4ba8fa71b9a4ef4f29a5f519f411b95818640855c7efee`
- Recorder SHA-256: `abc00b729d09b5dd728dc638b28f256ef25c07a2024af858dd618691294c2a67`

The normal command symlinks, user desktop entry and recorder service override now
select this candidate. The recorder is active and both running executable paths
were verified. Hyprland confirmed a mapped Omakade window. Saved configuration was
preserved during installation; ProtonDB remains opt-in in Settings → Connections.
Package-owned files were not changed.

## Validation

The unchanged candidate hashes were checked against the previous manifest before
installation. Its completed validation is 215/215 isolated Release tests, staged
smoke, desktop/AppStream checks, two SBOM tests and inspected card renders. No new
source changes were needed for this installation, so the suite was not repeated.

The first installation attempt rolled back automatically after an immediate
recorder executable check failed. The installer now waits for process startup.
The successful attempt verified both executables and the visible window.
The startup log was empty; this does not substitute for interactive acceptance.

## Recovery

The previous matching build remains at
`/home/bts/.local/lib/omakade/f8d00f713426/`.

Rollback files and a consistent SQLite backup are in:
`/home/bts/.local/state/omakade/local-install-80457ed4d7f7-20260909-161409/`.
The directory is owner-only and includes saved desktop/service/config files,
previous symlink targets and the installation manifest. The SQLite backup passed
`PRAGMA quick_check`.

To roll back, close Omakade, restore both command symlinks from
`previous-links.json`, restore `application.desktop` to the user desktop entry and
`local-build.conf` to the recorder service override, then run
`systemctl --user daemon-reload` and restart `omakade-sessiond.service`.
Launch the previous app normally. Do not restore the database merely to change
binaries; doing so would discard newer play history.

## Next local acceptance

1. Enable ProtonDB badges in Settings → Connections and inspect familiar Steam
   games, report counts and links. Restart and check cached results offline.
2. Verify desktop/Couch Mode navigation with the physical controller.
3. Check linked Steam/Manual installation selection and return after a real game.
4. Continue SNES matching, custom artwork priority and manual-search acceptance.

Physical controller, game/session and ARM64 acceptance remain pending. Xenia and
TV helper integration remain deferred as documented in POST-1.8-LOCAL.md.

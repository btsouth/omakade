# Security

## Reporting a vulnerability

Please report vulnerabilities through a
[private GitHub security advisory](https://github.com/btsouth/omakade/security/advisories/new).
Do not open a public issue for secrets, credential exposure, or a working exploit.

Include the affected Omakade version, the shortest reproduction you can provide,
and whether the source launcher is native or Flatpak. Say what an attacker needs
first (another local user, a crafted file, a server you do not control) and what
they get.

## Supported versions

Security fixes go into the latest release only. Fixes land on `main` and ship in
the next release on GitHub Releases and the
[signed package repository](https://github.com/btsouth/pkgs). The Omarchy
Package Repository may carry an older release for a while. Older versions do
not get backported fixes, so check the issue on the latest release before
reporting.

## What to expect

Omakade has one maintainer, so these are targets rather than promises:

- An acknowledgement within 7 days.
- An assessment, and a severity if the report is confirmed, within 14 days.
- A fixed release within 30 days for high or critical issues, and within
  90 days for the rest.

If a fix needs longer, the advisory says why and when to expect it.

## Disclosure and credit

Please keep the details private until a fixed release is out, or until 90 days
have passed since your report, whichever comes first. The advisory is published
when the fix ships, and the release notes link it. Reporters are credited by
name or handle in the advisory and the changelog unless they ask not to be.
There is no bug bounty.

## In scope

- The `omakade` app, including the library, game pages, launching, settings and
  Game Mode
- `omakade-sessiond`, the play-time recorder and resident in-game guide
- `omakade-guide-button` and `omakade-guide-guard`
- The Omarchy guide plugin Omakade installs under `/usr/share/omakade/omarchy-plugin`
- Packaging: the Arch package, its systemd user units and desktop entry, and
  release artifacts with their checksums and attestations
- Backup import and restore, and emulator save restore
- How Omakade reads launcher files, ROMs and disc images, and network responses
- How Omakade stores credentials and exposes local sockets

## Out of scope

- Bugs in the launchers and emulators Omakade reads or starts, such as Steam,
  Lutris, Heroic, RetroArch or Xodus Nexus and its backend. Please report those
  upstream. A way to make Omakade itself misuse them is in scope.
- Omarchy, Hyprland, Qt and other dependencies, unless Omakade uses them unsafely
- Omarchy plugins, emulator cores and scripts you install yourself
- What a game or emulator does once you launch it. It runs with your account's
  rights.
- Attacks that already need your account or write access to your own files,
  such as editing your launcher configuration or Omakade's settings
- Reports without a concrete impact, such as scanner output or a missing
  hardening option

## Trust model

Omakade is a desktop app for one user. It runs as that user and has no
privileged helper, and its background services are systemd user units.

It trusts files your own account controls, such as launcher and emulator
configuration and game manifests. Before running a program it finds in another
app's folder, such as Xodus Nexus's shortcuts and backend, it checks that you
own it and that no other user can write to it.

It treats network responses, ROM and disc image contents, and backup archives
as untrusted. A crash, file write or code execution from a crafted one is in
scope. A backup archive is checked in memory against fixed limits before
anything from it is restored.

Optional credentials (Steam, RetroAchievements, IGDB, SteamGridDB and RomM)
are kept in the desktop Secret Service, not in Omakade's settings or database.

Omakade opens no listening network ports. The in-game guide's local sockets live
in `$XDG_RUNTIME_DIR` and accept only your user. A way for another local user to
control Omakade, or to read its data, is in scope.

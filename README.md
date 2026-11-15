# Omakade

[![CI](https://github.com/btsouth/omakade/actions/workflows/ci.yml/badge.svg)](https://github.com/btsouth/omakade/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-8cd3cb.svg)](COPYRIGHT)

**Your games, beautifully together.**

Every game on your Linux machine in one library that follows your Omarchy theme.
Steam, Heroic, Lutris, Faugus, Battle.net, GOG, Game Pass (through Xodus Nexus) and a
dozen emulators, played with a keyboard, mouse or controller.

[![Omakade library](docs/assets/library-preview.webp)](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)

[Watch the demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4) ·
[Homepage](https://btsouth.github.io/omakade/) · [Guide](docs/GUIDE.md) ·
[Troubleshooting](docs/TROUBLESHOOTING.md) · [Support](SUPPORT.md)

## Features

- **One library** for installed games, plus your owned Steam and Heroic libraries
- **Consoles:** RetroArch, PCSX2, RPCS3, PPSSPP, Ryujinx, Lichii, Cemu, melonDS, shadPS4,
  Dolphin, Xenia and Kyty, with [native PC ports](docs/NATIVE-PORTS.md) found for you
- **[Game Mode](docs/GAME-MODE.md):** a controller-first fullscreen mode for the couch,
  on any display and sound output, that puts your desktop back when you leave
- **[In-game guide](docs/GAME-GUIDE.md)** over any game, and
  **[Highlights](docs/HIGHLIGHTS.md)** that save the last 30 seconds or clip Steam
  achievements as they unlock
- **Stats:** play time, session history and a Year in Review card
- **Organize:** favorites, collections, tags, saved filters and custom artwork
- **Details:** Steam and Xbox achievements, plus optional RetroAchievements, IGDB,
  SteamGridDB and ProtonDB
- **Safe saves:** emulator save backups, library backup and restore, and Sunshine and
  Moonlight export

![Game details, playtime, and achievements](docs/assets/game-details.webp)

Works offline with no account. Games launch through their own launcher or emulator,
which keeps your downloads, updates and cloud saves. Omakade is a community project,
not an official Omarchy app.

## Install

On Omarchy, install it from the Omarchy Package Repository (OPR) and it updates
with your system:

```bash
sudo pacman -S omarchy/omakade
```

OPR can be a release or two behind. To get new releases as soon as they ship, add
my [signed package repository](https://github.com/btsouth/pkgs) (x86_64 and ARM64,
Omarchy or Arch) and install from it:

```bash
curl -fsSL https://pkgs.btso.dev/install.sh | bash
sudo pacman -S btsouth/omakade
```

Normal system updates prefer OPR's copy. To take a newer release early, run
`omarchy update` and then `sudo pacman -S btsouth/omakade`.

Or install a release package directly:

```bash
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.19.0/omakade-1.19.0-1-x86_64.pkg.tar.zst
sudo pacman -U ./omakade-1.19.0-1-x86_64.pkg.tar.zst
```

For ARM64, replace `x86_64` with `aarch64`. To check a package was built by this
repository's release workflow, run
`gh attestation verify <package> -R btsouth/omakade` with the
[GitHub CLI](https://cli.github.com/). Every package is also on the
[releases page](https://github.com/btsouth/omakade/releases/latest) with its
`SHA256SUMS`. Upgrading keeps your library and settings.

Omakade is also in [OmaStore](https://github.com/KitsuneForgering/OmaStore), which
installs it into your home directory along with its services.

## Getting started

Open Omakade from the app launcher or run `omakade`. Installed games show up on their
own. `omakade --demo` opens a sample library.

| Key or button | Action |
| --- | --- |
| `Ctrl+F` | Search |
| F11 or controller Start | Game Mode on this display |
| `Super+Ctrl+G` | Game Mode on your gaming display, or the in-game guide over a game |
| Controller Home | The in-game guide over a game |
| `Ctrl+D` | Settings |

Set up `Super+Ctrl+G`, the Home button and your gaming display in **Settings → Controls**.

## Docs

[Guide](docs/GUIDE.md) · [Game Mode](docs/GAME-MODE.md) ·
[In-game guide](docs/GAME-GUIDE.md) · [Highlights](docs/HIGHLIGHTS.md) ·
[Save protection](docs/SAVE-SETS.md) · [Native ports](docs/NATIVE-PORTS.md) ·
[Compatibility](docs/COMPATIBILITY.md) · [Troubleshooting](docs/TROUBLESHOOTING.md) ·
[Changelog](CHANGELOG.md)

Your library lives in `~/.local/share/omakade/` and settings in
`~/.config/omakade/config.toml`. Passwords and API keys stay in Secret Service.
See [Privacy](PRIVACY.md) and [Security](SECURITY.md).

## Contributing

[Contributing](CONTRIBUTING.md) has the build and test commands, and
[Architecture](docs/ARCHITECTURE.md) maps the source tree.

## Also by me

[Omaroll](https://github.com/btsouth/omaroll), a photo and video viewer and library
for Omarchy that follows your theme.

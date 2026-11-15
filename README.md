# Omakade

[![CI](https://github.com/btsouth/omakade/actions/workflows/ci.yml/badge.svg)](https://github.com/btsouth/omakade/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-8cd3cb.svg)](COPYRIGHT)

**Your games, beautifully together.**

Omakade brings your Linux games into one library that follows your Omarchy theme.
Browse, organize, and launch games from Steam, Heroic, Lutris, Faugus, Battle.net,
GOG, Microsoft Store and PC Game Pass (through Xodus Nexus), and supported emulators
with a keyboard, mouse, or controller.

[![Omakade library](docs/assets/library-preview.webp)](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)

[Watch the demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4) ·
[Homepage](https://btsouth.github.io/omakade/) · [Guide](docs/GUIDE.md) ·
[Compatibility](docs/COMPATIBILITY.md) · [Troubleshooting](docs/TROUBLESHOOTING.md) ·
[Support](SUPPORT.md)

## Features

- One library for installed games, with optional Steam and Heroic owned-library views
- Console libraries for RetroArch, PCSX2, RPCS3, PPSSPP, Ryujinx, Lichii, Cemu, melonDS,
  shadPS4, Dolphin, Xenia, and Kyty (PS5)
- Automatic discovery of known native ports, with links to available ports for your console dumps
- Controller-first fullscreen [Game Mode](docs/GAME-MODE.md), on the display you are
  using or on a chosen display and sound output, with desktop restoration when you leave
- Favorites, collections, tags, saved filters, custom artwork, and a random pick
- Optional playtime recording, session history, stats, and a Year in Review image
- [Highlights](docs/HIGHLIGHTS.md): save the last moments of a game from the in-game guide, a key or a
  controller's capture button, and clip Steam achievements as they unlock
- Steam and Xbox achievements and optional RetroAchievements, IGDB, SteamGridDB, and ProtonDB details
- Emulator save backups, personal library backup and restore, and Sunshine/Moonlight export

![Game details, playtime, and achievements](docs/assets/game-details.webp)

Installed-game discovery and core browsing work offline without an account or API
key. Play uses the owning launcher or emulator; accounts, downloads, updates,
DRM, cloud saves, and compatibility settings stay with those applications.

Omakade is an independent community project, not an official Omarchy application.

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

Open Omakade from the application launcher or run `omakade`. Installed games
appear automatically. Use `omakade --demo` to try a fictional library.

| Control | Action |
| --- | --- |
| `Ctrl+F` | Search |
| Arrow keys / Enter | Navigate / open details |
| Escape | Go back |
| F11 / controller Start | Start or leave Game Mode on this display |
| `Super+Ctrl+G` | Game Mode on your chosen display, or the in-game guide over a game (add it in **Settings → Controls**) |
| Controller Home button | In-game guide over a game, otherwise same as Start (turn it on in **Settings → Controls**) |
| `Ctrl+D` | Settings and source diagnostics |
| `Ctrl+M` | Toggle reduced motion |

Fullscreen is always Game Mode. F11, Start and `omakade --couch` start it on the display
you are using and leave sound and notifications alone. Use **Settings → Controls → Start
Game Mode**, `Super+Ctrl+G` or `omakade --game-mode` for a dedicated gaming display and
sound output.

## User guides

- [Guide](docs/GUIDE.md): sources and everyday features
- [Game Mode](docs/GAME-MODE.md): fullscreen play, a dedicated gaming display, and recovery
- [In-game guide](docs/GAME-GUIDE.md): the overlay that opens over a running game
- [Highlights](docs/HIGHLIGHTS.md): save clips of the last moments of a game
- [Emulator save protection](docs/SAVE-SETS.md): automatic save copies and restores
- [Native ports](docs/NATIVE-PORTS.md): native PC ports of console games
- [Compatibility](docs/COMPATIBILITY.md): what each source supports, and test reports
- [Troubleshooting](docs/TROUBLESHOOTING.md): missing games, sources that are not
  found, launch errors, covers, and logs

## Local data

Your library and custom artwork live in `~/.local/share/omakade/`, settings in
`~/.config/omakade/config.toml`, and downloaded artwork in `~/.cache/omakade/`.
Optional Steam, RetroAchievements, IGDB, SteamGridDB, and RomM credentials are
stored in Secret Service, never in the settings file.

## Contributing

See [Contributing](CONTRIBUTING.md) for dependencies and build and test commands, and
[Architecture](docs/ARCHITECTURE.md) for a map of the source tree.

[Privacy](PRIVACY.md) · [Security](SECURITY.md) · [Changelog](CHANGELOG.md)

## Also by me

[Omaroll](https://github.com/btsouth/omaroll), a photo and video viewer and library
for Omarchy that follows your theme.

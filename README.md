# Omakade

[![CI](https://github.com/btsouth/omakade/actions/workflows/ci.yml/badge.svg)](https://github.com/btsouth/omakade/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-8cd3cb.svg)](COPYRIGHT)

**Your games, beautifully together.**

Omakade brings your Linux games into one library that follows your Omarchy theme.
Browse, organize, and launch games from Steam, Heroic, Lutris, Faugus, Battle.net,
GOG, and supported emulators with a keyboard, mouse, or controller.

[![Omakade library](docs/assets/library-preview.webp)](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)

[Watch the demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4) ·
[Homepage](https://btsouth.github.io/omakade/) · [Guide](docs/GUIDE.md) ·
[Support](SUPPORT.md)

## Features

- One library for installed games, with optional Steam and Heroic owned-library views
- Console libraries for RetroArch, PCSX2, RPCS3, PPSSPP, Ryujinx, Cemu, melonDS,
  shadPS4, Dolphin, and Xenia
- Controller-first Couch Mode, plus [Game Mode](docs/GAME-MODE.md) for a chosen
  display and sound output with desktop restoration when you leave
- Favorites, collections, tags, saved filters, custom artwork, and a random pick
- Optional playtime recording, session history, stats, and a Year in Review image
- Steam achievements and optional RetroAchievements, IGDB, SteamGridDB, and ProtonDB details
- Emulator save backups, personal library backup and restore, and Sunshine/Moonlight export

![Game details, playtime, and achievements](docs/assets/game-details.webp)

Installed-game discovery and core browsing work offline without an account or API
key. Play uses the owning launcher or emulator; accounts, downloads, updates,
DRM, cloud saves, and compatibility settings stay with those applications.

Omakade is an independent community project, not an official Omarchy application.

## Install

On Omarchy:

```bash
sudo pacman -S omarchy/omakade
```

Omakade then updates with your system. OPR may carry an older release.

For Omarchy or Arch, my [signed package repository](https://github.com/btsouth/pkgs)
provides x86_64 and ARM64 packages. Add it once, then install:

```bash
curl -fsSL https://pkgs.btso.dev/install.sh | bash
sudo pacman -S btsouth/omakade
```

On Omarchy, use `sudo pacman -Syu btsouth/omakade` to update from this repository
explicitly; normal system updates prefer OPR's copy.

You can also download and verify a release package directly:

```bash
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.15.0/omakade-1.15.0-1-x86_64.pkg.tar.zst
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.15.0/SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
sudo pacman -U ./omakade-1.15.0-1-x86_64.pkg.tar.zst
```

For ARM64, replace `x86_64` with `aarch64`. Packages are also available under
[release assets](https://github.com/btsouth/omakade/releases/latest).
Upgrading preserves your library and settings.

Omakade is also listed in [OmaStore](https://github.com/KitsuneForgering/OmaStore),
a community app store that installs it into your home directory. That install
does not set up the `omakade-sessiond` service, so playtime recording stays off
unless you start it yourself. Use a package above if you want playtime recording.

## Getting started

Open Omakade from the application launcher or run `omakade`. Installed games
appear automatically. Use `omakade --demo` to try a fictional library.

| Control | Action |
| --- | --- |
| `Ctrl+F` | Search |
| Arrow keys / Enter | Navigate / open details |
| Escape | Go back |
| F11 / controller Start | Toggle Couch Mode |
| `Super+Ctrl+G` | Return to the desktop / resume Game Mode (add in Settings) |
| Controller Home button | Same as `Super+Ctrl+G` (turn on in Settings) |
| `Ctrl+D` | Settings and source diagnostics |
| `Ctrl+M` | Toggle reduced motion |

`omakade --couch` starts fullscreen. Use **Settings → Controls → Start Game Mode**
or `omakade --game-mode` for a dedicated gaming display and sound output.

The [guide](docs/GUIDE.md) covers sources, owned libraries, organization, recording,
backups, and streaming. See the [Game Mode guide](docs/GAME-MODE.md) for setup and recovery.

## Build

See [Contributing](CONTRIBUTING.md) for dependencies and build/test commands.

## Local data

Your library and custom artwork live in `~/.local/share/omakade/`, settings in
`~/.config/omakade/config.toml`, and downloaded artwork in `~/.cache/omakade/`.
Optional Steam and IGDB credentials use Secret Service.

[Privacy](PRIVACY.md) · [Changelog](CHANGELOG.md) ·
[Compatibility reports](docs/COMPATIBILITY.md) · [Security](SECURITY.md)

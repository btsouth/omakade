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
- Console libraries for RetroArch, PCSX2, RPCS3, PPSSPP, Ryujinx, Cemu, melonDS,
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

On Omarchy, install it from the Omarchy Package Repository (OPR):

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

On Omarchy, normal system updates prefer OPR's copy. To take this repository's
newer version early, run `omarchy update` and then `sudo pacman -S btsouth/omakade`.

You can also download and verify a release package directly:

```bash
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.18.0/omakade-1.18.0-1-x86_64.pkg.tar.zst
curl -fLO https://github.com/btsouth/omakade/releases/download/v1.18.0/SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
sudo pacman -U ./omakade-1.18.0-1-x86_64.pkg.tar.zst
```

For ARM64, replace `x86_64` with `aarch64`. Packages are also available under
[release assets](https://github.com/btsouth/omakade/releases/latest).
Upgrading preserves your library and settings.

`SHA256SUMS` comes from the same place as the package, so it only catches a damaged
download. To check that a package was built by this repository's release workflow,
verify its GitHub build attestation with the [GitHub CLI](https://cli.github.com/):

```bash
gh attestation verify omakade-1.18.0-1-x86_64.pkg.tar.zst -R btsouth/omakade
gh attestation verify omakade-1.18.0-1-aarch64.pkg.tar.zst -R btsouth/omakade
```

Omakade is also listed in [OmaStore](https://github.com/KitsuneForgering/OmaStore),
a community app store that installs it into your home directory. OmaStore 0.4
and newer also set up the `omakade-sessiond` service, so playtime recording works
there too, and the controller Home button service, which stays off until you turn
it on in Settings.

## Getting started

Open Omakade from the application launcher or run `omakade`. Installed games
appear automatically. Use `omakade --demo` to try a fictional library.

| Control | Action |
| --- | --- |
| `Ctrl+F` | Search |
| Arrow keys / Enter | Navigate / open details |
| Escape | Go back |
| F11 / controller Start | Start or leave Game Mode on this display |
| `Super+Ctrl+G` | Start Game Mode on your chosen display; over a game, open the in-game guide; in Game Mode, return to the desktop or resume (add it in **Settings → Controls**) |
| Controller Home button | In-game guide over a game; otherwise same as Start; hold it to switch Game Mode and the desktop (turn it on in **Settings → Controls**) |
| `Ctrl+D` | Settings and source diagnostics |
| `Ctrl+M` | Toggle reduced motion |

Fullscreen is always Game Mode. F11, Start and `omakade --couch` start it on the display
you are using and leave sound and notifications alone. Use **Settings → Controls → Start
Game Mode**, `Super+Ctrl+G` or `omakade --game-mode` for a dedicated gaming display and
sound output.

The [guide](docs/GUIDE.md) covers sources, Home, owned libraries, organization,
connections, recording, stats, backups, streaming, and the command line.

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

## Build

See [Contributing](CONTRIBUTING.md) for dependencies and build/test commands, and
[Architecture](docs/ARCHITECTURE.md) for a map of the source tree.

## Local data

Your library and custom artwork live in `~/.local/share/omakade/`, settings in
`~/.config/omakade/config.toml`, and downloaded artwork in `~/.cache/omakade/`.
Optional Steam, RetroAchievements, IGDB, SteamGridDB, and RomM credentials are
stored in Secret Service, never in the settings file.

[Privacy](PRIVACY.md) · [Changelog](CHANGELOG.md) ·
[Compatibility](docs/COMPATIBILITY.md) · [Security](SECURITY.md)

## More for Omarchy

- [Omaroll](https://github.com/btsouth/omaroll): a photo and video viewer and library that follows your theme.
- [Omaframe](https://github.com/btsouth/omaframe): screenshots and screen recordings, with frames and backgrounds.
- [Omalink](https://github.com/btsouth/omalink): your Android phone on the desktop, through KDE Connect.

# Omakade

[![CI](https://github.com/tsouth89/omakade/actions/workflows/ci.yml/badge.svg)](https://github.com/tsouth89/omakade/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-8cd3cb.svg)](COPYRIGHT)

**Your games, beautifully together.**

[![Omakade library showing installed games from multiple launchers](docs/assets/library-preview.webp)](https://tsouth89.github.io/omakade/assets/omakade-demo.mp4)

[Watch the 18-second demo](https://tsouth89.github.io/omakade/assets/omakade-demo.mp4)

Omakade is a fast, local-first game library built for Omarchy. It brings
installed Steam, Lutris, Heroic, Faugus, RetroArch, Battle.net, Epic, GOG, and Amazon games
into one quiet, cover-focused home that follows the active Omarchy theme.

[Project homepage](https://tsouth89.github.io/omakade/) ·
[Roadmap](PLAN.md) · [Support](SUPPORT.md)

> Omakade is an independent community project. It is not an official Omarchy
> application.

## Current release

Omakade includes:

- Native and Flatpak Steam, Lutris, Heroic, Faugus, and RetroArch discovery,
  including games sideloaded into Heroic, plus Battle.net games from Wine,
  Proton, and Bottles prefixes
- One-click details and delegated launching through the owning platform
- Omarchy palette, font, transparency, and live theme updates
- Search, favorites, hidden games, sorting, and source filters
- Runtime source controls with scan status and detected locations
- Optional close-after-launch behavior
- Collections, tags, completion states, and smart organization filters
- Local Steam achievements plus optional Web API enrichment
- Optional Steam owned-library sync with installed and ready-to-install views
- Optional IGDB critic aggregates and game-length estimates
- Local, downloaded, and user-selected cover artwork
- Explicit linking for games installed through multiple sources
- ProtonDB and PCGamingWiki shortcuts with actionable launch errors
- Keyboard, mouse, and controller navigation

![Omakade game details showing playtime, IGDB insights, and Steam achievements](docs/assets/game-details.webp)

Omakade reads launcher data without modifying it. Core discovery, browsing,
artwork, and launching work offline. Run `omakade --demo` to explore the UI
with a deterministic fictional library.

## Install on Omarchy or Arch

### Install or upgrade from the Omarchy Package Repository

On Omarchy, install Omakade from OPR with:

```bash
sudo pacman -S omarchy/omakade
```

After that, Omakade updates with normal Omarchy system updates.

### Install or upgrade from the terminal

These commands download Omakade and its checksum into the current directory,
verify the package, and install it. If Omakade is already installed, `pacman -U`
upgrades it in place without removing your settings or library data:

```bash
curl -fLO https://github.com/tsouth89/omakade/releases/download/v1.3.1/omakade-1.3.1-1-x86_64.pkg.tar.zst
curl -fLO https://github.com/tsouth89/omakade/releases/download/v1.3.1/SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing
sudo pacman -U ./omakade-1.3.1-1-x86_64.pkg.tar.zst
```

### Install or upgrade from a browser download

1. Open the [latest release](https://github.com/tsouth89/omakade/releases/latest).
2. Under **Assets**, download `omakade-1.3.1-1-x86_64.pkg.tar.zst` and
   `SHA256SUMS` into the same folder.
3. Open a terminal in that folder and run:

```bash
sha256sum -c SHA256SUMS --ignore-missing
sudo pacman -U ./omakade-1.3.1-1-x86_64.pkg.tar.zst
```

Launch Omakade from the application launcher or run `omakade` in a terminal.

### Include uninstalled Steam games

Omakade shows installed games by default. To include the rest of your Steam
library, open Settings, save your Steam ID and Web API key, then select **Sync
owned Steam library**. Your Steam Game Details must be public. After syncing,
use **All Games** or **Ready to Install** in the library. Installation is handed
off to Steam.

Omakade keeps its local library and settings when the package is upgraded or
removed. The owning launchers remain responsible for games, accounts, updates,
cloud saves, DRM, and compatibility tools.

RetroArch games come from its configured playlists. Omakade uses local
RetroArch thumbnails and runtime logs, then launches each game with its assigned
core. Entries without a core association remain visible and explain how to fix
launching after you press Play.

Battle.net games come from the Battle.net Agent database inside a Wine, Proton,
or Bottles prefix. Omakade launches each title through that prefix's Battle.net
client. Wine, umu-launcher, or Bottles must be installed to play.

## Build

Requirements:

- CMake 3.24 or newer
- Ninja
- C++20 compiler
- Qt 6.8 or newer with Concurrent, Core, Gui, Network, Qml, Quick, Quick
  Controls, SQL, and Test, plus the SVG and image format plugins
- SDL 3
- libsecret

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/omakade
```

Use `Ctrl+F` to search, arrow keys to navigate, Enter to open details, Escape
to return, and F11 to toggle fullscreen. `Ctrl+M` toggles reduced motion and
`Ctrl+D` opens settings and source diagnostics.

## Local data

- Library: `~/.local/share/omakade/library.sqlite3`
- Settings: `~/.config/omakade/config.toml`
- Downloaded artwork: `~/.cache/omakade/`
- Selected custom covers: `~/.local/share/omakade/artwork/`

Core library discovery, local achievements, artwork, search, organization,
controller navigation, and launching require no Steam API key or network
connection. An optional Steam connection can sync public owned games and hand
uninstalled titles to Steam for installation. Optional Steam and IGDB
credentials are stored by Secret Service, and cached metadata stays available
offline.

See [PRIVACY.md](PRIVACY.md) for retained data and external requests,
[CHANGELOG.md](CHANGELOG.md) for release notes, and the current
[compatibility report](docs/COMPATIBILITY.md) for tested platform layouts.

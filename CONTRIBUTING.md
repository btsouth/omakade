# Contributing to Omakade

Bug reports and focused improvements are welcome. See [Support](SUPPORT.md) for
useful report details and [Security](SECURITY.md) for private vulnerability reports.

## Build from source

Requirements:

- CMake 3.24+, Ninja, pkg-config, and a C++20 compiler
- Qt 6.8+ with Concurrent, Core, Gui, Network, Qml, Quick, Quick Controls, SQL,
  and Test, plus the SVG and image format plugins
- SDL 3, OpenSSL, zstd, libsecret, libzip, and LayerShellQt
- Python 3 for the tests
- Optional Wayland headers, protocols, scanner, and Qt GuiPrivate for idle inhibition

On Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf python \
  qt6-base qt6-declarative qt6-svg qt6-imageformats sdl3 openssl zstd \
  libsecret libzip layer-shell-qt wayland wayland-protocols
```

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/omakade
```

The `release` presets configure and test a Release build instead. Some tests use
Qt Quick, session services, or desktop fixtures; run them in an isolated test
desktop when working from your everyday Omarchy session.

## Useful references

- [User guide](docs/GUIDE.md)
- [Controller and keyboard navigation](docs/CONTROLLER-NAVIGATION.md)
- [Library backup format](docs/BACKUP-FORMAT.md)
- [Emulator save protection](docs/SAVE-SETS.md)
- [Recording coverage](docs/RECORDING-COVERAGE.md)
- [Game Mode](docs/GAME-MODE.md)
- [Release procedure](RELEASING.md)

Keep launcher discovery read-only and test parsing or recovery changes with
fixtures and disposable data. Launchers retain responsibility for accounts,
installation, updates, and compatibility tools.

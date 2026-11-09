# Contributing to Omakade

Bug reports and focused improvements are welcome. See [Support](SUPPORT.md) for
useful report details and [Security](SECURITY.md) for private vulnerability reports.

## Build from source

Requirements:

- CMake 3.25+ (the presets use schema version 6), Ninja, pkg-config, and a C++20
  compiler
- Qt 6.8+ with Concurrent, Core, DBus, Gui, GuiPrivate, Network, Qml, Quick, Quick
  Controls, SQL, and Test, plus the SVG and image format plugins. GuiPrivate is
  required; Arch ships it in `qt6-base`
- SDL 3, OpenSSL, zstd, libsecret, libzip, and LayerShellQt
- Python 3 for the tests
- Optional Wayland client headers, protocols, and scanner for idle inhibition. Without
  them the build still works and the screen can blank during a game

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

The `release` presets configure and test a Release build instead.

## Tests without a desktop

The suite needs no display. The test registrations set `QT_QPA_PLATFORM=offscreen`
and Qt Quick's software backend, and CI runs the suite in a plain container. When you run it from your own desktop session, detach it from that
session so no test can reach your compositor, notifications, or Secret Service:

```bash
env -u WAYLAND_DISPLAY -u DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE \
  dbus-run-session -- ctest --preset dev --output-on-failure
```

`dbus-run-session` comes with the `dbus` package. Running the suite in a container,
as CI does, works too.

To look at the app without touching your own library or window, give it a scratch
home, its own D-Bus session and a nested compositor window, such as `weston` or a
nested Hyprland:

```bash
scratch=$(mktemp -d)
mkdir -m700 "$scratch/run"
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_STATE_HOME \
  -u HYPRLAND_INSTANCE_SIGNATURE -u DISPLAY \
  HOME="$scratch" TMPDIR="$scratch" XDG_RUNTIME_DIR="$scratch/run" \
  WAYLAND_DISPLAY="$XDG_RUNTIME_DIR/wayland-1" \
  dbus-run-session -- ./build/dev/omakade --demo
```

Omakade's single-instance and guide sockets live in `XDG_RUNTIME_DIR`, so the scratch
runtime dir keeps it from handing the command to an Omakade window that is already
open. Because the Wayland socket is looked up there too, `WAYLAND_DISPLAY` is the full
path in your real runtime dir. `wayland-1` is an example; use the socket name your
nested compositor prints. Under a nested Hyprland, set `HYPRLAND_INSTANCE_SIGNATURE`
to that instance's signature instead of unsetting it, or Game Mode cannot drive it.

The scratch runtime dir also hides your audio server's socket, so the demo usually
plays no sound. It does not isolate your systemd user manager: the controller-button
switch in Settings enables or disables the real `omakade-guide-button` service, so
leave it alone here. To try audio switching or that service safely, use an isolated
test desktop such as omabox.

## Useful references

- [Architecture](docs/ARCHITECTURE.md): what each folder owns and how to add a
  launcher source
- [User guide](docs/GUIDE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Controller and keyboard navigation](docs/CONTROLLER-NAVIGATION.md)
- [Library backup format](docs/BACKUP-FORMAT.md)
- [Emulator save protection](docs/SAVE-SETS.md)
- [Recording coverage](docs/RECORDING-COVERAGE.md)
- [Game Mode](docs/GAME-MODE.md)
- [Release procedure](RELEASING.md)

Keep launcher discovery read-only and test parsing or recovery changes with
fixtures and disposable data. Launchers retain responsibility for accounts,
installation, updates, and compatibility tools.

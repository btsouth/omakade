# In-game guide

The guide is the `omakade.guide` Omarchy shell overlay in `omarchy-plugin/`.
Omakade owns game discovery, controller input and pause state. The shell renders the
approved D2 interface. Install and enable the plugin before using the native guide.
On other desktops, Omakade retains its existing Game Mode controls.

While a known game runs, the existing Super + Ctrl + G shortcut and the controller
Home button open or close the guide. With no game, the shortcut keeps its existing
Game Mode start, desktop and resume behavior. In Game Mode, F11 and the controls action use the guide
on Omarchy. `omakade --guide-toggle` opens it directly, including without a game.

A cold shortcut over a recognized running game starts Omakade with the guide,
without entering the library's Game Mode first.

Guide-button detection still uses `omakade-guide-button` and its short, solitary press
policy. It forwards the originating event node. While open, Omakade grabs all detected
Guide-capable controller evdev nodes, including virtual pads, and translates their
physical button positions, hats and calibrated left stick into guide actions. The
keyboard uses the same actions. B, Guide and Start close the guide; LB/RB change tabs.
Grabs are per device. A pad that cannot be grabbed does not block opening; the guide
keeps reading any pad it can open and warns that the named pad may still reach the game.
A pad it cannot open is skipped with a warning. A disconnected reader or a dropped input report closes
and releases every grab. Omakade's SDL navigation remains in the library, while guide
navigation uses the separate evdev translator.

## Game data and pause

Recorder sessions from `omakade-sessiond`, direct Omakade launches, and Steam process
app IDs supply game identity. Launcher-backed data does not require play-session
recording. Launcher processes are never expanded to guess which game to pause.
The guide uses the game's actual window monitor, falling back to the Game Mode output.
Opening makes that game accessible, and closing restores its focus after the overlay
parks. Unknown playtime, achievements, art and notes remain absent.

Pause defaults on. A library tag `online` or `multiplayer` defaults it off; the guide's
Pause control saves a per-game override. Games without a verified process start time
remain running, and the guide reports the actual paused state. Omakade does not infer
whether an untagged native game is online.

`omakade-guide-guard` pins the game process and its descendants with pidfds, checks
process start identities, stops parents before descendants, and resumes only processes
it stopped. Its stdin is owned by Omakade: close, normal exit, a crash or SIGKILL of
Omakade closes the pipe and sends SIGCONT. Controller grabs belong to Omakade's file
descriptors and are released by RAII or process death. Processes already stopped by
another owner are never claimed. Before each stop, the guard reports the identity and waits for Omakade to pin it
with a pidfd. Omakade watches guard exit and resumes that recorded tree itself,
including guard death during pause setup.

EVIOCGRAB only isolates evdev readers. Physical hidraw readers, Steam Input behavior
and buffered input after SIGCONT still need controller/game hardware acceptance.
Pausing an online game can disconnect it, so tag it or switch Pause off before use.

## IPC v1

Omakade sends `omarchy-shell shell summon omakade.guide JSON`, then pushes complete data
snapshots with `omarchy-shell shell call omakade.guide update JSON` while open:

```json
{
  "version": 1,
  "output": "DP-2",
  "pad": "xbox",
  "backend": {"socket": "/run/user/1000/omakade-guide-1000", "token": "per-open-token"},
  "data": {
    "game": {
      "title": "A running game",
      "source": "Steam",
      "kind": "steam",
      "cover": "file:///path/to/cover.jpg",
      "banner": "file:///path/to/hero.jpg",
      "sessionMinutes": 12,
      "totalMinutes": 180,
      "achievements": {"unlocked": 3, "total": 20},
      "pauseWhileOpen": true,
      "paused": true
    }
  }
}
```

Optional unknown fields are omitted. Without a game `data` is empty. Families are
`keyboard`, `xbox`, `playstation`, `nintendo`, `deck`, or `generic`. The local socket is
user-only and receives newline-delimited `{version:1, token, action, value}` messages
for opened, closed and native actions. The socket also carries optional newline-delimited input, update and toast messages
from Omakade. Input is ordered on that persistent connection. Old v1 payload
readers ignore the added optional fields. Shell lifecycle commands remain serialized. `input`, `state` and `update` are callable root methods. An open-only
heartbeat releases ownership if the shell disappears or opens a different payload.

The fixture payload (`fixture`, `tab`, `pad`, `scale`, `audit`) remains supported by
preview tooling. A generic summon with `{}` clears game and notes and opens System,
with a real clock and independent quick settings. Generic capture, audio, media, batteries and quick settings stay plugin-owned.
Game controls are absent without a known game; unknown hardware values stay absent.

Frozen capture resets the source on each open and waits for `ScreencopyView.hasContent`
before mapping the overlay. If capture is unsupported, it opens after a bounded 750 ms
with the theme scrim. Theme changes snapshot the complete previous panel and fade it
out over the new panel in 180 ms; reduced motion applies the new theme immediately.

## Game actions

Notes use one local text file per source and game identity under Omakade's data
folder, in `guide-notes`. A keyboard edits the text, Ctrl+S saves, and Escape
returns. Controllers see read-only notes with an Edit with a keyboard hint. There
is no on-screen keyboard. Notes are limited to 8 KiB and writes are atomic.

Quit resumes first, then sends SIGTERM to the game's pinned process tree. After
five seconds, a surviving tree enables explicit Force quit with SIGKILL. The guide
stays open during that grace period. Tracked games also use the play-session store's
stop path so the library reflects the pending stop. Generic mode uses Hyprland's close-window
request, then checks the original window and process identity before force quit.
Library and Desktop resume the game and leave the guide. Steam handoff resumes,
closes, restores the known window and sends Shift+Tab after 250 ms.

A directly launched emulator retains its actual launch save context. Backup uses
the existing save-layout resolver and SaveSetStore, pauses the known tree while
copying, and keeps ten timestamped versions under `guide-backups`. The control
hides for recorder-only sessions and unresolved layouts. It never guesses paths
from an emulator name. Existing backup integrity and size limits still apply.

Omakade adds `MANGOHUD=1` only to directly launched Manual games and emulators when
MangoHud is installed. Each launch has a unique configuration and abstract control
socket. The HUD always starts hidden. Steam shows the launch option
`MANGOHUD=1 %command%`; games with no verified control socket show setup guidance.
The Performance page reports driver-provided CPU/GPU, RAM/VRAM, temperature and
power counters where available. It omits unavailable sensors and FPS telemetry.

MangoHud's normal Vulkan/OpenGL socket accepts `:hud;` to toggle visibility. It
supports neither selecting HUD detail nor setting a frame limit. `mangohudctl`
uses a separate System V protocol for mangoapp and does not control the normal
injected HUD. Off/FPS/FPS+frametime/Full and Off/30/40/60/120/display-rate choices
save preferences; visibility changes now when connected, detail and limit apply
on the next launch, as the UI says. No external FPS route was verified.
Sources: [socket implementation](https://github.com/flightlessmango/MangoHud/blob/master/src/control.cpp),
[normal socket client](https://github.com/flightlessmango/MangoHud/blob/master/control/src/control/__init__.py),
[mangoapp client](https://github.com/flightlessmango/MangoHud/blob/master/src/app/control.c).

Controller identify uses a 500 ms rumble effect on Omakade's existing evdev fd
while the native guide is open. Generic mode uses the plugin helper. Controllers
without rumble support report an error. Physical rumble requires hardware testing.

See `omarchy-plugin/README.md` for capture paths, replay, audio, batteries, settings
and couch scale. Those features work independently of Omakade.

## Isolated verification

Build and full CTest run on devbox. For graphical acceptance, mount the plugin in an
omabox and copy the matching devbox binaries into ignored `build/guide-i1/`:

```bash
omabox up --plugin "$PWD/omarchy-plugin" --net isolated
omabox run -- python3 tools/guide-i1/seed.py
omabox run -d -- build/guide-i1/omakade --guide-input-test
omabox run -d -- /usr/lib/qt6/bin/qml "$PWD/tools/guide-i1/FullscreenGame.qml"
omabox run -d -- build/guide-i1/omakade-sessiond
omabox run -- build/guide-i1/omakade --guide-toggle
omabox wait cmd -- bash -c 'test "$(omarchy-shell omakade.guide ready)" = ready'
omabox run -- python3 tools/guide-i1/inject.py 311
```

The seed writes only the box's private library and recorder profile. Enable
`track_play_sessions = true` there to exercise recorder data, or launch the private
manual entry through Omakade with recording disabled to exercise launcher data.
The moving client is truly fullscreen. Raw event injection is explicitly enabled
by `--guide-input-test`; production sessions reject it. It exercises the same mapping
used by the evdev reader, without claiming that a box has real controller devices.

Native tests cover payload and plugin parser agreement, unknown data, button mapping,
axis calibration/hysteresis, family selection, process-tree pause, identity refusal,
pipe-loss and guard-death resume, per-device grabs, notes storage, quit escalation,
MangoHud configuration and Auto couch-scale boundaries. A box covers shell IPC, capture, navigation, themes, process
signals and compositor focus. Physical evdev grabs, hidraw/Steam Input leak paths and
multiple physical monitors remain hardware checks.

For input latency, run `tools/guide-i2/latency.py` inside the box. Native
`--guide-input-test` emits receive-to-focus-acknowledgment timing, including the
return trip. `OMAKADE_GUIDE_LEGACY_INPUT=1` selects the old per-input shell spawn
only in that test mode. `OMAKADE_GUIDE_TEST_UNGRABBABLE=1` injects a refused pad
access in the same opted-in mode; it does not claim physical-device acceptance.

The generic Game library tile appears only when Omakade is installed. Steam Deck
bumper hints use L1/R1; other pad families keep their printed labels.

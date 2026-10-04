# In-game guide (I1)

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
A failed grab refuses to open. A disconnected reader or a dropped input report closes
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
another owner are never claimed. The guard itself being forcibly killed cannot run
cleanup.

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
for opened, closed and native actions. Shell commands are serialized so input ordering
is preserved. `input`, `state` and `update` are callable root methods. An open-only
heartbeat releases ownership if the shell disappears or opens a different payload.

The fixture payload (`fixture`, `tab`, `pad`, `scale`, `audit`) remains supported by
preview tooling. A generic summon with `{}` clears game and notes and opens System,
with a real clock and independent quick settings. I2 actions show their unavailable
state rather than reporting a fictional screenshot, recording or replay.

Frozen capture resets the source on each open and waits for `ScreencopyView.hasContent`
before mapping the overlay. If capture is unsupported, it opens after a bounded 750 ms
with the theme scrim. Theme changes snapshot the complete previous panel and fade it
out over the new panel in 500 ms; reduced motion applies the new theme immediately.

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
and pipe-loss resume. A box covers shell IPC, capture, navigation, themes, process
signals and compositor focus. Physical evdev grabs, hidraw/Steam Input leak paths and
multiple physical monitors remain hardware checks.

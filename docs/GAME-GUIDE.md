# In-game guide

Press the controller Home button or Super + Ctrl + G over a known running game.
One Omarchy card opens over it, with your theme's colors, type and spacing.
Resume starts the game again. B or Escape backs out of a list or confirmation
first, then resumes. With no game, Home and the shortcut keep the existing
Game Mode behavior.

The card has Resume, Return to desktop, Game library, Screenshot, Record clip,
Volume and Quit. Steam games with achievement data get an in-card list, with
recent unlocks first. Sound output cycles available outputs. Save last N seconds
appears while a replay buffer runs. The header shows session time, the clock,
controller batteries reported by UPower, and available performance readings.
Unknown readings stay absent.

Return to desktop keeps the game paused and parked. Home restores the game and
opens the guide while it is still paused; B or Resume starts it again. A managed
Game Mode session uses the same park and resume path as the Game Mode button.
Games launched outside Game Mode retain their verified window and pause guard.
Game library opens Omakade and resumes the game.

## Setup

The package includes `omakade.guide`. Omakade enables it on Omarchy when no game
is running. `omarchy plugin disable omakade.guide` turns it off and restores the
existing Game Mode controls. Enable it again with `omarchy plugin enable
omakade.guide`. Other desktops keep Game Mode.

The resident backend runs in `omakade-sessiond`; Home does not load the library
GUI. Source builds can link `omarchy-plugin/` into the private test desktop's
plugin directory and enable it there.

## Pause and input

Pause defaults on; games tagged online or multiplayer default to running.
Existing per-game pause preferences are respected. The card reports the actual
pause state. Return to desktop explicitly pauses the game before parking it.

The guide pins verified process identities with pidfds. Its guard stops only the
processes it owns and resumes them on Resume, normal exit, or backend death.
Parking retains that guard until Home and Resume, or backend exit. A pad that
cannot be grabbed produces a notification; available readers keep working.

One controller gesture moves one row. Hats, sticks and buttons share a report;
physical and virtual mirrors are grouped. B, Home and Start require release
before firing again. Keyboard arrows, Enter, Escape and Y use the same actions.
Quit asks first, then sends SIGTERM to the pinned tree. A surviving game offers
Force quit after five seconds.

## Capture, sound and scale

Screenshot and Record clip hide both the card and scrim and wait for a submitted
guide-free frame and at least 80 ms. Screenshot then captures the game's output;
Record clip closes the guide and starts Omarchy's recorder.
Next to an existing replay buffer the plugin starts its own recording instead.
Screenshot success is reported only after a file exists. Volume and sound output
use Omarchy's output selection and PipeWire.

Couch Mode sends top-level `scale: 1.7`, matching Omakade's existing couch reading
scale. The running library window supplies its current mode; without a library
window the resident service reads `couch_mode_enabled` from `config.toml` at
startup. Desktop mode sends 1. The card scales its Omarchy sizes and gaps and
shrinks to fit the output. There is no separate guide scale setting.

Frame readings come only from an existing source associated with the game:
MangoHud's explicitly configured `output_folder` CSV for that executable, created
during the process's lifetime, or a gamescope ancestor's `--stats-path` regular
file. Only bounded file samples are read. Files older than five seconds, invalid
values, FIFOs and unavailable sources are omitted. MangoHud provides FPS and
frame time in milliseconds; gamescope's stats file provides FPS alone. The guide
does not start logging or infer frame time from FPS. CPU/GPU readings are read
by the plugin from available drivers.

Formats: [MangoHud logging](https://github.com/flightlessmango/MangoHud/blob/master/src/logging.cpp)
and [gamescope stats](https://github.com/ValveSoftware/gamescope/blob/master/src/steamcompmgr.cpp).

## IPC

The initial `omarchy-shell shell summon omakade.guide JSON` payload contains
version 1, output, pad, scale, backend socket/token and data. Data includes the
game title, source, pause state, known session time, art, achievements and
available frame readings. Achievement items contain title, description,
unlocked, when, icon, rarity and hidden; secret descriptions remain absent until
unlocked.

After opening, newline-delimited socket messages keep the shape
`{type: "update", payload: {...}}`. `payload.delta: true` marks changed fields in
`data`: absent fields retain their previous value, null removes a field, arrays
replace as a whole. Unchanged payloads send nothing. Static achievements and art
are sent on open and again only when their content changes. The integrated
plugin merges a delta before validating the complete version 1 data.

The user-only socket accepts `{version: 1, token, action, value}`. Controller input
and updates share that ordered connection. Lifecycle commands are serialized;
an open-only heartbeat closes ownership if the shell disappears. Test event
injection is available only with `--guide-input-test`.

## Verification

Builds and CTest run on devbox. Run graphical checks in an owned omabox with the
matching binaries and plugin. `tools/guide-i1/` seeds a private library and a
fullscreen test game; `tools/guide-i2/latency.py` sends synthetic controller events
through the native translator. Fixtures in `tools/guide-overlay-preview/` cover
layout and unavailable states.

Unit tests cover pause ownership and recovery, parked identities, payload deltas,
real-reading validation, input arbitration, process termination and plugin setup.
A box covers shell IPC, focus, pause signals, capture exclusion and rendering.
Physical controller delivery, grabs, hidraw/Steam Input, audio, UPower and actual
recording encoders require their corresponding devices and services.

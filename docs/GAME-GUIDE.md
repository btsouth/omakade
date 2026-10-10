# In-game guide

Press the controller Home button or Super + Ctrl + G over a known running game.
A panel opens at the right edge of the screen, with your theme's colors, type
and spacing. Resume starts the game again. B or Escape backs out of a
confirmation first, then resumes. With no game, Home and the shortcut keep the
existing Game Mode behavior.

The panel shows the game's art, session and total time, the clock and any
controller battery UPower reports. Below that: Resume, Screenshot and Record
clip, sound volume and output, the performance readings that are available, and
Quit, which asks before it acts. Save last N seconds appears while a replay
buffer runs. Unknown readings stay absent.

In Game Mode the panel adds **Desktop**, which parks the session as Home did
before the guide: the game is hidden and muted and keeps running. Home on the
desktop brings you back to the game, paused, with the guide open. Holding Home
for half a second goes to the desktop and back without the guide; after that
kind of park, Home returns to the library as before.

When Omakade starts RetroArch, Home opens only the guide: RetroArch's controller
profiles bind Home to its menu, so that run uses a copy of the profiles without
it. RetroArch's menu moves to L3 + R3 (both sticks clicked), unless you set a
menu combo yourself, and the guide shows the combo. A menu key or button you
chose yourself is kept. Your `retroarch.cfg` is put back as it was after
RetroArch exits.

To keep Home exactly as it was before the guide, set **Home button in games**
to **Game Mode** in Omakade's settings.

## Setup

The package includes `omakade.guide`. Omakade enables it on Omarchy when no game
is running. If login starts with a game already running, setup waits until the
next no-game snapshot. `omarchy plugin disable omakade.guide` turns it off and restores the
existing Game Mode controls. Enable it again with `omarchy plugin enable
omakade.guide`. Other desktops keep Game Mode.

The resident backend runs in `omakade-sessiond`; Home does not load the library
GUI. Source builds can link `omarchy-plugin/` into the private test desktop's
plugin directory and enable it there.

## Pause and input

Pause defaults on; games tagged online or multiplayer default to running.
Existing per-game pause preferences are respected. The card reports the actual
pause state. Desktop resumes the game before Game Mode hides it, so it runs,
muted, while you are on the desktop, as a Game Mode park always has.

The guide pins verified process identities with pidfds. Its guard stops only the
processes it owns and resumes them on Resume, normal exit, or backend death.
After the panel closes, each pad stays grabbed until its buttons and sticks are
back at rest, so the press that closed it never reaches the game. A pad that
cannot be grabbed produces a notification; available readers keep working.

One controller gesture moves one row. Hats, sticks and buttons share a report;
physical and virtual mirrors are grouped. B, Home and Start require release
before firing again. Keyboard arrows, Enter, Escape and Y use the same actions.
Quit asks first, then sends SIGTERM to the pinned tree. A surviving game offers
Force quit after five seconds.

## Capture, sound and scale

Screenshot and Record clip hide both the card and scrim and wait for a submitted
guide-free frame and at least 80 ms. Screenshot then captures the game's output;
Record clip closes the guide and records the game with its own recorder. A screen
recording you start from Omarchy is separate: the guide does not show it as its
clip or stop it.
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

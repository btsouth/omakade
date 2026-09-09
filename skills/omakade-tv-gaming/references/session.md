# Dedicated TV session guide

## Discover before configuring

For Hyprland, inspect `hyprctl version`, `hyprctl -j monitors all`,
`hyprctl -j workspaces` and `hyprctl -j clients`. Inspect PipeWire's PulseAudio
compatibility sinks with `pactl -f json list sinks`. Match the TV by its display
description and audio port, not by assuming connector numbering is stable.

Record the connector, reserved workspace, game sink, supported display mode,
and existing work monitor layout in local configuration. Configure the actual
monitor mode, not just the game's render resolution. An EDID advertisement is
not proof that the current cable/driver/output path accepts that mode.

Hyprland configuration syntax varies by version. Newer Omarchy installations
use Lua; older installations use hyprlang. Use that version's monitor and
window-rule APIs rather than copying a configuration from another release.
Keep changes in user configuration and validate reload errors. Back up the
files being edited; do not replace unrelated monitor or keybinding settings.

## Local controller lifecycle

A session controller should own a runtime state file and expose start, stop and
status operations. A user service is one way to supervise it. Its state machine
should implement:

| Event | Expected behavior |
| --- | --- |
| Start | Enable the TV output, bind the reserved workspace, install session rules, verify audio, open Couch Mode. Repeated starts reuse the session. |
| Game launch | Put the game on the TV and route only its audio there. Keep desktop audio on its previous sink. |
| Normal stop | Refuse to discard an active game; close the owned library and disable the TV once the game exits. Remove temporary rules and stop polling. |
| Confirmed standby or unplug | Disable the output and preserve game state. Mute or isolate any retained game's audio before removing its sink so it cannot migrate to the headset. Report that retained games still consume resources. |
| TV network timeout | Treat power state as unknown. Network failure alone does not establish that the TV is off. |
| Restart or failure | Reconcile runtime state with live processes and outputs. Clean up only owned state, leaving work monitors and applications intact. |

Reserve the workspace even outside gaming sessions. Add the reservation to
agent workspace allocation, not just a window rule. Route Omakade and games
only while that local session is active. Revalidate window identity by address
and PID before moving or closing it. A broad permanent `gamescope` rule can
also capture an unrelated agent's test window.

`omakade --quit` addresses the existing Omakade instance; it is appropriate
only when that instance belongs to the gaming session. Owning the Couch Mode
process in a user service makes cleanup explicit. Keep Omakade open after
launch if it should remain available when the game closes.

TV remote pairing, standby detection and Wake-on-LAN are vendor-specific
extensions. HDMI screenshots show the PC image, not the TV's internal picture
menus. Use direct user feedback for those menus. A generic skill should not
ship private network addresses, MAC addresses or pairing tokens.

## Optional Gamescope launcher

`scripts/omakade-tv-game.py` is an opt-in wrapper for a **configured Hyprland TV
session**. It requires Python 3, `hyprctl`, `pactl` and Gamescope with the Wayland
backend. `--fps` additionally requires MangoHud's `mangoapp` executable.
Omakade's normal launch behavior and package dependencies remain unchanged.

Use `pactl -f json list sinks` and `hyprctl -j monitors` to choose the local
identifiers. Replace the example connector and sink below with those values:

```sh
python3 /usr/share/omakade/skills/omakade-tv-gaming/scripts/omakade-tv-game.py \
  --output HDMI-A-2 --workspace 10 --sink YOUR_TV_SINK --check
```

Use the same arguments followed by `--dry-run -- COMMAND ARGUMENTS` to inspect
the launch plan. In Steam's per-game launch options, use an absolute installed
path and `%command%`:

```text
python3 /usr/share/omakade/skills/omakade-tv-gaming/scripts/omakade-tv-game.py --output HDMI-A-2 --workspace 10 --sink YOUR_TV_SINK --fps -- %command%
```

The wrapper refuses to launch if the TV is disabled, displays a different
workspace, or lacks the requested sink. It does not enable outputs, install
window rules, or change the default audio device. Set up the session's window
placement first and focus Couch Mode on the TV before launching. The check is
a launch precondition, not ongoing hotplug or audio supervision; the local
session controller must handle the lifecycle above.

The default size and refresh come from the active TV mode. Optional `--width`,
`--height` and `--refresh` arguments select the Gamescope canvas; they do not
change the physical display mode. HDR is opt-in with `--hdr` after verifying
the compositor, display and in-game HDR settings. An HDR-capable output alone
does not enable HDR rendering inside a game.

The FPS option shows a small counter using MangoApp, without enabling Steam's
interactive overlay. A counter can include generated frames. Distinguish base
rendering FPS from displayed FPS when evaluating frame generation or latency.
Game resolution, DLSS/FSR, ray tracing and quality presets remain game settings;
the wrapper does not overwrite them or install drivers.

## Verify on the user's machine

1. Compare work monitor modes, positions and active workspaces before and after
   a start/stop cycle. Confirm the reservation is excluded from agent allocation.
2. Test real controller buttons in Couch Mode and in a game. If the device uses
   the wrong protocol, consult its exact model's manual before changing drivers.
3. Play game audio to the TV alongside desktop audio on the user's usual sink.
   Test the unavailable-sink path before relying on automatic shutdown.
4. Confirm game resolution, image quality and FPS in actual gameplay or an
   agreed visible benchmark. Record HDR and refresh support separately.
5. Stop normally and verify the TV is disabled and session-owned processes have
   exited. Test unplug/standby recovery without risking an unsaved game.

The underlying approach was exercised on one Omarchy/Hyprland workstation
with three work monitors, a TV and a controller. The portable wrapper has
automated preflight/argument tests; other hardware and full physical hotplug
lifecycles still require local verification.

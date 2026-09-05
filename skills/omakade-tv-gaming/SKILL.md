---
name: omakade-tv-gaming
description: Set up or operate Omakade Couch Mode on a dedicated TV while preserving desktop work, and adapt agent activity when the user starts or finishes gaming.
---

# Omakade on a shared workstation

Omakade runs on the PC and delegates games to their owning launchers. A TV
workspace separates windows; it does not partition GPU, CPU, memory, or input.
The skill provides instructions, not a background service or an input sandbox.

## Set up the local session

Read [the session guide](references/session.md) when configuring a machine.
Discover its compositor version, TV connector, workspaces and audio sinks.
Keep machine identifiers and any TV pairing credentials in local configuration.
Reserve a user-chosen workspace for games, including when the TV is off, and
exclude it from any agent desktop allocator. Workspace 10 is an example.

Reuse existing local start/stop/status commands when available. Otherwise,
configure a session controller for that compositor using the guide's lifecycle
and verification checklist. Record the chosen commands and identifiers in the
user's local agent instructions. Do not assume that another machine has a
`tv-gaming` command, a Samsung TV, or a particular GPU.

## Start gaming

1. Read the local session status and preserve the work monitors' layout and
   active workspaces. Enable only the configured TV output.
2. Verify the TV's audio sink before launching audio. Keep the desktop default
   sink unchanged and route the game to the TV. An unavailable TV sink is a
   reason to stop the launch, not to fall back to the user's headset.
3. Open `omakade --couch` on the reserved workspace. Check the resulting window
   and controller navigation; enumerating a controller alone is not an input
   test. Omakade normally relinquishes controller input after launching a game.
4. Verify the launched game's output, audio and focus. Treat a measured result
   and a prepared graphics configuration as different outcomes.

The optional [Gamescope launcher](scripts/omakade-tv-game.py) checks the live TV
workspace and sink before starting a game. See the session guide for its scope,
dependencies and Steam launch options. It does not configure workspace rules,
power the TV, or supervise an entire gaming session.

## Work alongside the player

Continue file, terminal and API work without taking the player's focus. Use
targeted browser automation or an isolated agent desktop when UI work would
otherwise inject global input. Keep agent windows and notifications on work
displays; capture only the work display or isolated desktop needed for the task.

Respect ongoing jobs and the user's performance preference. Local GPU inference
and memory allocations can compete with games even in other workspaces. Check
actual utilization before proposing changes; a workspace is not a resource cap.

Run game benchmarks in a session the user has agreed to observe. If an explicitly
requested test uses an invisible display, establish isolated audio before
starting it and report where it runs. Turning off the TV is not authorization
to start an invisible game with sound on the desktop headset.

## Finish gaming

Use the local stop command and inspect the result. Let the user save or close
an active game normally; preserve unsaved state if the TV disappears mid-game.
Close the session-owned Omakade instance and disable only the TV output.
Restore any audio configuration the session changed. Verify remaining game and
Gamescope processes as well as the output: an invisible or parked game can still
use resources. Report retained games instead of claiming everything is off.

Confirm that work displays and workspaces remain as they were. The gaming
workspace stays reserved for the next session, and idle session monitoring
should stop when it is no longer needed.

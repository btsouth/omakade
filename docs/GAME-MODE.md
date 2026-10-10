# Game Mode

Game Mode turns the computer into a console for a while. It opens Couch Mode on the
display you choose, sends sound there, silences notifications, and puts everything back
when you leave. It works with every source Omakade supports, not only Steam.

[Watch the local Game Mode demo](assets/game-mode-demo.mp4) (isolated desktop).

## Use it

Open **Settings → Controls** and look under **Game Mode**.

- **Display** is where Couch Mode opens. **Current display** uses the display Omakade is
  already on. A display listed as **off until Game Mode** is turned on when Game Mode
  starts and turned off again when you return to the desktop or end the session.
- **Sound** is the output used while Game Mode is on. **Keep current sound output** leaves
  sound alone.
- **Notifications in Game Mode** silences Omarchy notifications for the session.
- **Keyboard shortcut** adds Super + Ctrl + G, which Omarchy leaves free. It starts Game
  Mode, returns to the desktop when active, and resumes the same session on the next
  press. If the key is already used for something else,
  Omakade says what and adds nothing.
- **Controller Home button** lets the Xbox, PlayStation or Home button on any controller
  do what the keyboard shortcut does. It is off until you turn it on. See
  [Use a controller's Home button](#use-a-controllers-home-button).

Choose **Start Game Mode**, or run:

```bash
omakade --game-mode
```

That works whether or not Omakade is already open. The shortcut is one line in
`~/.config/hypr/bindings.lua`, so you can also add it yourself, on any key:

```lua
o.bind("SUPER + CTRL + G", "Game Mode", "omakade --game-mode-toggle")
```

`omakade --game-mode-toggle` starts Game Mode, returns to the desktop when active,
and resumes the retained session on the next press. It works with or without a running
game. Your details page, selection, navigation, expanded sections and scroll position
stay in memory. Returning never opens the controls or stops a game.

If Game Mode launched Omakade, returning hides its window while keeping the same
session and IPC owner. If Omakade was already open, it returns to its previous window,
layout and mode. Resuming uses that same window and library. Reopening Omakade while
parked resumes the session too. Each completed return/resume cycle captures the current
desktop and the warm window's current home and compositor mode.

The desktop entry also carries a **Game Mode** action for launchers that show actions.

With the shell guide plugin enabled on Omarchy, these controls use the
[in-game guide](GAME-GUIDE.md). While a known game runs, the Guide button and
Super + Ctrl + G open or close it.

Press Start on the controller or F11 on the keyboard while Omakade is in front to open
the compact Game Mode controls. **Back to Library** keeps the session active, and
**Back to Game** dismisses the overlay when it is over a game. Escape or the controller's
back button dismisses the controls too. While a game has focus, Start belongs to the
game. Bring Omakade to the foreground to use F11 or Start for its controls; the shortcut
continues to return and resume directly.

**Return to Desktop** always retains the complete session. Running games stay on their
own workspace with only attributable game audio muted. The same shortcut resumes and
focuses the game, or the retained library when no game is running. A game ending while
you are away releases its game/audio records and keeps your library available.
`omakade --game-mode-desktop` performs the same return action from a terminal.

To end the session explicitly, choose **Stop Games and Leave…** and confirm the named
running games. A failed stop keeps the session available. When no safe stop targets are
available, choose **End Game Mode**; this restores desktop effects and releases the
session without stopping untracked games. Settings offers **Resume Game Mode** and
**Game Mode Controls** while parked, and display, sound and notification choices stay
locked until the session ends. `omakade --game-mode-exit` ends it too, as does closing
Omakade. Ending closes an Omakade instance launched just for Game Mode; a previously
open instance remains open. Retention lasts only in the running process, so crash
recovery restores effects and clears the interrupted session rather than reopening its UI.

Games and emulators that pause when unfocused keep that behavior. Muting alone does
not pause gameplay. If Omakade cannot identify or silence game audio safely, it keeps
the game visible and explains why Return to Desktop could not complete.

## Use a controller's Home button

Turn on **Controller Home button** in **Settings → Controls**. A short press of the
Xbox, PlayStation or Home button on any connected controller then does what
Super + Ctrl + G does: with no game it starts Game Mode, returns to the desktop and
resumes; over a running game it opens the [in-game guide](GAME-GUIDE.md). Holding
the button for half a second always switches between Game Mode and the desktop,
without the guide. It works while a game has focus and when Omakade is closed.

To have a short press switch Game Mode and the desktop as before the guide, set
**Home button in games** to **Game Mode**.

Turning it on starts a small user service, `omakade-guide-button`, and adds it to your
desktop session's startup. Turning it off stops the service and removes it from startup.
From a terminal:

```bash
systemctl --user enable --now omakade-guide-button
systemctl --user disable --now omakade-guide-button
```

The button-detection service only reads controllers. While the in-game guide is
open, Omakade separately grabs controller evdev nodes and can pause the game
process tree. Closing releases the grabs and resumes processes it paused.
Outside the guide, games and Steam Input receive ordinary input as before. It reads only
devices that report a Home button, asks the kernel for button events alone, and needs
no root access or extra permissions.

A press counts when it is short, alone and new:

- Holding the button counts once, as a hold, after half a second. Holding on to switch
  a controller off does nothing more.
- Pressing it together with another button, the D-pad or a trigger does nothing, whichever
  goes down first. Those are hotkeys for Steam or an emulator.
- The press that switches a wireless controller on, or a button already held when a
  controller connects, does not count.

One press counts once even when Linux reports it twice, such as from the controller and
from the pad Steam Input makes from it. Presses on several controllers within a second
count once. Presses while the screen is locked are ignored, as the keyboard shortcut is.

### With Steam open

Steam uses the same button. In Steam, open **Settings → Controller → Advanced
Settings** and turn off:

- **Guide button focuses Steam**
- **Enable Guide Button Chords for controllers**

Steam Input can stay on. These are Steam's settings for every game: the button no longer
brings up Steam, and Guide button chords stop working. Omakade does not change Steam's
settings for you. A game or emulator that uses the Home button for its own menu still
gets the press as well. RetroArch started by Omakade is the exception: its menu moves
off Home for that run, and the guide opens it instead.

### If a controller does nothing

```bash
omakade-guide-button --list
journalctl --user -u omakade-guide-button
```

`--list` shows each controller the service watches. A controller that is missing does not
report a Home button to Linux, which happens with some generic controllers and some
controller modes. **no read access** means your session cannot read that controller.
The log shows each controller as it connects and each press that counted.

Turn the setting off before uninstalling Omakade, or systemd keeps a startup entry for
the removed service and warns about it at login.

## Keep a TV for games only

Hyprland turns on every connected display by default, so a TV becomes part of the
desktop. To keep it off except in Game Mode, disable it in `~/.config/hypr/monitors.lua`:

```lua
hl.monitor({ output = "HDMI-A-2", disabled = true })
```

Use the connector name shown in the **Display** choice. A mode, position or scale you
set for that output is kept; Game Mode only switches it on and off.

## What it changes and puts back

| While Game Mode is on | When you leave |
| --- | --- |
| A display that was off is turned on | It is turned off again |
| A display that was already on shows a Game Mode workspace | It shows the workspace it had before |
| Omakade moves to that workspace in Couch Mode | It returns to the same spot in its workspace and its previous mode |
| The chosen sound output becomes the default | The previous default returns |
| Notifications are silenced | They are unsilenced |
| The screen is kept awake | Idle behaviour returns to normal |

While a tiled Omakade window is away, a placeholder window holds its place in the
layout, so the windows beside it keep their size and position. Omakade already
fullscreen in Couch Mode covers its workspace and returns fullscreen.

Game Mode only undoes its own changes. A display that was already on stays on. If you
switch to another sound output during the session, that choice is kept. Notifications
that were already silenced stay silenced. Windows on other displays and workspaces are
not touched.

Starting either completes or undoes itself. If the display does not turn on within ten
seconds, or the sound output is not available, nothing is left changed and Omakade says
what was missing.

## If something interrupts it

- **The display is unplugged or disabled while playing.** Game Mode ends and sound goes back to the
  previous output. Windows that were on the display move to another one, as Hyprland
  does for any display that goes away. A parked session stays quiet if its configured
  display cannot be restored; the shortcut reports the failure with a notification.
- **Omakade crashes or is killed.** The changes are recorded in
  `~/.local/state/omakade/game-mode.json`. The next time Omakade starts, or when you run
  `omakade --game-mode-exit`, the display is turned off again and sound and notifications
  are put back.
- **A parked game can no longer be kept quiet.** Omakade makes the game accessible,
  restores its audio, and reports the failure. Recovery state remains recorded until
  any pending desktop changes are restored.
- **A game is still running when you leave.** The controls name running games and offer
  **Stop Games and Leave…**. A failed stop keeps Game Mode active. If you choose
  **Return to Desktop**, the game stays on its own workspace with its audio muted. Your previous desktop
  and focused window return. Super + Ctrl + G resumes the same session and focuses
  the game, rather than opening a new library over it. Gameplay continues unless
  the game or emulator supports pausing while unfocused.

## Limits

- Choosing a display and the dedicated workspace need Hyprland with a Lua configuration,
  which is what Omarchy 4 ships. On other setups Game Mode uses the current display and
  still handles sound, but Return to Desktop is refused when safe game discovery and
  desktop restoration are unavailable. Use End Game Mode or `--game-mode-exit` instead.
  Library-only parking does not require game audio controls; parking a running game does.
- Sound switching requires `pactl` (`libpulse` on Arch) and a running
  PulseAudio-compatible server, such as PipeWire with `pipewire-pulse`. Omarchy
  includes these. Without them, leave **Sound** at **Keep current sound output**.
- Sound is switched for the whole desktop, not only for the game. Music or a call that
  follows the default output moves to the Game Mode output until you leave.
- Games open where Hyprland places new windows, which is the Game Mode workspace while
  it has focus. A launcher that was already open on another workspace can still put a
  game there.
- Game Mode does not run games inside Gamescope and does not manage HDR, variable refresh
  rate or resolution scaling. Configure those in the game's launcher. The optional
  [TV gaming skill](../skills/omakade-tv-gaming/SKILL.md) covers a Gamescope wrapper for
  people who want one.
- Turning the TV itself on or switching its input is not handled. Game Mode enables the
  computer's output; the TV has to be listening on it. Some displays remain visible
  to the computer while powered off or showing another input, so software cannot
  reliably detect whether the panel is showing the game.

## RetroArch picture cropped on a scaled display

If RetroArch's picture is enlarged or cut off, compare a direct launch with Game
Mode first. On a tested 125% Wayland display, Vulkan cropped the picture even with
shaders disabled; OpenGL Core (`glcore`) displayed it correctly with CRT-Royale.
This workaround was verified with Super Mario World using Snes9x. Compatibility
with other cores has not been established.

To limit the change to one game, use a RetroArch
[game override](https://docs.libretro.com/guides/overrides/) under
`~/.config/retroarch/config/<core-name>/<ROM-filename-without-extension>.cfg`:

```ini
video_driver = "glcore"
```

Preserve any existing settings in that file. Remove the added setting to undo it.
Omakade does not change RetroArch's renderer automatically.

If GLCore freezes when the game is covered by another window, a separate
workaround is `video_vsync = "false"` in the same game override. This avoided a
Wayland/EGL wait in an isolated software-rendered comparison, and the game then
closed normally through Stop Games and Leave. The same Mario-only override then
passed a physical DP-2 launch, Cancel and Stop Games and Leave check with CRT-Royale.
Extended gameplay and tearing were not assessed. Disabling VSync may affect tearing. Remove this setting to
restore the previous behavior. Omakade does not apply either override automatically.

## Hardware acceptance

The automated acceptance script exercises Hyprland, notifications, and a private
PipeWire server with virtual displays and null sound outputs. It does not prove
physical display behavior, audible sound, or real game behavior. The following
checks can also be driven automatically on a real machine; record physical
observations separately:

1. One display: start Game Mode with no game, navigate to details and scroll, then
   toggle to desktop and back three times. The same page, selection, focus and scroll
   return; the previous desktop workspace and window layout return on each park.
   End Game Mode explicitly and verify the journal and temporary owner are cleared.
2. A second display that is normally on: Couch Mode opens there, and leaving restores the
   workspace it was showing and returns focus to the first display.
3. A display that is disabled in `monitors.lua`: it turns on, and turns off when leaving.
4. A chosen sound output: game audio plays there, and the previous output returns.
5. Launch a Steam game, an emulator and one Heroic or Lutris game. Each opens on the
   Game Mode display. Return and resume with the shortcut, then return and quit the
   game while parked. The library session remains and resumes without dead game/audio
   records. Stop Games and Leave still asks for confirmation and ends the session.
6. Kill Omakade during Game Mode, then start it again. The display, sound and
   notifications are put back.
7. Unplug or disable the display during Game Mode. Game Mode ends and sound returns.
8. Controller Home button, with Steam open and Steam Input on: a short press starts,
   returns and resumes, including from inside a game. A long hold and a Home button
   chord do nothing, and the game and Steam still receive ordinary input.
9. With the display powered off, start Game Mode. If Hyprland cannot enable its
   output, it reports the failure and rolls back. If the display still advertises
   an active connection, Game Mode may start; verify this separately from the
   automated timeout test.

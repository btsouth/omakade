# In-game guide (Omarchy shell plugin)

The card Omakade opens over a running game. It is built like Omarchy's own menu,
from the shell's kit (`qs.Commons`, `qs.Ui`), so it follows the active theme's
menu colours, font, corner radius and spacing.

```
Lantern Road                      9:47 PM
42 min · Paused
58 fps   17.2 ms   CPU 41% 67°   GPU 88% 71°
──────────────────────────────────────────
 ▶  Resume
    Screenshot
    Record clip            (Stop recording  01:12)
    Save last 30 s         (while a replay buffer runs)
    Volume  ━━━━━━━━━━━○───  72%
──────────────────────────────────────────
    Quit game              (asks first, Keep playing focused)
A select   B resume   Y screenshot
```

The readings line leaves out what is not available: frame rate and frame time
come from Omakade when MangoHud reports them, CPU and GPU load and temperature
from the drivers. Without a game the header says so and Resume and Quit are
hidden.

## Input

Actions arrive from the keyboard or, for controllers, from Omakade over the guide
socket:

```sh
omarchy-shell omakade.guide input <up|down|left|right|a|b|y|guide>
```

Up and down move one row and wrap, left and right change the volume, A
activates, B backs out of the quit question first and then resumes. Y takes a
screenshot. Keyboard: arrows, Enter or Space (A), Escape (B), Y, G or Home
(Guide). Button names in the hint line follow the pad: Xbox and Steam Deck
letters, PlayStation symbols, Nintendo's swapped A and B.

## Capture and sound

Screenshot hides the card for a frame, saves the game's output with grim where
Omasnap saves screenshots, and reports through an Omarchy notification once the
file exists. Record clip starts Omarchy's own recorder
(`omarchy-capture-screenrecording --fullscreen --with-desktop-audio`) and
resumes the game; Stop recording stops it, and Omarchy's notification follows
when the clip is saved. Next to a running gpu-screen-recorder replay buffer,
which that recorder would stop, the plugin records on its own instead. Save last
30 s appears only while a replay buffer runs and sends it SIGUSR1; the length
comes from the buffer's own `-r`. Volume sets the output Omarchy's volume keys
use (`omarchy-audio-output-sink`), through Quickshell PipeWire.

## Rendering

See `tools/guide-overlay-preview/README.md`.

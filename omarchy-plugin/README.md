# In-game guide (Omarchy shell plugin)

The card Omakade opens over a running game. It is built like Omarchy's own
panels (audio, monitor, weather) from the shell's kit (`qs.Commons`, `qs.Ui`),
so it follows the active theme's menu colours, font, corner radius and spacing.

```
┌──────────────────────────────────────────────┐
│ ▣  Lantern Road                      9:47 PM │
│    42 MIN · PAUSED · PAD 80%                 │
├──────────────────────────────────────────────┤
│   FPS      FRAME      CPU         GPU        │
│   58       17.2 ms    41% 67°     88% 71°    │
├──────────────────────────────────────────────┤
│ [Screenshot]  [Record]                       │  (+ [Save 30 s] while a replay buffer runs)
├──────────────────────────────────────────────┤
│ ACHIEVEMENTS                         23 / 63 │
│ ◇  ━━━━━━━━━━━━━──────────────────────       │
├──────────────────────────────────────────────┤
│ SOUND                                    72% │
│ ◁  ━━━━━━━━━━━━━━━━━━━━━━○──────────         │
│ ◁  Living room TV (HDMI)               1 / 3 │  (only with more than one output)
├──────────────────────────────────────────────┤
│ [ ▶ Resume ]              [ ⇥ Quit game ]    │
│        A select   B resume   Y screenshot    │
└──────────────────────────────────────────────┘
```

The hero shows the game's cover (or a controller glyph), the session time,
Paused and controller batteries when UPower reports them. The readings leave
out what is not available: frame rate and frame time come from an existing game
telemetry file through Omakade, CPU and GPU load and temperature from the
drivers; with none the block is not shown. Record turns into Stop with the
clip's running time. Achievements (Steam games with data) opens a list in place
of the tiles and sections: unlocked, newest first, then locked, with their
pictures; B goes back. The output row cycles the outputs. Quit asks in the card,
starting on Keep playing. Sections that do not apply are left out, with their
rules.

Layout is `components/GuideCard.qml`; the cursor's moves are `GuideFocus.js`;
what each control does is `activate()` and `act()` in `Guide.qml`.

A payload `scale` (1.7 in Omakade's Couch Mode) multiplies every size and gap of the
card, so it reads from a couch; 1 is exactly Omarchy's sizes. Where the scaled
card would not fit the screen it shrinks only as far as it must.

With no game, or while Game Mode is parked on the desktop, Home and
Super + Ctrl + G keep their Game Mode behavior.

## Input

Actions arrive from the keyboard or, for controllers, from Omakade over the guide
socket:

```sh
omarchy-shell omakade.guide input <up|down|left|right|a|b|y|guide>
```

The cursor starts on Resume each time the card opens. Up and down move between
rows (the tiles, achievements, volume, output, Resume and Quit) and wrap; left
and right move along the tiles and between Resume and Quit. Moving between
rows keeps the cursor's place across the card, through the one-control rows
too, so Record and down three times lands on Quit. On volume left and right
change the level, on the output row they pick the next output. A activates, B
backs out of the list or the quit question first and then resumes. Y takes a
screenshot. Keyboard: arrows, Enter or Space (A), Escape (B), Y, G or Home
(Guide). Button names in the hint line follow the pad: Xbox and Steam Deck
letters, PlayStation symbols, Nintendo's swapped A and B.

## Capture and sound

Screenshot hides the card for a frame, saves the game's output with grim where
Omasnap saves screenshots, and reports through an Omarchy notification once the
file exists. Screenshot and Record clip wait for a submitted guide-free frame
and at least 80 ms. Record clip closes the guide, then starts Omarchy's own recorder
(`omarchy-capture-screenrecording --fullscreen --with-desktop-audio`) and
resumes the game; Stop recording stops it, and Omarchy's notification follows
when the clip is saved. Next to a running gpu-screen-recorder replay buffer,
which that recorder would stop, the plugin records on its own instead. Save last
30 s appears only while a replay buffer runs and sends it SIGUSR1; the length
comes from the buffer's own `-r`. Volume sets the output Omarchy's volume keys
use (`omarchy-audio-output-sink`), through Quickshell PipeWire.

## Rendering

See `tools/guide-overlay-preview/README.md`.

Backend updates use the same version 1 payload shape. With `delta: true`, data is
a merge patch: absent fields are retained, null removes fields, arrays replace.
Static art and achievements are kept until their content changes.

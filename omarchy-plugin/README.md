# In-game guide (Omarchy shell plugin)

The card Omakade opens over a running game: a panel at the right edge of the
screen, centred down it, over a dim that deepens towards it. It is built from
the Omarchy shell's kit (`qs.Commons`, `qs.Ui`), so it follows the active
theme's menu colours, font, corner radius and spacing.

```
┌───────────────────────────────────────────────┐
│ [II PAUSED]       ◎ Xbox controller  9:47 PM  │
│                                               │
│ ┌────┐  Lantern Road                          │
│ │    │  Steam                                 │
│ └────┘  42 min this session   18 h total      │
│                                               │
│ ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓  │
│ ┃ ▶  Resume                            (B) ┃  │
│ ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛  │
│ [ ▭  Desktop        ] [ ≡  RetroArch menu  ]  │  (only where they apply)
│ [ ✦  What now?                   Codex  (X) ] │  (with Ask on; see Ask below)
│                                               │
│ [ ◎             (Y) ] [ ◉                  ]  │
│ [ Screenshot        ] [ Record clip        ]  │  (+ [Clip 30 s] with highlights, or [Save 30 s] while an outside replay buffer runs)
│                                               │
│ SOUND                                    72%  │
│ ◁  ━━━━━━━━━━━━━━━━━━━━━━━━━━○─────────────   │
│ ▢  Living room TV (HDMI)              1 / 3 › │  (only with more than one output)
│                                               │
│ PERFORMANCE                                   │
│ [ FPS  58           ] [ FRAME  17.2 ms     ]  │
│ [ CPU  41%  67°C    ] [ GPU  88%  71°C     ]  │  (one box per available reading)
├───────────────────────────────────────────────┤
│ [ ⏻  Quit game                              ] │
│    (A) Select   (B) Resume   (Y) Screenshot   │
└───────────────────────────────────────────────┘
```

The status line shows PAUSED, or REC and the clip's time while a clip records,
then the pad (with its battery when UPower reports one) and the clock. The game
block shows the cover at a fixed height and as wide as the picture is, the
title, the platform and source when the payload has them, and the session and
total play time Omakade knows. Desktop appears for Game Mode games, the
RetroArch menu when Omakade can open it. Record clip turns into Stop clip with
the clip's running time. The performance boxes leave out what is not
available: frame rate and frame time come from an existing game telemetry file
through Omakade, CPU and GPU load and temperature from the drivers (nvidia-smi
on NVIDIA); with none the section is not shown. The output row cycles the
outputs. Quit asks in Quit's place, starting on Keep playing. Sections that do not apply are left out.

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
omarchy-shell omakade.guide input <up|down|left|right|a|b|x|y|guide>
```

The cursor starts on Resume each time the card opens. Up and down move between
rows (Resume, Desktop and the RetroArch menu, the capture tiles, volume, output,
Quit) and wrap; left and right move along a row of several. Moving between rows
keeps the cursor's place across the card, through the one-control rows too, so
Record, down through volume and Quit and round past Resume, comes back to
Record. On volume left and right change the level, on the output row they pick
the next output. A activates, B backs out of the quit question first and then
resumes. Y takes a screenshot and X asks (with Ask on). Keyboard: arrows, Enter or
Space (A), Escape (B), Y, X, G or Home (Guide). The button glyphs on the card and in the hint line follow
the pad: Xbox and Steam Deck letters, PlayStation symbols, Nintendo's swapped A
and B.

## Capture and sound

Screenshot hides the card for a frame, saves the game's output with grim where
Omasnap saves screenshots, and reports through an Omarchy notification once the
file exists. Screenshot and Record clip wait for a submitted guide-free frame
and at least 80 ms. Record clip closes the guide, then records the game's output
with desktop audio through its own gpu-screen-recorder and resumes the game; Stop
recording stops it, and a notification follows when the clip is saved. The guide
only shows and stops the recording it started, so a screen recording started from
Omarchy runs on untouched, and Omarchy's stop leaves the guide's clip alone. With Omakade's
highlights on, a third tile, Clip 30 s, asks Omakade to save the last seconds of its own
replay buffer, ending where the game was when the guide was summoned; the tile shows Saving…
and then Saved or Not saved. With three tiles Record clip reads Record. Without highlights, Save last 30 s appears only
while an outside replay buffer runs and sends it SIGUSR1; the length comes from the
buffer's own `-r`. Volume sets the output Omarchy's volume keys
use (`omarchy-audio-output-sink`), through Quickshell PipeWire.

## Ask

With Ask on, Omakade's payload has `ask` and the card shows **What now?** under Resume,
with the agent's name. A or X takes a picture of the game the way Screenshot does, card
and dim out of it, scaled to about 960 pixels wide, into the folder `ask.shots` names
(`guide-files.py ask-shot`), and sends the action `ask` with `{mode: "next", image}`. The
button reads **Asking Codex…** and counts the seconds until Omakade's status says
`answer` or `failed`. The answer then sits above **Ask again** and **Look it up** under
a WHAT NOW heading with the agent and how long ago it answered; a failure reads in the
urgent colour above **Try again**. Look it up sends `{mode: "lookup"}`, needs no new
picture, reads **Looking it up…**, and its answer comes under FROM A GUIDE. One question
runs at a time: A and X do nothing while one is out.

## Rendering

See `tools/guide-overlay-preview/README.md`.

Backend updates use the same version 1 payload shape. With `delta: true`, data is
a merge patch: absent fields are retained, null removes fields, arrays replace.
Static art is kept until its content changes.

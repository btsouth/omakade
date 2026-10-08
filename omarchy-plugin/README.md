# In-game guide (Omarchy shell plugin)

The guide Omakade opens over a running game: a panel on the left with six pages
(Game, Capture, Performance, Sound, Controllers, System), a status strip, and
button prompts in the connected controller's own glyphs. It is an Omarchy shell
overlay plugin built from the shell's own kit (`qs.Commons`, `qs.Ui`), so it
follows the active theme's colours, font, corner radius, spacing and popup
border.

Live actions are connected. Fixture payloads remain available for isolated layout previews.

## Input

Actions arrive from the keyboard or over IPC:

```sh
omarchy-shell omakade.guide input <up|down|left|right|a|b|x|y|lb|rb|guide>
omarchy-shell omakade.guide tab <game|capture|performance|audio|controllers|system>
```

Keyboard: arrows, Enter (A), Escape (B), Q/E or Tab (bumpers), Y, X, G (Guide).

## Legibility

`Contrast.js` picks the panel tint per theme: the clearest value at which body
text keeps 7:1, dim text 4.5:1 and the accent and urgent colours 3:1 (or a share
of their contrast on the theme's own background) over a pure black and a pure
white game frame. Focus is a ring, never a fill under text. Face-button glyphs
fall back to a neutral disc when their colour cannot hold 3:1.

## Rendering

Inside an omabox box, never on a real desktop:

```sh
omabox up --plugin "$PWD/omarchy-plugin"
tools/guide-overlay-preview/render-plugin.sh OUT_DIR kanagawa flexoki-light
```

The script shows a still "game" fullscreen, then summons the guide with
`tools/guide-overlay-preview/fixtures/lantern-road.json` on each page. Payload
keys: `fixture`, `tab`, `pad` (`xbox`, `playstation`, `nintendo`, `deck`,
`keyboard`), `scale`, `confirm`, `toast`, `audit` (logs the contrast audit).

## Live actions

The plugin owns capture, audio, media, battery status and quick settings. It works
without Omakade. Omakade adds the game card, safe pause and quit, Steam
handoff, known emulator save backups and MangoHud launch setup.

Screenshot saves the frame taken before the guide appeared, on the game's output.
It uses Omasnap's output directory and filename settings, including the Omarchy
and Omasnap environment overrides. Recording and replay use separate plugin-owned
gpu-screen-recorder processes on that output. Replay starts off; choose 30, 60 or
120 seconds and turn it on before saving a replay. REC and REPLAY show while their
processes run. Capture errors produce an error toast. Recent captures come from
the real screenshot and recording directories. The recording folder opens with
the desktop handler.

Sound uses Quickshell PipeWire for output volume, output and input selection and
microphone mute. MPRIS supplies artwork and playback controls. UPower supplies
battery readings; controller identity without a battery comes from sysfs. Missing
readings stay absent. Low battery alerts fire once at 20% and once at 10% for each
controller per shell session. Closed-guide alerts use desktop notifications and
respect DND. Identification uploads a short Linux force-feedback rumble only when the pad
advertises FF_RUMBLE and its evdev node is writable. Other pads hide that action. Pair opens Omarchy's Bluetooth panel.

Couch scale adds Auto, 1x, 1.25x, 1.5x and 2x to the shell's size tokens. Auto uses
the game's output physical width: at least 800 mm selects 1.5x, at most 200 mm
selects 1.25x, and other or unknown widths select 1x. Capture sound, replay length,
button prompts and couch scale are stored in `$XDG_STATE_HOME/omarchy/guide/settings.json`.
The replay process itself is never restarted automatically after shell restart.

The fixture preview remains separate from live state. Its example capture files,
FPS and devices are for rendering only.

The generic Game library tile appears only when Omakade is installed. Steam Deck
bumper hints use L1/R1; other pad families keep their printed labels.

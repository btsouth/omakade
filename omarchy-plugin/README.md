# In-game guide (Omarchy shell plugin)

The guide Omakade opens over a running game: a panel on the left with six pages
(Game, Capture, Performance, Sound, Controllers, System), a status strip, and
button prompts in the connected controller's own glyphs. It is an Omarchy shell
overlay plugin built from the shell's own kit (`qs.Commons`, `qs.Ui`), so it
follows the active theme's colours, font, corner radius, spacing and popup
border.

Status: design stage. Every page runs on fixture data; nothing controls a game,
capture, audio or hardware yet. Omakade's service will supply live data and
controller input.

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

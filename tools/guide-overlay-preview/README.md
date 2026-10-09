# Guide card previews

Render the `omakade.guide` card over a still "game" inside an omabox box, never
on a real desktop:

```sh
omabox up --plugin "$PWD/omarchy-plugin"
tools/guide-overlay-preview/render-plugin.sh OUT_DIR apple-dark gruvbox catppuccin-latte
```

Each theme gets one full-screen shot per fixture in `fixtures/`, named
`THEME-FIXTURE.png`. `FIXTURES="lantern-road recording"` picks fixtures, `PAD`
the button names (`xbox`, `playstation`, `nintendo`, `deck`, `keyboard`) and
`OMABOX_BOX` the box. Use `omabox up --size 1280x720` or `2560x1440` for other
screen sizes.

A fixture is the card's data without the backend: `game` as Omakade sends it,
`performance` (`fps`, `frametime`), `stats` (`cpu`, `cpuTemp`, `gpu`,
`gpuTemp`), `audio` (`volume`, `muted`), `capture` (`recording: {seconds}`,
`replay: {seconds}`), plus `clock`, `cursor` (the focused row) and `confirm`
(the quit question open). Actions in fixture mode change the fixture and are
logged as `GUIDE_ACT`.

`omarchy-shell shell call omakade.guide state ""` returns the card's state,
including the geometry of the card and its rows and the colours it draws.
`contrast.py STATE.json...` reports their WCAG contrast over a black and a
white game frame.

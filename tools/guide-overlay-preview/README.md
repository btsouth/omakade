# In-game guide design preview

This is a standalone QML prototype. It does not load Omakade, control a game,
change audio, capture the desktop, or write game notes to disk. Session, capture,
controller and music data are fixtures. Integration follows design acceptance.

Run every preview and export inside omabox. Use the active desktop's font and
corner radius, as `OmarchyTheme` does, rather than selecting a new design palette:

```sh
omabox up --net isolated
omabox hyprctl getoption decoration:rounding -j
omabox run -- fc-match monospace -f '%{family}\n'
omabox run -d --wait -- /usr/lib/qt6/bin/qml tools/guide-overlay-preview/Preview.qml -- --theme=osaka-jade --radius=12 --font='JetBrainsMono Nerd Font'
```

Pass the radius and first font family reported by those queries. The example
values are not defaults. `gen-themes.py` reads the installed Omarchy palettes;
`themes.js` is their preview snapshot. Theme changes cross-fade a frozen previous
render into the new render over 500 ms.

Use arrows as D-pad, Enter as A, Escape as B, G as Guide, and T to cycle themes.
The numbered rows show focus order. Up/down wraps through actions and captures.
A opens Controls & more; left/right or A changes its settings. B returns to the
main page. Notes has a D-pad keyboard, Save and Cancel. Quit defaults to Keep
playing and requires a second selection. `--page=tools`, `--focus=3`,
`--variant=emulator`, `--backdrop=game2.jpg` and `--closed` select preview states.

The glass has a saturated blurred frame, fine grain below the protective tint,
soft shadow and a one-pixel edge. Translucent channels surround protected text
zones, so light themes can expose the game without weakening text contrast.
Focus uses an accent light bar and outline, with no coloured fill behind text.

`Contrast.js` is shared by the preview and audit. It keeps the 0.6 base and the
original role floors, choosing the clearest tint in 0.01 steps from 0.55 to 1.00.
It includes 8-bit blend rounding in its bounds. The audit covers steady themes;
transition animation does not establish text contrast at every intermediate frame.

To export all 22 themes, create an output directory in the box HOME and pass
`--export=/home/sbx/themes`. Each screenshot follows a rendered frame; QML exits
when the set is complete. Copy results from `$(omabox path)/home/themes` into an
external evidence directory. `--audit` prints the shared contrast audit and exits.
Export a second pair of sets over pure black and white PNG contrast fixtures,
then run:

```sh
python3 tools/guide-overlay-preview/audit-contrast.py AUDIT_LOG BLACK_DIR WHITE_DIR EXTERNAL_OUTPUT_DIR
```

That report checks all four roles over both frames, samples empty patches of the
actual 1920×1080 main-page glass, and produces JSON, CSV and a Markdown table.
Keep renders, reports and review evidence outside the repository. D-pad checks
in omabox prove the prototype flow; physical controller routing and action
integrations belong to subsequent implementation milestones.

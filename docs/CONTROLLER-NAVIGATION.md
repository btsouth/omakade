# Controller and keyboard navigation

The Game Mode library follows the rendered layout:

- Left and Right move between adjacent controls in the same visible row. Row edges stay put.
- Up and Down move to the closest control horizontally in the immediately adjacent row.
- Down from the last control row enters the games. Up from the first game row returns to the control used to enter it.
- A wrapped filter bar is several rows, each reachable vertically.
- Hidden and disabled controls are excluded. Empty results keep focus on usable controls.
- Search and Filters own navigation while open. Back restores their opener.
- Tab and Shift+Tab cycle through the visible library controls and games.

`omakade_couch_navigation_contract_*` checks every directional link independently against the rendered positions. It uses keyboard events and an SDL virtual gamepad connected to the production controller polling code, with both D-pad and analog input. It checks reachability, both library layouts, changing filter labels, console navigation, empty results, dialog focus, destination return, game details, and held-button repeat/release.

The matrix covers wrapped, 720p, 1080p, ultrawide and 4K layouts. These tests are part of the normal CTest suite and CI. Existing navigation and workflow tests cover the deeper destination and editor controls.

`omakade_startup_navigation_*` checks cold launches in desktop mode and both Game Mode layouts. Unlike the route tests, it never requests window activation or assigns focus. It uses the normal startup activation and first-frame controller initialization, then checks the first arrow and confirm. Separate processes test keyboard and SDL input, populated and empty libraries, and delayed Game Mode results. This catches an active window whose focus stops on the library container instead of an interactive destination.

Run desktop-sensitive tests in isolation:

```sh
omabox run -- ctest --preset release --output-on-failure -R 'navigation|directions|controls'
```

New controls and changed layouts must pass this contract. Routine navigation regression testing belongs in automation. Physical controller checks are still useful for device pairing, button mapping and streaming transport; they are not a substitute for the navigation suite.

# Omakade: an Omarchy-native game library

Current project summary for Omarchy discussions and the menu proposal below.

Omakade is a game library built for Omarchy. It shows installed games
from Steam, Lutris, Heroic, Faugus, Battle.net, and GOG in one cover-focused
window that follows the active Omarchy theme and font, including live theme
changes. It also picks up emulated games from RetroArch, PCSX2, RPCS3, PPSSPP,
Ryujinx, Cemu, melonDS, shadPS4, Dolphin, and Xenia, and has a controller-first
Couch Mode for playing on a TV.

Omakade does not install games, manage accounts, or replace the launchers. Play
hands each game to the launcher that owns it, and launcher data is never
modified. Browsing and launching work offline with no account.

It is already in the Omarchy Package Repository as `omakade`
for x86_64 and ARM64. Each upstream release publishes a source tarball with
checksums, SBOMs, and signed provenance.

## Proposal

Add an optional `Install > Gaming > Omakade` entry with a matching
`Remove > Gaming > Omakade`. The installer only needs `omarchy-pkg-add omakade`.
Nothing is installed by default and no keybinding is requested.

## Known limits

- ARM64 was tested on Apple Silicon with Asahi Linux in
  [issue #13](https://github.com/btsouth/omakade/issues/13). The `fex-steam`
  wrapper can still fail to start games from `steam://` requests, which Omakade
  cannot work around.
- Fixture tests cover every source. Reports from real Flatpak launcher setups are
  still being collected in [issue #9](https://github.com/btsouth/omakade/issues/9).

Omakade remains an independent community project, not an official Omarchy app.

- [Project](https://github.com/btsouth/omakade)
- [Latest release](https://github.com/btsouth/omakade/releases/latest)
- [Demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)
- [Guide](GUIDE.md)
- [Compatibility report](COMPATIBILITY.md)

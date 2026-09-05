# Omakade: an Omarchy-native game library

Current project summary for Omarchy discussions. This replaces the early 0.5
proposal; Omakade is now available through the Omarchy Package Repository.

Omakade 1.6.0 is a Linux game library with controller-first Couch Mode, Detail
and Grid views, on-screen search, direct GOG support, and discovery from Steam,
Lutris, Heroic, Faugus, RetroArch, PCSX2, Ryujinx, and Battle.net.

Native Linux GOG games launch directly. Windows GOG game builds run on Linux
through UMU; Heroic-managed games keep their Heroic launch settings. Omakade
does not install games, manage accounts, or replace source launchers.

The release includes x86_64 and ARM64 packages with checksums, SBOMs, and signed
provenance. Automated checks pass on both architectures. ARM64 hardware testing
and broader real native/Flatpak library reports remain open.

Omakade remains an independent community project, not an official Omarchy app.

- [Project](https://github.com/btsouth/omakade)
- [Release 1.6.0](https://github.com/btsouth/omakade/releases/tag/v1.6.0)
- [Demo](https://btsouth.github.io/omakade/assets/omakade-demo.mp4)
- [Compatibility report](COMPATIBILITY.md)

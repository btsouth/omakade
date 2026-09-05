# Compatibility report

## Reference Omarchy system

Verified through September 4, 2026:

| Component | Version | Result |
| --- | --- | --- |
| Omarchy | 4.0.0.r1979.gb686ed8-1 | Pass |
| Hyprland | 0.56.2 | Pass |
| Linux | 7.1.11-arch1-1 | Pass |
| Qt | 6.11.2 | Pass |
| SDL | 3.4.14 | Pass |
| Native Steam | 1.0.0.87-3 | Library, artwork, launch delegation, and local achievements pass |
| Native Faugus | 2.2.1-1 | Binary and delegated launch command contract pass |
| Native RetroArch | 1.22.2-5 | Signed Arch binary and `-L` CLI contract pass |

The reference library contains 45 installed Steam games. Theme colors, font,
launcher transparency, one-click details, keyboard navigation, and the
controller input path have been exercised on this system.

## Release and completion status, September 5, 2026

Version 1.6.0 was published at `c91b14e`. The release record in Linear
SBS-1135 confirms maintainer approval, both architecture builds, lifecycle
tests, dependency scans, checksums, and signed provenance. ARM64 packages
are available; M1/Asahi hardware validation and broader real-library reports
remain open in GitHub #13 and #9 respectively.

The unpublished completion worktree on `codex/completion` passes 81 development
CTest entries, including personal-data migration from the released schema,
backup recovery, controller flows, offscreen layouts, and a cached large-library
fixture. These checks are automated evidence. They do not establish physical
controller, real launcher, ARM64, or final maintainer acceptance. See
[COMPLETION-PROGRESS.md](COMPLETION-PROGRESS.md) for commands and limitations.

### Historical pre-release 1.6 validation

The earlier 1.6 candidate passed 41 tests in Debug and Release, covering Couch
Mode layouts, focus, held navigation, reconnects, input handoff, cursor visibility,
and a cached 1,000-game library. The former pending-publication notes were
superseded by the released candidate above. The dated reference system and
source-specific evidence below retain their original scope.

## Automated visual matrix

Verified offscreen on August 31, 2026:

| Fixture | Size | Result |
| --- | --- | --- |
| Catppuccin Latte light theme | 820 × 590 | Pass |
| Osaka Jade dark theme | 1380 × 880 | Pass |
| Everforest at 1.25 scale | 1380 × 880 physical | Pass |
| Tokyo Night ultrawide | 2560 × 1080 | Pass |
| No compositor blur | 820 × 590 | Pass |

These deterministic renders verify layout, clipping, card aspect ratios, and
theme contrast without changing the active desktop. A render smoke test runs in
CI. Additional real-user reports expand compatibility coverage after v1.

## Contract-tested sources

Lutris native and Flatpak discovery, Heroic native and Flatpak discovery,
Faugus and RetroArch native and Flatpak discovery, PCSX2 and Ryujinx scanner
contracts (native and Flatpak roots), direct GOG manifests and launch tasks,
Epic, GOG, and Amazon manifests, and
Battle.net product.db discovery across Wine, Proton, and Bottles
prefixes are covered by repeatable local fixtures. These
paths still need reports from users with those launchers installed before the
stable release gate can close.

## Still needed

- A clean Omarchy installation
- Native and Flatpak Lutris libraries from real users
- Native and Flatpak Heroic libraries from real users
- A configured native or Flatpak Faugus library from a real user
- A configured native or Flatpak RetroArch library from a real user
- A Battle.net library from a real Wine, Proton, or Bottles prefix
- Steam Flatpak from a real user
- Light, scaled, ultrawide, and blur-disabled checks on real displays

Reports should follow [SUPPORT.md](../SUPPORT.md) and must not include secrets.

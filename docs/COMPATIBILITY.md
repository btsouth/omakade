# Compatibility report

## Reference Omarchy system

Verified through September 1, 2026:

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
contracts (native and Flatpak roots), and Epic, GOG, and Amazon
manifests are covered by repeatable local fixtures. These
paths still need reports from users with those launchers installed before the
stable release gate can close.

## Still needed

- A clean Omarchy installation
- Native and Flatpak Lutris libraries from real users
- Native and Flatpak Heroic libraries from real users
- A configured native or Flatpak Faugus library from a real user
- A configured native or Flatpak RetroArch library from a real user
- Steam Flatpak from a real user
- Light, scaled, ultrawide, and blur-disabled checks on real displays

Reports should follow [SUPPORT.md](../SUPPORT.md) and must not include secrets.

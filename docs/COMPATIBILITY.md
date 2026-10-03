# Compatibility report

These dated reports describe the versions and environments tested at the time.
They do not certify every launcher layout or the current release. For supported
sources and setup, see the [guide](GUIDE.md).

## 1.12 PPSSPP local acceptance, September 20, 2026

Native PPSSPP `1.20.4-4` from the Arch repositories was tested with the MIT-licensed
[2048PSP 1.0.0](https://github.com/violinmelody/2048PSP/releases/tag/1.0.0) homebrew PBP.
The release ZIP is SHA-256
`74f91f15ab315f2e71cbfbf473b3f0be4dea0d0d4ac63d6f5b5a76e5c241e5aa`; the extracted
`EBOOT.PBP` is SHA-256 `d981baac8a8e7e5662f9ffaaf99de806f5e4c814c3def8b1caa7faae743ea2a2`.
Omakade read `UCJS10041`, version `1.00`, launched `/usr/bin/PPSSPPSDL` with the PBP as its
single positional argument, and the isolated recorder closed one PPSSPP row after 59 billed
seconds across a 60-second wall span. The source settings render check passed at 1380 × 880.

The same source was then accepted with the supplied real title
`God of War - Ghost of Sparta (USA) (PSN).iso`, SHA-256
`ba519b219cb69c4085f76dd42b27105cdb512c9bd5b9e082d1beba3a932cf287`. Omakade read `NPUG80508`,
version `1.00`, region `USA`, launched `/usr/bin/PPSSPPSDL` with the ISO as its single positional
argument, and the isolated recorder closed one attributed PPSSPP row after 79 billed seconds.
This establishes launch, identity, recording, and session closure for a real PSP title; gameplay
quality is not claimed.

The `org.ppsspp.PPSSPP` Flatpak was installed and exercised. Its host-path route is not accepted:
PPSSPP attempted a write/access probe beside the ROM, which the read-only sandbox cannot satisfy.
Omakade now refuses that configuration with an actionable error instead of launching a guaranteed
failure; sandbox-local content remains supported. Compressed CSO/CHD identity parsing is
deliberately deferred because the image needs a bounded decompression layer before its PARAM.SFO
can be read; unconverted ISO files and PBP/ELF homebrew are supported.

## 1.12 RPCS3 local source wiring, September 20, 2026

Native RPCS3 `0.0.42-20024-9e86f165` from AUR `rpcs3-bin` was tested with the GPL-2.0-or-later
[iPSX3 Test Cart v1.0](https://github.com/otti83/ipsx3-test-cart/releases/tag/v1.0) homebrew.
The release ISO is SHA-256 `54797e0837c8aee1026ad43205f5c083245f7b7f95f1e63decf75eebbb713556`;
Omakade read its `PS3_GAME/PARAM.SFO` as `IPSX30001`, grouped it as PlayStation 3, and passed its
extracted `EBOOT.BIN` to RPCS3. RPCS3 rejected the upstream ISO as an invalid file/folder because
its declared ISO volume is larger than the published 900 KiB file. With supplied firmware 4.93
(`PS3UPDAT493.PUP`, SHA-256
`158471fd834f8ea8036136b6aab43cd86c7ba73d79ca30e0af3c0fe0001cf365`) installed in an isolated
RPCS3 root, the extracted homebrew booted as `iPSX3 Test Cart [IPSX30001]` and the isolated
recorder closed one attributed RPCS3 row after 79 billed seconds.

The `net.rpcs3.RPCS3` Flatpak was installed but is not accepted. Its isolated Flatpak profile did
not contain the supplied firmware, and the runtime also hit a distribution font lookup problem
before the firmware check. The scanner, launch argv, title index, AUR AppRun process matching and
PARAM.SFO save resolver are also covered by fixtures.

## 1.12 PCSX2 local acceptance, September 20, 2026

Native PCSX2 `2.9.34-1` from AUR `pcsx2-latest-bin` was tested with the supplied SCPH-39001 BIOS
(SHA-256 `f4c948e61a291d4b3f92a141e550cf8357204287a31ff784caccbedaef910c9d`) and
`Black (USA).iso` (SHA-256
`65768cade88bfbaf566b67a3ef7a0bda5a9669779833914ffe7cd73a3e45cccd`). Omakade scanned the title
from PCSX2's game-list cache, launched `/usr/bin/pcsx2-qt -fullscreen` with the ISO, and PCSX2
identified `Black`, serial `SLUS-21376`, version `1.00`, CRC `5C891FF1`. Live-log attribution then
closed one PCSX2 row after 75 billed seconds. A disc change made inside PCSX2's own UI remains
fixture-tested only.

## 1.12 melonDS local acceptance, September 20, 2026

Native melonDS `1.1-2` from the AUR was tested with the MIT-licensed
[DS-Craft beta 1.7.1](https://github.com/moltony/ds-craft/releases/tag/beta1.7.1) homebrew ROM
(SHA-256 `4b471947e663fb716bad5f87835db1b2053996dce6228d102aec5f463e884aa0`). Omakade scanned the
ROM from an isolated DS ROM folder, identified it as homebrew from its header, launched
`/usr/bin/melonDS` with the ROM as its positional argument, and the isolated session recorder
closed one melonDS row after 129 billed seconds across a 130-second wall span. The source
settings render check also passed at 1380 × 880.

This is native acceptance only. The `net.kuribo64.melonDS` Flatpak route is implemented from the
verified app id and was exercised with the same homebrew; it launched and the recorder closed one
14-second row. melonDS still has no current-session artifact that proves which game its own file
picker loaded, so that path remains command-line and window-title attribution only.

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

## 1.7 candidate

The maintainer has exercised the development build on this desktop with Ryujinx,
Dolphin, and Cemu. Exact 1.7.0 package validation and architecture CI are separate
release gates. Automated fixtures now cover Dolphin, Cemu, shadPS4, ROM folders,
console grouping, Switch/Wii U metadata extraction, IGDB matching, portrait batches,
cover sizing, and source-filter scrolling. Fixtures do not establish compatibility
with every real native or Flatpak launcher installation.

Known limitations:

- If a game accepts controller input while Omakade retains keyboard focus on a
  second monitor, both can react. Unfocused Omakade ignores controller input;
  no manual-resume workflow or automatic game-session tracking is enabled.
- SteamGridDB requires a separate key. Its API responses and image handling are
  tested offline; live verification for this candidate is pending.
- Additional bundled console artwork does not add emulator or scanner support.

## 1.6 release validation

Published 1.6.0 is commit `c91b14e40437a14849f37c188d5762c12655299e`. The
maintainer tested the exact installed candidate, including the controller-focus
fix, and approved publication. All 41 release tests pass locally and on x86_64
and aarch64 CI. The preceding candidate also passed all 41 Debug tests.

Automated coverage includes Couch Mode detail and grid layouts, focus paths,
held navigation, controller reconnects, keyboard and mouse handoff, cursor
visibility, and a cached 1,000-game library. Regressions cover layout selection,
empty states, background controller input, GOG ownership after inventory errors,
and removing the last direct GOG game.

Both architecture packages passed lifecycle checks and dependency scans. Public
checksums and provenance were verified against the release commit. The public
x86_64 package also passed upgrade from 1.5.0, removal, reinstall, and smoke tests
in a disposable Arch container. This does not replace real Omarchy ARM64 hardware
or native/Flatpak library reports. Those gaps remain open with maintainer approval.

## Main after 1.6.0

The role-name cleanup in PR #28 merged as `2de11d5`. All nine models retain
identical role IDs and names, and all 41 release tests pass on the integrated
code. This is an unreleased maintenance change, not an update to the 1.6.0
packages. It does not add hardware or real-library compatibility evidence.

## 1.7 candidate validation

The completion candidate passes 81 automated checks covering personal-data
migration, backup recovery, controller flows, offscreen layouts, and a cached
large-library fixture. The maintainer confirmed the installed grid fix looks
good. Physical controller, broader real-launcher, and native ARM64 validation
remain separate from these checks.

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

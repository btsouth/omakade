# Recording coverage

Profiles recognize emulator processes and game paths in command-line arguments, and
on Hyprland also match an emulator's window title against titles Omakade already
knows. A listed profile is not evidence that every launcher, wrapper, or internal
game change works. Recording does not inject code, control emulators, or detect pauses.

| Area | Automated evidence | Remaining acceptance |
| --- | --- | --- |
| Ryujinx and Eden | Direct ROM arguments, paths with spaces, missing-path rejection | Exact candidate launch, return, and short-session accounting |
| Other shipped profiles | Shared matcher and profile parsing | Real launch arguments for RetroArch, PCSX2, Dolphin, Cemu, shadPS4 and other listed binaries |
| Window-title attribution | Cache reading, exact and whole-name matching, ambiguity and short-name refusal, decorated titles, malformed compositor answers, and a live Hyprland run that recorded and closed a session for a process naming no game path | The same run against a real emulator and a real ROM on each supported compositor |
| Pause on unfocus | Focus parsing, the never-treat-unknown-as-unfocused rule, billing and pausing across polls with a controlled clock, and a live Hyprland run where an unfocused game billed 0s against a control's 40s | The same run with a game that loses and regains focus mid-session |
| Session accounting | Monotonic duration, game changes in supplied snapshots, restart recovery, baseline overlap, write failure | Compare displayed time with a real short session |
| Recorder ownership | Private daemon start, duplicate rejection, termination and restart | Installed service behavior on supported hosts |
| Imported totals | Source parser and baseline fixtures | Updated emulator formats and unusual library layouts |

The earlier local Ryujinx launch/return produced a closed session. That is
historical evidence, not acceptance of the current candidate. Game tests remain
paused at the maintainer's request. The Z-A freezes have not been attributed to
Omakade, LSFG, or focus changes.

New configurations default to recording off. Existing explicit choices persist;
legacy configurations without the key retain the previous enabled default.
The preference controls recording and whether recorded totals contribute to the
display. Turning it off keeps the stored history. Daemon status is checked against
the owner of this library's recorder lock and the live process executable. It
reports a running process, not a guarantee that a particular game is recognized.

Paused emulator time counts while its process remains matched. Imported and
recorded totals may overlap. Displayed time is the larger of the imported total
and the captured baseline plus recorded time. Loading games internally without a
recognizable command-line path is covered on Hyprland when the emulator's window
title names the game and Omakade already knows that title, and stays uncovered
otherwise. ARM64 hardware acceptance remains separate from cross-architecture
build and package checks.

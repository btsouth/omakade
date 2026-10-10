#pragma once

#include <QString>

// Home opens Omakade's guide. RetroArch's controller profiles bind the same button to its
// own menu, and RetroArch reads that profile bind for every port whose own menu bind is
// unset, which is every port but the first. A launch from Omakade therefore points
// RetroArch at a copy of its profiles without the menu binds, through --appendconfig.
// The user's own menu key or button is untouched.
//
// The guide opens RetroArch's menu instead, through RetroArch's stdin command interface:
// the launch connects RetroArch's input to commandPipe() and appends stdin_cmd_enable.
// Keys sent through the compositor are no use: RetroArch often reads the keyboard with a
// driver that sees none of them.
//
// RetroArch writes appended settings back into retroarch.cfg when it saves on exit, so
// the original lines are recorded at launch and put back once RetroArch has exited.
namespace RetroArchHome {

struct Paths {
  QString config;   // retroarch.cfg
  QString override; // the file passed to --appendconfig
  QString marker;   // the original settings, while an override may be in retroarch.cfg
  QString profiles; // the copy of RetroArch's controller profiles
  QString fallbackProfiles; // RetroArch's profiles when retroarch.cfg names none
};

[[nodiscard]] Paths paths(bool flatpak);

// Where Omakade sends RetroArch commands; RetroArch reads it as its standard input.
[[nodiscard]] QString commandPipe();

// Returns the file to append, or empty when the profiles cannot be copied.
[[nodiscard]] QString prepare(const Paths& paths);

// Restores retroarch.cfg after RetroArch exits. Call only while RetroArch is not running.
void repair(const Paths& paths);

[[nodiscard]] bool retroArchRunning();

} // namespace RetroArchHome

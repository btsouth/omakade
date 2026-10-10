#pragma once

#include <QString>

// Home opens Omakade's guide. RetroArch's controller profiles bind the same button to its
// own menu, and RetroArch reads that profile bind for every port whose own menu bind is
// unset, which is every port but the first. A launch from Omakade therefore points
// RetroArch at a copy of its profiles without the menu binds, through --appendconfig.
// The user's own menu key or button is untouched.
//
// RetroArch's menu stays on a controller through its own gamepad combo: L3 + R3, appended
// when the user has chosen no combo. (Its stdin command interface crashes RetroArch at
// startup now and then, and keys sent through the compositor never reach a RetroArch
// that reads the keyboard with another driver.)
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

// The combo that opens RetroArch's menu in the RetroArch running as `pid`, as the guide
// shows it ("L3 + R3"), or empty when it has none or Omakade did not start it.
[[nodiscard]] QString menuCombo(qint64 pid);

// Returns the file to append, or empty when the profiles cannot be copied.
[[nodiscard]] QString prepare(const Paths& paths);

// Restores retroarch.cfg after RetroArch exits. Call only while RetroArch is not running.
void repair(const Paths& paths);

[[nodiscard]] bool retroArchRunning();

} // namespace RetroArchHome

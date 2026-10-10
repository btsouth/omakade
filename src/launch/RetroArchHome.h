#pragma once

#include <QString>

// Home opens Omakade's guide. RetroArch's controller profiles bind the same button to its
// own menu, and a menu bind left unset in retroarch.cfg falls back to the profile, so a
// Home press would open both. A launch from Omakade points the bind at a button no pad
// has, through --appendconfig, unless the user chose a menu button themselves.
//
// RetroArch writes appended settings back into retroarch.cfg when it saves on exit, so
// the original line is recorded at launch and put back once RetroArch has exited.
namespace RetroArchHome {

struct Paths {
  QString config;   // retroarch.cfg
  QString override; // the file passed to --appendconfig
  QString marker;   // the original bind, while an override may be in retroarch.cfg
};

[[nodiscard]] Paths paths(bool flatpak);

// The value written over the profile's bind. RetroArch's udev and SDL pads have fewer
// buttons, so it is never pressed.
inline constexpr const char* kUnusedButton = "99";

// Returns the file to append, or empty when the user's own bind stands.
[[nodiscard]] QString prepare(const Paths& paths);

// Restores retroarch.cfg after RetroArch exits. Call only while RetroArch is not running.
void repair(const Paths& paths);

[[nodiscard]] bool retroArchRunning();

} // namespace RetroArchHome

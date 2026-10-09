#pragma once

#include <QString>
#include <QObject>
#include <functional>

// The in-game guide is an Omarchy shell plugin shipped inside the package. Omakade links it
// into the user's plugin folder and enables it the first time it runs, so installing or
// upgrading the package is all a user does.
namespace GuidePlugin {

inline constexpr auto kId = "omakade.guide";

struct Paths {
  QString pluginsDir;                              // ~/.config/omarchy/plugins
  QString shellConfig;                             // ~/.config/omarchy/shell.json
  QString bundledDir;                              // <prefix>/share/omakade/omarchy-plugin
  QString markerPath;                              // set once the plugin has been enabled
  QString shellProgram = QStringLiteral("omarchy-shell");
};

Paths defaultPaths(const QString& configRoot, const QString& stateRoot, const QString& applicationDir);

// The plugin can be summoned: its manifest resolves and the shell has it enabled.
bool usable(const Paths& paths);

// Links the packaged plugin when nothing occupies its folder (a user's own copy or link is
// never replaced) and enables it once. Without Omarchy's shell running it enables nothing
// and tries again on the next launch. Blocks for at most a few seconds. Returns usable().
bool ensure(const Paths& paths);
// Only called during a calm startup, never by toggle. Work stays off the owner thread.
void ensureAsync(const Paths& paths, QObject* owner, std::function<void(bool)> done = {});

}  // namespace GuidePlugin

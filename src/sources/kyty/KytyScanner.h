#pragma once

#include "sources/LauncherGame.h"
#include "sources/desktop/DesktopEntry.h"

#include <QString>
#include <QStringList>

// Kyty (KytyPS5) runs PlayStation 5 games. It keeps no library of its own beyond the game
// folders its launcher saves in Kyty.ini, so Omakade reads those folders and the folders named
// by the player's own Kyty scripts, and starts a game through the player's script when one
// exists, since such scripts usually carry settings tuned for that game.
class KytyScanner final {
public:
  struct Options {
    QStringList applicationDirectories;
    QStringList configFiles;
    QStringList installDirectories;
    QStringList gameDirectories;
  };
  [[nodiscard]] static Options defaultOptions();
  [[nodiscard]] static LauncherScanResult scan(const Options& options);
  // The command Kyty's own launcher runs for a game folder.
  [[nodiscard]] static QStringList emulatorArguments(const QString& gameDirectory);
};

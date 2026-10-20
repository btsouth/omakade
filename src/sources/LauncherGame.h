#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// A game Omakade starts with a command it found rather than through an emulator's library:
// a Kyty game, through the player's own wrapper when there is one, or an installed native port.
struct LauncherGameRecord {
  QString gameId;
  QString title;
  QString system;
  QString subtitle;
  QString path;
  QString serial;
  QString coverPath;
  QString heroPath;
  QString executable;
  QStringList arguments;
  QString directory;
  // The desktop entry the command came from, when it came from one.
  QString desktopEntry;
  QString link;
};

struct LauncherScanResult {
  QVector<LauncherGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

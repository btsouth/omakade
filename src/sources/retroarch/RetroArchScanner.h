#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct RetroArchGameRecord {
  QString gameId;
  QString title;
  QString contentPath;
  QString corePath;
  QString coreName;
  QString coverPath;
  QString heroPath;
  QString system;
  qint64 playtimeSeconds = 0;
  qint64 lastPlayed = 0;
  bool flatpak = false;
};

struct RetroArchScanResult {
  QVector<RetroArchGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

class RetroArchScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static RetroArchScanResult scan(const QStringList& roots);
};

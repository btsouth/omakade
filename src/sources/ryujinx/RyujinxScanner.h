#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct RyujinxGameRecord {
  QString gameId;      // stable key: title id when known, else "path:<file>"
  QString titleId;     // 16-hex title id, uppercase, may be empty
  QString title;
  QString path;        // ROM file path when known
  QString coverPath;
  qint64 playtimeSeconds = 0;
  qint64 lastPlayed = 0;
  bool flatpak = false;
};

struct RyujinxScanResult {
  QVector<RyujinxGameRecord> games;
  QStringList roots;      // Ryujinx config roots that were scanned
  QStringList warnings;
  bool incomplete = false;
};

class RyujinxScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static RyujinxScanResult scan(const QStringList& roots);
};

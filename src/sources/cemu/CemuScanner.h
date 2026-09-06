#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct CemuGameRecord {
  QString gameId;
  QString titleId;
  QString title;
  QString path;
  QString coverPath;
  bool flatpak = false;
};

struct CemuScanResult {
  QVector<CemuGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

class CemuScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static CemuScanResult scan(const QStringList& roots);
};

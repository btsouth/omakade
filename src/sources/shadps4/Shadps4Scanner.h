#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Shadps4GameRecord {
  QString gameId;
  QString titleId;
  QString title;
  QString path;
  QString coverPath;
  QString heroPath;
  bool flatpak = false;
  QString flatpakAppId;
};

struct Shadps4ScanResult {
  QVector<Shadps4GameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
};

class Shadps4Scanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static Shadps4ScanResult scan(const QStringList& roots);
};

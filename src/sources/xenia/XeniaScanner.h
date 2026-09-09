#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct XeniaGameRecord {
  QString gameId;      // title id (e.g. "4D5307F1") when known, else "path:<xex>"
  QString titleId;     // eight-character title id, may be empty
  QString title;
  QString path;        // path passed to Xenia: a default.xex, .iso, or .xex file
  QString coverPath;
  bool flatpak = false;
  QString flatpakAppId;
};

struct XeniaScanResult {
  QVector<XeniaGameRecord> games;
  QStringList roots;      // Xenia configuration/storage folders that were read
  QStringList warnings;
  bool incomplete = false;
};

class XeniaScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static XeniaScanResult scan(const QStringList& roots);
};

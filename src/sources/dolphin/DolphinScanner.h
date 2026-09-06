#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct DolphinGameRecord {
  QString gameId;     // six-character disc id when known, else "path:<file>"
  QString discId;     // six-character id, may be empty
  QString title;
  QString path;
  QString platform;   // "GameCube" or "Wii"
  QString coverPath;
  bool flatpak = false;
  QString flatpakAppId;
};

struct DolphinScanResult {
  QVector<DolphinGameRecord> games;
  QStringList roots;      // Dolphin configuration folders that were read
  QStringList folders;    // game folders that were scanned
  QStringList warnings;
  bool incomplete = false;
};

// Reads the disc header out of a GameCube or Wii dump without decompressing
// it. RVZ and WIA keep a plain copy of the header, and the plain formats start
// with it.
struct DolphinDiscHeader {
  QString discId;
  QString title;
  QString platform;
  [[nodiscard]] bool valid() const { return !discId.isEmpty(); }
};

class DolphinScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static bool dolphinInstalled();
  [[nodiscard]] static DolphinDiscHeader readDiscHeader(const QString& path);
  [[nodiscard]] static QString coverFromDolphinCache(const QString& discId);
  [[nodiscard]] static DolphinScanResult scan(const QStringList& roots, const QStringList& extraFolders = {},
                                              bool autoDiscover = false);
};

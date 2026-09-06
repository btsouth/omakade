#pragma once

#include "sources/retroarch/RetroArchScanner.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct RomFolder {
  QString path;
  QString system;
};

class RomFolderScanner final {
public:
  [[nodiscard]] static QVector<RomFolder> parseEncoded(const QStringList& encoded);
  [[nodiscard]] static QVector<RomFolder> discoverAutoFolders();
  [[nodiscard]] static RetroArchScanResult scan(const QVector<RomFolder>& folders);
  [[nodiscard]] static QString encode(const QString& path, const QString& system);
  [[nodiscard]] static QString canonicalPath(const QString& path);
};

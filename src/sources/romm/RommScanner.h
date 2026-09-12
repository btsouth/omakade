#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

struct RommGameRecord {
  QString appId;
  QString title;
  QString description;
  QString contentPath;
  QString system;
  QString platform;
  QString coverReference;
};

struct RommScanResult {
  QVector<RommGameRecord> games;
  QStringList warnings;
  int nextOffset = 0;
  int total = 0;
  bool hasMore = false;
  bool complete = true;
};

class RommScanner final {
public:
  [[nodiscard]] static RommScanResult parsePage(const QByteArray& payload,
                                                const QString& localLibraryRoot);
};

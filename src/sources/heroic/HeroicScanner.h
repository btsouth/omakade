#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

struct GogLaunchTask {
  QString executablePath;
  QStringList arguments;
  QString workingDirectory;
  bool windows = false;
};

struct HeroicGameRecord {
  QString key;
  QString appId;
  QString runner;
  QString title;
  QString installPath;
  QString coverPath;
  QString heroPath;
  int playtimeMinutes = 0;
  qint64 lastPlayed = 0;
  bool flatpak = false;
};

struct HeroicScanResult {
  QVector<HeroicGameRecord> games;
  QStringList roots;
  QStringList warnings;
  bool incomplete = false;
  bool gogIncomplete = false;
  bool managedGogIncomplete = false;
};

class HeroicScanner final {
public:
  [[nodiscard]] static QStringList discoverRoots();
  [[nodiscard]] static HeroicScanResult scan(const QStringList& roots);
  [[nodiscard]] static std::optional<GogLaunchTask> gogLaunchTask(const QString& installPath,
                                                                  const QString& appId);
};

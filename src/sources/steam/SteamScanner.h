#pragma once

#include <QStringList>
#include <QVector>

struct SteamAchievementRecord {
  QString apiName;
  QString title;
  QString description;
  QString iconUrl;
  bool unlocked = false;
  qint64 unlockTime = 0;
  double rarity = 0.0;
  bool hidden = false;
  double currentProgress = 0.0;
  double maximumProgress = 0.0;

  bool operator==(const SteamAchievementRecord&) const = default;
};

struct SteamGameRecord {
  QString appId;
  QString title;
  QString installDirectory;
  QString libraryPath;
  QString manifestPath;
  QString coverPath;
  QString heroPath;
  QString logoPath;
  qint64 lastPlayed = 0;
  int playtimeMinutes = 0;
  int achievementsUnlocked = 0;
  int achievementsTotal = 0;
  QVector<SteamAchievementRecord> achievements;

  bool operator==(const SteamGameRecord&) const = default;
};

struct SteamScanResult {
  QVector<SteamGameRecord> games;
  QStringList steamRoots;
  QStringList libraryPaths;
  QStringList warnings;
  bool incomplete = false;
};

class SteamScanner final {
public:
  [[nodiscard]] static bool isTool(const QString& name);
  [[nodiscard]] static QStringList discoverSteamRoots();
  [[nodiscard]] static SteamScanResult scan(const QStringList& steamRoots);
};

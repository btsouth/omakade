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
  QStringList unreadableManifests;
  bool incomplete = false;
  // Libraries listed in libraryfolders.vdf whose steamapps folder is missing or unreadable,
  // such as an unplugged drive. Their cached installations are kept.
  QStringList unavailableLibraries;
  bool operator==(const SteamScanResult&) const = default;
};

class SteamScanner final {
public:
  [[nodiscard]] static QStringList discoverSteamRoots();
  [[nodiscard]] static bool isToolTitle(const QString& name);
  [[nodiscard]] static SteamScanResult scan(const QStringList& steamRoots);

  // The Proton prefix Steam gives a game, derived from the game's install path:
  // <library>/steamapps/compatdata/<appId>/pfx. Empty when the install path is
  // not inside a Steam library or the app id is not numeric, because the app id
  // becomes a path segment here.
  [[nodiscard]] static QString protonPrefixPath(const QString& installPath, const QString& appId);
  // The same path, but only when Steam has actually created the prefix. A game
  // never run through Proton has no prefix, and a stop plan must not name one.
  [[nodiscard]] static QString protonPrefix(const QString& installPath, const QString& appId);
};

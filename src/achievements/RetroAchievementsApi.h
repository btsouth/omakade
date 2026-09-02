#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

enum class RetroAchievementsApiState {
  Ready,
  Offline,
  InvalidKey,
  RateLimited,
  RemoteError,
};

struct RetroAchievementsConsoleRecord {
  int id = 0;
  QString name;
};

struct RetroAchievementsHashRecord {
  qint64 gameId = 0;
  QString title;
  QStringList md5Hashes;
};

struct RetroAchievementsAchievementRecord {
  QString apiName;
  QString title;
  QString description;
  QString iconUrl;
  bool unlocked = false;
  qint64 unlockTime = 0;
  double rarity = 0.0;
  bool hidden = false;
  double currentProgress = 0.0;
  double maximumProgress = 1.0;
};

struct RetroAchievementsProgressResult {
  int unlocked = 0;
  int total = 0;
  QVector<RetroAchievementsAchievementRecord> achievements;
};

// Stateless URL-builder and JSON-parser for the RetroAchievements Web API
// (https://api-docs.retroachievements.org/), mirroring SteamAchievementApi/IgdbApi: no network or
// storage of its own, so it can be unit-tested against hand-built JSON fixtures.
class RetroAchievementsApi final {
public:
  [[nodiscard]] static QString host();
  [[nodiscard]] static QUrl consoleIdsUrl(const QString& apiKey);
  [[nodiscard]] static QUrl gameListUrl(const QString& apiKey, int consoleId);
  [[nodiscard]] static QUrl gameInfoAndProgressUrl(const QString& apiKey, qint64 gameId,
                                                   const QString& username);
  [[nodiscard]] static RetroAchievementsApiState classifyHttpResponse(int statusCode,
                                                                      bool networkError);
  [[nodiscard]] static bool parseConsoleIds(const QByteArray& contents,
                                            QVector<RetroAchievementsConsoleRecord>* result);
  // Picks the console whose name best matches targetName, or 0 if none match at all. An exact
  // (case-insensitive) match always wins over a substring one: several console families share a
  // name prefix with a close relative (e.g. "Game Boy" is a substring of "Game Boy Color" and
  // "Game Boy Advance"; "WonderSwan" of "WonderSwan Color"; "Neo Geo Pocket" of "Neo Geo Pocket
  // Color"), so naive substring containment can silently bind to the wrong system.
  [[nodiscard]] static int bestConsoleMatch(const QVector<RetroAchievementsConsoleRecord>& consoles,
                                            const QString& targetName);
  [[nodiscard]] static bool parseGameList(const QByteArray& contents,
                                          QVector<RetroAchievementsHashRecord>* result);
  [[nodiscard]] static RetroAchievementsApiState
  parseGameInfoAndProgress(const QByteArray& contents, RetroAchievementsProgressResult* result,
                           QString* error = nullptr);
};

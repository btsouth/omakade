#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

struct SteamOwnedGameRecord {
  QString appId;
  QString title;
  int playtimeMinutes = 0;
  qint64 lastPlayed = 0;

  bool operator==(const SteamOwnedGameRecord&) const = default;
};

struct SteamOwnedGamesResult {
  QVector<SteamOwnedGameRecord> games;
  bool valid = false;
  QString error;
};

class SteamOwnedGames final {
public:
  [[nodiscard]] static QUrl requestUrl(const QString& host, const QByteArray& apiKey,
                                       const QString& steamId);
  [[nodiscard]] static SteamOwnedGamesResult parse(const QByteArray& payload);
};

#include "sources/steam/SteamOwnedGames.h"

#include "sources/steam/SteamScanner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

QUrl SteamOwnedGames::requestUrl(const QString& host, const QByteArray& apiKey,
                                 const QString& steamId) {
  QUrl url;
  url.setScheme(QStringLiteral("https"));
  url.setHost(host);
  url.setPath(QStringLiteral("/IPlayerService/GetOwnedGames/v1/"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("key"), QString::fromLatin1(apiKey));
  query.addQueryItem(QStringLiteral("steamid"), steamId);
  query.addQueryItem(QStringLiteral("include_appinfo"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("include_played_free_games"), QStringLiteral("1"));
  url.setQuery(query);
  return url;
}

SteamOwnedGamesResult SteamOwnedGames::parse(const QByteArray& payload) {
  SteamOwnedGamesResult result;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = QStringLiteral("Steam returned an unreadable owned-games response.");
    return result;
  }
  const QJsonValue response = document.object().value(QStringLiteral("response"));
  if (!response.isObject()) {
    result.error = QStringLiteral("Steam returned an unreadable owned-games response.");
    return result;
  }
  const QJsonObject object = response.toObject();
  if (!object.contains(QStringLiteral("games"))) {
    // Steam answers a private "Game details" profile with an empty response object rather
    // than an error, so explain the privacy setting instead of reporting an empty library.
    result.error = QStringLiteral(
        "Steam returned no owned games. Set Steam privacy for Game details to Public, then "
        "refresh.");
    return result;
  }

  QSet<QString> seen;
  for (const QJsonValue& value : object.value(QStringLiteral("games")).toArray()) {
    const QJsonObject game = value.toObject();
    const qint64 appId = game.value(QStringLiteral("appid")).toInteger();
    const QString title = game.value(QStringLiteral("name")).toString().trimmed();
    if (appId <= 0 || title.isEmpty() || SteamScanner::isTool(title)) {
      continue;
    }
    const QString identifier = QString::number(appId);
    if (seen.contains(identifier)) {
      continue;
    }
    seen.insert(identifier);
    result.games.append({
        .appId = identifier,
        .title = title,
        .playtimeMinutes = game.value(QStringLiteral("playtime_forever")).toInt(),
        .lastPlayed = game.value(QStringLiteral("rtime_last_played")).toInteger(),
    });
  }

  std::sort(result.games.begin(), result.games.end(), [](const auto& left, const auto& right) {
    return left.title.localeAwareCompare(right.title) < 0;
  });
  result.valid = true;
  return result;
}

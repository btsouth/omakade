#include "achievements/RetroAchievementsApi.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimeZone>
#include <QUrlQuery>

#include <limits>

namespace {
QJsonValue parseValue(const QByteArray& contents, bool* okay) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(contents, &error);
  *okay = error.error == QJsonParseError::NoError && (document.isObject() || document.isArray());
  if (!*okay) {
    return {};
  }
  return document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
}

qint64 parseDate(const QJsonObject& achievement) {
  const QString hardcore = achievement.value(QStringLiteral("DateEarnedHardcore")).toString();
  const QString normal = achievement.value(QStringLiteral("DateEarned")).toString();
  const QString value = hardcore.isEmpty() ? normal : hardcore;
  if (value.isEmpty()) {
    return 0;
  }
  QDateTime parsed = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::ISODate);
  }
  parsed.setTimeZone(QTimeZone::UTC);
  return parsed.isValid() ? parsed.toSecsSinceEpoch() : 0;
}

QString badgeUrl(const QString& badgeName, bool unlocked) {
  if (badgeName.isEmpty()) {
    return {};
  }
  return QStringLiteral("https://media.retroachievements.org/Badge/%1%2.png")
      .arg(badgeName, unlocked ? QString{} : QStringLiteral("_lock"));
}
} // namespace

QString RetroAchievementsApi::host() { return QStringLiteral("retroachievements.org"); }

QUrl RetroAchievementsApi::consoleIdsUrl(const QString& apiKey) {
  QUrl url;
  url.setScheme(QStringLiteral("https"));
  url.setHost(host());
  url.setPath(QStringLiteral("/API/API_GetConsoleIDs.php"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("y"), apiKey);
  url.setQuery(query);
  return url;
}

QUrl RetroAchievementsApi::gameListUrl(const QString& apiKey, int consoleId) {
  QUrl url;
  url.setScheme(QStringLiteral("https"));
  url.setHost(host());
  url.setPath(QStringLiteral("/API/API_GetGameList.php"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("y"), apiKey);
  query.addQueryItem(QStringLiteral("i"), QString::number(consoleId));
  query.addQueryItem(QStringLiteral("h"), QStringLiteral("1"));
  url.setQuery(query);
  return url;
}

QUrl RetroAchievementsApi::gameInfoAndProgressUrl(const QString& apiKey, qint64 gameId,
                                                  const QString& username) {
  QUrl url;
  url.setScheme(QStringLiteral("https"));
  url.setHost(host());
  url.setPath(QStringLiteral("/API/API_GetGameInfoAndUserProgress.php"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("y"), apiKey);
  query.addQueryItem(QStringLiteral("g"), QString::number(gameId));
  query.addQueryItem(QStringLiteral("u"), username);
  url.setQuery(query);
  return url;
}

RetroAchievementsApiState RetroAchievementsApi::classifyHttpResponse(int statusCode,
                                                                     bool networkError) {
  if (statusCode == 401 || statusCode == 403) {
    return RetroAchievementsApiState::InvalidKey;
  }
  if (statusCode == 429) {
    return RetroAchievementsApiState::RateLimited;
  }
  if (statusCode >= 200 && statusCode < 300) {
    return RetroAchievementsApiState::Ready;
  }
  if (statusCode > 0) {
    return RetroAchievementsApiState::RemoteError;
  }
  return networkError ? RetroAchievementsApiState::Offline : RetroAchievementsApiState::RemoteError;
}

bool RetroAchievementsApi::parseConsoleIds(const QByteArray& contents,
                                           QVector<RetroAchievementsConsoleRecord>* result) {
  if (result == nullptr) {
    return false;
  }
  bool okay = false;
  const QJsonValue root = parseValue(contents, &okay);
  if (!okay || !root.isArray()) {
    return false;
  }
  QVector<RetroAchievementsConsoleRecord> parsed;
  for (const QJsonValue& value : root.toArray()) {
    const QJsonObject console = value.toObject();
    const QString name = console.value(QStringLiteral("Name")).toString();
    const int id = console.value(QStringLiteral("ID")).toInt();
    if (id > 0 && !name.isEmpty()) {
      parsed.append({.id = id, .name = name});
    }
  }
  *result = parsed;
  return true;
}

int RetroAchievementsApi::bestConsoleMatch(const QVector<RetroAchievementsConsoleRecord>& consoles,
                                           const QString& targetName) {
  const QString target = targetName.toLower();
  if (target.isEmpty()) {
    return 0;
  }
  int matchedId = 0;
  int bestScore = -1;
  for (const RetroAchievementsConsoleRecord& console : consoles) {
    const QString candidate = console.name.toLower();
    if (candidate.isEmpty()) {
      continue;
    }
    int score = -1;
    if (candidate == target) {
      score = std::numeric_limits<int>::max();
    } else if (candidate.contains(target) || target.contains(candidate)) {
      score = -qAbs(candidate.size() - target.size());
    } else {
      continue;
    }
    if (score > bestScore) {
      bestScore = score;
      matchedId = console.id;
    }
  }
  return matchedId;
}

bool RetroAchievementsApi::parseGameList(const QByteArray& contents,
                                         QVector<RetroAchievementsHashRecord>* result) {
  if (result == nullptr) {
    return false;
  }
  bool okay = false;
  const QJsonValue root = parseValue(contents, &okay);
  if (!okay || !root.isArray()) {
    return false;
  }
  QVector<RetroAchievementsHashRecord> parsed;
  for (const QJsonValue& value : root.toArray()) {
    const QJsonObject game = value.toObject();
    const qint64 gameId = game.value(QStringLiteral("ID")).toVariant().toLongLong();
    if (gameId <= 0) {
      continue;
    }
    QStringList hashes;
    const QJsonValue hashesValue = game.value(QStringLiteral("Hashes"));
    for (const QJsonValue& hashValue : hashesValue.toArray()) {
      const QString hash =
          hashValue.isString() ? hashValue.toString()
                               : hashValue.toObject().value(QStringLiteral("MD5")).toString();
      if (!hash.isEmpty()) {
        hashes.append(hash.toLower());
      }
    }
    parsed.append({.gameId = gameId,
                   .title = game.value(QStringLiteral("Title")).toString(),
                   .md5Hashes = hashes});
  }
  *result = parsed;
  return true;
}

RetroAchievementsApiState
RetroAchievementsApi::parseGameInfoAndProgress(const QByteArray& contents,
                                               RetroAchievementsProgressResult* result,
                                               QString* error) {
  if (result == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("No progress result destination was provided");
    }
    return RetroAchievementsApiState::RemoteError;
  }
  bool okay = false;
  const QJsonValue root = parseValue(contents, &okay);
  if (!okay || !root.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("RetroAchievements returned malformed game data");
    }
    return RetroAchievementsApiState::RemoteError;
  }
  const QJsonObject game = root.toObject();
  if (game.contains(QStringLiteral("Error"))) {
    if (error != nullptr) {
      *error = game.value(QStringLiteral("Error")).toString();
    }
    return RetroAchievementsApiState::RemoteError;
  }

  RetroAchievementsProgressResult parsed;
  const QJsonValue achievementsValue = game.value(QStringLiteral("Achievements"));
  const auto handleAchievement = [&parsed](const QJsonObject& achievement) {
    const QString apiName = achievement.value(QStringLiteral("ID")).toVariant().toString();
    if (apiName.isEmpty()) {
      return;
    }
    const qint64 unlockTime = parseDate(achievement);
    const bool unlocked = unlockTime > 0;
    const QString badgeName = achievement.value(QStringLiteral("BadgeName")).toString();
    parsed.achievements.append({
        .apiName = apiName,
        .title = achievement.value(QStringLiteral("Title")).toString(apiName),
        .description = achievement.value(QStringLiteral("Description")).toString(),
        .iconUrl = badgeUrl(badgeName, unlocked),
        .unlocked = unlocked,
        .unlockTime = unlockTime,
        .rarity = 0.0,
        .hidden = false,
        .currentProgress = unlocked ? 1.0 : 0.0,
        .maximumProgress = 1.0,
    });
    if (unlocked) {
      ++parsed.unlocked;
    }
  };
  if (achievementsValue.isObject()) {
    const QJsonObject achievements = achievementsValue.toObject();
    for (auto iterator = achievements.constBegin(); iterator != achievements.constEnd();
        ++iterator) {
      handleAchievement(iterator.value().toObject());
    }
  } else if (achievementsValue.isArray()) {
    for (const QJsonValue& value : achievementsValue.toArray()) {
      handleAchievement(value.toObject());
    }
  }
  parsed.total = parsed.achievements.size();
  *result = parsed;
  if (error != nullptr) {
    error->clear();
  }
  return RetroAchievementsApiState::Ready;
}

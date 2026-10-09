#include "guide/GuidePayload.h"
#include "guide/GuideArt.h"

#include <QJsonDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>

QJsonObject GuidePayload::build(const QVariantMap& session, const QVariantMap& originalMetadata,
                                const QString& output, const QString& pad,
                                bool pauseWhileOpen, bool paused, const QString& artCacheRoot, bool resolveArt) {
  const auto metadata = resolveArt ? GuideArt::select(session.value("source").toString(), originalMetadata,
      artCacheRoot.isEmpty() ? GuideArt::cacheRoot() : artCacheRoot) : originalMetadata;
  QJsonObject payload{{"version", kVersion}, {"output", output}, {"pad", pad}};
  QJsonObject data;
  if (!session.isEmpty()) {
    QJsonObject game{{"title", metadata.value("title", session.value("name")).toString()},
                     {"source", session.value("source").toString()},
                     {"pauseWhileOpen", pauseWhileOpen}, {"paused", paused}};
    const QString source = session.value("source").toString();
    game.insert("kind", source == "Steam" ? "steam" : metadata.value("kind", "native").toString());
    for (const auto& pair : {qMakePair("coverPath", "cover"), qMakePair("heroPath", "banner"),
                             qMakePair("logoPath", "logo")}) {
      const QString art = metadata.value(QLatin1String(pair.first)).toString();
      if (!art.isEmpty()) game.insert(QLatin1String(pair.second), art);
    }
    // Steam's header capsule has the title painted in, so the guide must not crop it.
    const QString hero = metadata.value("heroPath").toString();
    if (!hero.isEmpty() && QFileInfo(QUrl(hero).path()).completeBaseName().compare("header", Qt::CaseInsensitive) == 0)
      game.insert("bannerKind", "header");
    if (session.contains("elapsedSeconds"))
      game.insert("sessionMinutes", session.value("elapsedSeconds").toLongLong() / 60);
    if (metadata.contains("playtimeSeconds") && metadata.value("playtimeSeconds").toLongLong() >= 0)
      game.insert("totalMinutes", metadata.value("playtimeSeconds").toLongLong() / 60);
    if (metadata.contains("achievementsTotal") && metadata.value("achievementsTotal").toInt() > 0 &&
        metadata.contains("achievementsUnlocked")) {
      game.insert("achievements", QJsonObject{{"total", metadata.value("achievementsTotal").toInt()},
                                              {"unlocked", metadata.value("achievementsUnlocked").toInt()}});
    }
    data.insert("game", game);
  }
  payload.insert("data", data);
  return payload;
}

bool GuidePayload::parse(const QByteArray& json, QJsonObject* payload) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  if (!payload || error.error != QJsonParseError::NoError || !document.isObject()) return false;
  const auto object = document.object();
  if (object.value("version") != QJsonValue(kVersion) || !object.value("data").isObject() ||
      !object.value("output").isString() || !object.value("pad").isString()) return false;
  const auto game = object.value("data").toObject().value("game");
  if (!game.isUndefined() && !game.isNull() &&
      (!game.isObject() || !game.toObject().value("title").isString() ||
       !game.toObject().value("source").isString() ||
       !game.toObject().value("pauseWhileOpen").isBool() ||
       !game.toObject().value("paused").isBool())) return false;
  *payload = object;
  return true;
}

QString GuidePayload::padFamily(const QString& name) {
  const QString lower = name.toLower();
  if (lower.contains("playstation") || lower.contains("dualsense") || lower.contains("dualshock") ||
      lower.contains("sony") || lower.contains("ps4") || lower.contains("ps5")) return "playstation";
  if (lower.contains("nintendo") || lower.contains("switch") || lower.contains("joy-con")) return "nintendo";
  if (lower.contains("steam deck") || lower.contains("steam controller")) return "deck";
  if (lower.contains("xbox") || lower.contains("x-box") || lower.contains("xinput") || lower.contains("microsoft")) return "xbox";
  return "generic";
}

QJsonObject GuidePayload::difference(const QJsonObject& before, const QJsonObject& after) {
  QJsonObject patch;
  for (auto it = before.begin(); it != before.end(); ++it)
    if (!after.contains(it.key())) patch.insert(it.key(), QJsonValue::Null);
  for (auto it = after.begin(); it != after.end(); ++it) {
    if (before.value(it.key()) == it.value()) continue;
    if (it.value().isObject() && before.value(it.key()).isObject())
      patch.insert(it.key(), difference(before.value(it.key()).toObject(), it.value().toObject()));
    else patch.insert(it.key(), it.value());
  }
  return patch;
}

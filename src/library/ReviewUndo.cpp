#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameMetadata.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlQuery>

namespace {
bool artworkField(const QString& key) {
  static const QSet<QString> fields{
      "portrait",      "portraitUpdated", "selectedCoverPath", "gridId",           "gridCoverId",
      "coverAttempt",  "coverRules",      "fallbackCover",     "igdbCoverAttempt", "igdbCoverUrl",
      "gridConfirmed", "gridTitle",       "gridYear"};
  return fields.contains(key);
}
} // namespace

QVariantMap UnifiedGameModel::reviewGame(int row) const {
  QVariantMap game;
  if (row < 0 || row >= rowCount())
    return game;
  const auto roles = roleNames();
  for (int role : {GameRoles::Title, GameRoles::AppId, GameRoles::Runner, GameRoles::Source,
                   GameRoles::System, GameRoles::IsPortal, GameRoles::Installed,
                   GameRoles::InstallPath, GameRoles::LaunchTarget, GameRoles::Flatpak,
                   GameRoles::CoverPath, GameRoles::SourceCoverPath, GameRoles::MetadataKey})
    game.insert(QString::fromUtf8(roles.value(role)), data(index(row), role));
  return game;
}

QStringList UnifiedGameModel::reviewReasons(int row) const {
  const auto idx = index(row);
  if (!idx.isValid() || idx.data(GameRoles::IsPortal).toBool())
    return {};
  QStringList reasons;
  if (idx.data(GameRoles::NeedsIdentification).toBool())
    reasons << "identification";
  if (idx.data(GameRoles::CoverPath).toString().isEmpty())
    reasons << "artwork";
  if (idx.data(GameRoles::Source).toString() != "Steam" && !idx.data(GameRoles::Installed).toBool())
    reasons << "unavailable";
  const auto title = GameMetadata::normalizedTitle(idx.data(GameRoles::Title).toString());
  const auto system = idx.data(GameRoles::System).toString();
  if (m_reviewIndexDirty) {
    m_reviewTitleCounts.clear();
    for (int other = 0; other < rowCount(); ++other) {
      const auto item = index(other);
      if (item.data(GameRoles::IsPortal).toBool())
        continue;
      const auto name = GameMetadata::normalizedTitle(item.data(GameRoles::Title).toString());
      if (!name.isEmpty())
        ++m_reviewTitleCounts[item.data(GameRoles::System).toString() + QChar(0) + name];
    }
    m_reviewIndexDirty = false;
  }
  if (!title.isEmpty() && m_reviewTitleCounts.value(system + QChar(0) + title) > 1)
    reasons << "duplicates";
  return reasons;
}

bool UnifiedGameModel::hasRepairCheckpoint(const QString& key, const QString& kind) const {
  if (!m_database.isOpen())
    return false;
  QSqlQuery q(m_database);
  q.prepare("SELECT 1 FROM review_undo WHERE game_key=? AND kind=?");
  q.addBindValue(key);
  q.addBindValue(kind);
  return q.exec() && q.next();
}

bool UnifiedGameModel::repairCheckpoint(const QString& key, const QString& kind, bool restore) {
  if (!m_database.isOpen() || !m_metadata || !m_metadata->reviewWritable() ||
      !QStringList{"identity", "artwork"}.contains(kind) || !sourceForKey(key).model)
    return false;
  QSqlQuery q(m_database);
  if (!q.exec("CREATE TABLE IF NOT EXISTS review_undo(game_key TEXT,kind TEXT,payload TEXT NOT "
              "NULL,PRIMARY KEY(game_key,kind))"))
    return false;
  QVariantMap snapshot;
  if (restore) {
    q.prepare("SELECT payload FROM review_undo WHERE game_key=? AND kind=?");
    q.addBindValue(key);
    q.addBindValue(kind);
    if (!q.exec() || !q.next())
      return false;
    snapshot = QJsonDocument::fromJson(q.value(0).toByteArray()).toVariant().toMap();
    if (snapshot.value("version").toInt() != 1)
      return false;
  } else {
    snapshot = {{"version", 1}, {"metadata", m_metadata->entry(key)}};
    if (kind == "artwork") {
      QVariantMap overrides{{"cover", m_coverOverrides.value(key)},
                            {"hero", m_heroOverrides.value(key)},
                            {"logo", m_logoOverrides.value(key)}};
      const QString root = m_artworkRoot + "/review-undo";
      if (QFileInfo(root).isSymLink() || !QDir().mkpath(root))
        return false;
      qint64 used = 0;
      QDirIterator files(root, QDir::Files, QDirIterator::Subdirectories);
      while (files.hasNext()) {
        files.next();
        used += files.fileInfo().size();
      }
      auto preserve = [&](QVariant& value) {
        const QString original = value.toString();
        const QUrl url(original);
        const QString path = url.isLocalFile() ? url.toLocalFile() : original;
        if (path.isEmpty() || !QFileInfo(path).isFile())
          return true;
        if (QFileInfo(path).isSymLink() || QFileInfo(path).size() > 32 * 1024 * 1024)
          return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
          return false;
        const auto bytes = file.readAll();
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        const QString target =
            root + "/" + QString::fromLatin1(hash) + "." + QFileInfo(path).suffix();
        if (!QFileInfo::exists(target)) {
          if (used + bytes.size() > 512LL * 1024 * 1024 || !QFile::copy(path, target))
            return false;
          used += bytes.size();
        }
        value = url.isLocalFile() ? QUrl::fromLocalFile(target).toString() : target;
        return true;
      };
      for (auto it = overrides.begin(); it != overrides.end(); ++it)
        if (!preserve(it.value()))
          return false;
      auto metadata = snapshot.value("metadata").toMap();
      for (const auto& field : {"portrait", "selectedCoverPath", "fallbackCover"})
        if (metadata.contains(field) && !preserve(metadata[field]))
          return false;
      snapshot["metadata"] = metadata;
      snapshot["overrides"] = overrides;
    }
    q.prepare("INSERT OR REPLACE INTO review_undo VALUES(?,?,?)");
    q.addBindValue(key);
    q.addBindValue(kind);
    q.addBindValue(
        QString::fromUtf8(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact)));
    return q.exec();
  }
  auto value = m_metadata->entry(key);
  const auto previous = snapshot.value("metadata").toMap();
  QSet<QString> fields;
  for (auto it = value.cbegin(); it != value.cend(); ++it)
    fields.insert(it.key());
  for (auto it = previous.cbegin(); it != previous.cend(); ++it)
    fields.insert(it.key());
  for (const auto& field : fields)
    if (artworkField(field) == (kind == "artwork")) {
      if (previous.contains(field))
        value[field] = previous[field];
      else
        value.remove(field);
    }
  if (!m_database.transaction())
    return false;
  auto fail = [&] {
    m_database.rollback();
    return false;
  };
  q.prepare("INSERT OR REPLACE INTO game_metadata(game_key,payload) VALUES(?,?)");
  q.addBindValue(key);
  q.addBindValue(
      QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact)));
  if (!q.exec())
    return fail();
  if (kind == "artwork") {
    const auto parts = key.split(QChar(0));
    if (parts.size() != 3)
      return fail();
    const auto art = snapshot.value("overrides").toMap();
    q.prepare("INSERT OR REPLACE INTO "
              "artwork_overrides(source,runner,app_id,cover_path,hero_path,logo_path) "
              "VALUES(?,?,?,?,?,?)");
    for (const auto& part : parts)
      q.addBindValue(part);
    for (const auto& field : {"cover", "hero", "logo"})
      q.addBindValue(art.value(field).toString().isEmpty() ? QString("")
                                                           : art.value(field).toString());
    if (!q.exec())
      return fail();
  }
  q.prepare("DELETE FROM review_undo WHERE game_key=? AND kind=?");
  q.addBindValue(key);
  q.addBindValue(kind);
  if (!q.exec() || !m_database.commit())
    return fail();
  m_coverOverrides.clear();
  m_heroOverrides.clear();
  m_logoOverrides.clear();
  loadArtworkOverrides();
  m_metadata->reloadReviewEntry(key);
  if (rowCount())
    emit dataChanged(index(0), index(rowCount() - 1));
  return true;
}

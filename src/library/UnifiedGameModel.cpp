#include "metadata/GameMetadata.h"
#include "library/UnifiedGameModel.h"

#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace {
constexpr qint64 kMaximumArtworkBytes = 32 * 1024 * 1024;
constexpr qint64 kMaximumArtworkPixels = 64 * 1024 * 1024;

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

QString runnerFor(const QModelIndex& game) {
  const QString runner = game.data(GameRoles::Runner).toString();
  return runner.isNull() ? QStringLiteral("") : runner;
}

QString normalizedStatus(const QString& status) {
  const QString value = status.trimmed().toLower();
  static const QSet<QString> allowed = {QString{}, QStringLiteral("backlog"),
                                        QStringLiteral("playing"), QStringLiteral("completed"),
                                        QStringLiteral("abandoned")};
  return allowed.contains(value) ? value : QString{};
}

QStringList normalizedTags(const QString& input) {
  QStringList result;
  for (QString tag : input.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    tag = tag.trimmed().simplified().left(32);
    if (tag.isEmpty()) {
      continue;
    }
    const bool duplicate = std::any_of(result.cbegin(), result.cend(), [&tag](const QString& item) {
      return item.compare(tag, Qt::CaseInsensitive) == 0;
    });
    if (!duplicate) {
      result.append(tag);
    }
    if (result.size() == 20) {
      break;
    }
  }
  return result;
}

QString normalizedCollectionName(const QString& input) {
  QString name = input.trimmed().simplified();
  if (name.isEmpty() || name.size() > 48) {
    return {};
  }
  for (const QChar character : name) {
    if (character.category() == QChar::Other_Control) {
      return {};
    }
  }
  return name;
}

} // namespace

UnifiedGameModel::UnifiedGameModel(const QString& databasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-artwork-%1").arg(QUuid::createUuid().toString())) {
  if (!databasePath.isEmpty()) {
    openArtworkDatabase(databasePath);
  }
}

UnifiedGameModel::~UnifiedGameModel() {
  if (m_database.isValid()) {
    m_database.close();
    m_database = {};
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

void UnifiedGameModel::addSourceModel(QAbstractItemModel* model) {
  if (model == nullptr || m_models.contains(model)) {
    return;
  }
  m_models.append(model);
  rebuildRows();

  connect(model, &QAbstractItemModel::modelReset, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::rowsInserted, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::rowsRemoved, this, &UnifiedGameModel::rebuildRows);
  connect(model, &QAbstractItemModel::dataChanged, this,
          [this, model](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                        const QList<int>& roles) {
            // Forward only the affected rows so the proxy does not re-filter and re-sort the
            // whole library for every cover download or favorite toggle.
            if (!topLeft.isValid() || !bottomRight.isValid()) {
              if (!m_rows.isEmpty()) {
                emit dataChanged(index(0), index(m_rows.size() - 1));
              }
              return;
            }
            QSet<QString> changedGroups;
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
              const QString groupId = m_groupForGame.value(gameKey({.model = model, .row = row}));
              if (!groupId.isEmpty()) {
                changedGroups.insert(groupId);
              }
            }
            for (int row = 0; row < m_rows.size(); ++row) {
              const SourceRow& source = m_rows.at(row);
              const bool direct = source.model == model && source.row >= topLeft.row() &&
                                  source.row <= bottomRight.row();
              if (direct || (!changedGroups.isEmpty() &&
                             changedGroups.contains(m_groupForGame.value(gameKey(source))))) {
                emit dataChanged(index(row), index(row), roles);
              }
            }
          });
}

void UnifiedGameModel::setSourceEnabled(const QString& source, bool enabled) {
  const bool changed = enabled ? m_disabledSources.remove(source) > 0
                               : !m_disabledSources.contains(source);
  if (!enabled) {
    m_disabledSources.insert(source);
  }
  if (changed) {
    rebuildRows();
  }
}

int UnifiedGameModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_rows.size();
}

QVariant UnifiedGameModel::data(const QModelIndex& index, int role) const {
  const SourceRow source = mapRow(index.row());
  if (source.model == nullptr) {
    return {};
  }
  if (role == GameRoles::MetadataKey) return gameKey(source);
  if (role == GameRoles::Rating || role == GameRoles::RatingCount || role == GameRoles::Popularity) {
    const auto metadata = m_metadata ? m_metadata->entry(gameKey(source)) : QVariantMap{};
    return metadata.value(role == GameRoles::Rating ? "rating" : role == GameRoles::Popularity ? "popularity" : "ratingCount", role == GameRoles::RatingCount ? 0 : -1);
  }
  switch (role) {
  case GameRoles::Linked:
  case GameRoles::LinkedSources:
  case GameRoles::CompletionStatus:
  case GameRoles::Tags:
  case GameRoles::Collections:
  case GameRoles::Favorite:
  case GameRoles::Hidden:
  case GameRoles::Recent:
  case GameRoles::LastPlayed:
  case GameRoles::Hours:
  case GameRoles::Installed:
  case GameRoles::Pinned:
    break;
  case GameRoles::CoverPath:
  case GameRoles::CustomCover: {
    const QString override = m_coverOverrides.value(gameKey(source));
    if (role == GameRoles::CoverPath && !override.isEmpty() && QFileInfo::exists(override)) {
      return localUrl(override);
    }
    if (role == GameRoles::CustomCover) {
      return !override.isEmpty() && QFileInfo::exists(override);
    }
    if (m_metadata) {
      const QString portrait = m_metadata->entry(gameKey(source)).value("portrait").toString();
      if (!portrait.isEmpty() && QFileInfo::exists(portrait)) return localUrl(portrait);
    }
    return source.model->index(source.row, 0).data(role);
  }
  default:
    return source.model->index(source.row, 0).data(role);
  }
  const QVector<SourceRow> members = groupRows(source);
  if (role == GameRoles::Linked) {
    return members.size() > 1;
  }
  if (role == GameRoles::Pinned) {
    for (const SourceRow& member : members) {
      if (m_organizationForGame.value(gameKey(member)).pinned) {
        return true;
      }
    }
    return false;
  }
  if (role == GameRoles::LinkedSources) {
    QStringList sources;
    for (const SourceRow& member : members) {
      const QString name = member.model->index(member.row, 0).data(GameRoles::Source).toString();
      if (!sources.contains(name)) {
        sources.append(name);
      }
    }
    return sources.join(QStringLiteral(" + "));
  }
  if (role == GameRoles::CompletionStatus) {
    for (const SourceRow& member : members) {
      const QString status = m_organizationForGame.value(gameKey(member)).status;
      if (!status.isEmpty()) {
        return status;
      }
    }
    return QString{};
  }
  if (role == GameRoles::Tags || role == GameRoles::Collections) {
    QStringList values;
    for (const SourceRow& member : members) {
      const QStringList memberValues = role == GameRoles::Tags
                                           ? m_organizationForGame.value(gameKey(member)).tags
                                           : m_collectionsForGame.value(gameKey(member));
      for (const QString& value : memberValues) {
        if (std::none_of(values.cbegin(), values.cend(), [&value](const QString& item) {
              return item.compare(value, Qt::CaseInsensitive) == 0;
            })) {
          values.append(value);
        }
      }
    }
    values.sort(Qt::CaseInsensitive);
    return values;
  }
  if (role == GameRoles::Favorite || role == GameRoles::Hidden) {
    bool value = role == GameRoles::Hidden;
    for (const SourceRow& member : members) {
      const bool memberValue = member.model->index(member.row, 0).data(role).toBool();
      value = role == GameRoles::Hidden ? value && memberValue : value || memberValue;
    }
    return value;
  }
  if (role == GameRoles::Recent || role == GameRoles::LastPlayed) {
    qint64 lastPlayed = 0;
    for (const SourceRow& member : members) {
      const QModelIndex game = member.model->index(member.row, 0);
      lastPlayed = std::max(lastPlayed, game.data(GameRoles::LastPlayed).toLongLong());
      lastPlayed = std::max(lastPlayed, m_lastLaunchForGame.value(gameKey(member)));
    }
    return role == GameRoles::Recent ? lastPlayed > 0 : lastPlayed;
  }
  if (role == GameRoles::Hours) {
    int hours = 0;
    for (const SourceRow& member : members) {
      hours = std::max(hours, member.model->index(member.row, 0).data(role).toInt());
    }
    return hours;
  }
  if (role == GameRoles::Installed) {
    for (const SourceRow& member : members) {
      const QModelIndex game = member.model->index(member.row, 0);
      const QVariant installed = game.data(role);
      if (!installed.isValid() || installed.toBool()) {
        return true;
      }
    }
    return false;
  }
  return {};
}

QHash<int, QByteArray> UnifiedGameModel::roleNames() const {
  QHash<int, QByteArray> roles =
      m_models.isEmpty() ? QHash<int, QByteArray>{} : m_models.constFirst()->roleNames();
  roles.insert(GameRoles::MetadataKey, "metadataKey");
  roles.insert(GameRoles::Rating, "rating");
  roles.insert(GameRoles::RatingCount, "ratingCount");
  roles.insert(GameRoles::Popularity, "popularity");
  roles.insert(GameRoles::CustomCover, "customCover");
  roles.insert(GameRoles::Linked, "linked");
  roles.insert(GameRoles::LinkedSources, "linkedSources");
  roles.insert(GameRoles::CompletionStatus, "completionStatus");
  roles.insert(GameRoles::Tags, "tags");
  roles.insert(GameRoles::Collections, "collections");
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  roles.insert(GameRoles::Installed, "installed");
  roles.insert(GameRoles::System, "system");
  roles.insert(GameRoles::IsPortal, "isPortal");
  return roles;
}

void UnifiedGameModel::toggleFavorite(int row) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr) {
    return;
  }
  const bool desired = !data(index(row), GameRoles::Favorite).toBool();
  for (const SourceRow& member : groupRows(source)) {
    if (member.model->index(member.row, 0).data(GameRoles::Favorite).toBool() != desired) {
      QMetaObject::invokeMethod(member.model, "toggleFavorite", Q_ARG(int, member.row));
    }
  }
}

void UnifiedGameModel::toggleHidden(int row) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr) {
    return;
  }
  const bool desired = !data(index(row), GameRoles::Hidden).toBool();
  for (const SourceRow& member : groupRows(source)) {
    if (member.model->index(member.row, 0).data(GameRoles::Hidden).toBool() != desired) {
      QMetaObject::invokeMethod(member.model, "toggleHidden", Q_ARG(int, member.row));
    }
  }
}

bool UnifiedGameModel::setCustomCover(int row, const QUrl& sourceUrl) {
  const SourceRow source = mapRow(row);
  const QString sourcePath = sourceUrl.toLocalFile();
  const QFileInfo sourceInfo(sourcePath);
  if (source.model == nullptr || !m_database.isOpen() || !sourceInfo.isFile() ||
      sourceInfo.size() <= 0 || sourceInfo.size() > kMaximumArtworkBytes) {
    return false;
  }

  QImageReader reader(sourcePath);
  const QSize size = reader.size();
  const QByteArray format = reader.format().toLower();
  const bool supported = format == "jpg" || format == "jpeg" || format == "png" || format == "webp";
  if (!supported || !size.isValid() || size.width() > 16384 || size.height() > 16384 ||
      static_cast<qint64>(size.width()) * size.height() > kMaximumArtworkPixels) {
    return false;
  }

  const QString key = gameKey(source);
  if (key.isEmpty() || !QDir().mkpath(m_artworkRoot)) {
    return false;
  }
  const QString extension = format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format);
  QFile input(sourcePath);
  if (!input.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray contents = input.readAll();
  const QString digest = QString::fromLatin1(
      QCryptographicHash::hash(key.toUtf8() + contents, QCryptographicHash::Sha256).toHex());
  const QString destination =
      m_artworkRoot + QLatin1Char('/') + digest + QLatin1Char('.') + extension;
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() ||
      !output.commit()) {
    return false;
  }

  const QModelIndex game = source.model->index(source.row, 0);
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("INSERT OR REPLACE INTO artwork_overrides(source, runner, app_id, cover_path) "
                     "VALUES(?, ?, ?, ?)"));
  query.addBindValue(game.data(GameRoles::Source).toString());
  query.addBindValue(runnerFor(game));
  query.addBindValue(game.data(GameRoles::AppId).toString());
  query.addBindValue(destination);
  if (!query.exec()) {
    QFile::remove(destination);
    return false;
  }

  const QString previous = m_coverOverrides.value(key);
  m_coverOverrides.insert(key, destination);
  if (!previous.isEmpty() && previous != destination) {
    QFile::remove(previous);
  }
  emit dataChanged(index(row), index(row), {GameRoles::CoverPath, GameRoles::CustomCover});
  return true;
}

bool UnifiedGameModel::resetCustomCover(int row) {
  const SourceRow source = mapRow(row);
  const QString key = gameKey(source);
  if (source.model == nullptr || !m_database.isOpen() || !m_coverOverrides.contains(key)) {
    return false;
  }
  const QModelIndex game = source.model->index(source.row, 0);
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "DELETE FROM artwork_overrides WHERE source = ? AND runner = ? AND app_id = ?"));
  query.addBindValue(game.data(GameRoles::Source).toString());
  query.addBindValue(runnerFor(game));
  query.addBindValue(game.data(GameRoles::AppId).toString());
  if (!query.exec()) {
    return false;
  }
  const QString path = m_coverOverrides.take(key);
  QFile::remove(path);
  emit dataChanged(index(row), index(row), {GameRoles::CoverPath, GameRoles::CustomCover});
  return true;
}

QVariantList UnifiedGameModel::installations(int row) const {
  QVariantList result;
  const SourceRow source = mapRow(row);
  for (const SourceRow& member : groupRows(source)) {
    result.append(gameMap(member));
  }
  return result;
}

QVariantList UnifiedGameModel::linkCandidates(int row, const QString& search) const {
  QVariantList result;
  const SourceRow selected = mapRow(row);
  if (selected.model == nullptr) {
    return result;
  }
  QString query = search.trimmed();
  const QString selectedGroup = m_groupForGame.value(gameKey(selected));
  for (const SourceRow& candidate : m_rows) {
    const QString candidateKey = gameKey(candidate);
    if (candidateKey == gameKey(selected) ||
        (!selectedGroup.isEmpty() && m_groupForGame.value(candidateKey) == selectedGroup)) {
      continue;
    }
    const QModelIndex game = candidate.model->index(candidate.row, 0);
    if (!query.isEmpty() &&
        !game.data(GameRoles::Title).toString().contains(query, Qt::CaseInsensitive)) {
      continue;
    }
    result.append(gameMap(candidate));
    if (result.size() == 50) {
      break;
    }
  }
  return result;
}

bool UnifiedGameModel::linkGames(int row, const QString& sourceName, const QString& runner,
                                 const QString& appId) {
  const SourceRow selected = mapRow(row);
  SourceRow target;
  for (QAbstractItemModel* model : m_models) {
    for (int sourceRow = 0; sourceRow < model->rowCount(); ++sourceRow) {
      const QModelIndex game = model->index(sourceRow, 0);
      if (game.data(GameRoles::Source).toString() == sourceName && runnerFor(game) == runner &&
          game.data(GameRoles::AppId).toString() == appId) {
        target = {.model = model, .row = sourceRow};
        break;
      }
    }
    if (target.model != nullptr) {
      break;
    }
  }
  const QString selectedKey = gameKey(selected);
  const QString targetKey = gameKey(target);
  if (selectedKey.isEmpty() || targetKey.isEmpty() || selectedKey == targetKey ||
      !m_database.isOpen() ||
      (!m_groupForGame.value(selectedKey).isEmpty() &&
       m_groupForGame.value(selectedKey) == m_groupForGame.value(targetKey))) {
    return false;
  }

  const QString selectedGroup = m_groupForGame.value(selectedKey);
  const QString targetGroup = m_groupForGame.value(targetKey);
  const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!m_database.transaction()) {
    return false;
  }
  QSqlQuery merge(m_database);
  merge.prepare(QStringLiteral(
      "UPDATE game_link_members SET group_id = ?, is_primary = 0 WHERE group_id = ?"));
  for (const QString& existingGroup : {selectedGroup, targetGroup}) {
    if (existingGroup.isEmpty()) {
      continue;
    }
    merge.bindValue(0, groupId);
    merge.bindValue(1, existingGroup);
    if (!merge.exec()) {
      m_database.rollback();
      return false;
    }
  }
  QSqlQuery insert(m_database);
  insert.prepare(QStringLiteral(
      "INSERT OR REPLACE INTO game_link_members(group_id, source, runner, app_id, is_primary) "
      "VALUES(?, ?, ?, ?, ?)"));
  for (const SourceRow& member : {selected, target}) {
    const QModelIndex game = member.model->index(member.row, 0);
    insert.bindValue(0, groupId);
    insert.bindValue(1, game.data(GameRoles::Source).toString());
    insert.bindValue(2, runnerFor(game));
    insert.bindValue(3, game.data(GameRoles::AppId).toString());
    insert.bindValue(4, gameKey(member) == selectedKey);
    if (!insert.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  loadLinks();
  rebuildRows();
  return true;
}

bool UnifiedGameModel::unlinkGames(int row) {
  const SourceRow source = mapRow(row);
  const QString groupId = m_groupForGame.value(gameKey(source));
  if (groupId.isEmpty() || !m_database.isOpen()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM game_link_members WHERE group_id = ?"));
  query.addBindValue(groupId);
  if (!query.exec()) {
    return false;
  }
  loadLinks();
  rebuildRows();
  return true;
}

bool UnifiedGameModel::recordLaunch(int row, const QString& sourceName, const QString& runner,
                                    const QString& appId) {
  const SourceRow selected = mapRow(row);
  if (selected.model == nullptr || !m_database.isOpen()) {
    return false;
  }
  SourceRow launched;
  const QString normalizedRunner = runner.isNull() ? QStringLiteral("") : runner;
  for (const SourceRow& member : groupRows(selected)) {
    const QModelIndex game = member.model->index(member.row, 0);
    if (game.data(GameRoles::Source).toString() == sourceName &&
        runnerFor(game) == normalizedRunner &&
        game.data(GameRoles::AppId).toString() == appId) {
      launched = member;
      break;
    }
  }
  const QString key = gameKey(launched);
  if (key.isEmpty()) {
    return false;
  }
  const qint64 launchedAt = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO launch_activity(source, runner, app_id, last_launched, launch_count) "
      "VALUES(?, ?, ?, ?, 1) ON CONFLICT(source, runner, app_id) DO UPDATE SET last_launched = "
      "excluded.last_launched, launch_count = launch_count + 1"));
  query.addBindValue(sourceName);
  query.addBindValue(normalizedRunner);
  query.addBindValue(appId);
  query.addBindValue(launchedAt);
  if (!query.exec()) {
    return false;
  }
  m_lastLaunchForGame.insert(key, launchedAt);
  emit dataChanged(index(row), index(row), {GameRoles::Recent, GameRoles::LastPlayed});
  return true;
}

bool UnifiedGameModel::setCompletionStatus(int row, const QString& status) {
  const SourceRow source = mapRow(row);
  const QString normalized = normalizedStatus(status);
  if (source.model == nullptr || !m_database.isOpen() ||
      (!status.trimmed().isEmpty() && normalized.isEmpty()) || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json) "
      "VALUES(?, ?, ?, ?, '[]') ON CONFLICT(source, runner, app_id) DO UPDATE SET "
      "completion_status = excluded.completion_status"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, game.data(GameRoles::Source).toString());
    query.bindValue(1, runnerFor(game));
    query.bindValue(2, game.data(GameRoles::AppId).toString());
    query.bindValue(3, normalized);
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  for (const SourceRow& member : members) {
    m_organizationForGame[gameKey(member)].status = normalized;
  }
  emit dataChanged(index(row), index(row), {GameRoles::CompletionStatus});
  return true;
}

bool UnifiedGameModel::setPinned(int row, bool pinned) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr || !m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json, pinned) "
      "VALUES(?, ?, ?, '', '[]', ?) ON CONFLICT(source, runner, app_id) DO UPDATE SET "
      "pinned = excluded.pinned"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, game.data(GameRoles::Source).toString());
    query.bindValue(1, runnerFor(game));
    query.bindValue(2, game.data(GameRoles::AppId).toString());
    query.bindValue(3, pinned ? 1 : 0);
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  for (const SourceRow& member : members) {
    m_organizationForGame[gameKey(member)].pinned = pinned;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Pinned});
  return true;
}

bool UnifiedGameModel::setTags(int row, const QString& tags) {
  const SourceRow source = mapRow(row);
  if (source.model == nullptr || !m_database.isOpen()) {
    return false;
  }
  const QStringList normalized = normalizedTags(tags);
  QJsonArray jsonTags;
  for (const QString& tag : normalized) {
    jsonTags.append(tag);
  }
  const QString json = QString::fromUtf8(QJsonDocument(jsonTags).toJson(QJsonDocument::Compact));
  if (!m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO game_organization(source, runner, app_id, completion_status, tags_json) "
      "VALUES(?, ?, ?, '', ?) ON CONFLICT(source, runner, app_id) DO UPDATE SET tags_json = "
      "excluded.tags_json"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, game.data(GameRoles::Source).toString());
    query.bindValue(1, runnerFor(game));
    query.bindValue(2, game.data(GameRoles::AppId).toString());
    query.bindValue(3, json);
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  for (const SourceRow& member : members) {
    m_organizationForGame[gameKey(member)].tags = normalized;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Tags});
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::createCollection(const QString& name) {
  const QString normalized = normalizedCollectionName(name);
  if (normalized.isEmpty() || !m_database.isOpen()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("INSERT INTO collections(name, created_at) VALUES(?, ?)"));
  query.addBindValue(normalized);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  if (!query.exec()) {
    return false;
  }
  loadCollections();
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::deleteCollection(const QString& name) {
  QString storedName;
  for (const QString& collection : m_collectionNames) {
    if (collection.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      storedName = collection;
      break;
    }
  }
  if (storedName.isEmpty() || !m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM collection_games WHERE collection_name = ?"));
  query.addBindValue(storedName);
  bool okay = query.exec();
  query.prepare(QStringLiteral("DELETE FROM collections WHERE name = ?"));
  query.addBindValue(storedName);
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  loadCollections();
  if (!m_rows.isEmpty()) {
    emit dataChanged(index(0), index(m_rows.size() - 1), {GameRoles::Collections});
  }
  emit collectionsChanged();
  return true;
}

bool UnifiedGameModel::setCollectionMembership(int row, const QString& name, bool included) {
  const SourceRow source = mapRow(row);
  QString storedName;
  for (const QString& collection : m_collectionNames) {
    if (collection.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
      storedName = collection;
      break;
    }
  }
  if (source.model == nullptr || storedName.isEmpty() || !m_database.isOpen() ||
      !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(included
                    ? QStringLiteral("INSERT OR IGNORE INTO collection_games(collection_name, "
                                     "source, runner, app_id) VALUES(?, ?, ?, ?)")
                    : QStringLiteral("DELETE FROM collection_games WHERE collection_name = ? AND "
                                     "source = ? AND runner = ? AND app_id = ?"));
  const QVector<SourceRow> members = groupRows(source);
  for (const SourceRow& member : members) {
    const QModelIndex game = member.model->index(member.row, 0);
    query.bindValue(0, storedName);
    query.bindValue(1, game.data(GameRoles::Source).toString());
    query.bindValue(2, runnerFor(game));
    query.bindValue(3, game.data(GameRoles::AppId).toString());
    if (!query.exec()) {
      m_database.rollback();
      return false;
    }
  }
  if (!m_database.commit()) {
    return false;
  }
  loadCollections();
  emit dataChanged(index(row), index(row), {GameRoles::Collections});
  return true;
}

QStringList UnifiedGameModel::collectionNames() const { return m_collectionNames; }

QStringList UnifiedGameModel::tagNames() const {
  QStringList result;
  for (const OrganizationState& state : m_organizationForGame) {
    for (const QString& tag : state.tags) {
      if (std::none_of(result.cbegin(), result.cend(), [&tag](const QString& item) {
            return item.compare(tag, Qt::CaseInsensitive) == 0;
          })) {
        result.append(tag);
      }
    }
  }
  result.sort(Qt::CaseInsensitive);
  return result;
}

UnifiedGameModel::SourceRow UnifiedGameModel::mapRow(int row) const {
  if (row < 0 || row >= m_rows.size()) {
    return {};
  }
  return m_rows.at(row);
}

QString UnifiedGameModel::gameKey(const SourceRow& source) const {
  if (source.model == nullptr) {
    return {};
  }
  const QModelIndex game = source.model->index(source.row, 0);
  const QString gameSource = game.data(GameRoles::Source).toString();
  const QString appId = game.data(GameRoles::AppId).toString();
  if (gameSource.isEmpty() || appId.isEmpty()) {
    return {};
  }
  return gameSource + QChar::Null + runnerFor(game) + QChar::Null + appId;
}

UnifiedGameModel::SourceRow UnifiedGameModel::sourceForKey(const QString& key) const {
  return m_rowForKey.value(key);
}

bool UnifiedGameModel::sourceEnabled(const SourceRow& source) const {
  if (source.model == nullptr) {
    return false;
  }
  return !m_disabledSources.contains(
      source.model->index(source.row, 0).data(GameRoles::Source).toString());
}

QVector<UnifiedGameModel::SourceRow> UnifiedGameModel::groupRows(const SourceRow& source) const {
  QVector<SourceRow> rows;
  if (source.model == nullptr) {
    return rows;
  }
  rows.append(source);
  const QString key = gameKey(source);
  const QString groupId = m_groupForGame.value(key);
  if (groupId.isEmpty()) {
    return rows;
  }
  for (const SourceRow& candidate : m_rowsForGroup.value(groupId)) {
    if (candidate.model != source.model || candidate.row != source.row) {
      rows.append(candidate);
    }
  }
  return rows;
}

QVariantMap UnifiedGameModel::gameMap(const SourceRow& source) const {
  QVariantMap result;
  if (source.model == nullptr) {
    return result;
  }
  const QModelIndex game = source.model->index(source.row, 0);
  const auto roles = source.model->roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    result.insert(QString::fromUtf8(iterator.value()), game.data(iterator.key()));
  }
  const QString override = m_coverOverrides.value(gameKey(source));
  if (QFileInfo::exists(override)) {
    result.insert(QStringLiteral("coverPath"), localUrl(override));
    result.insert(QStringLiteral("customCover"), true);
  } else {
    result.insert(QStringLiteral("customCover"), false);
  }
  const qint64 lastPlayed = std::max(game.data(GameRoles::LastPlayed).toLongLong(),
                                     m_lastLaunchForGame.value(gameKey(source)));
  result.insert(QStringLiteral("lastPlayed"), lastPlayed);
  result.insert(QStringLiteral("recent"), lastPlayed > 0);
  result.insert(QStringLiteral("completionStatus"),
                m_organizationForGame.value(gameKey(source)).status);
  result.insert(QStringLiteral("tags"), m_organizationForGame.value(gameKey(source)).tags);
  result.insert(QStringLiteral("collections"), m_collectionsForGame.value(gameKey(source)));
  return result;
}

void UnifiedGameModel::rebuildRows() {
  beginResetModel();
  m_rows.clear();
  m_rowForKey.clear();
  m_rowsForGroup.clear();
  for (QAbstractItemModel* model : m_models) {
    for (int row = 0; row < model->rowCount(); ++row) {
      const SourceRow source{.model = model, .row = row};
      if (!sourceEnabled(source)) {
        continue;
      }
      const QString key = gameKey(source);
      if (key.isEmpty()) {
        continue;
      }
      m_rowForKey.insert(key, source);
      const QString groupId = m_groupForGame.value(key);
      if (!groupId.isEmpty()) {
        m_rowsForGroup[groupId].append(source);
      }
    }
  }
  QSet<QString> addedGroups;
  for (QAbstractItemModel* model : m_models) {
    for (int row = 0; row < model->rowCount(); ++row) {
      const SourceRow source{.model = model, .row = row};
      if (!sourceEnabled(source)) {
        continue;
      }
      const QString groupId = m_groupForGame.value(gameKey(source));
      if (groupId.isEmpty()) {
        m_rows.append(source);
      } else if (!addedGroups.contains(groupId)) {
        SourceRow representative = sourceForKey(m_primaryForGroup.value(groupId));
        m_rows.append(representative.model == nullptr ? source : representative);
        addedGroups.insert(groupId);
      }
    }
  }
  endResetModel();
}

bool UnifiedGameModel::openArtworkDatabase(const QString& path) {
  m_databasePath = path;
  m_artworkRoot = QFileInfo(path).absolutePath() + QStringLiteral("/artwork");
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    return false;
  }
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS artwork_overrides (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, cover_path TEXT NOT NULL, PRIMARY KEY(source, runner, "
          "app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS game_link_members (group_id TEXT NOT NULL, source TEXT NOT "
          "NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, is_primary INTEGER NOT NULL DEFAULT "
          "0, PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS launch_activity (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, last_launched INTEGER NOT NULL, launch_count INTEGER NOT "
          "NULL DEFAULT 1, PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS game_organization (source TEXT NOT NULL, runner TEXT NOT "
          "NULL, app_id TEXT NOT NULL, completion_status TEXT NOT NULL DEFAULT '', tags_json TEXT "
          "NOT NULL DEFAULT '[]', pinned INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source, runner, app_id))"))) {
    return false;
  }
  // Libraries created before pinning existed get the column added in place.
  {
    QSqlQuery columns(m_database);
    bool hasPinned = false;
    if (columns.exec(QStringLiteral("PRAGMA table_info(game_organization)"))) {
      while (columns.next()) {
        hasPinned = hasPinned || columns.value(1).toString() == QLatin1String("pinned");
      }
    }
    if (!hasPinned) {
      query.exec(QStringLiteral("ALTER TABLE game_organization ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0"));
    }
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS collections (name TEXT PRIMARY KEY COLLATE NOCASE, "
          "created_at INTEGER NOT NULL)"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS collection_games (collection_name TEXT NOT NULL, source "
          "TEXT NOT NULL, runner TEXT NOT NULL, app_id TEXT NOT NULL, PRIMARY KEY(collection_name, "
          "source, runner, app_id))"))) {
    return false;
  }
  // SteamGameModel owns PRAGMA user_version for the shared database. The organization tables
  // above are created idempotently, so nothing here depends on a version stamp.
  loadArtworkOverrides();
  loadLinks();
  loadLaunchActivity();
  loadOrganization();
  loadCollections();
  return true;
}

void UnifiedGameModel::loadArtworkOverrides() {
  QSqlQuery query(m_database);
  if (!query.exec(
          QStringLiteral("SELECT source, runner, app_id, cover_path FROM artwork_overrides"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    m_coverOverrides.insert(key, query.value(3).toString());
  }
}

void UnifiedGameModel::loadLinks() {
  m_groupForGame.clear();
  m_primaryForGroup.clear();
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT group_id, source, runner, app_id, is_primary FROM game_link_members"))) {
    return;
  }
  while (query.next()) {
    const QString groupId = query.value(0).toString();
    const QString key = query.value(1).toString() + QChar::Null + query.value(2).toString() +
                        QChar::Null + query.value(3).toString();
    m_groupForGame.insert(key, groupId);
    if (query.value(4).toBool()) {
      m_primaryForGroup.insert(groupId, key);
    }
  }
}

void UnifiedGameModel::loadLaunchActivity() {
  m_lastLaunchForGame.clear();
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT source, runner, app_id, last_launched FROM launch_activity"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    m_lastLaunchForGame.insert(key, query.value(3).toLongLong());
  }
}

void UnifiedGameModel::loadOrganization() {
  m_organizationForGame.clear();
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT source, runner, app_id, completion_status, tags_json, pinned FROM game_organization"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(0).toString() + QChar::Null + query.value(1).toString() +
                        QChar::Null + query.value(2).toString();
    QStringList tags;
    const QJsonArray values = QJsonDocument::fromJson(query.value(4).toByteArray()).array();
    for (const QJsonValue& value : values) {
      if (value.isString()) {
        tags.append(value.toString());
      }
    }
    m_organizationForGame.insert(key, {.status = normalizedStatus(query.value(3).toString()),
                                       .tags = tags,
                                       .pinned = query.value(5).toBool()});
  }
}

void UnifiedGameModel::loadCollections() {
  m_collectionNames.clear();
  m_collectionsForGame.clear();
  QSqlQuery query(m_database);
  if (query.exec(QStringLiteral("SELECT name FROM collections ORDER BY name COLLATE NOCASE"))) {
    while (query.next()) {
      m_collectionNames.append(query.value(0).toString());
    }
  }
  if (!query.exec(QStringLiteral(
          "SELECT collection_name, source, runner, app_id FROM collection_games ORDER BY "
          "collection_name COLLATE NOCASE"))) {
    return;
  }
  while (query.next()) {
    const QString key = query.value(1).toString() + QChar::Null + query.value(2).toString() +
                        QChar::Null + query.value(3).toString();
    m_collectionsForGame[key].append(query.value(0).toString());
  }
}

void UnifiedGameModel::setMetadata(GameMetadata* metadata) {
  if (m_metadata) disconnect(m_metadata, nullptr, this, nullptr);
  m_metadata = metadata;
  if (metadata) connect(metadata, &GameMetadata::entryChanged, this, [this](const QString& key) {
    for (int row = 0; row < m_rows.size(); ++row) if (gameKey(m_rows.at(row)) == key)
      emit dataChanged(index(row), index(row), {GameRoles::CoverPath, GameRoles::Rating, GameRoles::RatingCount, GameRoles::Popularity});
  });
  if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size()-1), {GameRoles::CoverPath, GameRoles::Rating, GameRoles::RatingCount, GameRoles::Popularity});
}

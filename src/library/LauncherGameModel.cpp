#include "library/LauncherGameModel.h"

#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QtConcurrent>

namespace {
QColor colorFor(const QString& id, int offset) {
  const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

QByteArray encoded(const LauncherGameRecord& game) {
  return QJsonDocument(
             QJsonObject{{"gameId", game.gameId},
                         {"title", game.title},
                         {"system", game.system},
                         {"subtitle", game.subtitle},
                         {"path", game.path},
                         {"serial", game.serial},
                         {"coverPath", game.coverPath},
                         {"heroPath", game.heroPath},
                         {"executable", game.executable},
                         {"arguments", QJsonArray::fromStringList(game.arguments)},
                         {"directory", game.directory},
                         {"desktopEntry", game.desktopEntry},
                         {"link", game.link}})
      .toJson(QJsonDocument::Compact);
}

LauncherGameRecord decoded(const QByteArray& json) {
  const QJsonObject object = QJsonDocument::fromJson(json).object();
  QStringList arguments;
  for (const QJsonValue& value : object.value("arguments").toArray()) {
    arguments.append(value.toString());
  }
  return {.gameId = object.value("gameId").toString(),
          .title = object.value("title").toString(),
          .system = object.value("system").toString(),
          .subtitle = object.value("subtitle").toString(),
          .path = object.value("path").toString(),
          .serial = object.value("serial").toString(),
          .coverPath = object.value("coverPath").toString(),
          .heroPath = object.value("heroPath").toString(),
          .executable = object.value("executable").toString(),
          .arguments = arguments,
          .directory = object.value("directory").toString(),
          .desktopEntry = object.value("desktopEntry").toString(),
          .link = object.value("link").toString()};
}
} // namespace

LauncherGameModel::LauncherGameModel(Definition definition, const QString& omakadeDatabasePath,
                                     PlaySessionStore* playSessions, QObject* parent)
    : QAbstractListModel(parent), m_definition(std::move(definition)),
      m_connectionName(QStringLiteral("omakade-%1-%2")
                           .arg(m_definition.key)
                           .arg(reinterpret_cast<quintptr>(this))),
      m_playSessions(playSessions) {
  if (m_playSessions != nullptr) {
    connect(m_playSessions, &PlaySessionStore::totalsChanged, this, [this] {
      if (!m_games.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_games.size()) - 1),
                         {GameRoles::Hours, GameRoles::PlaytimeSeconds, GameRoles::PlaytimeText,
                          GameRoles::PlaytimeProvenance, GameRoles::LastPlayed});
      }
    });
  }
  connect(&m_scanWatcher, &QFutureWatcher<LauncherScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

LauncherGameModel::~LauncherGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int LauncherGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant LauncherGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> LauncherGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

QStringList LauncherGameModel::gameIds() const {
  QStringList ids;
  for (const Game& game : m_games) {
    ids.append(game.record.gameId);
  }
  return ids;
}

QString LauncherGameModel::launchTarget(const LauncherGameRecord& game) {
  return QString::fromUtf8(
      QJsonDocument(QJsonObject{{"id", game.gameId},
                                {"executable", game.executable},
                                {"arguments", QJsonArray::fromStringList(game.arguments)},
                                {"directory", game.directory}})
          .toJson(QJsonDocument::Compact));
}

void LauncherGameModel::setFlag(int row, const char* column, bool Game::*flag, int role) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.*flag = !(game.*flag);
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE %1 SET %2 = ? WHERE game_id = ?")
                    .arg(table(), QString::fromLatin1(column)));
  query.addBindValue(game.*flag);
  query.addBindValue(game.record.gameId);
  if (!query.exec()) {
    game.*flag = !(game.*flag);
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {role});
}

void LauncherGameModel::toggleFavorite(int row) {
  setFlag(row, "favorite", &Game::favorite, GameRoles::Favorite);
}

void LauncherGameModel::toggleHidden(int row) {
  setFlag(row, "hidden", &Game::hidden, GameRoles::Hidden);
}

void LauncherGameModel::refresh() {
  if (m_scanWatcher.isRunning() || !m_definition.scan) {
    return;
  }
  m_scanning = true;
  setStatus(QStringLiteral("Scanning %1").arg(m_definition.source));
  m_scanWatcher.setFuture(QtConcurrent::run(m_definition.scan));
}

bool LauncherGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("%1 cache unavailable").arg(m_definition.source),
              m_database.lastError().text());
    return false;
  }
  return true;
}

bool LauncherGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (game_id TEXT PRIMARY KEY, "
                                 "record TEXT NOT NULL, favorite INTEGER NOT NULL DEFAULT 0, "
                                 "hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)")
                      .arg(table())) ||
      !query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize the %1 cache").arg(m_definition.source),
              query.lastError().text());
    return false;
  }
  return true;
}

void LauncherGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("SELECT record, favorite, hidden FROM %1 WHERE observed_at > 0")
                      .arg(table()))) {
    setStatus(QStringLiteral("Could not load cached %1 games").arg(m_definition.source),
              query.lastError().text());
    return;
  }
  while (query.next()) {
    const LauncherGameRecord record = decoded(query.value(0).toByteArray());
    if (record.gameId.isEmpty()) {
      continue;
    }
    loaded.append({.record = record,
                   .favorite = query.value(1).toBool(),
                   .hidden = query.value(2).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  std::sort(loaded.begin(), loaded.end(), [](const Game& left, const Game& right) {
    return left.record.title.compare(right.record.title, Qt::CaseInsensitive) < 0;
  });
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void LauncherGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = ?"));
  query.addBindValue(m_definition.key);
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_detected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached %1 games").arg(m_definition.source);
  }
}

void LauncherGameModel::applyScan(const LauncherScanResult& result) {
  m_detected = !result.roots.isEmpty();
  // Default discovery can omit an unmounted folder entirely. Remember roots that held
  // cached games so that disappearance cannot turn a failed scan into an empty library.
  QStringList warnings = result.warnings;
  bool missingRoot = false;
  for (const QString& root : m_detectedPaths) {
    if (QFileInfo::exists(root)) continue;
    for (const Game& game : m_games) {
      if (game.record.path.startsWith(QDir::cleanPath(root) + QLatin1Char('/'))) {
        missingRoot = true;
        warnings.append(QStringLiteral("Game folder is unavailable: %1").arg(root));
        break;
      }
    }
  }
  // A game folder that went missing, such as an unmounted drive, keeps the cached library.
  if (missingRoot || result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty() && !result.warnings.isEmpty())) {
    setStatus(QStringLiteral("%1 scan interrupted; kept the cached library").arg(m_definition.source),
              warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update %1 games").arg(m_definition.source),
              m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE %1 SET observed_at = 0").arg(table()));
  for (const LauncherGameRecord& game : result.games) {
    query.prepare(QStringLiteral("INSERT INTO %1(game_id, record, observed_at) VALUES(?, ?, ?) "
                                 "ON CONFLICT(game_id) DO UPDATE SET record = excluded.record, "
                                 "observed_at = excluded.observed_at")
                      .arg(table()));
    query.addBindValue(game.gameId);
    query.addBindValue(QString::fromUtf8(encoded(game)));
    query.addBindValue(scanTimestamp);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES(?, ?, ?, ?) "
      "ON CONFLICT(source) DO UPDATE SET last_scan = excluded.last_scan, last_error = "
      "excluded.last_error, paths = excluded.paths"));
  query.addBindValue(m_definition.key);
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update %1 games").arg(m_definition.source),
              query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_detected ? QStringLiteral("Found %1 %2 game(s)")
                             .arg(result.games.size())
                             .arg(m_definition.source)
                       : QStringLiteral("No %1 games were found").arg(m_definition.source),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant LauncherGameModel::valueForRole(const Game& game, int role) const {
  const LauncherGameRecord& record = game.record;
  switch (role) {
  case GameRoles::Title:
    return record.title;
  case GameRoles::Subtitle:
    return record.subtitle;
  case GameRoles::Description:
    return m_definition.description;
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_playSessions, record.path, -1);
  case GameRoles::PlaytimeSeconds:
    return PlaySessionStore::displayedSeconds(m_playSessions, record.path, 0);
  case GameRoles::Hours:
    return static_cast<int>(PlaySessionStore::displayedSeconds(m_playSessions, record.path, 0) /
                            3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, record.path, 0) > 0;
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, record.path, 0);
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return record.title.left(1).toUpper();
  case GameRoles::AppId:
    return record.gameId;
  case GameRoles::CoverPath:
    return localUrl(record.coverPath);
  case GameRoles::HeroPath:
    return localUrl(record.heroPath);
  case GameRoles::LogoPath:
  case GameRoles::Runner:
    return QString{};
  case GameRoles::InstallPath:
    return record.path;
  case GameRoles::Source:
    return m_definition.source;
  case GameRoles::LaunchTarget:
    return launchTarget(record);
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return record.system;
  default:
    return {};
  }
}

void LauncherGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

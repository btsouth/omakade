#include "library/RyujinxGameModel.h"

#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
} // namespace

RyujinxGameModel::RyujinxGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-ryujinx-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<RyujinxScanResult>::finished, this,
          [this] {
            m_scanning = false;
            applyScan(m_scanWatcher.result());
            emit statusChanged();
          });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

RyujinxGameModel::~RyujinxGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int RyujinxGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant RyujinxGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> RyujinxGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool RyujinxGameModel::ryujinxDetected() const { return m_ryujinxDetected; }
QString RyujinxGameModel::statusText() const { return m_statusText; }
QString RyujinxGameModel::errorText() const { return m_errorText; }
QStringList RyujinxGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 RyujinxGameModel::lastScan() const { return m_lastScan; }

void RyujinxGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE ryujinx_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.ryujinx.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void RyujinxGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE ryujinx_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.ryujinx.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void RyujinxGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = RyujinxScanner::discoverRoots();
  setStatus(QStringLiteral("Scanning Ryujinx library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return RyujinxScanner::scan(roots); }));
}

void RyujinxGameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(RyujinxScanner::scan(roots));
}

bool RyujinxGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Ryujinx cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool RyujinxGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS ryujinx_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT, title_id TEXT, last_played INTEGER NOT NULL DEFAULT 0, playtime_seconds "
          "INTEGER NOT NULL DEFAULT 0, cover_path TEXT, flatpak INTEGER NOT NULL DEFAULT 0, "
          "flatpak_app_id TEXT NOT NULL DEFAULT '', favorite INTEGER NOT NULL DEFAULT 0, hidden "
          "INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize Ryujinx cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize Ryujinx cache"), query.lastError().text());
    return false;
  }
  bool hasPaths = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(source_state)"))) {
    while (query.next()) {
      hasPaths = hasPaths || query.value(1).toString() == QStringLiteral("paths");
    }
  }
  if (!hasPaths && !query.exec(QStringLiteral(
                       "ALTER TABLE source_state ADD COLUMN paths TEXT NOT NULL DEFAULT ''"))) {
    setStatus(QStringLiteral("Could not migrate Ryujinx cache"), query.lastError().text());
    return false;
  }
  bool hasFlatpakAppId = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(ryujinx_games)"))) {
    while (query.next()) {
      hasFlatpakAppId =
          hasFlatpakAppId || query.value(1).toString() == QStringLiteral("flatpak_app_id");
    }
  }
  if (!hasFlatpakAppId &&
      !query.exec(QStringLiteral(
          "ALTER TABLE ryujinx_games ADD COLUMN flatpak_app_id TEXT NOT NULL DEFAULT ''"))) {
    setStatus(QStringLiteral("Could not migrate Ryujinx cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "UPDATE ryujinx_games SET flatpak_app_id = 'io.github.ryubing.Ryujinx' "
          "WHERE flatpak = 1 AND flatpak_app_id = ''"))) {
    setStatus(QStringLiteral("Could not migrate Ryujinx cache"), query.lastError().text());
    return false;
  }
  return true;
}

void RyujinxGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("SELECT game_id, name, path, title_id, cover_path, last_played, "
                                 "playtime_seconds, flatpak, flatpak_app_id, favorite, hidden FROM "
                                 "ryujinx_games WHERE observed_at > 0 ORDER BY name COLLATE "
                                 "NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Ryujinx games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    RyujinxGameRecord record{.gameId = query.value(0).toString(),
                             .titleId = query.value(3).toString(),
                             .title = query.value(1).toString(),
                             .path = query.value(2).toString(),
                             .coverPath = query.value(4).toString(),
                             .playtimeSeconds = query.value(6).toLongLong(),
                             .lastPlayed = query.value(5).toLongLong(),
                             .flatpak = query.value(7).toBool(),
                             .flatpakAppId = query.value(8).toString()};
    loaded.append({.ryujinx = record,
                   .favorite = query.value(9).toBool(),
                   .hidden = query.value(10).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void RyujinxGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'ryujinx'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_ryujinxDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Ryujinx games");
  }
}

void RyujinxGameModel::applyScan(const RyujinxScanResult& result) {
  m_ryujinxDetected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Ryujinx scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Ryujinx games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE ryujinx_games SET observed_at = 0"));
  for (const RyujinxGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO ryujinx_games(game_id, name, path, title_id, cover_path, last_played, "
        "playtime_seconds, flatpak, flatpak_app_id, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, "
        "?, strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, path "
        "= excluded.path, title_id = excluded.title_id, cover_path = excluded.cover_path, "
        "last_played = excluded.last_played, playtime_seconds = excluded.playtime_seconds, "
        "flatpak = excluded.flatpak, flatpak_app_id = excluded.flatpak_app_id, observed_at = "
        "excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.titleId);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.playtimeSeconds);
    query.addBindValue(game.flatpak);
    query.addBindValue(game.flatpakAppId.isNull() ? QStringLiteral("") : game.flatpakAppId);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('ryujinx', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Ryujinx games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_ryujinxDetected ? QStringLiteral("Imported %1 Ryujinx game(s)").arg(result.games.size())
                              : QStringLiteral("Ryujinx was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant RyujinxGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.ryujinx.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Ryujinx");
  case GameRoles::Description:
    return QStringLiteral("Nintendo Switch game launched through Ryujinx.");
  case GameRoles::Hours:
    return static_cast<int>(game.ryujinx.playtimeSeconds / 3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.ryujinx.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.ryujinx.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.ryujinx.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.ryujinx.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.ryujinx.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.ryujinx.path;
  case GameRoles::Source:
    return QStringLiteral("Ryujinx");
  case GameRoles::Runner:
    return game.ryujinx.flatpakAppId;
  case GameRoles::LaunchTarget:
    // Ryujinx's positional argument must be a ROM file path, never a title id.
    return game.ryujinx.path;
  case GameRoles::Flatpak:
    return game.ryujinx.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return QStringLiteral("switch");
  default:
    return {};
  }
}

void RyujinxGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

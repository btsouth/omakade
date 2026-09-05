#include "library/FaugusGameModel.h"

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
QColor colorFor(const QString& key, int offset) {
  const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}
} // namespace

FaugusGameModel::FaugusGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-faugus-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<FaugusScanResult>::finished, this,
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

FaugusGameModel::~FaugusGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int FaugusGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant FaugusGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> FaugusGameModel::roleNames() const {
  return GameRoles::names();
}

bool FaugusGameModel::faugusDetected() const { return m_faugusDetected; }
QString FaugusGameModel::statusText() const { return m_statusText; }
QString FaugusGameModel::errorText() const { return m_errorText; }
QStringList FaugusGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 FaugusGameModel::lastScan() const { return m_lastScan; }

void FaugusGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE faugus_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.faugus.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void FaugusGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE faugus_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.faugus.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void FaugusGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = FaugusScanner::discoverRoots();
  setStatus(QStringLiteral("Scanning Faugus library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return FaugusScanner::scan(roots); }));
}

void FaugusGameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(FaugusScanner::scan(roots));
}

bool FaugusGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    setStatus(QStringLiteral("Faugus cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool FaugusGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS faugus_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "executable_path TEXT, runner TEXT, cover_path TEXT, hero_path TEXT, playtime_seconds "
          "INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT "
          "NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    return false;
  }
  return true;
}

void FaugusGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, executable_path, runner, cover_path, hero_path, "
          "playtime_seconds, flatpak, favorite, hidden FROM faugus_games WHERE observed_at > 0 "
          "ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Faugus games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    FaugusGameRecord record{.gameId = query.value(0).toString(),
                            .title = query.value(1).toString(),
                            .executablePath = query.value(2).toString(),
                            .runner = query.value(3).toString(),
                            .coverPath = query.value(4).toString(),
                            .heroPath = query.value(5).toString(),
                            .playtimeSeconds = query.value(6).toLongLong(),
                            .flatpak = query.value(7).toBool()};
    loaded.append({.faugus = record,
                   .favorite = query.value(8).toBool(),
                   .hidden = query.value(9).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void FaugusGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'faugus'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_faugusDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Faugus library");
  }
}

void FaugusGameModel::applyScan(const FaugusScanResult& result) {
  m_faugusDetected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Faugus scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Faugus games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE faugus_games SET observed_at = 0"));
  for (const FaugusGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO faugus_games(game_id, name, executable_path, runner, cover_path, hero_path, "
        "playtime_seconds, flatpak, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, "
        "strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, "
        "executable_path = excluded.executable_path, runner = excluded.runner, cover_path = "
        "excluded.cover_path, hero_path = excluded.hero_path, playtime_seconds = "
        "excluded.playtime_seconds, flatpak = excluded.flatpak, observed_at = "
        "excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.executablePath);
    query.addBindValue(game.runner);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroPath);
    query.addBindValue(game.playtimeSeconds);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('faugus', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Faugus games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_faugusDetected ? QStringLiteral("Imported %1 Faugus game(s)").arg(result.games.size())
                             : QStringLiteral("Faugus was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant FaugusGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.faugus.title;
  case GameRoles::Subtitle:
    return game.faugus.runner.isEmpty() ? QStringLiteral("Faugus")
                                        : QStringLiteral("Faugus · %1").arg(game.faugus.runner);
  case GameRoles::Description:
    return QStringLiteral("Configured and managed by Faugus.");
  case GameRoles::Hours:
    return game.faugus.playtimeSeconds / 3600;
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.faugus.playtimeSeconds > 0;
  case GameRoles::LastPlayed:
    return 0;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.faugus.title.left(1).toUpper();
  case GameRoles::AppId:
    return game.faugus.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.faugus.coverPath);
  case GameRoles::HeroPath:
    return localUrl(game.faugus.heroPath);
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.faugus.executablePath;
  case GameRoles::Source:
    return QStringLiteral("Faugus");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::Flatpak:
    return game.faugus.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void FaugusGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

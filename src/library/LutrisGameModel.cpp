#include "library/LutrisGameModel.h"

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

LutrisGameModel::LutrisGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-lutris-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<LutrisScanResult>::finished, this,
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

LutrisGameModel::~LutrisGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int LutrisGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant LutrisGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> LutrisGameModel::roleNames() const {
  return GameRoles::names();
}

bool LutrisGameModel::lutrisDetected() const { return m_lutrisDetected; }
QString LutrisGameModel::statusText() const { return m_statusText; }
QString LutrisGameModel::errorText() const { return m_errorText; }
QStringList LutrisGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 LutrisGameModel::lastScan() const { return m_lastScan; }

void LutrisGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE lutris_games SET favorite = ? WHERE id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.lutris.id);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void LutrisGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE lutris_games SET hidden = ? WHERE id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.lutris.id);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void LutrisGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList paths = LutrisScanner::discoverDatabases();
  setStatus(QStringLiteral("Scanning Lutris library"));
  m_scanWatcher.setFuture(QtConcurrent::run([paths] { return LutrisScanner::scan(paths); }));
}

void LutrisGameModel::refreshFromDatabases(const QStringList& paths) {
  applyScan(LutrisScanner::scan(paths));
}

bool LutrisGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Lutris cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool LutrisGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS lutris_games (id TEXT PRIMARY KEY, slug TEXT NOT NULL, name TEXT "
      "NOT NULL, runner TEXT, directory TEXT, platform TEXT, year INTEGER NOT NULL DEFAULT 0, "
      "last_played INTEGER NOT NULL DEFAULT 0, playtime_minutes INTEGER NOT NULL DEFAULT 0, "
      "cover_path TEXT, flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, "
      "hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    return false;
  }
  bool hasPaths = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(source_state)"))) {
    while (query.next()) {
      hasPaths = hasPaths || query.value(1).toString() == QStringLiteral("paths");
    }
  }
  return hasPaths || query.exec(QStringLiteral(
                         "ALTER TABLE source_state ADD COLUMN paths TEXT NOT NULL DEFAULT ''"));
}

void LutrisGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("SELECT id, slug, name, runner, directory, platform, year, "
                                 "last_played, playtime_minutes, "
                                 "cover_path, flatpak, favorite, hidden FROM lutris_games WHERE "
                                 "observed_at > 0 ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Lutris games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    LutrisGameRecord record{.id = query.value(0).toString(),
                            .slug = query.value(1).toString(),
                            .title = query.value(2).toString(),
                            .runner = query.value(3).toString(),
                            .installPath = query.value(4).toString(),
                            .platform = query.value(5).toString(),
                            .coverPath = query.value(9).toString(),
                            .year = query.value(6).toInt(),
                            .lastPlayed = query.value(7).toLongLong(),
                            .playtimeMinutes = query.value(8).toInt(),
                            .flatpak = query.value(10).toBool()};
    loaded.append({.lutris = record,
                   .favorite = query.value(11).toBool(),
                   .hidden = query.value(12).toBool(),
                   .accentStart = colorFor(record.id, 0),
                   .accentEnd = colorFor(record.id, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void LutrisGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'lutris'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_lutrisDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Lutris library");
  }
}

void LutrisGameModel::applyScan(const LutrisScanResult& result) {
  m_lutrisDetected = !result.databasePaths.isEmpty();
  if (result.incomplete || (result.databasePaths.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Lutris scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Lutris games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE lutris_games SET observed_at = 0"));
  for (const LutrisGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO lutris_games(id, slug, name, runner, directory, platform, year, last_played, "
        "playtime_minutes, cover_path, flatpak, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, strftime('%s', 'now')) ON CONFLICT(id) DO UPDATE SET slug = excluded.slug, name = "
        "excluded.name, runner = excluded.runner, directory = excluded.directory, platform = "
        "excluded.platform, year = excluded.year, last_played = excluded.last_played, "
        "playtime_minutes = excluded.playtime_minutes, cover_path = excluded.cover_path, flatpak = "
        "excluded.flatpak, observed_at = excluded.observed_at"));
    query.addBindValue(game.id);
    query.addBindValue(game.slug);
    query.addBindValue(game.title);
    query.addBindValue(game.runner);
    query.addBindValue(game.installPath);
    query.addBindValue(game.platform);
    query.addBindValue(game.year);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.playtimeMinutes);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('lutris', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.databasePaths.isEmpty()
                         ? QStringLiteral("")
                         : result.databasePaths.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Lutris games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.databasePaths;
  m_lastScan = scanTimestamp;
  setStatus(m_lutrisDetected ? QStringLiteral("Imported %1 Lutris game(s)").arg(result.games.size())
                             : QStringLiteral("Lutris was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant LutrisGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.lutris.title;
  case GameRoles::Subtitle:
    return game.lutris.runner.isEmpty() ? QStringLiteral("Lutris")
                                        : QStringLiteral("Lutris · %1").arg(game.lutris.runner);
  case GameRoles::Description:
    return QStringLiteral("Installed locally through Lutris.");
  case GameRoles::Hours:
    return game.lutris.playtimeMinutes / 60;
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.lutris.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.lutris.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.lutris.title.left(1).toUpper();
  case GameRoles::Year:
    return game.lutris.year;
  case GameRoles::AppId:
    return game.lutris.id;
  case GameRoles::CoverPath:
    return localUrl(game.lutris.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.lutris.installPath;
  case GameRoles::Source:
    return QStringLiteral("Lutris");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::Flatpak:
    return game.lutris.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void LutrisGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

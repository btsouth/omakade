#include "library/LichiiGameModel.h"

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

LichiiGameModel::LichiiGameModel(const QString& omakadeDatabasePath, PlaySessionStore* playSessions,
                                 QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-lichii-%1").arg(reinterpret_cast<quintptr>(this))),
      m_playSessions(playSessions) {
  if (m_playSessions != nullptr) {
    connect(m_playSessions, &PlaySessionStore::totalsChanged, this, [this] {
      if (!m_games.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_games.size()) - 1),
                         {GameRoles::Hours, GameRoles::PlaytimeSeconds, GameRoles::PlaytimeText,
                          GameRoles::PlaytimeProvenance, GameRoles::LastPlayed, GameRoles::Recent});
      }
    });
  }
  connect(&m_scanWatcher, &QFutureWatcher<LichiiScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

LichiiGameModel::~LichiiGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int LichiiGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant LichiiGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> LichiiGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool LichiiGameModel::lichiiDetected() const { return m_lichiiDetected; }
QString LichiiGameModel::statusText() const { return m_statusText; }
QString LichiiGameModel::errorText() const { return m_errorText; }
QStringList LichiiGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 LichiiGameModel::lastScan() const { return m_lastScan; }

void LichiiGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE lichii_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.lichii.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void LichiiGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE lichii_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.lichii.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void LichiiGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QVector<LichiiInstall> installs = LichiiScanner::discoverInstalls();
  setStatus(QStringLiteral("Scanning Lichii library"));
  m_scanWatcher.setFuture(QtConcurrent::run([installs] { return LichiiScanner::scan(installs); }));
}

void LichiiGameModel::refreshFromInstalls(const QVector<LichiiInstall>& installs) {
  applyScan(LichiiScanner::scan(installs));
}

bool LichiiGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Lichii cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool LichiiGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS lichii_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT, title_id TEXT, cover_path TEXT, playtime_seconds INTEGER NOT NULL DEFAULT 0, "
          "last_played INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, hidden "
          "INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize Lichii cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize Lichii cache"), query.lastError().text());
    return false;
  }
  return true;
}

void LichiiGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, title_id, cover_path, playtime_seconds, last_played, "
          "favorite, hidden FROM lichii_games WHERE observed_at > 0 ORDER BY name COLLATE "
          "NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Lichii games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    LichiiGameRecord record{.gameId = query.value(0).toString(),
                            .titleId = query.value(3).toString(),
                            .title = query.value(1).toString(),
                            .path = query.value(2).toString(),
                            .coverPath = query.value(4).toString(),
                            .playtimeSeconds = query.value(5).toLongLong(),
                            .lastPlayed = query.value(6).toLongLong()};
    if (m_playSessions != nullptr) {
      m_playSessions->observeImportedPlaytime(record.path, record.playtimeSeconds);
    }
    loaded.append({.lichii = record,
                   .favorite = query.value(7).toBool(),
                   .hidden = query.value(8).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void LichiiGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'lichii'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_lichiiDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Lichii games");
  }
}

void LichiiGameModel::applyScan(const LichiiScanResult& result) {
  m_lichiiDetected = !result.roots.isEmpty();
  if ((result.incomplete && result.games.isEmpty()) ||
      (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Lichii scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Lichii games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  // A scan that could not see every folder adds and updates what it found and keeps the
  // rest, so one unmounted drive does not hold back or hide the games on the others.
  bool okay = result.incomplete ||
              query.exec(QStringLiteral("UPDATE lichii_games SET observed_at = 0"));
  for (const LichiiGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO lichii_games(game_id, name, path, title_id, cover_path, playtime_seconds, "
        "last_played, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now')) ON "
        "CONFLICT(game_id) DO UPDATE SET name = excluded.name, path = excluded.path, title_id = "
        "excluded.title_id, cover_path = excluded.cover_path, playtime_seconds = "
        "excluded.playtime_seconds, last_played = excluded.last_played, observed_at = "
        "excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.titleId);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.playtimeSeconds);
    query.addBindValue(game.lastPlayed);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('lichii', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Lichii games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  const QString imported = result.incomplete
                               ? QStringLiteral("Imported %1 Lichii game(s); kept games in "
                                                "folders that could not be read")
                               : QStringLiteral("Imported %1 Lichii game(s)");
  setStatus(m_lichiiDetected ? imported.arg(result.games.size())
                             : QStringLiteral("Lichii was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant LichiiGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.lichii.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Lichii");
  case GameRoles::Description:
    return LichiiScanner::isSwitch2(game.lichii)
               ? QStringLiteral("Nintendo Switch 2 game launched through Lichii.")
               : QStringLiteral("Nintendo Switch game launched through Lichii.");
  case GameRoles::PlaytimeProvenance:
    return PlaySessionStore::provenance(m_playSessions, game.lichii.path,
                                        game.lichii.playtimeSeconds);
  case GameRoles::PlaytimeSeconds:
    return PlaySessionStore::displayedSeconds(m_playSessions, game.lichii.path,
                                              game.lichii.playtimeSeconds);
  case GameRoles::Hours:
    return static_cast<int>(PlaySessionStore::displayedSeconds(m_playSessions, game.lichii.path,
                                                               game.lichii.playtimeSeconds) /
                            3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.lichii.path,
                                                 game.lichii.lastPlayed) > 0;
  case GameRoles::LastPlayed:
    return PlaySessionStore::displayedLastPlayed(m_playSessions, game.lichii.path,
                                                 game.lichii.lastPlayed);
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.lichii.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.lichii.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.lichii.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.lichii.path;
  case GameRoles::Source:
    return QStringLiteral("Lichii");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::LaunchTarget:
    return game.lichii.path;
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    // Switch 2 games share the Nintendo Switch console card until a console of
    // their own exists in the catalog.
    return QStringLiteral("switch");
  default:
    return {};
  }
}

void LichiiGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

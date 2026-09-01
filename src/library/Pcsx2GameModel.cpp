#include "library/Pcsx2GameModel.h"

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

Pcsx2GameModel::Pcsx2GameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-pcsx2-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<Pcsx2ScanResult>::finished, this,
          [this] { applyScan(m_scanWatcher.result()); });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

Pcsx2GameModel::~Pcsx2GameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int Pcsx2GameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant Pcsx2GameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> Pcsx2GameModel::roleNames() const {
  return {{GameRoles::Title, "title"},
          {GameRoles::Subtitle, "subtitle"},
          {GameRoles::Description, "description"},
          {GameRoles::Hours, "hours"},
          {GameRoles::Progress, "progress"},
          {GameRoles::AchievementsUnlocked, "achievementsUnlocked"},
          {GameRoles::AchievementsTotal, "achievementsTotal"},
          {GameRoles::Favorite, "favorite"},
          {GameRoles::Recent, "recent"},
          {GameRoles::LastPlayed, "lastPlayed"},
          {GameRoles::AccentStart, "accentStart"},
          {GameRoles::AccentEnd, "accentEnd"},
          {GameRoles::CoverMark, "coverMark"},
          {GameRoles::Year, "year"},
          {GameRoles::AppId, "appId"},
          {GameRoles::CoverPath, "coverPath"},
          {GameRoles::HeroPath, "heroPath"},
          {GameRoles::LogoPath, "logoPath"},
          {GameRoles::InstallPath, "installPath"},
          {GameRoles::Source, "source"},
          {GameRoles::Runner, "runner"},
          {GameRoles::Flatpak, "flatpak"},
          {GameRoles::Hidden, "hidden"},
          {GameRoles::LaunchTarget, "launchTarget"}};
}

bool Pcsx2GameModel::pcsx2Detected() const { return m_pcsx2Detected; }
QString Pcsx2GameModel::statusText() const { return m_statusText; }
QString Pcsx2GameModel::errorText() const { return m_errorText; }
QStringList Pcsx2GameModel::detectedPaths() const { return m_detectedPaths; }
qint64 Pcsx2GameModel::lastScan() const { return m_lastScan; }

void Pcsx2GameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE pcsx2_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.pcsx2.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void Pcsx2GameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE pcsx2_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.pcsx2.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void Pcsx2GameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  const QStringList roots = Pcsx2Scanner::discoverRoots();
  setStatus(QStringLiteral("Scanning PCSX2 library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return Pcsx2Scanner::scan(roots); }));
}

void Pcsx2GameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(Pcsx2Scanner::scan(roots));
}

bool Pcsx2GameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    setStatus(QStringLiteral("PCSX2 cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool Pcsx2GameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS pcsx2_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT NOT NULL, serial TEXT, region TEXT, cover_path TEXT, last_played INTEGER NOT "
          "NULL DEFAULT 0, playtime_seconds INTEGER NOT NULL DEFAULT 0, is_elf INTEGER NOT NULL "
          "DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, "
          "hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize PCSX2 cache"), query.lastError().text());
    return false;
  }
  {
    QSqlQuery columns(m_database);
    bool hasIsElf = false;
    if (columns.exec(QStringLiteral("PRAGMA table_info(pcsx2_games)"))) {
      while (columns.next()) {
        hasIsElf = hasIsElf || columns.value(1).toString() == QStringLiteral("is_elf");
      }
    }
    if (!hasIsElf && !query.exec(QStringLiteral(
                         "ALTER TABLE pcsx2_games ADD COLUMN is_elf INTEGER NOT NULL DEFAULT 0"))) {
      setStatus(QStringLiteral("Could not migrate PCSX2 cache"), query.lastError().text());
      return false;
    }
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize PCSX2 cache"), query.lastError().text());
    return false;
  }
  bool hasPaths = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(source_state)"))) {
    while (query.next()) {
      hasPaths = hasPaths || query.value(1).toString() == QStringLiteral("paths");
    }
  }
  if (hasPaths) {
    return true;
  }
  if (!query.exec(QStringLiteral(
          "ALTER TABLE source_state ADD COLUMN paths TEXT NOT NULL DEFAULT ''"))) {
    setStatus(QStringLiteral("Could not migrate PCSX2 cache"), query.lastError().text());
    return false;
  }
  return true;
}

void Pcsx2GameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, serial, region, cover_path, last_played, playtime_seconds, "
          "is_elf, flatpak, favorite, hidden FROM pcsx2_games WHERE observed_at > 0 ORDER BY name "
          "COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached PCSX2 games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    Pcsx2GameRecord record{.gameId = query.value(0).toString(),
                           .title = query.value(1).toString(),
                           .path = query.value(2).toString(),
                           .serial = query.value(3).toString(),
                           .coverPath = query.value(5).toString(),
                           .region = query.value(4).toString(),
                           .playtimeSeconds = query.value(7).toLongLong(),
                           .lastPlayed = query.value(6).toLongLong(),
                           .isElf = query.value(8).toBool(),
                           .flatpak = query.value(9).toBool()};
    loaded.append({.pcsx2 = record,
                   .favorite = query.value(10).toBool(),
                   .hidden = query.value(11).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void Pcsx2GameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT last_scan, last_error, paths FROM source_state WHERE source = 'pcsx2'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_pcsx2Detected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached PCSX2 games");
  }
}

void Pcsx2GameModel::applyScan(const Pcsx2ScanResult& result) {
  m_pcsx2Detected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("PCSX2 scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update PCSX2 games"), m_database.lastError().text());
    return;
  }
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE pcsx2_games SET observed_at = 0"));
  for (const Pcsx2GameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO pcsx2_games(game_id, name, path, serial, region, cover_path, last_played, "
        "playtime_seconds, is_elf, flatpak, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, path = "
        "excluded.path, serial = excluded.serial, region = excluded.region, cover_path = "
        "excluded.cover_path, last_played = excluded.last_played, playtime_seconds = "
        "excluded.playtime_seconds, is_elf = excluded.is_elf, flatpak = excluded.flatpak, "
        "observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.serial);
    query.addBindValue(game.region);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.playtimeSeconds);
    query.addBindValue(game.isElf);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('pcsx2', "
      "strftime('%s', 'now'), ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("") : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update PCSX2 games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = QDateTime::currentSecsSinceEpoch();
  setStatus(m_pcsx2Detected ? QStringLiteral("Imported %1 PCSX2 game(s)").arg(result.games.size())
                            : QStringLiteral("PCSX2 was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant Pcsx2GameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.pcsx2.title;
  case GameRoles::Subtitle:
    return game.pcsx2.region.isEmpty() ? QStringLiteral("PCSX2")
                                       : QStringLiteral("PCSX2 · %1").arg(game.pcsx2.region);
  case GameRoles::Description:
    return QStringLiteral("PlayStation 2 game launched through PCSX2.");
  case GameRoles::Hours:
    return static_cast<int>(game.pcsx2.playtimeSeconds / 3600);
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.pcsx2.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.pcsx2.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.pcsx2.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    // Launch key: PCSX2 boots by disc path; serial stays in Runner for display.
    return QStringLiteral("path:%1").arg(game.pcsx2.path);
  case GameRoles::CoverPath:
    return localUrl(game.pcsx2.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.pcsx2.path;
  case GameRoles::Source:
    return QStringLiteral("PCSX2");
  case GameRoles::Runner:
    return game.pcsx2.serial;
  case GameRoles::LaunchTarget:
    return game.pcsx2.isElf ? QStringLiteral("elf") : QStringLiteral("disc");
  case GameRoles::Flatpak:
    return game.pcsx2.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void Pcsx2GameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}
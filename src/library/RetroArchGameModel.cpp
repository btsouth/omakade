#include "library/RetroArchGameModel.h"

#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPair>
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

RetroArchGameModel::RetroArchGameModel(const QString& databasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(
          QStringLiteral("omakade-retroarch-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<RetroArchScanResult>::finished, this,
          [this] { applyScan(m_scanWatcher.result()); });
  if (openDatabase(databasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

RetroArchGameModel::~RetroArchGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int RetroArchGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant RetroArchGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> RetroArchGameModel::roleNames() const {
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

bool RetroArchGameModel::retroArchDetected() const { return m_retroArchDetected; }
QString RetroArchGameModel::statusText() const { return m_statusText; }
QString RetroArchGameModel::errorText() const { return m_errorText; }
QStringList RetroArchGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 RetroArchGameModel::lastScan() const { return m_lastScan; }

void RetroArchGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE retroarch_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.retroArch.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void RetroArchGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE retroarch_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.retroArch.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void RetroArchGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  const QStringList roots = RetroArchScanner::discoverRoots();
  setStatus(QStringLiteral("Scanning RetroArch library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return RetroArchScanner::scan(roots); }));
}
void RetroArchGameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(RetroArchScanner::scan(roots));
}

bool RetroArchGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!m_database.open()) {
    setStatus(QStringLiteral("RetroArch cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool RetroArchGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS retroarch_games (game_id TEXT PRIMARY KEY, name TEXT NOT "
          "NULL, content_path TEXT NOT NULL, core_path TEXT, core_name TEXT, cover_path TEXT, "
          "hero_path TEXT, system TEXT NOT NULL DEFAULT '', playtime_seconds INTEGER NOT NULL "
          "DEFAULT 0, last_played INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, "
          "favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at "
          "INTEGER NOT NULL)"))) {
    return false;
  }
  // Added after the initial release; existing installs need the column added explicitly since
  // CREATE TABLE IF NOT EXISTS above is a no-op once the table already exists.
  if (!query.exec(QStringLiteral("SELECT system FROM retroarch_games LIMIT 1"))) {
    query.exec(QStringLiteral("ALTER TABLE retroarch_games ADD COLUMN system TEXT NOT NULL "
                              "DEFAULT ''"));
  }
  return query.exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
      "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"));
}

void RetroArchGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, content_path, core_path, core_name, cover_path, hero_path, "
          "system, playtime_seconds, last_played, flatpak, favorite, hidden FROM retroarch_games "
          "WHERE observed_at > 0 ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached RetroArch games"), query.lastError().text());
    return;
  }
  QHash<QString, QPair<int, int>> achievementSummaries;
  QSqlQuery achievementsQuery(m_database);
  if (achievementsQuery.exec(QStringLiteral(
          "SELECT app_id, unlocked, total FROM achievement_summary WHERE source = "
          "'retroachievements'"))) {
    while (achievementsQuery.next()) {
      achievementSummaries.insert(achievementsQuery.value(0).toString(),
                                  {achievementsQuery.value(1).toInt(),
                                   achievementsQuery.value(2).toInt()});
    }
  }
  while (query.next()) {
    RetroArchGameRecord record{.gameId = query.value(0).toString(),
                               .title = query.value(1).toString(),
                               .contentPath = query.value(2).toString(),
                               .corePath = query.value(3).toString(),
                               .coreName = query.value(4).toString(),
                               .coverPath = query.value(5).toString(),
                               .heroPath = query.value(6).toString(),
                               .system = query.value(7).toString(),
                               .playtimeSeconds = query.value(8).toLongLong(),
                               .lastPlayed = query.value(9).toLongLong(),
                               .flatpak = query.value(10).toBool()};
    const QPair<int, int> achievements = achievementSummaries.value(record.gameId);
    loaded.append({.retroArch = record,
                   .favorite = query.value(11).toBool(),
                   .hidden = query.value(12).toBool(),
                   .achievementsUnlocked = achievements.first,
                   .achievementsTotal = achievements.second,
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void RetroArchGameModel::reloadAchievementSummary(const QString& gameId) {
  if (!m_database.isOpen()) {
    return;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT unlocked, total FROM achievement_summary WHERE app_id = ? AND source = "
      "'retroachievements'"));
  query.addBindValue(gameId);
  if (!query.exec() || !query.next()) {
    return;
  }
  for (int row = 0; row < m_games.size(); ++row) {
    Game& game = m_games[row];
    if (game.retroArch.gameId != gameId) {
      continue;
    }
    game.achievementsUnlocked = query.value(0).toInt();
    game.achievementsTotal = query.value(1).toInt();
    emit dataChanged(
        index(row), index(row),
        {GameRoles::Progress, GameRoles::AchievementsUnlocked, GameRoles::AchievementsTotal});
    return;
  }
}

void RetroArchGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'retroarch'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_retroArchDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached RetroArch library");
  }
}

void RetroArchGameModel::applyScan(const RetroArchScanResult& result) {
  m_retroArchDetected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("RetroArch scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update RetroArch games"), m_database.lastError().text());
    return;
  }
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE retroarch_games SET observed_at = 0"));
  for (const RetroArchGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO retroarch_games(game_id, name, content_path, core_path, core_name, "
        "cover_path, "
        "hero_path, system, playtime_seconds, last_played, flatpak, observed_at) VALUES(?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = "
        "excluded.name, content_path = excluded.content_path, core_path = excluded.core_path, "
        "core_name = excluded.core_name, cover_path = excluded.cover_path, hero_path = "
        "excluded.hero_path, system = excluded.system, playtime_seconds = "
        "excluded.playtime_seconds, last_played = excluded.last_played, flatpak = "
        "excluded.flatpak, observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.contentPath);
    query.addBindValue(game.corePath);
    query.addBindValue(game.coreName);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroPath);
    query.addBindValue(game.system);
    query.addBindValue(game.playtimeSeconds);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('retroarch', "
      "strftime('%s', 'now'), ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update RetroArch games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = QDateTime::currentSecsSinceEpoch();
  setStatus(m_retroArchDetected
                ? QStringLiteral("Imported %1 RetroArch game(s)").arg(result.games.size())
                : QStringLiteral("RetroArch was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant RetroArchGameModel::valueForRole(const Game& game, int role) const {
  const RetroArchGameRecord& record = game.retroArch;
  switch (role) {
  case GameRoles::Title:
    return record.title;
  case GameRoles::Subtitle:
    return record.coreName.isEmpty() ? QStringLiteral("RetroArch")
                                     : QStringLiteral("RetroArch · %1").arg(record.coreName);
  case GameRoles::Description:
    return record.corePath.isEmpty()
               ? QStringLiteral("Set a core association in RetroArch before launching.")
               : QStringLiteral("Configured and managed by RetroArch.");
  case GameRoles::Hours:
    return record.playtimeSeconds / 3600;
  case GameRoles::Progress:
    return game.achievementsTotal > 0
               ? (game.achievementsUnlocked * 100) / game.achievementsTotal
               : 0;
  case GameRoles::AchievementsUnlocked:
    return game.achievementsUnlocked;
  case GameRoles::AchievementsTotal:
    return game.achievementsTotal;
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return record.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return record.lastPlayed;
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
    return QString{};
  case GameRoles::InstallPath:
    return record.contentPath;
  case GameRoles::Source:
    return QStringLiteral("RetroArch");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::Flatpak:
    return record.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::LaunchTarget:
    return record.corePath;
  default:
    return {};
  }
}

void RetroArchGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

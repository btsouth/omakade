#include "library/SteamGameModel.h"

#include "app/AppSettings.h"
#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

namespace {
constexpr qint64 kMaximumCoverBytes = 8 * 1024 * 1024;
constexpr int kMaximumConcurrentCoverDownloads = 4;

QColor colorFor(const QString& appId, int offset) {
  const QByteArray hash = QCryptographicHash::hash(appId.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

bool isLandscapeHeader(const QString& path) {
  return QFileInfo(path).completeBaseName().compare(QStringLiteral("header"),
                                                    Qt::CaseInsensitive) == 0;
}

QString coverCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers");
}

qint64 otherCoverCacheBytes() {
  qint64 total = 0;
  for (const QString& subdirectory :
       {QStringLiteral("/battlenet"), QStringLiteral("/libretro"), QStringLiteral("/switch"),
        QStringLiteral("/wiiu")}) {
    QDirIterator iterator(coverCacheRoot() + subdirectory, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      total += QFileInfo(iterator.next()).size();
    }
  }
  return total;
}

QString coverCachePath(const QString& appId) {
  return coverCacheRoot() + QLatin1Char('/') + appId + QStringLiteral(".jpg");
}

QUrl coverUrl(const QString& appId, int attempt) {
  const QString filename = attempt == 0 ? QStringLiteral("library_600x900_2x.jpg")
                                        : QStringLiteral("library_600x900.jpg");
  return QUrl(QStringLiteral("https://shared.steamstatic.com/store_item_assets/steam/apps/%1/%2")
                  .arg(appId, filename));
}
} // namespace

SteamGameModel::SteamGameModel(const QString& databasePath, AppSettings* settings, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-%1").arg(reinterpret_cast<quintptr>(this))),
      m_settings(settings) {
  m_coverWriteTimer.setSingleShot(true);
  m_coverWriteTimer.setInterval(750);
  connect(&m_coverWriteTimer, &QTimer::timeout, this, &SteamGameModel::flushCoverWrites);
  m_rescanTimer.setSingleShot(true);
  m_rescanTimer.setInterval(700);
  connect(&m_rescanTimer, &QTimer::timeout, this,
          [this] { refreshFromRoots(SteamScanner::discoverSteamRoots()); });
  connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, &m_rescanTimer,
          qOverload<>(&QTimer::start));
  connect(&m_fileWatcher, &QFileSystemWatcher::directoryChanged, &m_rescanTimer,
          qOverload<>(&QTimer::start));
  connect(&m_scanWatcher, &QFutureWatcher<SteamScanResult>::finished, this, [this] {
    applyScan(m_scanWatcher.result());
    m_scanning = false;
    emit scanningChanged();
    if (m_rescanPending) {
      m_rescanPending = false;
      QTimer::singleShot(0, this,
                         [this] { refreshFromRoots(SteamScanner::discoverSteamRoots()); });
    }
  });

  const QString path = databasePath.isEmpty() ? defaultDatabasePath() : databasePath;
  if (openDatabase(path) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
    QTimer::singleShot(800, this, [this] { requestMissingCovers(); });
  }
  if (m_settings != nullptr) {
    connect(m_settings, &AppSettings::steamIdChanged, this, &SteamGameModel::reloadOwnedGames);
  }
}

SteamGameModel::~SteamGameModel() {
  flushCoverWrites();
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int SteamGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant SteamGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> SteamGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::Installed, "installed");
  return roles;
}

bool SteamGameModel::scanning() const { return m_scanning; }

bool SteamGameModel::steamDetected() const { return m_steamDetected; }

QString SteamGameModel::statusText() const { return m_statusText; }

QString SteamGameModel::errorText() const { return m_errorText; }

int SteamGameModel::artworkCount() const {
  return std::count_if(m_games.cbegin(), m_games.cend(),
                       [](const Game& game) { return !game.steam.coverPath.isEmpty(); });
}

QStringList SteamGameModel::detectedPaths() const { return m_detectedPaths; }

qint64 SteamGameModel::lastScan() const { return m_lastScan; }

QString SteamGameModel::databasePath() const { return m_databasePath; }

QVariantMap SteamGameModel::get(int row) const {
  if (row < 0 || row >= m_games.size()) {
    return {};
  }
  QVariantMap result;
  const auto roles = roleNames();
  for (auto iterator = roles.cbegin(); iterator != roles.cend(); ++iterator) {
    result.insert(QString::fromUtf8(iterator.value()),
                  valueForRole(m_games.at(row), iterator.key()));
  }
  return result;
}

void SteamGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE games SET favorite = ? WHERE app_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.steam.appId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void SteamGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE games SET hidden = ? WHERE app_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.steam.appId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void SteamGameModel::refresh() {
  m_explicitRefresh = true;
  refreshFromRoots(SteamScanner::discoverSteamRoots());
}

void SteamGameModel::reloadAchievementSummary(const QString& appId) {
  if (!m_database.isOpen()) {
    return;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("SELECT unlocked, total FROM achievement_summary WHERE app_id = ?"));
  query.addBindValue(appId);
  if (!query.exec() || !query.next()) {
    return;
  }
  for (int row = 0; row < m_games.size(); ++row) {
    Game& game = m_games[row];
    if (game.steam.appId != appId) {
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

void SteamGameModel::reloadOwnedGames() {
  loadDatabase();
  QSet<QString> currentAppIds;
  for (const Game& game : m_games) {
    currentAppIds.insert(game.steam.appId);
  }
  QQueue<CoverRequest> retained;
  while (!m_coverQueue.isEmpty()) {
    const CoverRequest request = m_coverQueue.dequeue();
    if (currentAppIds.contains(request.appId)) {
      retained.enqueue(request);
    } else {
      m_pendingCovers.remove(request.appId);
    }
  }
  m_coverQueue = retained;
}

void SteamGameModel::requestCover(const QString& appId) {
  for (const Game& game : m_games) {
    if (game.steam.appId == appId) {
      requestCoverForGame(game);
      startNextCoverDownloads();
      return;
    }
  }
}

void SteamGameModel::refreshFromRoots(const QStringList& roots) {
  if (m_scanning) {
    m_rescanPending = true;
    return;
  }
  m_scanning = true;
  setStatus(QStringLiteral("Scanning Steam libraries"));
  emit scanningChanged();
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return SteamScanner::scan(roots); }));
}

QString SteamGameModel::defaultDatabasePath() {
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                            QStringLiteral("/omakade");
  QDir().mkpath(directory);
  return directory + QStringLiteral("/library.sqlite3");
}

QVariant SteamGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.steam.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Steam");
  case GameRoles::Description:
    return game.installed ? QStringLiteral("Installed locally through Steam.")
                          : QStringLiteral("Owned on Steam and ready to install.");
  case GameRoles::Hours:
    return game.steam.playtimeMinutes / 60;
  case GameRoles::Progress:
    return game.achievementsTotal == 0 ? 0
                                       : (game.achievementsUnlocked * 100) / game.achievementsTotal;
  case GameRoles::AchievementsUnlocked:
    return game.achievementsUnlocked;
  case GameRoles::AchievementsTotal:
    return game.achievementsTotal;
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.steam.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.steam.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.steam.title.left(1).toUpper();
  case GameRoles::AppId:
    return game.steam.appId;
  case GameRoles::CoverPath:
    return localUrl(game.steam.coverPath);
  case GameRoles::HeroPath:
    return localUrl(game.steam.heroPath);
  case GameRoles::LogoPath:
    return localUrl(game.steam.logoPath);
  case GameRoles::InstallPath:
    if (!game.installed) {
      return QString{};
    }
    return QDir(game.steam.libraryPath + QStringLiteral("/steamapps/common"))
        .absoluteFilePath(game.steam.installDirectory);
  case GameRoles::Source:
    return QStringLiteral("Steam");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::Installed:
    return game.installed;
  default:
    return {};
  }
}

bool SteamGameModel::openDatabase(const QString& path) {
  m_databasePath = path;
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Library database unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool SteamGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  int schemaVersion = 0;
  if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
    schemaVersion = query.value(0).toInt();
  }
  const QStringList statements = {
      QStringLiteral("PRAGMA foreign_keys = ON"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS games ("
                     "app_id TEXT PRIMARY KEY, title TEXT NOT NULL, favorite INTEGER NOT NULL "
                     "DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS installations ("
                     "app_id TEXT PRIMARY KEY REFERENCES games(app_id), install_dir TEXT NOT NULL, "
                     "library_path TEXT NOT NULL, manifest_path TEXT NOT NULL, cover_path TEXT, "
                     "hero_path TEXT, logo_path TEXT, last_played INTEGER NOT NULL DEFAULT 0, "
                     "playtime_minutes INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS source_state ("
                     "source TEXT PRIMARY KEY, last_scan INTEGER, last_error TEXT, paths TEXT NOT "
                     "NULL DEFAULT '')"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS achievement_summary ("
                     "app_id TEXT PRIMARY KEY REFERENCES games(app_id), unlocked INTEGER NOT NULL, "
                     "total INTEGER NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL)"),
      QStringLiteral(
          "CREATE TABLE IF NOT EXISTS achievements ("
          "app_id TEXT NOT NULL REFERENCES games(app_id), api_name TEXT NOT NULL, "
          "title TEXT NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, "
          "unlocked INTEGER NOT NULL, unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, "
          "hidden INTEGER NOT NULL, current_progress REAL NOT NULL, maximum_progress REAL "
          "NOT NULL, source TEXT NOT NULL, PRIMARY KEY(app_id, api_name))"),
      QStringLiteral(
          "CREATE TABLE IF NOT EXISTS game_insights ("
          "source TEXT NOT NULL, app_id TEXT NOT NULL, provider TEXT NOT NULL, provider_game_id "
          "INTEGER NOT NULL, title TEXT NOT NULL, critic_score INTEGER NOT NULL DEFAULT -1, "
          "critic_review_count INTEGER NOT NULL DEFAULT 0, rushed_seconds INTEGER NOT NULL "
          "DEFAULT 0, normal_seconds INTEGER NOT NULL DEFAULT 0, complete_seconds INTEGER NOT "
          "NULL DEFAULT 0, time_sample_count INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT "
          "NULL, PRIMARY KEY(source, app_id, provider))"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS owned_games ("
                     "steam_id TEXT NOT NULL, app_id TEXT NOT NULL REFERENCES games(app_id), "
                     "playtime_minutes INTEGER NOT NULL DEFAULT 0, synced_at INTEGER NOT NULL, "
                     "PRIMARY KEY(steam_id, app_id))"),
  };
  for (const QString& statement : statements) {
    if (!query.exec(statement)) {
      setStatus(QStringLiteral("Could not prepare the library database"), query.lastError().text());
      return false;
    }
  }
  bool hasPaths = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(source_state)"))) {
    while (query.next()) {
      hasPaths = hasPaths || query.value(1).toString() == QStringLiteral("paths");
    }
  }
  if (!hasPaths &&
      !query.exec(QStringLiteral("ALTER TABLE source_state ADD COLUMN paths TEXT NOT NULL DEFAULT "
                                 "''"))) {
    setStatus(QStringLiteral("Could not update source diagnostics"), query.lastError().text());
    return false;
  }
  bool ownedGamesHaveSteamId = false;
  if (query.exec(QStringLiteral("PRAGMA table_info(owned_games)"))) {
    while (query.next()) {
      ownedGamesHaveSteamId =
          ownedGamesHaveSteamId || query.value(1).toString() == QStringLiteral("steam_id");
    }
  }
  if (!ownedGamesHaveSteamId) {
    if (!query.exec(QStringLiteral("DROP TABLE owned_games")) ||
        !query.exec(QStringLiteral("CREATE TABLE owned_games ("
                                   "steam_id TEXT NOT NULL, app_id TEXT NOT NULL REFERENCES "
                                   "games(app_id), playtime_minutes INTEGER NOT NULL DEFAULT 0, "
                                   "synced_at INTEGER NOT NULL, PRIMARY KEY(steam_id, app_id))"))) {
      setStatus(QStringLiteral("Could not update the owned-games cache"), query.lastError().text());
      return false;
    }
  }
  if (schemaVersion < 9 && !query.exec(QStringLiteral("PRAGMA user_version = 9"))) {
    setStatus(QStringLiteral("Could not update the library database version"),
              query.lastError().text());
    return false;
  }
  return true;
}

void SteamGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'steam'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_steamDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Steam library");
  }
}

void SteamGameModel::loadDatabase() {
  QVector<Game> loaded;
  QVector<QPair<QString, QString>> cachedCoverUpdates;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT g.app_id, g.title, g.favorite, g.hidden, i.install_dir, i.library_path, "
      "i.manifest_path, i.cover_path, i.hero_path, i.logo_path, i.last_played, "
      "MAX(COALESCE(i.playtime_minutes, 0), COALESCE(o.playtime_minutes, 0)), "
      "COALESCE(a.unlocked, 0), COALESCE(a.total, 0), i.app_id IS NOT NULL FROM games g "
      "LEFT JOIN installations i ON i.app_id = g.app_id LEFT JOIN owned_games o ON "
      "o.app_id = g.app_id AND o.steam_id = ? LEFT JOIN achievement_summary a ON "
      "a.app_id = g.app_id "
      "WHERE i.app_id IS NOT NULL OR o.app_id IS NOT NULL "
      "ORDER BY g.title COLLATE NOCASE"));
  query.addBindValue(m_settings == nullptr ? QString{} : m_settings->steamId());
  if (!query.exec()) {
    setStatus(QStringLiteral("Could not load the game library"), query.lastError().text());
    return;
  }
  while (query.next()) {
    SteamGameRecord steam{
        .appId = query.value(0).toString(),
        .title = query.value(1).toString(),
        .installDirectory = query.value(4).toString(),
        .libraryPath = query.value(5).toString(),
        .manifestPath = query.value(6).toString(),
        .coverPath = query.value(7).toString(),
        .heroPath = query.value(8).toString(),
        .logoPath = query.value(9).toString(),
        .lastPlayed = query.value(10).toLongLong(),
        .playtimeMinutes = query.value(11).toInt(),
        .achievementsUnlocked = query.value(12).toInt(),
        .achievementsTotal = query.value(13).toInt(),
        .achievements = {},
    };
    if (isLandscapeHeader(steam.coverPath)) {
      steam.coverPath.clear();
    }
    if (steam.coverPath.startsWith(coverCacheRoot()) && !QFileInfo::exists(steam.coverPath)) {
      // A pruned cached cover must be requested again instead of showing a broken image.
      steam.coverPath.clear();
    }
    const QString cachedCover = coverCachePath(steam.appId);
    if (steam.coverPath.isEmpty() && QFileInfo::exists(cachedCover)) {
      steam.coverPath = cachedCover;
      cachedCoverUpdates.append({steam.appId, cachedCover});
    }
    loaded.append({.steam = steam,
                   .favorite = query.value(2).toBool(),
                   .hidden = query.value(3).toBool(),
                   .achievementsUnlocked = query.value(12).toInt(),
                   .achievementsTotal = query.value(13).toInt(),
                   .installed = query.value(14).toBool(),
                   .accentStart = colorFor(steam.appId, 0),
                   .accentEnd = colorFor(steam.appId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
  query.finish();
  if (cachedCoverUpdates.isEmpty()) {
    return;
  }
  // One transaction instead of one autocommit per game keeps this off the critical path.
  const bool transaction = m_database.transaction();
  for (const auto& [appId, coverPath] : cachedCoverUpdates) {
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE installations SET cover_path = ? WHERE app_id = ?"));
    update.addBindValue(coverPath);
    update.addBindValue(appId);
    update.exec();
  }
  if (transaction) {
    m_database.commit();
  }
}

void SteamGameModel::applyScan(const SteamScanResult& result) {
  m_steamDetected = !result.steamRoots.isEmpty();
  if (result.incomplete || (result.steamRoots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.isOpen()) {
    setStatus(QStringLiteral("Scan finished but the library database is unavailable"), m_errorText);
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  if (result == m_appliedScan) {
    // Steam rewrites manifests and libraryfolders.vdf constantly while it downloads. When the
    // scan resolves to the same library, leave the database, model, and cover state alone so
    // the grid does not reset and failed covers are not re-requested every few seconds.
    m_lastScan = scanTimestamp;
    QSqlQuery touch(m_database);
    touch.prepare(QStringLiteral("UPDATE source_state SET last_scan = ? WHERE source = 'steam'"));
    touch.addBindValue(scanTimestamp);
    touch.exec();
    if (m_explicitRefresh) {
      m_explicitRefresh = false;
      m_failedCovers.clear();
      requestMissingCovers();
    }
    // Steam saves by rename, which drops the watch on the replaced file, so re-arm them.
    rebuildWatchPaths(result);
    reportScan(result);
    return;
  }

  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update the library"), m_database.lastError().text());
    return;
  }
  QSqlQuery query(m_database);
  bool okay = true;
  if (result.unreadableManifests.isEmpty()) {
    okay = query.exec(QStringLiteral("DELETE FROM installations"));
  } else {
    // Keep the cached rows for manifests Steam is still writing or that are corrupt so a
    // transient read failure never drops an installed game.
    QStringList placeholders;
    for (int index = 0; index < result.unreadableManifests.size(); ++index) {
      placeholders.append(QStringLiteral("?"));
    }
    query.prepare(QStringLiteral("DELETE FROM installations WHERE manifest_path NOT IN (%1)")
                      .arg(placeholders.join(QStringLiteral(", "))));
    for (const QString& manifest : result.unreadableManifests) {
      query.addBindValue(manifest);
    }
    okay = query.exec();
  }
  for (const SteamGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO games(app_id, title) VALUES(?, ?) ON CONFLICT(app_id) DO UPDATE SET "
        "title = excluded.title"));
    query.addBindValue(game.appId);
    query.addBindValue(game.title);
    okay = okay && query.exec();

    query.prepare(QStringLiteral(
        "INSERT INTO installations(app_id, install_dir, library_path, manifest_path, cover_path, "
        "hero_path, logo_path, last_played, playtime_minutes, observed_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now')) ON CONFLICT(app_id) DO UPDATE "
        "SET "
        "install_dir = excluded.install_dir, library_path = excluded.library_path, manifest_path = "
        "excluded.manifest_path, cover_path = excluded.cover_path, hero_path = excluded.hero_path, "
        "logo_path = excluded.logo_path, last_played = excluded.last_played, playtime_minutes = "
        "excluded.playtime_minutes, observed_at = excluded.observed_at"));
    query.addBindValue(game.appId);
    query.addBindValue(game.installDirectory);
    query.addBindValue(game.libraryPath);
    query.addBindValue(game.manifestPath);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroPath);
    query.addBindValue(game.logoPath);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.playtimeMinutes);
    okay = okay && query.exec();

    if (game.achievementsTotal > 0) {
      query.prepare(QStringLiteral(
          "INSERT INTO achievement_summary(app_id, unlocked, total, source, updated_at) "
          "VALUES(?, ?, ?, 'steam-local', strftime('%s', 'now')) ON CONFLICT(app_id) DO UPDATE SET "
          "unlocked = excluded.unlocked, total = excluded.total, source = excluded.source, "
          "updated_at = excluded.updated_at WHERE achievement_summary.source != 'steam-web'"));
      query.addBindValue(game.appId);
      query.addBindValue(game.achievementsUnlocked);
      query.addBindValue(game.achievementsTotal);
      okay = okay && query.exec();
    }

    for (const SteamAchievementRecord& achievement : game.achievements) {
      query.prepare(QStringLiteral(
          "INSERT INTO achievements(app_id, api_name, title, description, icon_url, icon_path, "
          "unlocked, unlock_time, rarity, hidden, current_progress, maximum_progress, source) "
          "VALUES(?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?, ?, 'steam-local') ON CONFLICT(app_id, "
          "api_name) DO UPDATE SET title = excluded.title, description = excluded.description, "
          "icon_url = excluded.icon_url, unlocked = excluded.unlocked, unlock_time = "
          "excluded.unlock_time, rarity = excluded.rarity, hidden = excluded.hidden, "
          "current_progress = excluded.current_progress, maximum_progress = "
          "excluded.maximum_progress, source = excluded.source WHERE achievements.source != "
          "'steam-web'"));
      query.addBindValue(game.appId);
      query.addBindValue(achievement.apiName);
      query.addBindValue(achievement.title);
      query.addBindValue(achievement.description);
      query.addBindValue(achievement.iconUrl);
      query.addBindValue(achievement.unlocked);
      query.addBindValue(achievement.unlockTime);
      query.addBindValue(achievement.rarity);
      query.addBindValue(achievement.hidden);
      query.addBindValue(achievement.currentProgress);
      query.addBindValue(achievement.maximumProgress);
      okay = okay && query.exec();
    }
  }
  QStringList detectedPaths = result.steamRoots;
  for (const QString& path : result.libraryPaths) {
    if (!detectedPaths.contains(path)) {
      detectedPaths.append(path);
    }
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('steam', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(detectedPaths.isEmpty() ? QStringLiteral("")
                                             : detectedPaths.join(QLatin1Char('\n')));
  okay = okay && query.exec();

  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update the library"), query.lastError().text());
    return;
  }

  loadDatabase();
  m_appliedScan = result;
  m_explicitRefresh = false;
  m_detectedPaths = detectedPaths;
  m_lastScan = scanTimestamp;
  m_failedCovers.clear();
  requestMissingCovers();
  rebuildWatchPaths(result);
  reportScan(result);
}

void SteamGameModel::reportScan(const SteamScanResult& result) {
  if (!result.warnings.isEmpty()) {
    setStatus(
        QStringLiteral("Imported %1 installed game(s) with warnings").arg(result.games.size()),
        result.warnings.join(QLatin1Char('\n')));
  } else if (!m_steamDetected) {
    setStatus(QStringLiteral("Steam was not found"));
  } else {
    setStatus(QStringLiteral("Imported %1 installed game(s)").arg(result.games.size()));
  }
}

void SteamGameModel::rebuildWatchPaths(const SteamScanResult& result) {
  const QStringList oldFiles = m_fileWatcher.files();
  const QStringList oldDirectories = m_fileWatcher.directories();
  if (!oldFiles.isEmpty()) {
    m_fileWatcher.removePaths(oldFiles);
  }
  if (!oldDirectories.isEmpty()) {
    m_fileWatcher.removePaths(oldDirectories);
  }

  QStringList files;
  QStringList directories;
  for (const SteamGameRecord& game : result.games) {
    files.append(game.manifestPath);
    directories.append(QFileInfo(game.manifestPath).absolutePath());
  }
  for (const QString& root : result.steamRoots) {
    for (const QString& relative : {QStringLiteral("/config/libraryfolders.vdf"),
                                    QStringLiteral("/steamapps/libraryfolders.vdf")}) {
      const QString folders = root + relative;
      if (QFileInfo::exists(folders)) {
        files.append(folders);
      }
    }
    QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString& user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString userConfig = userdata.absoluteFilePath(user + QStringLiteral("/config"));
      const QString localConfig = userConfig + QStringLiteral("/localconfig.vdf");
      const QString shortcuts = userConfig + QStringLiteral("/shortcuts.vdf");
      const QString grid = userConfig + QStringLiteral("/grid");
      if (QFileInfo::exists(localConfig)) {
        files.append(localConfig);
      }
      if (QFileInfo::exists(shortcuts)) {
        files.append(shortcuts);
      }
      if (QFileInfo(userConfig).isDir()) {
        directories.append(userConfig);
      }
      if (QFileInfo(grid).isDir()) {
        directories.append(grid);
      }
    }
  }
  for (const QString& library : result.libraryPaths) {
    const QString steamapps = library + QStringLiteral("/steamapps");
    if (QFileInfo(steamapps).isDir()) {
      directories.append(steamapps);
    }
  }
  files.removeDuplicates();
  directories.removeDuplicates();
  m_fileWatcher.addPaths(files);
  m_fileWatcher.addPaths(directories);
}

void SteamGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

void SteamGameModel::requestMissingCovers() {
  for (const Game& game : m_games) {
    if (game.installed) {
      requestCoverForGame(game);
    }
  }
  startNextCoverDownloads();
}

void SteamGameModel::requestCoverForGame(const Game& game) {
  if (!game.steam.coverPath.isEmpty() || m_pendingCovers.contains(game.steam.appId) ||
      m_failedCovers.contains(game.steam.appId)) {
    return;
  }
  bool numeric = false;
  const quint64 appId = game.steam.appId.toULongLong(&numeric);
  if (!numeric) {
    return;
  }
  // Non-Steam shortcuts have no Steam store capsule. Grid art or the shortcut
  // icon is already resolved during the local scan.
  if (appId <= 0xFFFFFFFFull && (appId & 0x80000000ull) != 0) {
    return;
  }
  const QString cached = coverCachePath(game.steam.appId);
  if (QFileInfo::exists(cached)) {
    applyCover(game.steam.appId, cached);
    return;
  }
  m_pendingCovers.insert(game.steam.appId);
  m_coverQueue.enqueue({game.steam.appId, 0});
}

void SteamGameModel::startNextCoverDownloads() {
  while (m_activeCoverDownloads < kMaximumConcurrentCoverDownloads && !m_coverQueue.isEmpty()) {
    const CoverRequest request = m_coverQueue.dequeue();
    downloadCover(request.appId, request.attempt);
  }
}

void SteamGameModel::downloadCover(const QString& appId, int attempt) {
  QNetworkRequest request(coverUrl(appId, attempt));
  request.setTransferTimeout(12000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  ++m_activeCoverDownloads;
  m_coverBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_coverBuffers[reply];
    const qsizetype remaining = kMaximumCoverBytes - buffer.size();
    buffer.append(reply->read(remaining + 1));
    if (buffer.size() > kMaximumCoverBytes) {
      reply->setProperty("tooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, appId, attempt] {
    QByteArray contents = m_coverBuffers.take(reply);
    if (contents.size() <= kMaximumCoverBytes) {
      contents.append(reply->read(kMaximumCoverBytes + 1 - contents.size()));
    }
    const bool tooLarge =
        reply->property("tooLarge").toBool() || contents.size() > kMaximumCoverBytes;
    const bool valid = reply->error() == QNetworkReply::NoError && !contents.isEmpty() &&
                       !tooLarge && !QImage::fromData(contents).isNull();
    bool saved = false;
    if (valid) {
      const QString path = coverCachePath(appId);
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile file(path);
      saved = file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() &&
              file.commit();
      if (saved) {
        applyCover(appId, path);
      }
    }
    reply->deleteLater();
    --m_activeCoverDownloads;
    if (!saved && attempt == 0) {
      m_coverQueue.enqueue({appId, 1});
    } else {
      m_pendingCovers.remove(appId);
      if (!saved) {
        m_failedCovers.insert(appId);
      }
    }
    startNextCoverDownloads();
    if (m_activeCoverDownloads == 0 && m_coverQueue.isEmpty()) {
      pruneCoverCache();
    }
  });
}

void SteamGameModel::applyCover(const QString& appId, const QString& path) {
  for (int row = 0; row < m_games.size(); ++row) {
    if (m_games.at(row).steam.appId != appId) {
      continue;
    }
    m_games[row].steam.coverPath = path;
    m_pendingCoverWrites.insert(appId, path);
    if (!m_coverWriteTimer.isActive()) {
      m_coverWriteTimer.start();
    }
    emit dataChanged(index(row), index(row), {GameRoles::CoverPath});
    emit statusChanged();
    return;
  }
}

void SteamGameModel::flushCoverWrites() {
  if (m_pendingCoverWrites.isEmpty() || !m_database.isOpen()) {
    m_pendingCoverWrites.clear();
    return;
  }
  const QHash<QString, QString> pending = m_pendingCoverWrites;
  m_pendingCoverWrites.clear();
  if (!m_database.transaction()) {
    return;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE installations SET cover_path = ? WHERE app_id = ?"));
  for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
    query.addBindValue(it.value());
    query.addBindValue(it.key());
    query.exec();
  }
  m_database.commit();
}

void SteamGameModel::pruneCoverCache() {
  const int limitMb = m_settings == nullptr ? 1024 : m_settings->artworkCacheLimitMb();
  const qint64 configuredLimit = static_cast<qint64>(limitMb) * 1024 * 1024;
  const qint64 limit = qMax<qint64>(0, configuredLimit - otherCoverCacheBytes());
  struct CachedFile {
    QString path;
    QDateTime modified;
    qint64 size = 0;
  };
  QVector<CachedFile> files;
  qint64 total = 0;
  QDirIterator iterator(coverCacheRoot(), QDir::Files);
  while (iterator.hasNext()) {
    const QFileInfo info(iterator.next());
    files.append({info.absoluteFilePath(), info.lastModified(), info.size()});
    total += info.size();
  }
  if (total <= limit) {
    return;
  }
  // Covers the library still shows go last, so leftovers from removed games are trimmed first.
  QSet<QString> referenced;
  for (const Game& game : m_games) {
    referenced.insert(game.steam.coverPath);
  }
  std::sort(files.begin(), files.end(),
            [&referenced](const CachedFile& left, const CachedFile& right) {
              const bool leftReferenced = referenced.contains(left.path);
              const bool rightReferenced = referenced.contains(right.path);
              if (leftReferenced != rightReferenced) {
                return !leftReferenced;
              }
              return left.modified < right.modified;
            });
  for (const CachedFile& file : files) {
    if (total <= limit) {
      break;
    }
    if (QFile::remove(file.path)) {
      total -= file.size;
      for (int row = 0; row < m_games.size(); ++row) {
        if (m_games[row].steam.coverPath == file.path) {
          m_games[row].steam.coverPath.clear();
          emit dataChanged(index(row), index(row), {GameRoles::CoverPath});
        }
      }
    }
  }
}

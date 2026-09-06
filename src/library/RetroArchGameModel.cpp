#include "library/RetroArchGameModel.h"

#include "app/AppSettings.h"
#include "library/ConsoleCatalog.h"
#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"
#include "sources/retro/RomFolderScanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

namespace {
QColor colorFor(const QString& key, int offset) {
  const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}

constexpr qint64 kMaximumCoverBytes = 8 * 1024 * 1024;
constexpr int kMaximumConcurrentCoverDownloads = 2;
constexpr int kMaximumQueuedCoverDownloads = 24;

QString sanitizedThumbnailName(QString name) {
  static const QRegularExpression invalid(QStringLiteral("[&*/:`<>?\\\\|]"));
  return name.replace(invalid, QStringLiteral("_")).trimmed();
}

QString shortenedLabel(QString label) {
  static const QRegularExpression suffix(QStringLiteral("\\s*(?:\\([^)]*\\)|\\[[^]]*\\])\\s*$"));
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  while (suffix.match(label).hasMatch()) {
    label.remove(suffix).replace(whitespace, QStringLiteral(" "));
  }
  return label.trimmed();
}

// A cover that Libretro does not have is remembered on disk, so every launch
// does not repeat the same string of 404s for every visible cartridge.
constexpr qint64 kMissingCoverRetryDays = 7;

QString missingCoverMarkerPath(const QString& cachePath) {
  return cachePath.isEmpty() ? QString{} : cachePath + QStringLiteral(".missing");
}

bool coverRecentlyMissing(const QString& cachePath) {
  const QFileInfo marker(missingCoverMarkerPath(cachePath));
  return marker.exists() &&
         marker.lastModified().daysTo(QDateTime::currentDateTime()) < kMissingCoverRetryDays;
}

void rememberMissingCover(const QString& cachePath) {
  const QString marker = missingCoverMarkerPath(cachePath);
  if (marker.isEmpty()) {
    return;
  }
  QDir().mkpath(QFileInfo(marker).absolutePath());
  QFile file(marker);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.close();
  }
}

QStringList regionAliases(const QString& token) {
  const QString folded = token.trimmed().toCaseFolded();
  if (folded == QLatin1String("na") || folded == QLatin1String("us") ||
      folded == QLatin1String("usa") || folded == QLatin1String("u") ||
      folded == QLatin1String("ntsc-u") || folded == QLatin1String("ntsc-us")) {
    return {QStringLiteral("USA"), QStringLiteral("US"), QStringLiteral("NA")};
  }
  if (folded == QLatin1String("jp") || folded == QLatin1String("j") ||
      folded == QLatin1String("japan") || folded == QLatin1String("jpn") ||
      folded == QLatin1String("ntsc-j")) {
    return {QStringLiteral("Japan"), QStringLiteral("JP"), QStringLiteral("J")};
  }
  if (folded == QLatin1String("eu") || folded == QLatin1String("eur") ||
      folded == QLatin1String("europe") || folded == QLatin1String("pal") ||
      folded == QLatin1String("e")) {
    return {QStringLiteral("Europe"), QStringLiteral("EU")};
  }
  if (folded == QLatin1String("tw") || folded == QLatin1String("twn") ||
      folded == QLatin1String("taiwan")) {
    return {QStringLiteral("Taiwan"), QStringLiteral("TWN")};
  }
  if (folded == QLatin1String("kr") || folded == QLatin1String("korea") ||
      folded == QLatin1String("kor")) {
    return {QStringLiteral("Korea"), QStringLiteral("KR")};
  }
  if (folded == QLatin1String("world") || folded == QLatin1String("w")) {
    return {QStringLiteral("World")};
  }
  return {};
}

void appendUniqueLabel(QStringList* labels, const QString& label) {
  const QString trimmed = label.trimmed();
  if (trimmed.isEmpty() || labels->contains(trimmed)) {
    return;
  }
  labels->append(trimmed);
}

void appendRegionVariants(QStringList* labels, const QString& seed) {
  const QString shortened = shortenedLabel(seed);
  static const QRegularExpression parens(QStringLiteral("\\(([^)]+)\\)"));
  auto iterator = parens.globalMatch(seed);
  while (iterator.hasNext()) {
    for (const QString& alias : regionAliases(iterator.next().captured(1))) {
      appendUniqueLabel(labels, QStringLiteral("%1 (%2)").arg(shortened, alias));
    }
  }
  appendUniqueLabel(labels, seed);
  appendUniqueLabel(labels, shortened);
}

bool looksLikeCoverImage(const QByteArray& bytes) {
  if (bytes.size() < 12) {
    return false;
  }
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
  if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4e && data[3] == 0x47) {
    return true;
  }
  if (data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
    return true;
  }
  return bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP";
}

QString coverCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/libretro");
}

qint64 otherCoverCacheBytes() {
  const QString sharedRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                             QStringLiteral("/omakade/covers");
  const QString libretroRoot = coverCacheRoot() + QLatin1Char('/');
  qint64 total = 0;
  QDirIterator iterator(sharedRoot, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QFileInfo info(iterator.next());
    if (!info.absoluteFilePath().startsWith(libretroRoot)) {
      total += info.size();
    }
  }
  return total;
}
} // namespace

RetroArchGameModel::RetroArchGameModel(const QString& databasePath, AppSettings* settings,
                                       QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(
          QStringLiteral("omakade-retroarch-%1").arg(reinterpret_cast<quintptr>(this))),
      m_settings(settings) {
  m_coverWriteTimer.setSingleShot(true);
  m_coverWriteTimer.setInterval(750);
  connect(&m_coverWriteTimer, &QTimer::timeout, this, &RetroArchGameModel::flushCoverWrites);
  connect(&m_scanWatcher, &QFutureWatcher<RetroArchScanResult>::finished, this,
          [this] {
            m_scanning = false;
            applyScan(m_scanWatcher.result());
            emit statusChanged();
          });
  if (openDatabase(databasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

RetroArchGameModel::~RetroArchGameModel() {
  flushCoverWrites();
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
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  roles.insert(GameRoles::System, "system");
  roles.insert(GameRoles::IsPortal, "isPortal");
  return roles;
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

namespace {
RetroArchScanResult mergeRetroScans(RetroArchScanResult playlists, const RetroArchScanResult& folders) {
  QHash<QString, QString> folderCovers;
  QSet<QString> seen;
  for (const RetroArchGameRecord& game : folders.games) {
    const QString key = RomFolderScanner::canonicalPath(game.contentPath);
    if (!key.isEmpty() && !game.coverPath.isEmpty()) {
      folderCovers.insert(key, game.coverPath);
    }
  }
  for (RetroArchGameRecord& game : playlists.games) {
    const QString key = RomFolderScanner::canonicalPath(game.contentPath);
    if (!key.isEmpty()) {
      seen.insert(key);
      if (game.coverPath.isEmpty() && folderCovers.contains(key)) {
        game.coverPath = folderCovers.value(key);
      }
    }
  }
  for (const RetroArchGameRecord& game : folders.games) {
    const QString key = RomFolderScanner::canonicalPath(game.contentPath);
    if (key.isEmpty() || seen.contains(key)) {
      continue;
    }
    playlists.games.append(game);
    seen.insert(key);
  }
  playlists.roots += folders.roots;
  playlists.warnings += folders.warnings;
  playlists.incomplete = playlists.incomplete || folders.incomplete;
  return playlists;
}

// Auto-discovered folders (EmuDeck-style ~/Emulation and /data/Emulation trees)
// only join the normal refresh. Callers that pass explicit roots, including the
// tests, get exactly the roots and configured folders they asked for.
RetroArchScanResult scanRetroSources(const QStringList& roots, const QStringList& encodedFolders,
                                     bool autoDiscover) {
  QVector<RomFolder> folders = RomFolderScanner::parseEncoded(encodedFolders);
  if (autoDiscover) {
    folders += RomFolderScanner::discoverAutoFolders();
  }
  return mergeRetroScans(RetroArchScanner::scan(roots), RomFolderScanner::scan(folders));
}
} // namespace

void RetroArchGameModel::setConfiguredRomFolders(const QStringList& encoded) {
  if (m_configuredRomFolders == encoded) {
    return;
  }
  m_configuredRomFolders = encoded;
}

void RetroArchGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = RetroArchScanner::discoverRoots();
  const QStringList folders = m_configuredRomFolders;
  setStatus(QStringLiteral("Scanning RetroArch library"));
  m_scanWatcher.setFuture(
      QtConcurrent::run([roots, folders] { return scanRetroSources(roots, folders, true); }));
}
void RetroArchGameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(scanRetroSources(roots, m_configuredRomFolders, false));
}

void RetroArchGameModel::refreshFromSources(const QStringList& retroArchRoots,
                                            const QStringList& encodedFolders) {
  applyScan(scanRetroSources(retroArchRoots, encodedFolders, false));
}

bool RetroArchGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
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
  if (!query.exec(QStringLiteral("SELECT system FROM retroarch_games LIMIT 1")) &&
      !query.exec(QStringLiteral("ALTER TABLE retroarch_games ADD COLUMN system TEXT NOT NULL "
                                 "DEFAULT ''"))) {
    return false;
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
  if (!query.exec()) {
    return;
  }
  const bool found = query.next();
  const int unlocked = found ? query.value(0).toInt() : 0;
  const int total = found ? query.value(1).toInt() : 0;
  for (int row = 0; row < m_games.size(); ++row) {
    Game& game = m_games[row];
    if (game.retroArch.gameId != gameId) {
      continue;
    }
    game.achievementsUnlocked = unlocked;
    game.achievementsTotal = total;
    emit dataChanged(
        index(row), index(row),
        {GameRoles::Progress, GameRoles::AchievementsUnlocked, GameRoles::AchievementsTotal});
    return;
  }
}

void RetroArchGameModel::clearAchievementSummaries() {
  if (m_games.isEmpty()) {
    return;
  }
  bool changed = false;
  for (Game& game : m_games) {
    changed = changed || game.achievementsUnlocked != 0 || game.achievementsTotal != 0;
    game.achievementsUnlocked = 0;
    game.achievementsTotal = 0;
  }
  if (changed) {
    emit dataChanged(index(0), index(m_games.size() - 1),
                     {GameRoles::Progress, GameRoles::AchievementsUnlocked,
                      GameRoles::AchievementsTotal});
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
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
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
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
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
  m_lastScan = scanTimestamp;
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
               ? QStringLiteral("Launch uses a detected emulator or RetroArch core.")
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
  case GameRoles::System:
    return ConsoleCatalog::idFor(record.system);
  case GameRoles::IsPortal:
    return false;
  default:
    return {};
  }
}

void RetroArchGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

QString RetroArchGameModel::libretroCoverCachePath(const QString& gameId) {
  if (gameId.isEmpty() || gameId.contains(QLatin1Char('/')) || gameId.contains(QLatin1Char('\\'))) {
    return {};
  }
  return coverCacheRoot() + QLatin1Char('/') + gameId + QStringLiteral(".png");
}

QString RetroArchGameModel::libretroCoverUrl(const QString& playlist, const QString& label) {
  const QString safePlaylist = QString::fromUtf8(QUrl::toPercentEncoding(playlist));
  const QString safeLabel =
      QString::fromUtf8(QUrl::toPercentEncoding(sanitizedThumbnailName(label)));
  if (safePlaylist.isEmpty() || safeLabel.isEmpty()) {
    return {};
  }
  return QStringLiteral("https://thumbnails.libretro.com/%1/Named_Boxarts/%2.png")
      .arg(safePlaylist, safeLabel);
}

QStringList RetroArchGameModel::coverLabelCandidates(const QString& title,
                                                     const QString& fileBase) {
  QStringList labels;
  appendRegionVariants(&labels, title);
  appendRegionVariants(&labels, fileBase);
  return labels;
}

QStringList RetroArchGameModel::coverLabels(const Game& game) const {
  return coverLabelCandidates(game.retroArch.title,
                              QFileInfo(game.retroArch.contentPath).completeBaseName());
}

void RetroArchGameModel::requestCover(const QString& appId) {
  for (const Game& game : m_games) {
    if (game.retroArch.gameId == appId) {
      requestCoverForGame(game);
      startNextCoverDownloads();
      return;
    }
  }
}

void RetroArchGameModel::requestCoverForGame(const Game& game) {
  if (m_pendingCovers.contains(game.retroArch.gameId) ||
      m_failedCovers.contains(game.retroArch.gameId)) {
    return;
  }
  if (!game.retroArch.coverPath.isEmpty() && QFileInfo::exists(game.retroArch.coverPath)) {
    return;
  }
  const QString cached = libretroCoverCachePath(game.retroArch.gameId);
  if (QFileInfo::exists(cached)) {
    applyCover(game.retroArch.gameId, cached);
    return;
  }
  if (coverRecentlyMissing(cached)) {
    m_failedCovers.insert(game.retroArch.gameId);
    return;
  }
  if (coverLabels(game).isEmpty() ||
      ConsoleCatalog::libretroPlaylistFor(game.retroArch.system).isEmpty()) {
    return;
  }
  m_pendingCovers.insert(game.retroArch.gameId);
  m_coverQueue.enqueue({game.retroArch.gameId, 0});
  while (m_coverQueue.size() > kMaximumQueuedCoverDownloads) {
    const CoverRequest dropped = m_coverQueue.dequeue();
    m_pendingCovers.remove(dropped.gameId);
  }
}

void RetroArchGameModel::startNextCoverDownloads() {
  while (m_activeCoverDownloads < kMaximumConcurrentCoverDownloads && !m_coverQueue.isEmpty()) {
    const CoverRequest request = m_coverQueue.dequeue();
    downloadCover(request.gameId, request.attempt);
  }
}

void RetroArchGameModel::downloadCover(const QString& gameId, int attempt) {
  const Game* game = nullptr;
  for (const Game& candidate : m_games) {
    if (candidate.retroArch.gameId == gameId) {
      game = &candidate;
      break;
    }
  }
  if (game == nullptr) {
    m_pendingCovers.remove(gameId);
    startNextCoverDownloads();
    return;
  }
  const QStringList labels = coverLabels(*game);
  if (attempt < 0 || attempt >= labels.size()) {
    m_pendingCovers.remove(gameId);
    m_failedCovers.insert(gameId);
    rememberMissingCover(libretroCoverCachePath(gameId));
    startNextCoverDownloads();
    return;
  }
  const QUrl url(libretroCoverUrl(ConsoleCatalog::libretroPlaylistFor(game->retroArch.system),
                                  labels.at(attempt)));
  if (!url.isValid()) {
    m_coverQueue.enqueue({gameId, attempt + 1});
    startNextCoverDownloads();
    return;
  }
  QNetworkRequest request(url);
  request.setTransferTimeout(8000);
  request.setPriority(QNetworkRequest::LowPriority);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
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
  connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, attempt] {
    QByteArray contents = m_coverBuffers.take(reply);
    if (contents.size() <= kMaximumCoverBytes) {
      contents.append(reply->read(kMaximumCoverBytes + 1 - contents.size()));
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool tooLarge =
        reply->property("tooLarge").toBool() || contents.size() > kMaximumCoverBytes;
    const bool valid = reply->error() == QNetworkReply::NoError && status == 200 &&
                       !contents.isEmpty() && !tooLarge && looksLikeCoverImage(contents);
    bool saved = false;
    if (valid) {
      const QString path = libretroCoverCachePath(gameId);
      QDir().mkpath(QFileInfo(path).absolutePath());
      // A cache file: written whole, no fsync, so arrivals never stall the interface.
      QFile file(path + QStringLiteral(".part"));
      saved = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
      file.close();
      saved = saved && (QFile::remove(path) || !QFile::exists(path)) && QFile::rename(file.fileName(), path);
      if (saved) {
        QFile::remove(missingCoverMarkerPath(path));
        applyCover(gameId, path);
      }
    }
    reply->deleteLater();
    --m_activeCoverDownloads;
    if (!saved) {
      m_coverQueue.enqueue({gameId, attempt + 1});
    } else {
      m_pendingCovers.remove(gameId);
    }
    startNextCoverDownloads();
  });
}

void RetroArchGameModel::applyCover(const QString& gameId, const QString& path) {
  for (int row = 0; row < m_games.size(); ++row) {
    if (m_games.at(row).retroArch.gameId != gameId) {
      continue;
    }
    m_games[row].retroArch.coverPath = path;
    m_pendingCoverWrites.insert(gameId, path);
    if (!m_coverWriteTimer.isActive()) {
      m_coverWriteTimer.start();
    }
    emit dataChanged(index(row), index(row), {GameRoles::CoverPath});
    return;
  }
}

void RetroArchGameModel::flushCoverWrites() {
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
  query.prepare(QStringLiteral("UPDATE retroarch_games SET cover_path = ? WHERE game_id = ?"));
  for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
    query.addBindValue(it.value());
    query.addBindValue(it.key());
    query.exec();
  }
  m_database.commit();
}

void RetroArchGameModel::pruneCoverCache() {
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
  QSet<QString> referenced;
  for (const Game& game : m_games) {
    referenced.insert(game.retroArch.coverPath);
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
        if (m_games[row].retroArch.coverPath == file.path) {
          m_games[row].retroArch.coverPath.clear();
          emit dataChanged(index(row), index(row), {GameRoles::CoverPath});
        }
      }
    }
  }
}

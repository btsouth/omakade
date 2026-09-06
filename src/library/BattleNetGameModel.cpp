#include "library/BattleNetGameModel.h"

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
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
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

QString runnerLabel(const QString& runner) {
  if (runner == QStringLiteral("proton")) {
    return QStringLiteral("Proton");
  }
  if (runner == QStringLiteral("bottles")) {
    return QStringLiteral("Bottles");
  }
  return QStringLiteral("Wine");
}

constexpr qint64 kMaximumCoverBytes = 8 * 1024 * 1024;
constexpr int kMaximumConcurrentCoverDownloads = 4;

QString coverCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/battlenet");
}

qint64 otherCoverCacheBytes() {
  const QString sharedRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                             QStringLiteral("/omakade/covers");
  const QString battleNetRoot = coverCacheRoot() + QLatin1Char('/');
  qint64 total = 0;
  QDirIterator iterator(sharedRoot, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QFileInfo info(iterator.next());
    if (!info.absoluteFilePath().startsWith(battleNetRoot)) {
      total += info.size();
    }
  }
  return total;
}

bool safeProductId(const QString& productId) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z][A-Za-z0-9._-]{0,63}$"));
  return valid.match(productId).hasMatch();
}

QString cachedArtwork(const QString& productId, bool hero) {
  if (!safeProductId(productId)) {
    return {};
  }
  const QString base =
      coverCacheRoot() + QLatin1Char('/') + productId + (hero ? QStringLiteral("-hero") : QString{});
  for (const QString& extension : {QStringLiteral(".webp"), QStringLiteral(".jpg"),
                                   QStringLiteral(".jpeg"), QStringLiteral(".png")}) {
    const QString path = base + extension;
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

QString artworkExtension(const QByteArray& contents, const QString& contentType) {
  if (contentType.contains(QStringLiteral("webp")) || contents.startsWith("RIFF")) {
    return QStringLiteral(".webp");
  }
  if (contentType.contains(QStringLiteral("png")) || contents.startsWith("\x89PNG")) {
    return QStringLiteral(".png");
  }
  return QStringLiteral(".jpg");
}
} // namespace

BattleNetGameModel::BattleNetGameModel(const QString& omakadeDatabasePath, AppSettings* settings,
                                       QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(
          QStringLiteral("omakade-battlenet-%1").arg(reinterpret_cast<quintptr>(this))),
      m_settings(settings) {
  connect(&m_scanWatcher, &QFutureWatcher<BattleNetScanResult>::finished, this,
          [this] {
            applyScan(m_scanWatcher.result());
            m_scanning = false;
            emit scanningChanged();
          });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
    QTimer::singleShot(400, this, [this] { requestMissingCovers(); });
  }
}

BattleNetGameModel::~BattleNetGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  const auto replies = m_coverBuffers.keys();
  for (QNetworkReply* reply : replies) {
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
  m_coverBuffers.clear();
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int BattleNetGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant BattleNetGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> BattleNetGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  roles.insert(GameRoles::Installed, "installed");
  return roles;
}

bool BattleNetGameModel::battleNetDetected() const { return m_battleNetDetected; }
QString BattleNetGameModel::statusText() const { return m_statusText; }
QString BattleNetGameModel::errorText() const { return m_errorText; }
QStringList BattleNetGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 BattleNetGameModel::lastScan() const { return m_lastScan; }
bool BattleNetGameModel::scanning() const { return m_scanning; }

void BattleNetGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE battlenet_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.battlenet.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void BattleNetGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE battlenet_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.battlenet.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void BattleNetGameModel::requestCover(const QString& appId) {
  for (const Game& game : m_games) {
    if (game.battlenet.gameId == appId) {
      requestCoverForGame(game);
      startNextCoverDownloads();
      return;
    }
  }
}

void BattleNetGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  setStatus(QStringLiteral("Scanning Battle.net library"));
  m_scanning = true;
  emit scanningChanged();
  m_scanWatcher.setFuture(QtConcurrent::run(
      [] { return BattleNetScanner::scan(BattleNetScanner::discoverPrefixes()); }));
}

void BattleNetGameModel::refreshFromPrefixes(const QStringList& prefixes) {
  applyScan(BattleNetScanner::scan(prefixes));
}

bool BattleNetGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Battle.net cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool BattleNetGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS battlenet_games (game_id TEXT PRIMARY KEY, product_id TEXT "
          "NOT NULL, name TEXT NOT NULL, launch_code TEXT, install_path TEXT, wine_prefix TEXT, "
          "runner TEXT, cover_path TEXT, hero_path TEXT, last_played INTEGER NOT NULL DEFAULT 0, "
          "flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER "
          "NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    return false;
  }
  QSet<QString> columns;
  if (query.exec(QStringLiteral("PRAGMA table_info(battlenet_games)"))) {
    while (query.next()) {
      columns.insert(query.value(1).toString());
    }
  }
  if (!columns.contains(QStringLiteral("game_id"))) {
    if (!m_database.transaction()) {
      return false;
    }
    auto rollback = qScopeGuard([this] { m_database.rollback(); });
    if (!query.exec(QStringLiteral("ALTER TABLE battlenet_games RENAME TO battlenet_games_legacy")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE battlenet_games (game_id TEXT PRIMARY KEY, product_id TEXT NOT NULL, "
            "name TEXT NOT NULL, launch_code TEXT, install_path TEXT, wine_prefix TEXT, runner "
            "TEXT, cover_path TEXT, hero_path TEXT, last_played INTEGER NOT NULL DEFAULT 0, "
            "flatpak INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, hidden "
            "INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
      return false;
    }
    QSqlQuery legacy(m_database);
    if (!legacy.exec(QStringLiteral(
            "SELECT product_id, name, launch_code, install_path, wine_prefix, runner, cover_path, "
            "hero_path, last_played, flatpak, favorite, hidden, observed_at FROM "
            "battlenet_games_legacy"))) {
      return false;
    }
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO battlenet_games(game_id, product_id, name, launch_code, install_path, "
        "wine_prefix, runner, cover_path, hero_path, last_played, flatpak, favorite, hidden, "
        "observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    while (legacy.next()) {
      const QString productId = legacy.value(0).toString();
      const QString prefix = legacy.value(4).toString();
      insert.bindValue(0, prefix.isEmpty() ? productId
                                           : BattleNetScanner::gameIdFor(productId, prefix));
      for (int column = 0; column < 13; ++column) {
        insert.bindValue(column + 1, legacy.value(column));
      }
      if (!insert.exec()) {
        return false;
      }
    }
    if (!query.exec(QStringLiteral("DROP TABLE battlenet_games_legacy"))) {
      return false;
    }
    if (!m_database.commit()) {
      return false;
    }
    rollback.dismiss();
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    return false;
  }
  return true;
}

void BattleNetGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, product_id, name, launch_code, install_path, wine_prefix, runner, "
          "cover_path, hero_path, last_played, flatpak, favorite, hidden FROM battlenet_games "
          "WHERE observed_at > 0 ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Battle.net games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    BattleNetGameRecord record{.gameId = query.value(0).toString(),
                               .productId = query.value(1).toString(),
                               .title = query.value(2).toString(),
                               .launchCode = query.value(3).toString(),
                               .installPath = query.value(4).toString(),
                               .winePrefix = query.value(5).toString(),
                               .runner = query.value(6).toString(),
                               .coverPath = query.value(7).toString(),
                               .heroPath = query.value(8).toString(),
                               .lastPlayed = query.value(9).toLongLong(),
                               .flatpak = query.value(10).toBool()};
    loaded.append({.battlenet = record,
                   .favorite = query.value(11).toBool(),
                   .hidden = query.value(12).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void BattleNetGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'battlenet'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_battleNetDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Battle.net library");
  }
}

void BattleNetGameModel::applyScan(const BattleNetScanResult& result) {
  if (result.incomplete || (result.prefixes.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Battle.net scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  m_battleNetDetected = !result.prefixes.isEmpty();
  m_coverQueue.clear();
  m_pendingCovers.clear();
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Battle.net games"), m_database.lastError().text());
    return;
  }
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE battlenet_games SET observed_at = 0"));
  for (const BattleNetGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO battlenet_games(game_id, product_id, name, launch_code, install_path, "
        "wine_prefix, runner, cover_path, hero_path, last_played, flatpak, observed_at) VALUES(?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET "
        "product_id = excluded.product_id, name = excluded.name, launch_code = excluded.launch_code, "
        "install_path = excluded.install_path, wine_prefix = excluded.wine_prefix, runner = "
        "excluded.runner, cover_path = CASE WHEN excluded.cover_path IS NULL OR "
        "excluded.cover_path = '' THEN battlenet_games.cover_path ELSE excluded.cover_path END, "
        "hero_path = CASE WHEN excluded.hero_path IS NULL OR excluded.hero_path = '' THEN "
        "battlenet_games.hero_path ELSE excluded.hero_path END, last_played = excluded.last_played, "
        "flatpak = excluded.flatpak, observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.productId);
    query.addBindValue(game.title);
    query.addBindValue(game.launchCode);
    query.addBindValue(game.installPath);
    query.addBindValue(game.winePrefix);
    query.addBindValue(game.runner);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroPath);
    query.addBindValue(game.lastPlayed);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('battlenet', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.prefixes.isEmpty() ? QStringLiteral("")
                                               : result.prefixes.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Battle.net games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.prefixes;
  m_lastScan = scanTimestamp;
  m_failedCovers.clear();
  requestMissingCovers();
  setStatus(m_battleNetDetected
                ? QStringLiteral("Imported %1 Battle.net game(s)").arg(result.games.size())
                : QStringLiteral("Battle.net was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant BattleNetGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.battlenet.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Battle.net · %1").arg(runnerLabel(game.battlenet.runner));
  case GameRoles::Description:
    return QStringLiteral("Installed locally through Battle.net.");
  case GameRoles::Hours:
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return game.battlenet.lastPlayed > 0;
  case GameRoles::LastPlayed:
    return game.battlenet.lastPlayed;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.battlenet.title.left(1).toUpper();
  case GameRoles::AppId:
    return game.battlenet.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.battlenet.coverPath);
  case GameRoles::HeroPath:
    return localUrl(game.battlenet.heroPath);
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.battlenet.installPath;
  case GameRoles::Source:
    return QStringLiteral("Battle.net");
  case GameRoles::Runner:
    return game.battlenet.runner;
  case GameRoles::Flatpak:
    return game.battlenet.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::LaunchTarget:
    return game.battlenet.winePrefix;
  case GameRoles::Installed:
    return true;
  default:
    return {};
  }
}

void BattleNetGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

void BattleNetGameModel::requestMissingCovers() {
  for (const Game& game : m_games) {
    requestCoverForGame(game);
  }
  startNextCoverDownloads();
}

void BattleNetGameModel::requestCoverForGame(const Game& game) {
  const auto queueIfMissing = [this, &game](bool hero) {
    const QString current = hero ? game.battlenet.heroPath : game.battlenet.coverPath;
    const QString key = game.battlenet.gameId + (hero ? QStringLiteral("-hero") : QString{});
    if ((!current.isEmpty() && QFileInfo::exists(current)) || m_pendingCovers.contains(key) ||
        m_failedCovers.contains(key)) {
      return;
    }
    const QUrl url = hero ? BattleNetScanner::heroUrl(game.battlenet.productId)
                          : BattleNetScanner::coverUrl(game.battlenet.productId);
    if (!url.isValid() || url.isEmpty()) {
      return;
    }
    const QString cached = cachedArtwork(game.battlenet.productId, hero);
    if (!cached.isEmpty()) {
      applyArtwork(game.battlenet.gameId, cached, hero);
      return;
    }
    m_pendingCovers.insert(key);
    m_coverQueue.enqueue({game.battlenet.gameId, game.battlenet.productId, hero});
  };
  queueIfMissing(false);
  queueIfMissing(true);
}

void BattleNetGameModel::startNextCoverDownloads() {
  while (m_activeCoverDownloads < kMaximumConcurrentCoverDownloads && !m_coverQueue.isEmpty()) {
    const CoverRequest request = m_coverQueue.dequeue();
    downloadArtwork(request.gameId, request.productId, request.hero);
  }
}

void BattleNetGameModel::downloadArtwork(const QString& gameId, const QString& productId, bool hero) {
  if (!safeProductId(productId)) {
    const QString key = gameId + (hero ? QStringLiteral("-hero") : QString{});
    m_pendingCovers.remove(key);
    m_failedCovers.insert(key);
    startNextCoverDownloads();
    return;
  }
  const QUrl url = hero ? BattleNetScanner::heroUrl(productId) : BattleNetScanner::coverUrl(productId);
  QNetworkRequest request(url);
  request.setTransferTimeout(12000);
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
  connect(reply, &QNetworkReply::finished, this, [this, reply, gameId, productId, hero] {
    QByteArray contents = m_coverBuffers.take(reply);
    if (contents.size() <= kMaximumCoverBytes) {
      contents.append(reply->read(kMaximumCoverBytes + 1 - contents.size()));
    }
    const bool tooLarge =
        reply->property("tooLarge").toBool() || contents.size() > kMaximumCoverBytes;
    const QString contentType =
        QString::fromLatin1(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
    const bool valid = reply->error() == QNetworkReply::NoError && !contents.isEmpty() &&
                       !tooLarge && !QImage::fromData(contents).isNull();
    bool saved = false;
    if (valid) {
      const QString path = coverCacheRoot() + QLatin1Char('/') + productId +
                           (hero ? QStringLiteral("-hero") : QString{}) +
                           artworkExtension(contents, contentType);
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile file(path);
      saved = file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() &&
              file.commit();
      if (saved) {
        applyArtwork(gameId, path, hero);
      }
    }
    reply->deleteLater();
    --m_activeCoverDownloads;
    const QString key = gameId + (hero ? QStringLiteral("-hero") : QString{});
    m_pendingCovers.remove(key);
    if (!saved) {
      m_failedCovers.insert(key);
    }
    startNextCoverDownloads();
    if (m_activeCoverDownloads == 0 && m_coverQueue.isEmpty()) {
      pruneCoverCache();
    }
  });
}

void BattleNetGameModel::applyArtwork(const QString& gameId, const QString& path, bool hero) {
  for (int row = 0; row < m_games.size(); ++row) {
    if (m_games.at(row).battlenet.gameId != gameId) {
      continue;
    }
    QString& current =
        hero ? m_games[row].battlenet.heroPath : m_games[row].battlenet.coverPath;
    if (!current.isEmpty() && QFileInfo::exists(current) &&
        !current.startsWith(coverCacheRoot())) {
      return;
    }
    current = path;
    if (m_database.isOpen()) {
      QSqlQuery query(m_database);
      query.prepare(hero ? QStringLiteral("UPDATE battlenet_games SET hero_path = ? WHERE game_id = ?")
                         : QStringLiteral(
                               "UPDATE battlenet_games SET cover_path = ? WHERE game_id = ?"));
      query.addBindValue(path);
      query.addBindValue(gameId);
      query.exec();
    }
    emit dataChanged(index(row), index(row),
                     {hero ? GameRoles::HeroPath : GameRoles::CoverPath});
    return;
  }
}

void BattleNetGameModel::pruneCoverCache() {
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
  std::sort(files.begin(), files.end(), [](const CachedFile& left, const CachedFile& right) {
    return left.modified < right.modified;
  });
  for (const CachedFile& file : files) {
    if (total <= limit) {
      break;
    }
    if (QFile::remove(file.path)) {
      total -= file.size;
    }
  }
}

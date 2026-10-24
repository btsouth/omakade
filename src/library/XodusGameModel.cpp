#include "library/XodusGameModel.h"

#include "app/AppSettings.h"
#include "library/CoverCachePolicy.h"
#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
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

namespace {
constexpr qint64 kMaximumArtworkBytes = 8 * 1024 * 1024;
constexpr qint64 kMaximumCatalogBytes = 4 * 1024 * 1024;
constexpr int kMaximumConcurrentArtworkDownloads = 4;

QColor colorFor(const QString& key, int offset) {
  const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
  return QColor::fromHsl((static_cast<unsigned char>(hash.at(offset)) * 359) / 255, 115,
                         offset == 0 ? 105 : 72);
}

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
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

QString XodusGameModel::artworkCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/xodus");
}

XodusGameModel::XodusGameModel(const QString& omakadeDatabasePath, AppSettings* settings,
                               QObject* parent)
    : QAbstractListModel(parent), m_settings(settings),
      m_connectionName(QStringLiteral("omakade-xodus-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<XodusScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

XodusGameModel::~XodusGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  for (QNetworkReply* reply : m_artBuffers.keys()) {
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int XodusGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant XodusGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> XodusGameModel::roleNames() const {
  // The launch target carries the Wine prefix that Stop Game needs.
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool XodusGameModel::setFlag(int row, const char* column, bool Game::* flag, int role) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return false;
  }
  Game& game = m_games[row];
  game.*flag = !(game.*flag);
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("UPDATE xodus_games SET %1 = ? WHERE game_id = ?").arg(QLatin1String(column)));
  query.addBindValue(game.*flag);
  query.addBindValue(game.xodus.gameId);
  if (!query.exec()) {
    game.*flag = !(game.*flag);
    setStatus(m_statusText, query.lastError().text());
    return false;
  }
  emit dataChanged(index(row), index(row), {role});
  return true;
}

void XodusGameModel::toggleFavorite(int row) {
  setFlag(row, "favorite", &Game::favorite, GameRoles::Favorite);
}

void XodusGameModel::toggleHidden(int row) {
  setFlag(row, "hidden", &Game::hidden, GameRoles::Hidden);
}

void XodusGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  // A rescan retries artwork that failed before, such as while offline.
  m_catalogAsked.clear();
  m_failedArt.clear();
  const QStringList roots = XodusScanner::discoverRoots();
  const QString cache = artworkCacheRoot();
  setStatus(QStringLiteral("Scanning Xodus Nexus library"));
  m_scanWatcher.setFuture(
      QtConcurrent::run([roots, cache] { return XodusScanner::scan(roots, cache); }));
}

void XodusGameModel::refreshFromRoots(const QStringList& roots, const QString& artworkCache) {
  applyScan(XodusScanner::scan(roots, artworkCache));
}

bool XodusGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Xodus Nexus cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool XodusGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  return query.exec(QStringLiteral(
             "CREATE TABLE IF NOT EXISTS xodus_games (game_id TEXT PRIMARY KEY, name TEXT NOT "
             "NULL, publisher TEXT, store_id TEXT, install_path TEXT, executable TEXT, "
             "wine_prefix TEXT, cover_path TEXT, hero_url TEXT, poster_path "
             "TEXT, hero_path TEXT, anti_cheat INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT "
             "NULL DEFAULT 0, "
             "hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)")) &&
         query.exec(QStringLiteral(
             "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan "
             "INTEGER, last_error TEXT, paths TEXT NOT NULL DEFAULT '')"));
}

void XodusGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, publisher, store_id, install_path, executable, wine_prefix, "
          "cover_path, hero_url, poster_path, hero_path, anti_cheat, favorite, hidden FROM "
          "xodus_games WHERE observed_at > 0 ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Xodus Nexus games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    XodusGameRecord record{.gameId = query.value(0).toString(),
                           .title = query.value(1).toString(),
                           .publisher = query.value(2).toString(),
                           .storeId = query.value(3).toString(),
                           .installPath = query.value(4).toString(),
                           .executable = query.value(5).toString(),
                           .winePrefix = query.value(6).toString(),
                           .coverPath = query.value(7).toString(),
                           .heroUrl = XodusScanner::safeArtworkUrl(query.value(8).toString()),
                           .antiCheat = query.value(11).toBool()};
    const auto existing = [](const QString& path) {
      return !path.isEmpty() && QFileInfo::exists(path) ? path : QString{};
    };
    loaded.append({.xodus = record,
                   .posterPath = existing(query.value(9).toString()),
                   .heroPath = existing(query.value(10).toString()),
                   .favorite = query.value(12).toBool(),
                   .hidden = query.value(13).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void XodusGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'xodus'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_xodusDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Xodus Nexus library");
  }
}

void XodusGameModel::applyScan(const XodusScanResult& result) {
  m_xodusDetected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Xodus Nexus scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Xodus Nexus games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE xodus_games SET observed_at = 0"));
  for (const XodusGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO xodus_games(game_id, name, publisher, store_id, install_path, executable, "
        "wine_prefix, cover_path, hero_url, anti_cheat, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_id) DO UPDATE SET name = "
        "excluded.name, publisher = excluded.publisher, store_id = excluded.store_id, "
        "install_path = excluded.install_path, executable = excluded.executable, wine_prefix = "
        "excluded.wine_prefix, cover_path = "
        "excluded.cover_path, hero_url = excluded.hero_url, anti_cheat = excluded.anti_cheat, "
        "observed_at = excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.publisher);
    query.addBindValue(game.storeId);
    query.addBindValue(game.installPath);
    query.addBindValue(game.executable);
    query.addBindValue(game.winePrefix);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.heroUrl.toString(QUrl::FullyEncoded));
    query.addBindValue(game.antiCheat);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('xodus', ?, ?, ?) "
      "ON CONFLICT(source) DO UPDATE SET last_scan = excluded.last_scan, last_error = "
      "excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Xodus Nexus games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_xodusDetected
                ? QStringLiteral("Imported %1 Xodus Nexus game(s)").arg(result.games.size())
                : QStringLiteral("Xodus Nexus was not found"),
            result.warnings.join(QLatin1Char('\n')));
  requestMissingArtwork();
}

void XodusGameModel::requestMissingArtwork() {
  if (!m_downloadArtwork) {
    return;
  }
  QStringList storeIds;
  for (const Game& game : m_games) {
    if (!game.posterPath.isEmpty() && !game.heroPath.isEmpty()) {
      continue;
    }
    const QString& storeId = game.xodus.storeId;
    if (XodusScanner::validStoreId(storeId)) {
      if (!m_catalogAsked.contains(storeId)) {
        storeIds.append(storeId);
      }
    } else if (game.heroPath.isEmpty()) {
      // Without a store ID only the banner Nexus recorded is available.
      queueArtwork(game.xodus.gameId, game.xodus.heroUrl, true);
    }
  }
  storeIds.removeDuplicates();
  for (qsizetype first = 0; first < storeIds.size(); first += 20) {
    requestCatalog(storeIds.mid(first, 20));
  }
  startNextArtworkDownloads();
}

void XodusGameModel::requestCatalog(const QStringList& storeIds) {
  const QUrl url = XodusScanner::catalogUrl(storeIds);
  if (url.isEmpty()) {
    return;
  }
  for (const QString& id : storeIds) {
    m_catalogAsked.insert(id);
  }
  QNetworkRequest request(url);
  request.setTransferTimeout(12000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  m_artBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_artBuffers[reply];
    buffer.append(reply->read(kMaximumCatalogBytes + 1 - buffer.size()));
    if (buffer.size() > kMaximumCatalogBytes) {
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, storeIds] {
    QByteArray contents = m_artBuffers.take(reply);
    if (contents.size() <= kMaximumCatalogBytes) {
      contents.append(reply->read(kMaximumCatalogBytes + 1 - contents.size()));
    }
    const QHash<QString, XodusStoreArt> art =
        reply->error() == QNetworkReply::NoError && contents.size() <= kMaximumCatalogBytes
            ? XodusScanner::parseCatalog(contents)
            : QHash<QString, XodusStoreArt>{};
    reply->deleteLater();
    for (const Game& game : m_games) {
      if (!storeIds.contains(game.xodus.storeId)) {
        continue;
      }
      const XodusStoreArt entry = art.value(game.xodus.storeId);
      if (game.posterPath.isEmpty()) {
        queueArtwork(game.xodus.gameId, entry.poster, false);
      }
      if (game.heroPath.isEmpty()) {
        queueArtwork(game.xodus.gameId, entry.hero.isEmpty() ? game.xodus.heroUrl : entry.hero,
                     true);
      }
    }
    startNextArtworkDownloads();
  });
}

void XodusGameModel::queueArtwork(const QString& gameId, const QUrl& url, bool hero) {
  const QString key = gameId + (hero ? QStringLiteral("-hero") : QStringLiteral("-poster"));
  if (url.isEmpty() || m_pendingArt.contains(key) || m_failedArt.contains(key)) {
    return;
  }
  m_pendingArt.insert(key);
  m_artQueue.enqueue({gameId, url, hero});
}

void XodusGameModel::startNextArtworkDownloads() {
  while (m_activeArtDownloads < kMaximumConcurrentArtworkDownloads && !m_artQueue.isEmpty()) {
    downloadArtwork(m_artQueue.dequeue());
  }
}

void XodusGameModel::downloadArtwork(const ArtRequest& art) {
  QNetworkRequest request(art.url);
  request.setTransferTimeout(12000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  ++m_activeArtDownloads;
  m_artBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_artBuffers[reply];
    buffer.append(reply->read(kMaximumArtworkBytes + 1 - buffer.size()));
    if (buffer.size() > kMaximumArtworkBytes) {
      reply->setProperty("tooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, art] {
    QByteArray contents = m_artBuffers.take(reply);
    if (contents.size() <= kMaximumArtworkBytes) {
      contents.append(reply->read(kMaximumArtworkBytes + 1 - contents.size()));
    }
    const bool valid = reply->error() == QNetworkReply::NoError && !contents.isEmpty() &&
                       !reply->property("tooLarge").toBool() &&
                       contents.size() <= kMaximumArtworkBytes &&
                       !QImage::fromData(contents).isNull();
    const QString key =
        art.gameId + (art.hero ? QStringLiteral("-hero") : QStringLiteral("-poster"));
    bool saved = false;
    if (valid) {
      const QString contentType =
          QString::fromLatin1(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
      const QString path =
          artworkCacheRoot() + QLatin1Char('/') + key + artworkExtension(contents, contentType);
      QDir().mkpath(artworkCacheRoot());
      QSaveFile file(path);
      saved = file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() &&
              file.commit();
      if (saved) {
        applyArtwork(art.gameId, path, art.hero);
      }
    }
    reply->deleteLater();
    --m_activeArtDownloads;
    m_pendingArt.remove(key);
    if (!saved) {
      m_failedArt.insert(key);
      // A store hero that fails falls back to the banner Nexus recorded, once.
      for (const Game& game : m_games) {
        if (art.hero && game.xodus.gameId == art.gameId && game.xodus.heroUrl != art.url) {
          m_failedArt.remove(key);
          queueArtwork(art.gameId, game.xodus.heroUrl, true);
          if (!m_pendingArt.contains(key)) {
            m_failedArt.insert(key);
          }
        }
      }
    }
    startNextArtworkDownloads();
    if (m_activeArtDownloads == 0 && m_artQueue.isEmpty()) {
      pruneArtworkCache();
    }
  });
}

void XodusGameModel::applyArtwork(const QString& gameId, const QString& path, bool hero) {
  for (int row = 0; row < m_games.size(); ++row) {
    if (m_games.at(row).xodus.gameId != gameId) {
      continue;
    }
    QSqlQuery query(m_database);
    query.prepare(hero
                      ? QStringLiteral("UPDATE xodus_games SET hero_path = ? WHERE game_id = ?")
                      : QStringLiteral("UPDATE xodus_games SET poster_path = ? WHERE game_id = ?"));
    query.addBindValue(path);
    query.addBindValue(gameId);
    if (!query.exec()) {
      setStatus(m_statusText, QStringLiteral("Artwork cache changes could not be saved."));
      return;
    }
    (hero ? m_games[row].heroPath : m_games[row].posterPath) = path;
    emit dataChanged(index(row), index(row), {hero ? GameRoles::HeroPath : GameRoles::CoverPath});
    return;
  }
}

void XodusGameModel::pruneArtworkCache() {
  const int limitMb = m_settings == nullptr ? 1024 : m_settings->artworkCacheLimitMb();
  QSet<QString> referenced;
  for (const Game& game : m_games) {
    referenced.insert(game.xodus.coverPath);
    referenced.insert(game.posterPath);
    referenced.insert(game.heroPath);
  }
  CoverCachePolicy::prune(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                              QStringLiteral("/omakade/covers"),
                          artworkCacheRoot(), qint64(limitMb) * 1024 * 1024, referenced);
}

QVariant XodusGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.xodus.title;
  case GameRoles::Subtitle:
    return game.xodus.publisher.isEmpty()
               ? QStringLiteral("Xodus Nexus")
               : QStringLiteral("Xodus Nexus · %1").arg(game.xodus.publisher);
  case GameRoles::Description:
    return game.xodus.antiCheat
               ? QStringLiteral("Installed and managed by Xodus Nexus. This game uses "
                                "anti-cheat, so online play depends on its Linux support.")
               : QStringLiteral("Installed and managed by Xodus Nexus.");
  case GameRoles::PlaytimeSeconds:
  case GameRoles::Hours:
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
  case GameRoles::Year:
  case GameRoles::LastPlayed:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return false;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.xodus.title.left(1).toUpper();
  case GameRoles::AppId:
    return game.xodus.gameId;
  // The square box art Nexus saved stands in until the portrait poster arrives.
  case GameRoles::CoverPath:
    return localUrl(game.posterPath.isEmpty() ? game.xodus.coverPath : game.posterPath);
  case GameRoles::HeroPath:
    return localUrl(game.heroPath);
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.xodus.installPath;
  case GameRoles::Source:
    return QStringLiteral("Xodus Nexus");
  case GameRoles::Runner:
    return QString{};
  // Stopping a game goes through its prefix.
  case GameRoles::LaunchTarget:
    return game.xodus.winePrefix;
  case GameRoles::Flatpak:
    return false;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void XodusGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

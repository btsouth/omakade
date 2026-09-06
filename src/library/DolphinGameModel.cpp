#include "library/DolphinGameModel.h"

#include "library/DatabaseTuning.h"
#include "library/GameRoles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
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

DolphinGameModel::DolphinGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-dolphin-%1").arg(reinterpret_cast<quintptr>(this))) {
  m_coverWriteTimer.setSingleShot(true);
  m_coverWriteTimer.setInterval(750);
  connect(&m_coverWriteTimer, &QTimer::timeout, this, &DolphinGameModel::flushCoverWrites);
  connect(&m_scanWatcher, &QFutureWatcher<DolphinScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

DolphinGameModel::~DolphinGameModel() {
  flushCoverWrites();
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int DolphinGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant DolphinGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> DolphinGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool DolphinGameModel::dolphinDetected() const { return m_dolphinDetected; }
QString DolphinGameModel::statusText() const { return m_statusText; }
QString DolphinGameModel::errorText() const { return m_errorText; }
QStringList DolphinGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 DolphinGameModel::lastScan() const { return m_lastScan; }

void DolphinGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE dolphin_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.dolphin.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void DolphinGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE dolphin_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.dolphin.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void DolphinGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = DolphinScanner::discoverRoots();
  setStatus(QStringLiteral("Scanning Dolphin library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return DolphinScanner::scan(roots, {}, true); }));
}

void DolphinGameModel::refreshFromRoots(const QStringList& roots, const QStringList& folders) {
  applyScan(DolphinScanner::scan(roots, folders, false));
}

QString DolphinGameModel::coverCachePath(const QString& discId) {
  static const QRegularExpression valid(QStringLiteral("^[A-Z0-9]{6}$"));
  if (!valid.match(discId).hasMatch()) {
    return {};
  }
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/gametdb/") + discId + QStringLiteral(".png");
}

// The same GameTDB endpoint Dolphin uses for its own cover downloads. The
// fourth character of a disc id is its region.
QString DolphinGameModel::gameTdbCoverUrl(const QString& discId) {
  if (coverCachePath(discId).isEmpty()) {
    return {};
  }
  QString region = QStringLiteral("US");
  switch (discId.at(3).toLatin1()) {
  case 'J': region = QStringLiteral("JA"); break;
  case 'K': region = QStringLiteral("KO"); break;
  case 'P': case 'D': case 'F': case 'I': case 'S': case 'X': case 'Y': region = QStringLiteral("EN"); break;
  default: break;
  }
  return QStringLiteral("https://art.gametdb.com/wii/cover/%1/%2.png").arg(region, discId);
}

void DolphinGameModel::requestCover(const QString& appId) {
  for (const Game& game : m_games) {
    if (game.dolphin.gameId != appId) {
      continue;
    }
    const QString discId = game.dolphin.discId;
    if (!game.dolphin.coverPath.isEmpty() || m_pendingCovers.contains(appId) ||
        m_failedCovers.contains(appId) || coverCachePath(discId).isEmpty()) {
      return;
    }
    const QString cached = coverCachePath(discId);
    if (QFileInfo::exists(cached)) {
      applyCover(appId, cached);
      return;
    }
    const QFileInfo missing(cached + QStringLiteral(".missing"));
    if (missing.exists() && missing.lastModified().daysTo(QDateTime::currentDateTime()) < 7) {
      m_failedCovers.insert(appId);
      return;
    }
    QNetworkRequest request{QUrl(gameTdbCoverUrl(discId))};
    request.setTransferTimeout(8000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_pendingCovers.insert(appId);
    QNetworkReply* reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appId, cached] {
      const QByteArray bytes = reply->read(8 * 1024 * 1024);
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      reply->deleteLater();
      m_pendingCovers.remove(appId);
      QDir().mkpath(QFileInfo(cached).absolutePath());
      if (reply->error() == QNetworkReply::NoError && status == 200 && bytes.startsWith("\x89PNG")) {
        QSaveFile file(cached);
        if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
          QFile::remove(cached + QStringLiteral(".missing"));
          applyCover(appId, cached);
          return;
        }
      }
      m_failedCovers.insert(appId);
      QFile marker(cached + QStringLiteral(".missing"));
      if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.close();
      }
    });
    return;
  }
}

void DolphinGameModel::applyCover(const QString& gameId, const QString& path) {
  for (int row = 0; row < m_games.size(); ++row) {
    if (m_games.at(row).dolphin.gameId != gameId) {
      continue;
    }
    m_games[row].dolphin.coverPath = path;
    m_pendingCoverWrites.insert(gameId, path);
    if (!m_coverWriteTimer.isActive()) {
      m_coverWriteTimer.start();
    }
    emit dataChanged(index(row), index(row), {GameRoles::CoverPath});
    return;
  }
}

void DolphinGameModel::flushCoverWrites() {
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
  query.prepare(QStringLiteral("UPDATE dolphin_games SET cover_path = ? WHERE game_id = ?"));
  for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
    query.addBindValue(it.value());
    query.addBindValue(it.key());
    query.exec();
  }
  m_database.commit();
}

bool DolphinGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Dolphin cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool DolphinGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS dolphin_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT, disc_id TEXT, cover_path TEXT, platform TEXT NOT NULL DEFAULT 'GameCube', flatpak INTEGER NOT NULL "
          "DEFAULT 0, flatpak_app_id TEXT NOT NULL DEFAULT '', favorite INTEGER NOT NULL DEFAULT "
          "0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize Dolphin cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize Dolphin cache"), query.lastError().text());
    return false;
  }
  return true;
}

void DolphinGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, disc_id, cover_path, platform, flatpak, "
          "flatpak_app_id, favorite, hidden FROM dolphin_games WHERE observed_at > 0 ORDER BY "
          "name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Dolphin games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    DolphinGameRecord record{.gameId = query.value(0).toString(),
                             .discId = query.value(3).toString(),
                             .title = query.value(1).toString(),
                             .path = query.value(2).toString(),
                             .platform = query.value(5).toString(),
                             .coverPath = query.value(4).toString(),
                             .flatpak = query.value(6).toBool(),
                             .flatpakAppId = query.value(7).toString()};
    loaded.append({.dolphin = record,
                   .favorite = query.value(8).toBool(),
                   .hidden = query.value(9).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void DolphinGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT last_scan, last_error, paths FROM source_state WHERE source = 'dolphin'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_dolphinDetected = !m_detectedPaths.isEmpty() || DolphinScanner::dolphinInstalled();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Dolphin games");
  }
}

void DolphinGameModel::applyScan(const DolphinScanResult& result) {
  m_dolphinDetected = !result.roots.isEmpty() || DolphinScanner::dolphinInstalled();
  if (result.incomplete || (!m_dolphinDetected && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Dolphin scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Dolphin games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE dolphin_games SET observed_at = 0"));
  for (const DolphinGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO dolphin_games(game_id, name, path, disc_id, cover_path, platform, "
        "flatpak, flatpak_app_id, observed_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', "
        "'now')) ON CONFLICT(game_id) DO UPDATE SET name = excluded.name, path = excluded.path, "
        "disc_id = excluded.disc_id, cover_path = CASE WHEN excluded.cover_path = '' THEN "
        "dolphin_games.cover_path ELSE excluded.cover_path END, platform = excluded.platform, "
        "flatpak = excluded.flatpak, flatpak_app_id = excluded.flatpak_app_id, observed_at = "
        "excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.discId);
    query.addBindValue(game.coverPath.isNull() ? QStringLiteral("") : game.coverPath);
    query.addBindValue(game.platform);
    query.addBindValue(game.flatpak);
    query.addBindValue(game.flatpakAppId.isNull() ? QStringLiteral("") : game.flatpakAppId);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('dolphin', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Dolphin games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots + result.folders;
  m_lastScan = scanTimestamp;
  setStatus(m_dolphinDetected
                ? QStringLiteral("Imported %1 Dolphin game(s)").arg(result.games.size())
                : QStringLiteral("Dolphin was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant DolphinGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.dolphin.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Dolphin · %1").arg(game.dolphin.platform);
  case GameRoles::Description:
    return QStringLiteral("%1 disc launched through Dolphin.").arg(game.dolphin.platform);
  case GameRoles::Hours:
  case GameRoles::Progress:
  case GameRoles::AchievementsUnlocked:
  case GameRoles::AchievementsTotal:
    return 0;
  case GameRoles::Favorite:
    return game.favorite;
  case GameRoles::Recent:
    return false;
  case GameRoles::LastPlayed:
    return 0;
  case GameRoles::AccentStart:
    return game.accentStart;
  case GameRoles::AccentEnd:
    return game.accentEnd;
  case GameRoles::CoverMark:
    return game.dolphin.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.dolphin.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.dolphin.coverPath);
  case GameRoles::System:
    return game.dolphin.platform.compare(QStringLiteral("Wii"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("wii")
               : QStringLiteral("gamecube");
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.dolphin.path;
  case GameRoles::Source:
    return QStringLiteral("Dolphin");
  case GameRoles::Runner:
    return game.dolphin.flatpakAppId;
  case GameRoles::LaunchTarget:
    return game.dolphin.path;
  case GameRoles::Flatpak:
    return game.dolphin.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  default:
    return {};
  }
}

void DolphinGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

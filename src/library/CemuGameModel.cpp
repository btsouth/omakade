#include "library/CemuGameModel.h"

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

CemuGameModel::CemuGameModel(const QString& omakadeDatabasePath, QObject* parent)
    : QAbstractListModel(parent),
      m_connectionName(QStringLiteral("omakade-cemu-%1").arg(reinterpret_cast<quintptr>(this))) {
  connect(&m_scanWatcher, &QFutureWatcher<CemuScanResult>::finished, this, [this] {
    m_scanning = false;
    applyScan(m_scanWatcher.result());
    emit statusChanged();
  });
  if (openDatabase(omakadeDatabasePath) && ensureSchema()) {
    loadDatabase();
    loadSourceState();
  }
}

CemuGameModel::~CemuGameModel() {
  if (m_scanWatcher.isRunning()) {
    m_scanWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int CemuGameModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant CemuGameModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
    return {};
  }
  return valueForRole(m_games.at(index.row()), role);
}

QHash<int, QByteArray> CemuGameModel::roleNames() const {
  auto roles = GameRoles::names();
  roles.insert(GameRoles::LaunchTarget, "launchTarget");
  return roles;
}

bool CemuGameModel::cemuDetected() const { return m_cemuDetected; }
QString CemuGameModel::statusText() const { return m_statusText; }
QString CemuGameModel::errorText() const { return m_errorText; }
QStringList CemuGameModel::detectedPaths() const { return m_detectedPaths; }
qint64 CemuGameModel::lastScan() const { return m_lastScan; }

void CemuGameModel::toggleFavorite(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.favorite = !game.favorite;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE cemu_games SET favorite = ? WHERE game_id = ?"));
  query.addBindValue(game.favorite);
  query.addBindValue(game.cemu.gameId);
  if (!query.exec()) {
    game.favorite = !game.favorite;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Favorite});
}

void CemuGameModel::toggleHidden(int row) {
  if (row < 0 || row >= m_games.size() || !m_database.isOpen()) {
    return;
  }
  Game& game = m_games[row];
  game.hidden = !game.hidden;
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE cemu_games SET hidden = ? WHERE game_id = ?"));
  query.addBindValue(game.hidden);
  query.addBindValue(game.cemu.gameId);
  if (!query.exec()) {
    game.hidden = !game.hidden;
    setStatus(m_statusText, query.lastError().text());
    return;
  }
  emit dataChanged(index(row), index(row), {GameRoles::Hidden});
}

void CemuGameModel::refresh() {
  if (m_scanWatcher.isRunning()) {
    return;
  }
  m_scanning = true;
  const QStringList roots = CemuScanner::discoverRoots();
  setStatus(QStringLiteral("Scanning Cemu library"));
  m_scanWatcher.setFuture(QtConcurrent::run([roots] { return CemuScanner::scan(roots); }));
}

void CemuGameModel::refreshFromRoots(const QStringList& roots) {
  applyScan(CemuScanner::scan(roots));
}

bool CemuGameModel::openDatabase(const QString& path) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(path);
  if (!openTunedDatabase(m_database)) {
    setStatus(QStringLiteral("Cemu cache unavailable"), m_database.lastError().text());
    return false;
  }
  return true;
}

bool CemuGameModel::ensureSchema() {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS cemu_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
          "path TEXT, title_id TEXT, cover_path TEXT, flatpak INTEGER NOT NULL DEFAULT 0, "
          "favorite INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at "
          "INTEGER NOT NULL)"))) {
    setStatus(QStringLiteral("Could not initialize Cemu cache"), query.lastError().text());
    return false;
  }
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS source_state (source TEXT PRIMARY KEY, last_scan INTEGER, "
          "last_error TEXT, paths TEXT NOT NULL DEFAULT '')"))) {
    setStatus(QStringLiteral("Could not initialize Cemu cache"), query.lastError().text());
    return false;
  }
  return true;
}

void CemuGameModel::loadDatabase() {
  QVector<Game> loaded;
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral(
          "SELECT game_id, name, path, title_id, cover_path, flatpak, favorite, hidden FROM "
          "cemu_games WHERE observed_at > 0 ORDER BY name COLLATE NOCASE"))) {
    setStatus(QStringLiteral("Could not load cached Cemu games"), query.lastError().text());
    return;
  }
  while (query.next()) {
    CemuGameRecord record{.gameId = query.value(0).toString(),
                          .titleId = query.value(3).toString(),
                          .title = query.value(1).toString(),
                          .path = query.value(2).toString(),
                          .coverPath = query.value(4).toString(),
                          .flatpak = query.value(5).toBool()};
    loaded.append({.cemu = record,
                   .favorite = query.value(6).toBool(),
                   .hidden = query.value(7).toBool(),
                   .accentStart = colorFor(record.gameId, 0),
                   .accentEnd = colorFor(record.gameId, 1)});
  }
  beginResetModel();
  m_games = loaded;
  endResetModel();
}

void CemuGameModel::loadSourceState() {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT last_scan, last_error, paths FROM source_state WHERE source = 'cemu'"));
  if (!query.exec() || !query.next()) {
    return;
  }
  m_lastScan = query.value(0).toLongLong();
  m_errorText = query.value(1).toString();
  m_detectedPaths = query.value(2).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  m_cemuDetected = !m_detectedPaths.isEmpty();
  if (m_lastScan > 0) {
    m_statusText = QStringLiteral("Loaded cached Cemu games");
  }
}

void CemuGameModel::applyScan(const CemuScanResult& result) {
  m_cemuDetected = !result.roots.isEmpty();
  if (result.incomplete || (result.roots.isEmpty() && !m_games.isEmpty())) {
    setStatus(QStringLiteral("Cemu scan interrupted; kept the cached library"),
              result.warnings.join(QLatin1Char('\n')));
    return;
  }
  if (!m_database.transaction()) {
    setStatus(QStringLiteral("Could not update Cemu games"), m_database.lastError().text());
    return;
  }
  const qint64 scanTimestamp = QDateTime::currentSecsSinceEpoch();
  QSqlQuery query(m_database);
  bool okay = query.exec(QStringLiteral("UPDATE cemu_games SET observed_at = 0"));
  for (const CemuGameRecord& game : result.games) {
    query.prepare(QStringLiteral(
        "INSERT INTO cemu_games(game_id, name, path, title_id, cover_path, flatpak, "
        "observed_at) VALUES(?, ?, ?, ?, ?, ?, strftime('%s', 'now')) ON CONFLICT(game_id) DO "
        "UPDATE SET name = excluded.name, path = excluded.path, title_id = excluded.title_id, "
        "cover_path = excluded.cover_path, flatpak = excluded.flatpak, observed_at = "
        "excluded.observed_at"));
    query.addBindValue(game.gameId);
    query.addBindValue(game.title);
    query.addBindValue(game.path);
    query.addBindValue(game.titleId);
    query.addBindValue(game.coverPath);
    query.addBindValue(game.flatpak);
    okay = okay && query.exec();
  }
  query.prepare(QStringLiteral(
      "INSERT INTO source_state(source, last_scan, last_error, paths) VALUES('cemu', "
      "?, ?, ?) ON CONFLICT(source) DO UPDATE SET last_scan = "
      "excluded.last_scan, last_error = excluded.last_error, paths = excluded.paths"));
  query.addBindValue(scanTimestamp);
  query.addBindValue(result.warnings.join(QLatin1Char('\n')));
  query.addBindValue(result.roots.isEmpty() ? QStringLiteral("")
                                            : result.roots.join(QLatin1Char('\n')));
  okay = okay && query.exec();
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    setStatus(QStringLiteral("Could not update Cemu games"), query.lastError().text());
    return;
  }
  loadDatabase();
  m_detectedPaths = result.roots;
  m_lastScan = scanTimestamp;
  setStatus(m_cemuDetected ? QStringLiteral("Imported %1 Cemu game(s)").arg(result.games.size())
                           : QStringLiteral("Cemu was not found"),
            result.warnings.join(QLatin1Char('\n')));
}

QVariant CemuGameModel::valueForRole(const Game& game, int role) const {
  switch (role) {
  case GameRoles::Title:
    return game.cemu.title;
  case GameRoles::Subtitle:
    return QStringLiteral("Cemu");
  case GameRoles::Description:
    return QStringLiteral("Wii U game launched through Cemu.");
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
    return game.cemu.title.left(1).toUpper();
  case GameRoles::Year:
    return 0;
  case GameRoles::AppId:
    return game.cemu.gameId;
  case GameRoles::CoverPath:
    return localUrl(game.cemu.coverPath);
  case GameRoles::HeroPath:
  case GameRoles::LogoPath:
    return QString{};
  case GameRoles::InstallPath:
    return game.cemu.path;
  case GameRoles::Source:
    return QStringLiteral("Cemu");
  case GameRoles::Runner:
    return QString{};
  case GameRoles::LaunchTarget:
    return game.cemu.path;
  case GameRoles::Flatpak:
    return game.cemu.flatpak;
  case GameRoles::Hidden:
    return game.hidden;
  case GameRoles::System:
    return QStringLiteral("wiiu");
  default:
    return {};
  }
}

void CemuGameModel::setStatus(const QString& status, const QString& error) {
  m_statusText = status;
  m_errorText = error;
  emit statusChanged();
}

#include "tracking/SessionDatabase.h"

#include "library/DatabaseTuning.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

#include <functional>
#include <unistd.h>

namespace {
constexpr int kCurrentSchema = 1;
} // namespace

namespace SessionDatabase {

QString defaultDatabasePath() {
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                            QStringLiteral("/omakade");
  return directory + QStringLiteral("/library.sqlite3");
}

QString defaultConfigPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/omakade/config.toml");
}

QString appServerName() { return QStringLiteral("omakade-%1").arg(getuid()); }

bool open(QSqlDatabase& database, const QString& path, const QString& connectionName) {
  if (path != QStringLiteral(":memory:")) {
    QDir().mkpath(QFileInfo(path).absolutePath());
  }
  database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
  database.setDatabaseName(path);
  if (!openTunedDatabase(database)) {
    return false;
  }
  if (!database.transaction())
    return false;
  if (!ensureSchema(database) || !database.commit()) {
    database.rollback();
    return false;
  }
  return true;
}

bool ensureSchema(QSqlDatabase& database) {
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS play_sessions (id INTEGER PRIMARY KEY, game_path TEXT NOT "
          "NULL, source TEXT NOT NULL DEFAULT '', started_at INTEGER NOT NULL, ended_at INTEGER "
          "NOT "
          "NULL DEFAULT 0, seconds INTEGER NOT NULL DEFAULT 0, pid INTEGER NOT NULL DEFAULT 0, "
          "proc_start INTEGER NOT NULL DEFAULT -1, heartbeat_at INTEGER NOT NULL DEFAULT 0)")))
    return false;
  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS play_sessions_path ON play_sessions(game_path)")))
    return false;
  if (!query.exec(
          QStringLiteral("CREATE TABLE IF NOT EXISTS play_baselines (game_path TEXT PRIMARY "
                         "KEY, baseline_seconds INTEGER NOT NULL DEFAULT 0, captured_at "
                         "INTEGER NOT NULL, schema INTEGER NOT NULL DEFAULT %1)")
              .arg(kCurrentSchema)))
    return false;
  // The recorded-time watermark. Every row needs one before it can be used: an
  // existing installation must read exactly as it does today until its emulator
  // next writes a counter, so the migration sets the watermark to the imported
  // figure that already produced today's total.
  if (!query.exec("PRAGMA table_info(play_baselines)"))
    return false;
  QStringList baselineColumns;
  while (query.next())
    baselineColumns.append(query.value(1).toString());
  query.finish();
  if (!baselineColumns.contains(QStringLiteral("imported_seconds")) &&
      !query.exec("ALTER TABLE play_baselines ADD COLUMN imported_seconds INTEGER NOT NULL DEFAULT -1"))
    return false;
  if (!baselineColumns.contains(QStringLiteral("observed_seconds")) &&
      !query.exec("ALTER TABLE play_baselines ADD COLUMN observed_seconds INTEGER NOT NULL DEFAULT -1"))
    return false;
  if (!query.exec(QStringLiteral(
          "UPDATE play_baselines SET imported_seconds = baseline_seconds, "
          "observed_seconds = (SELECT COALESCE(SUM(seconds), 0) FROM play_sessions "
          "WHERE game_path = play_baselines.game_path) "
          "WHERE imported_seconds < 0")))
    return false;
  if (!query.exec("PRAGMA table_info(play_sessions)"))
    return false;
  bool hasKey = false;
  while (query.next())
    hasKey = hasKey || query.value(1).toString() == "session_key";
  query.finish();
  if (!hasKey && !query.exec("ALTER TABLE play_sessions ADD COLUMN session_key TEXT"))
    return false;
  if (!query.exec("SELECT id FROM play_sessions WHERE session_key IS NULL OR session_key=''"))
    return false;
  QList<qint64> legacy;
  while (query.next())
    legacy.append(query.value(0).toLongLong());
  query.finish();
  for (qint64 id : legacy) {
    query.prepare("UPDATE play_sessions SET session_key=? WHERE id=? AND (session_key IS NULL OR "
                  "session_key='')");
    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.addBindValue(id);
    if (!query.exec())
      return false;
  }
  if (!query.exec(
          "CREATE UNIQUE INDEX IF NOT EXISTS play_sessions_key ON play_sessions(session_key)"))
    return false;
  // A recorder from the previous build may still be running during a local upgrade.
  // Its inserts omit session_key; assign one without changing existing identities.
  return query.exec(
      "CREATE TRIGGER IF NOT EXISTS play_sessions_assign_key AFTER INSERT ON play_sessions "
      "WHEN NEW.session_key IS NULL OR NEW.session_key='' BEGIN "
      "UPDATE play_sessions SET session_key=lower(hex(randomblob(4)))||'-'||"
      "lower(hex(randomblob(2)))||'-'||lower(hex(randomblob(2)))||'-'||"
      "lower(hex(randomblob(2)))||'-'||lower(hex(randomblob(6))) WHERE id=NEW.id; END");
}

QVector<SessionRow> openSessions(QSqlDatabase& database) {
  QVector<SessionRow> rows;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral(
          "SELECT id, game_path, source, started_at, ended_at, seconds, pid, proc_start, "
          "heartbeat_at, session_key FROM play_sessions WHERE ended_at = 0 ORDER BY id"))) {
    return rows;
  }
  while (query.next()) {
    rows.append({.id = query.value(0).toLongLong(),
                 .sessionKey = query.value(9).toString(),
                 .gamePath = query.value(1).toString(),
                 .source = query.value(2).toString(),
                 .startedAt = query.value(3).toLongLong(),
                 .endedAt = query.value(4).toLongLong(),
                 .seconds = query.value(5).toLongLong(),
                 .pid = query.value(6).toLongLong(),
                 .procStart = query.value(7).toLongLong(),
                 .heartbeatAt = query.value(8).toLongLong()});
  }
  return rows;
}

qint64 beginSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                    qint64 startedAt, qint64 pid, qint64 procStart) {
  QSqlQuery query(database);
  query.prepare(
      QStringLiteral("INSERT INTO play_sessions(game_path, source, started_at, pid, "
                     "proc_start, heartbeat_at, session_key) VALUES(?, ?, ?, ?, ?, ?, ?)"));
  query.addBindValue(gamePath);
  query.addBindValue(source);
  query.addBindValue(startedAt);
  query.addBindValue(pid);
  query.addBindValue(procStart);
  query.addBindValue(startedAt);
  query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!query.exec()) {
    return 0;
  }
  return query.lastInsertId().toLongLong();
}

bool updateProgress(QSqlDatabase& database, qint64 id, qint64 seconds, qint64 heartbeatAt) {
  QSqlQuery query(database);
  query.prepare(
      QStringLiteral("UPDATE play_sessions SET seconds = ?, heartbeat_at = ? WHERE id = ?"));
  query.addBindValue(seconds);
  query.addBindValue(heartbeatAt);
  query.addBindValue(id);
  return query.exec() && query.numRowsAffected() == 1;
}

bool endSession(QSqlDatabase& database, qint64 id, qint64 endedAt, qint64 seconds) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("UPDATE play_sessions SET ended_at = ?, seconds = ? WHERE id = ?"));
  query.addBindValue(endedAt);
  query.addBindValue(seconds);
  query.addBindValue(id);
  return query.exec() && query.numRowsAffected() == 1;
}

bool endAllSessions(QSqlDatabase& database, qint64 endedAt) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE play_sessions SET ended_at = ?, seconds = CASE WHEN heartbeat_at > started_at "
      "THEN seconds ELSE 0 END WHERE ended_at = 0"));
  query.addBindValue(endedAt);
  return query.exec();
}

QVector<SessionRow> reconcileOpenSessions(QSqlDatabase& database,
                                          const std::function<bool(qint64, qint64)>& processAlive) {
  QVector<SessionRow> survivors;
  const QVector<SessionRow> open = openSessions(database);
  for (const SessionRow& row : open) {
    const bool alive =
        processAlive && processAlive(row.pid, row.procStart) && row.pid > 0 && row.procStart > 0;
    if (alive) {
      survivors.append(row);
      continue;
    }
    const qint64 endedAt = row.heartbeatAt > row.startedAt ? row.heartbeatAt : row.startedAt;
    if (!endSession(database, row.id, endedAt, row.seconds))
      survivors.append(row);
  }
  return survivors;
}

SessionRow sessionByKey(QSqlDatabase& database, const QString& sessionKey) {
  SessionRow row;
  if (sessionKey.trimmed().isEmpty()) {
    return row;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "SELECT id, game_path, source, started_at, ended_at, seconds, pid, proc_start, "
      "heartbeat_at FROM play_sessions WHERE session_key = ? LIMIT 1"));
  query.addBindValue(sessionKey);
  if (!query.exec() || !query.next()) {
    return row;
  }
  row.id = query.value(0).toLongLong();
  row.sessionKey = sessionKey;
  row.gamePath = query.value(1).toString();
  row.source = query.value(2).toString();
  row.startedAt = query.value(3).toLongLong();
  row.endedAt = query.value(4).toLongLong();
  row.seconds = query.value(5).toLongLong();
  row.pid = query.value(6).toLongLong();
  row.procStart = query.value(7).toLongLong();
  row.heartbeatAt = query.value(8).toLongLong();
  return row;
}

bool deleteSession(QSqlDatabase& database, const QString& sessionKey) {
  if (sessionKey.trimmed().isEmpty()) {
    return false;
  }
  QSqlQuery query(database);
  // ended_at = 0 is a session the recorder is still tracking; a deletion would
  // race the recorder's own flush and leave the row half-forgotten.
  query.prepare(QStringLiteral(
      "DELETE FROM play_sessions WHERE session_key = ? AND ended_at > 0"));
  query.addBindValue(sessionKey);
  return query.exec() && query.numRowsAffected() == 1;
}

int deleteSessionsForPaths(QSqlDatabase& database, const QStringList& gamePaths, int pathLimit) {
  QStringList paths;
  for (const QString& path : gamePaths) {
    const QString clean = path.trimmed();
    if (clean.isEmpty() || clean.size() > 4096 || paths.contains(clean)) {
      continue;
    }
    paths.append(clean);
    if (pathLimit > 0 && paths.size() == pathLimit) {
      break;
    }
  }
  if (paths.isEmpty()) {
    return 0;
  }
  QStringList placeholders;
  for (qsizetype index = 0; index < paths.size(); ++index) {
    placeholders.append(QStringLiteral("?"));
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("DELETE FROM play_sessions WHERE ended_at > 0 AND game_path IN "
                               "(%1)")
                    .arg(placeholders.join(',')));
  for (const QString& path : paths) {
    query.addBindValue(path);
  }
  if (!query.exec()) {
    return -1;
  }
  return query.numRowsAffected();
}

QHash<QString, qint64> trackedSecondsByPath(QSqlDatabase& database) {
  QHash<QString, qint64> totals;
  QSqlQuery query(database);
  if (!query.exec(
          QStringLiteral("SELECT game_path, SUM(seconds) FROM play_sessions GROUP BY game_path"))) {
    return totals;
  }
  while (query.next()) {
    totals.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return totals;
}

QHash<QString, qint64> lastPlayedByPath(QSqlDatabase& database) {
  QHash<QString, qint64> lastPlayed;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral(
          "SELECT game_path, MAX(COALESCE(NULLIF(ended_at, 0), started_at)) FROM play_sessions "
          "GROUP BY game_path"))) {
    return lastPlayed;
  }
  while (query.next()) {
    lastPlayed.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return lastPlayed;
}

void captureBaseline(QSqlDatabase& database, const QString& gamePath, qint64 importedSeconds,
                     qint64 capturedAt) {
  if (gamePath.isEmpty() || importedSeconds < 0) {
    return;
  }
  QSqlQuery query(database);
  // The watermark is stored with the baseline: the imported figure just seen, and
  // the recorded time already visible with it. Both come from the same statement so
  // they can never disagree about what was observed.
  query.prepare(QStringLiteral(
      "INSERT OR IGNORE INTO play_baselines(game_path, baseline_seconds, captured_at, "
      "imported_seconds, observed_seconds) "
      "SELECT ?, MAX(0, ? - COALESCE(SUM(seconds), 0)), ?, ?, COALESCE(SUM(seconds), 0) "
      "FROM play_sessions WHERE game_path = ?"));
  query.addBindValue(gamePath);
  query.addBindValue(importedSeconds);
  query.addBindValue(capturedAt);
  query.addBindValue(importedSeconds);
  query.addBindValue(gamePath);
  query.exec();
}

QHash<QString, qint64> baselinesByPath(QSqlDatabase& database) {
  QHash<QString, qint64> baselines;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT game_path, baseline_seconds FROM play_baselines"))) {
    return baselines;
  }
  while (query.next()) {
    baselines.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
  return baselines;
}

QHash<QString, ImportWatermark> importWatermarksByPath(QSqlDatabase& database) {
  QHash<QString, ImportWatermark> watermarks;
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT game_path, baseline_seconds, imported_seconds, "
                                 "observed_seconds FROM play_baselines"))) {
    return watermarks;
  }
  while (query.next()) {
    ImportWatermark watermark;
    watermark.baselineSeconds = query.value(1).toLongLong();
    watermark.importedSeconds = query.value(2).toLongLong();
    watermark.observedSeconds = query.value(3).toLongLong();
    watermarks.insert(query.value(0).toString(), watermark);
  }
  return watermarks;
}

ImportWatermark watermarkForPath(QSqlDatabase& database, const QString& gamePath) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT baseline_seconds, imported_seconds, observed_seconds "
                               "FROM play_baselines WHERE game_path = ?"));
  query.addBindValue(gamePath);
  ImportWatermark watermark;
  if (!query.exec() || !query.next()) {
    return watermark;
  }
  watermark.baselineSeconds = query.value(0).toLongLong();
  watermark.importedSeconds = query.value(1).toLongLong();
  watermark.observedSeconds = query.value(2).toLongLong();
  return watermark;
}

qint64 reconcileImportedAndTracked(qint64 importedSeconds, qint64 baselineSeconds,
                                   qint64 trackedSeconds, const ImportWatermark& watermark) {
  // No imported counter: recorded time is the whole of it.
  if (importedSeconds < 0) {
    return trackedSeconds;
  }
  // What the sources display today, which the result must never fall below.
  const qint64 displayed = baselineSeconds + trackedSeconds;
  if (watermark.importedSeconds < 0 || watermark.observedSeconds < 0 ||
      importedSeconds != watermark.importedSeconds) {
    // Either nothing has been observed yet, or the counter reads something other
    // than what was last observed. An unobserved figure may already include
    // sessions Omakade recorded, so crediting them again would invent playtime.
    // Fall back to the conservative merge until the counter is next observed.
    return qMax(importedSeconds, displayed);
  }
  // The counter still reads exactly what was last observed, so it cannot include
  // anything recorded since. Play recorded after that observation is genuinely
  // missing from it and is added. Once the emulator writes its own counter on exit
  // the figure changes, the next scan observes it, and the delta returns to zero,
  // so the same session is never counted twice.
  const qint64 sinceImport = qMax<qint64>(0, trackedSeconds - watermark.observedSeconds);
  return qMax(displayed, importedSeconds + sinceImport);
}

ImportWatermark observeImport(QSqlDatabase& database, const QString& gamePath,
                              qint64 importedSeconds, qint64 observedAt) {
  if (importedSeconds < 0 || gamePath.isEmpty()) {
    return ImportWatermark{};
  }
  ImportWatermark watermark = watermarkForPath(database, gamePath);
  if (watermark.importedSeconds == importedSeconds) {
    // The counter has not moved, which is the case on almost every scan. Nothing
    // to record, and no need to read anything else.
    return watermark;
  }
  const qint64 trackedSeconds = trackedSecondsByPath(database).value(gamePath, 0);
  // A first observation captures the baseline the sources display today, so an
  // upgrade cannot move a number by itself. A later observation only moves the
  // watermark: everything the counter has counted is inside the new figure.
  if (watermark.importedSeconds < 0) {
    captureBaseline(database, gamePath, importedSeconds, observedAt);
  } else {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE play_baselines SET imported_seconds = ?, "
                                 "observed_seconds = ? WHERE game_path = ?"));
    query.addBindValue(importedSeconds);
    query.addBindValue(trackedSeconds);
    query.addBindValue(gamePath);
    if (!query.exec()) {
      return watermark;
    }
  }
  return watermarkForPath(database, gamePath);
}

} // namespace SessionDatabase

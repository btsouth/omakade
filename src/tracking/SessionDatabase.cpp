#include "tracking/SessionDatabase.h"

#include "library/DatabaseTuning.h"

#include <QDateTime>
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
          "proc_start INTEGER NOT NULL DEFAULT -1, heartbeat_at INTEGER NOT NULL DEFAULT 0, "
          "provenance TEXT NOT NULL DEFAULT 'aggregate')")))
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
      !query.exec(
          "ALTER TABLE play_baselines ADD COLUMN imported_seconds INTEGER NOT NULL DEFAULT -1"))
    return false;
  if (!baselineColumns.contains(QStringLiteral("observed_seconds")) &&
      !query.exec(
          "ALTER TABLE play_baselines ADD COLUMN observed_seconds INTEGER NOT NULL DEFAULT -1"))
    return false;
  if (!query.exec(
          QStringLiteral("UPDATE play_baselines SET imported_seconds = baseline_seconds, "
                         "observed_seconds = (SELECT COALESCE(SUM(seconds), 0) FROM play_sessions "
                         "WHERE game_path = play_baselines.game_path) "
                         "WHERE imported_seconds < 0")))
    return false;
  if (!query.exec("PRAGMA table_info(play_sessions)"))
    return false;
  bool hasKey = false;
  bool hasProvenance = false;
  while (query.next()) {
    const QString column = query.value(1).toString();
    hasKey = hasKey || column == QStringLiteral("session_key");
    hasProvenance = hasProvenance || column == QStringLiteral("provenance");
  }
  query.finish();
  if (!hasKey && !query.exec("ALTER TABLE play_sessions ADD COLUMN session_key TEXT"))
    return false;
  if (!hasProvenance &&
      !query.exec(
          "ALTER TABLE play_sessions ADD COLUMN provenance TEXT NOT NULL DEFAULT 'aggregate'"))
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
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS session_intervals (session_key TEXT NOT NULL, seq INTEGER "
          "NOT NULL, wall_start INTEGER NOT NULL, wall_end INTEGER NOT NULL, billed_seconds "
          "INTEGER NOT NULL, kind TEXT NOT NULL, PRIMARY KEY(session_key, seq))")))
    return false;
  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS session_intervals_key ON session_intervals(session_key)")))
    return false;
  // Durable-recovery identity. A journal record can outlive the history it describes, so the
  // database carries a random incarnation (rewritten on restore or replacement) and the
  // per-game clear epochs and per-session tombstones that mark removed history.
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS session_journal_state (id INTEGER PRIMARY KEY CHECK(id = "
          "1), incarnation TEXT NOT NULL DEFAULT '')")))
    return false;
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS session_game_clears (game_path TEXT PRIMARY KEY, "
          "epoch INTEGER NOT NULL DEFAULT 0)")))
    return false;
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS session_tombstones (session_key TEXT PRIMARY KEY, "
          "created_at INTEGER NOT NULL DEFAULT 0)")))
    return false;
  // An unshipped intermediate build used generation columns in these two tables. Replace
  // them with the identity/tombstone shape rather than failing every query against them.
  {
    const auto hasColumn = [&query](const QString& table, const QString& column) {
      if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
      while (query.next()) {
        if (query.value(1).toString() == column)
          return true;
      }
      return false;
    };
    if (!hasColumn(QStringLiteral("session_journal_state"), QStringLiteral("journal_owner")) &&
        !query.exec(QStringLiteral(
            "ALTER TABLE session_journal_state ADD COLUMN journal_owner TEXT NOT NULL DEFAULT ''")))
      return false;
    if (!hasColumn(QStringLiteral("session_journal_state"), QStringLiteral("incarnation"))) {
      if (!query.exec(QStringLiteral("DROP TABLE session_journal_state")))
        return false;
      if (!query.exec(QStringLiteral(
              "CREATE TABLE session_journal_state (id INTEGER PRIMARY KEY CHECK(id = 1), "
              "incarnation TEXT NOT NULL DEFAULT '', "
              "journal_owner TEXT NOT NULL DEFAULT '')")))
        return false;
    }
    if (!hasColumn(QStringLiteral("session_tombstones"), QStringLiteral("session_key"))) {
      if (!query.exec(QStringLiteral("DROP TABLE session_tombstones")))
        return false;
      if (!query.exec(QStringLiteral(
              "CREATE TABLE session_tombstones (session_key TEXT PRIMARY KEY, created_at "
              "INTEGER NOT NULL DEFAULT 0)")))
        return false;
    }
    query.finish();
  }
  // A recorder from the previous build may still be running during a local upgrade.
  // Its inserts omit session_key; assign one without changing existing identities.
  return query.exec(
      "CREATE TRIGGER IF NOT EXISTS play_sessions_assign_key AFTER INSERT ON play_sessions "
      "WHEN NEW.session_key IS NULL OR NEW.session_key='' BEGIN "
      "UPDATE play_sessions SET session_key=lower(hex(randomblob(4)))||'-'||"
      "lower(hex(randomblob(2)))||'-'||lower(hex(randomblob(2)))||'-'||"
      "lower(hex(randomblob(2)))||'-'||lower(hex(randomblob(6))) WHERE id=NEW.id; END");
}

int openSessionsForPath(QSqlDatabase& database, const QString& gamePath) {
  QSqlQuery query(database);
  query.prepare(
      QStringLiteral("SELECT COUNT(*) FROM play_sessions WHERE ended_at = 0 AND game_path = ?"));
  query.addBindValue(gamePath);
  if (!query.exec() || !query.next()) {
    // An unreadable count is not a reason to refuse to record an observation. Reading
    // zero only means the watermark may advance, which the next scan corrects.
    return 0;
  }
  return query.value(0).toInt();
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
                    qint64 startedAt, qint64 pid, qint64 procStart, const QString& sessionKey) {
  const QString key =
      sessionKey.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : sessionKey;
  QSqlQuery query(database);
  // A retry after a partial success must resolve to the row that already exists rather than
  // failing on the unique key, or the recorder would keep treating a written session as
  // unwritten.
  query.prepare(
      QStringLiteral("INSERT OR IGNORE INTO play_sessions(game_path, source, started_at, pid, "
                     "proc_start, heartbeat_at, session_key) VALUES(?, ?, ?, ?, ?, ?, ?)"));
  query.addBindValue(gamePath);
  query.addBindValue(source);
  query.addBindValue(startedAt);
  query.addBindValue(pid);
  query.addBindValue(procStart);
  query.addBindValue(startedAt);
  query.addBindValue(key);
  if (!query.exec()) {
    return 0;
  }
  // lastInsertId is only meaningful when this statement actually inserted. After an ignored
  // duplicate it still refers to the last successful insert on this connection, which can be a
  // different session, so the row is resolved by its stable key instead.
  if (query.numRowsAffected() == 1) {
    const qint64 inserted = query.lastInsertId().toLongLong();
    if (inserted > 0) {
      return inserted;
    }
  }
  QSqlQuery existing(database);
  existing.prepare(QStringLiteral("SELECT id FROM play_sessions WHERE session_key = ? LIMIT 1"));
  existing.addBindValue(key);
  if (!existing.exec() || !existing.next()) {
    return 0;
  }
  return existing.value(0).toLongLong();
}

bool insertClosedSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                         qint64 startedAt, qint64 endedAt, qint64 seconds, qint64 pid,
                         qint64 procStart, const QString& sessionKey) {
  // A recovered session must never claim to have ended before it began, but it must also
  // not be dropped: the play total comes from a monotonic clock, so it is real even when
  // the wall clock stepped backwards (a suspend, an NTP correction), and a refused entry
  // would sit in the retry queue notifying for the rest of the daemon's life. An end
  // before the start is clamped; only a record that makes no sense at all is refused.
  if (gamePath.isEmpty() || startedAt <= 0 || seconds < 0) {
    return false;
  }
  endedAt = qMax(endedAt, startedAt);
  const QString key =
      sessionKey.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : sessionKey;
  QSqlQuery query(database);
  // INSERT OR IGNORE makes a replay with the same stable key a no-op, so a crash between
  // the database commit and the journal acknowledgment cannot double a session.
  query.prepare(
      QStringLiteral("INSERT OR IGNORE INTO play_sessions(game_path, source, started_at, ended_at, "
                     "seconds, heartbeat_at, pid, proc_start, session_key) "
                     "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
  query.addBindValue(gamePath);
  query.addBindValue(source);
  query.addBindValue(startedAt);
  query.addBindValue(endedAt);
  query.addBindValue(seconds);
  query.addBindValue(endedAt);
  query.addBindValue(pid);
  query.addBindValue(procStart);
  query.addBindValue(key);
  if (!query.exec()) {
    return false;
  }
  if (query.numRowsAffected() == 1) {
    return true;
  }
  // A row with this key already exists. If a close was journaled for a session that had a
  // row, finish that row instead of leaving it open forever.
  return endSessionByKey(database, key, endedAt, seconds);
}

bool endSessionByKey(QSqlDatabase& database, const QString& sessionKey, qint64 endedAt,
                     qint64 seconds) {
  if (sessionKey.isEmpty()) {
    return false;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE play_sessions SET ended_at = ?, seconds = ? WHERE session_key = ? AND ended_at = 0"));
  query.addBindValue(endedAt);
  query.addBindValue(seconds);
  query.addBindValue(sessionKey);
  if (!query.exec()) {
    return false;
  }
  if (query.numRowsAffected() == 1) {
    return true;
  }
  // Nothing to close means the row is already in the requested final state, which a replay
  // must accept as success rather than retry forever.
  QSqlQuery existing(database);
  existing.prepare(
      QStringLiteral("SELECT ended_at, seconds FROM play_sessions WHERE session_key = ? LIMIT 1"));
  existing.addBindValue(sessionKey);
  return existing.exec() && existing.next() && existing.value(0).toLongLong() == endedAt &&
         existing.value(1).toLongLong() == seconds;
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

bool moveSession(QSqlDatabase& database, qint64 id, qint64 pid, qint64 procStart) {
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE play_sessions SET pid = ?, proc_start = ? WHERE id = ? AND ended_at = 0"));
  query.addBindValue(pid);
  query.addBindValue(procStart);
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

bool replaceSessionIntervals(QSqlDatabase& database, const QString& sessionKey,
                             const QVector<SessionInterval>& intervals) {
  if (sessionKey.isEmpty() || intervals.size() > 2048)
    return false;
  if (intervals.isEmpty())
    return true;
  qint64 billed = 0;
  for (const SessionInterval& interval : intervals) {
    if (interval.wallStart <= 0 || interval.wallEnd < interval.wallStart ||
        interval.billedSeconds < 0 ||
        (interval.kind != QStringLiteral("playing") &&
         interval.kind != QStringLiteral("paused"))) {
      return false;
    }
    billed += interval.billedSeconds;
  }
  if (!database.transaction())
    return false;
  QSqlQuery row(database);
  row.prepare(QStringLiteral("SELECT seconds FROM play_sessions WHERE session_key = ? LIMIT 1"));
  row.addBindValue(sessionKey);
  if (!row.exec() || !row.next() || row.value(0).toLongLong() != billed) {
    database.rollback();
    return false;
  }
  QSqlQuery remove(database);
  remove.prepare(QStringLiteral("DELETE FROM session_intervals WHERE session_key = ?"));
  remove.addBindValue(sessionKey);
  if (!remove.exec()) {
    database.rollback();
    return false;
  }
  QSqlQuery insert(database);
  insert.prepare(QStringLiteral(
      "INSERT INTO session_intervals(session_key, seq, wall_start, wall_end, billed_seconds, "
      "kind) VALUES(?, ?, ?, ?, ?, ?)"));
  for (int index = 0; index < intervals.size(); ++index) {
    const SessionInterval& interval = intervals.at(index);
    insert.bindValue(0, sessionKey);
    insert.bindValue(1, index);
    insert.bindValue(2, interval.wallStart);
    insert.bindValue(3, interval.wallEnd);
    insert.bindValue(4, interval.billedSeconds);
    insert.bindValue(5, interval.kind);
    if (!insert.exec()) {
      database.rollback();
      return false;
    }
  }
  QSqlQuery provenance(database);
  provenance.prepare(
      QStringLiteral("UPDATE play_sessions SET provenance = 'observed' WHERE session_key = ?"));
  provenance.addBindValue(sessionKey);
  if (!provenance.exec() || !database.commit()) {
    database.rollback();
    return false;
  }
  return true;
}

QVector<SessionInterval> sessionIntervals(QSqlDatabase& database, const QString& sessionKey) {
  QVector<SessionInterval> intervals;
  if (sessionKey.isEmpty())
    return intervals;
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "SELECT wall_start, wall_end, billed_seconds, kind FROM session_intervals WHERE session_key "
      "= ? ORDER BY seq"));
  query.addBindValue(sessionKey);
  if (!query.exec())
    return intervals;
  while (query.next()) {
    intervals.append(SessionInterval{.wallStart = query.value(0).toLongLong(),
                                     .wallEnd = query.value(1).toLongLong(),
                                     .billedSeconds = query.value(2).toLongLong(),
                                     .kind = query.value(3).toString()});
  }
  return intervals;
}

ReplayOutcome finalizeSession(QSqlDatabase& database, qint64 id, const QString& sessionKey,
                              qint64 endedAt, qint64 seconds) {
  if (id <= 0 || !database.transaction())
    return ReplayOutcome::Error;
  bool lookupSucceeded = false;
  const SessionRow row = sessionByKey(database, sessionKey, &lookupSucceeded);
  if (!lookupSucceeded || row.id != id) {
    database.rollback();
    return !lookupSucceeded ? ReplayOutcome::Error : ReplayOutcome::Stale;
  }
  if (row.endedAt != 0) {
    if (!database.commit()) {
      database.rollback();
      return ReplayOutcome::Error;
    }
    return ReplayOutcome::Written;
  }
  // The original close boundary is authoritative. Progress is monotonic because a newer
  // checkpoint may have committed between the failed close and this retry.
  const qint64 finalSeconds = qMax(row.seconds, seconds);
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE play_sessions SET ended_at = ?, seconds = ? WHERE id = ? AND ended_at = 0"));
  query.addBindValue(qMax(row.startedAt, endedAt));
  query.addBindValue(finalSeconds);
  query.addBindValue(id);
  if (!query.exec() || query.numRowsAffected() != 1 || !database.commit()) {
    database.rollback();
    return ReplayOutcome::Error;
  }
  return ReplayOutcome::Written;
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

SessionRow sessionByKey(QSqlDatabase& database, const QString& sessionKey, bool* lookupSucceeded) {
  SessionRow row;
  if (lookupSucceeded)
    *lookupSucceeded = false;
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
  if (lookupSucceeded)
    *lookupSucceeded = true;
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

// Deletes one session and lowers the game's recorded-time watermark by the time that
// was removed. The watermark is an absolute total, so a deletion that left it alone
// would sit above the recorded total forever, and the credit max(0, tracked -
// watermark) would then show none of the play that happened after the deletion. Only
// the watermark is adjusted: the baseline is a historical figure and must not move.
bool deleteSession(QSqlDatabase& database, const QString& sessionKey) {
  if (sessionKey.trimmed().isEmpty()) {
    return false;
  }
  if (!database.transaction())
    return false;
  QString gamePath;
  qint64 removedSeconds = 0;
  {
    QSqlQuery read(database);
    read.prepare(QStringLiteral("SELECT game_path, seconds FROM play_sessions "
                                "WHERE session_key = ? AND ended_at > 0"));
    read.addBindValue(sessionKey);
    if (!read.exec() || !read.next()) {
      database.rollback();
      return false;
    }
    gamePath = read.value(0).toString();
    removedSeconds = read.value(1).toLongLong();
  }
  QSqlQuery query(database);
  // ended_at = 0 is a session the recorder is still tracking; a deletion would
  // race the recorder's own flush and leave the row half-forgotten.
  query.prepare(QStringLiteral("DELETE FROM play_sessions WHERE session_key = ? AND ended_at > 0"));
  query.addBindValue(sessionKey);
  if (!query.exec() || query.numRowsAffected() != 1) {
    database.rollback();
    return false;
  }
  QSqlQuery intervals(database);
  intervals.prepare(QStringLiteral("DELETE FROM session_intervals WHERE session_key = ?"));
  intervals.addBindValue(sessionKey);
  if (!intervals.exec()) {
    database.rollback();
    return false;
  }
  // Deleting one session tombstones only that key. A different pending session for the same
  // game stays valid; only a whole-game clear invalidates every record for the game.
  if (!lowerObservedWatermark(database, gamePath, removedSeconds) ||
      !tombstoneSession(database, sessionKey) || !database.commit()) {
    database.rollback();
    return false;
  }
  return true;
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
  if (!database.transaction())
    return -1;
  QStringList placeholders;
  for (qsizetype index = 0; index < paths.size(); ++index) {
    placeholders.append(QStringLiteral("?"));
  }
  // The recorded-time watermark is an absolute total, so deleting sessions without
  // lowering it would leave the watermark above the recorded total forever. The credit
  // is max(0, tracked - watermark), so the game would then show none of the play that
  // happened after the deletion, permanently. Read the time being removed first and
  // take it off the watermark, which is what keeps a deletion from suppressing future
  // playtime.
  QHash<QString, qint64> removed;
  QStringList removedKeys;
  {
    QSqlQuery read(database);
    read.prepare(QStringLiteral("SELECT game_path, COALESCE(SUM(seconds), 0) FROM play_sessions "
                                "WHERE ended_at > 0 AND game_path IN (%1) GROUP BY game_path")
                     .arg(placeholders.join(',')));
    for (const QString& path : paths) {
      read.addBindValue(path);
    }
    if (!read.exec()) {
      database.rollback();
      return -1;
    }
    while (read.next()) {
      removed.insert(read.value(0).toString(), read.value(1).toLongLong());
    }
    if (read.lastError().isValid()) {
      database.rollback();
      return -1;
    }
    QSqlQuery keys(database);
    keys.prepare(QStringLiteral(
        "SELECT session_key FROM play_sessions WHERE ended_at > 0 AND game_path IN (%1)")
                     .arg(placeholders.join(',')));
    for (const QString& path : paths) {
      keys.addBindValue(path);
    }
    if (!keys.exec()) {
      database.rollback();
      return -1;
    }
    while (keys.next()) {
      removedKeys.append(keys.value(0).toString());
    }
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("DELETE FROM play_sessions WHERE ended_at > 0 AND game_path IN "
                               "(%1)")
                    .arg(placeholders.join(',')));
  for (const QString& path : paths) {
    query.addBindValue(path);
  }
  if (!query.exec()) {
    database.rollback();
    return -1;
  }
  const int deleted = query.numRowsAffected();
  if (!removedKeys.isEmpty()) {
    QStringList keyPlaces;
    for (const QString& key : removedKeys) {
      Q_UNUSED(key);
      keyPlaces.append(QStringLiteral("?"));
    }
    QSqlQuery intervals(database);
    intervals.prepare(
        QStringLiteral("DELETE FROM session_intervals WHERE session_key IN (%1)")
            .arg(keyPlaces.join(',')));
    for (const QString& key : removedKeys) {
      intervals.addBindValue(key);
    }
    if (!intervals.exec()) {
      database.rollback();
      return -1;
    }
  }
  for (auto entry = removed.cbegin(); entry != removed.cend(); ++entry) {
    if (!lowerObservedWatermark(database, entry.key(), entry.value())) {
      database.rollback();
      return -1;
    }
  }
  // Move the clear boundary for every requested path, not only the ones that still had rows.
  // A game whose only history was a refused write sits in the journal with no row at all;
  // clearing it has to invalidate that record too, or the next replay would resurrect the
  // play the user just removed.
  for (const QString& path : paths) {
    if (!bumpGameClearEpoch(database, path)) {
      database.rollback();
      return -1;
    }
  }
  if (!database.commit()) {
    database.rollback();
    return -1;
  }
  return deleted;
}

// Takes recorded time off a game's watermark after that much history was deleted.
// Only the watermark moves: the baseline is a historical figure. A watermark of -1
// means unobserved and must stay that way, and the result never goes below zero,
// because a negative watermark would claim time was seen that never was.
bool lowerObservedWatermark(QSqlDatabase& database, const QString& gamePath, qint64 seconds) {
  if (gamePath.isEmpty() || seconds <= 0) {
    return true;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("UPDATE play_baselines SET observed_seconds = MAX(0, "
                               "observed_seconds - ?) WHERE game_path = ? AND "
                               "observed_seconds >= 0"));
  query.addBindValue(seconds);
  query.addBindValue(gamePath);
  return query.exec();
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
  // upgrade cannot move a number by itself.
  if (watermark.importedSeconds < 0) {
    captureBaseline(database, gamePath, importedSeconds, observedAt);
    return watermarkForPath(database, gamePath);
  }
  // An observation that lands while a session for this game is still open cannot be
  // trusted. The emulator writes its counter when the game exits, and the recorder
  // closes the session a few seconds later, so a scan in that window sees a counter
  // that already includes the session next to a recorded total that does not. Pinning
  // the watermark there would credit that session's unflushed seconds a second time,
  // because the later close makes the recorded total exceed the observed one. Nothing
  // is lost by waiting: the counter does not move again until the next session ends,
  // and that observation happens with no session open.
  if (openSessionsForPath(database, gamePath) > 0) {
    return watermark;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("UPDATE play_baselines SET imported_seconds = ?, "
                               "observed_seconds = ? WHERE game_path = ?"));
  query.addBindValue(importedSeconds);
  query.addBindValue(trackedSeconds);
  query.addBindValue(gamePath);
  if (!query.exec()) {
    return watermark;
  }
  return watermarkForPath(database, gamePath);
}

QString resetJournalIncarnation(QSqlDatabase& database) {
  const QString value = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QSqlQuery query(database);
  query.prepare(QStringLiteral("INSERT INTO session_journal_state(id, incarnation) VALUES(1, ?) "
                               "ON CONFLICT(id) DO UPDATE SET incarnation = excluded.incarnation, "
                               "journal_owner = excluded.incarnation"));
  query.addBindValue(value);
  if (!query.exec()) {
    return {};
  }
  return value;
}

QString journalOwnership(QSqlDatabase& database) {
  QSqlQuery query(database);
  if (!query.exec(QStringLiteral("SELECT journal_owner FROM session_journal_state WHERE id = 1")) ||
      !query.next()) {
    return {};
  }
  return query.value(0).toString();
}

bool bindJournalOwnership(QSqlDatabase& database, const QString& ownerToken) {
  if (ownerToken.isEmpty())
    return false;
  QSqlQuery query(database);
  query.prepare(QStringLiteral(
      "UPDATE session_journal_state SET journal_owner = ? WHERE id = 1 AND journal_owner = ''"));
  query.addBindValue(ownerToken);
  return query.exec() && query.numRowsAffected() == 1;
}

QString journalIncarnation(QSqlDatabase& database) {
  QSqlQuery query(database);
  if (query.exec(QStringLiteral("SELECT incarnation FROM session_journal_state WHERE id = 1")) &&
      query.next()) {
    const QString value = query.value(0).toString();
    if (!value.isEmpty()) {
      return value;
    }
  }
  // A database with no identity yet takes a fresh one. This is what makes a replaced or
  // restored database reject records written for a previous incarnation instead of trusting a
  // counter that the replacement could have carried over.
  return resetJournalIncarnation(database);
}

qint64 gameClearEpoch(QSqlDatabase& database, const QString& gamePath) {
  if (gamePath.isEmpty()) {
    return 0;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT epoch FROM session_game_clears WHERE game_path = ?"));
  query.addBindValue(gamePath);
  if (!query.exec() || !query.next()) {
    return 0;
  }
  return query.value(0).toLongLong();
}

bool bumpGameClearEpoch(QSqlDatabase& database, const QString& gamePath) {
  if (gamePath.isEmpty()) {
    return true;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("INSERT INTO session_game_clears(game_path, epoch) VALUES(?, 1) "
                               "ON CONFLICT(game_path) DO UPDATE SET epoch = epoch + 1"));
  query.addBindValue(gamePath);
  return query.exec();
}

bool tombstoneSession(QSqlDatabase& database, const QString& sessionKey) {
  if (sessionKey.isEmpty()) {
    return true;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("INSERT OR REPLACE INTO session_tombstones(session_key, created_at) "
                               "VALUES(?, strftime('%s','now'))"));
  query.addBindValue(sessionKey);
  return query.exec();
}

bool sessionTombstoned(QSqlDatabase& database, const QString& sessionKey) {
  if (sessionKey.isEmpty()) {
    return false;
  }
  QSqlQuery query(database);
  query.prepare(QStringLiteral("SELECT 1 FROM session_tombstones WHERE session_key = ? LIMIT 1"));
  query.addBindValue(sessionKey);
  return query.exec() && query.next();
}

namespace {
// The identity and tombstone guard, run inside the caller's transaction so a concurrent
// deletion cannot race the recorder into resurrecting removed history. A read that fails
// (a locked or busy database) is an Error, never Stale: a temporary failure must let the
// operation be retried, not discard it as if the database had been replaced.
SessionDatabase::ReplayOutcome guardReplay(QSqlDatabase& database, const QString& sessionKey,
                                           const QString& gamePath, QString incarnation,
                                           qint64 clearEpoch, const QString& ownerToken) {
  if (sessionKey.isEmpty()) {
    return SessionDatabase::ReplayOutcome::Stale;
  }
  QString current;
  QString currentOwner;
  {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT incarnation, journal_owner FROM session_journal_state WHERE id = 1"))) {
      return SessionDatabase::ReplayOutcome::Error;
    }
    if (query.next()) {
      current = query.value(0).toString();
      currentOwner = query.value(1).toString();
    }
  }
  if (incarnation.isEmpty()) {
    // No identity could be read when this was accepted. Anonymous is ambiguous, not stale:
    // it may be owned by the current journal after an outage, or belong to history removed by
    // a restore. The durable owner binding is the only proof that distinguishes those states.
    if (ownerToken.isEmpty()) {
      return SessionDatabase::ReplayOutcome::Stale;
    } else if (currentOwner.isEmpty()) {
      // Ownership has not yet been bound. Do not guess: keep the accepted record retryable.
      return SessionDatabase::ReplayOutcome::Error;
    } else if (currentOwner != ownerToken) {
      return SessionDatabase::ReplayOutcome::Stale;
    }
    incarnation = current;
  } else if (current.isEmpty() || current != incarnation) {
    return SessionDatabase::ReplayOutcome::Stale;
  }
  {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT epoch FROM session_game_clears WHERE game_path = ?"));
    query.addBindValue(gamePath);
    if (!query.exec()) {
      return SessionDatabase::ReplayOutcome::Error;
    }
    if (query.next() && query.value(0).toLongLong() > clearEpoch) {
      return SessionDatabase::ReplayOutcome::Stale;
    }
  }
  {
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM session_tombstones WHERE session_key = ? LIMIT 1"));
    query.addBindValue(sessionKey);
    if (!query.exec()) {
      return SessionDatabase::ReplayOutcome::Error;
    }
    if (query.next()) {
      return SessionDatabase::ReplayOutcome::Stale;
    }
  }
  return SessionDatabase::ReplayOutcome::Written;
}
} // namespace

ReplayOutcome replayClosedSession(QSqlDatabase& database, const QString& sessionKey,
                                  const QString& gamePath, const QString& source, qint64 startedAt,
                                  qint64 endedAt, qint64 seconds, qint64 pid, qint64 procStart,
                                  QString incarnation, qint64 clearEpoch,
                                  const QString& ownerToken,
                                  const QVector<SessionInterval>& intervals) {
  if (!database.transaction()) {
    return ReplayOutcome::Error;
  }
  const ReplayOutcome guard = guardReplay(database, sessionKey, gamePath, incarnation, clearEpoch,
                                          incarnation.isEmpty() ? ownerToken : QString());
  if (guard != ReplayOutcome::Written) {
    database.rollback();
    return guard;
  }
  if (!insertClosedSession(database, gamePath, source, startedAt, endedAt, seconds, pid, procStart,
                           sessionKey)) {
    database.rollback();
    return ReplayOutcome::Error;
  }
  if (!database.commit()) {
    database.rollback();
    return ReplayOutcome::Error;
  }
  if (!intervals.isEmpty() && !replaceSessionIntervals(database, sessionKey, intervals)) {
    return ReplayOutcome::Error;
  }
  return ReplayOutcome::Written;
}

ReplayOutcome replayOpenSession(QSqlDatabase& database, const QString& sessionKey,
                                const QString& gamePath, const QString& source, qint64 startedAt,
                                qint64 seconds, qint64 pid, qint64 procStart, QString incarnation,
                                qint64 clearEpoch, qint64 observedAt, qint64 closeAt,
                                qint64 closeSeconds, const QString& ownerToken,
                                const QVector<SessionInterval>& intervals) {
  if (!database.transaction()) {
    return ReplayOutcome::Error;
  }
  const ReplayOutcome guard = guardReplay(database, sessionKey, gamePath, incarnation, clearEpoch,
                                          incarnation.isEmpty() ? ownerToken : QString());
  if (guard != ReplayOutcome::Written) {
    database.rollback();
    return guard;
  }
  const SessionRow existing = sessionByKey(database, sessionKey);
  if (existing.id > 0) {
    // Already finished by a close that replayed first: keep it as it is.
    if (existing.endedAt != 0) {
      database.commit();
      return ReplayOutcome::Written;
    }
    // An older checkpoint or a checkpoint replayed after newer progress was written must not
    // lower the recorded seconds, and the heartbeat is the observation time, not the replay
    // instant, so recovering cannot invent play time after the last observation.
    const qint64 mergedSeconds = qMax(existing.seconds, seconds);
    const qint64 heartbeat = qMax(existing.heartbeatAt, observedAt);
    if (!updateProgress(database, existing.id, mergedSeconds, heartbeat)) {
      database.rollback();
      return ReplayOutcome::Error;
    }
  } else {
    const qint64 id =
        beginSession(database, gamePath, source, startedAt, pid, procStart, sessionKey);
    const qint64 heartbeat = qMax(startedAt, observedAt);
    if (id <= 0 || !updateProgress(database, id, seconds, heartbeat)) {
      database.rollback();
      return ReplayOutcome::Error;
    }
  }
  if (closeAt > 0) {
    const SessionRow row = sessionByKey(database, sessionKey);
    if (row.id <= 0) {
      database.rollback();
      return ReplayOutcome::Error;
    }
    const qint64 finalSeconds = qMax(row.seconds, closeSeconds);
    if (!endSessionByKey(database, sessionKey, qMax(row.heartbeatAt, qMax(row.startedAt, closeAt)),
                         finalSeconds)) {
      database.rollback();
      return ReplayOutcome::Error;
    }
  }
  if (!database.commit()) {
    database.rollback();
    return ReplayOutcome::Error;
  }
  if (!intervals.isEmpty() && !replaceSessionIntervals(database, sessionKey, intervals)) {
    return ReplayOutcome::Error;
  }
  return ReplayOutcome::Written;
}

} // namespace SessionDatabase

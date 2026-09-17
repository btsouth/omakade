#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>

// Session storage shared by Omakade and the omakade-sessiond recorder. Both open
// the same library database, so every function takes the caller's connection and
// the schema is created idempotently.
namespace SessionDatabase {

// One recorded play session. Sessions stay open with ended_at = 0 while the game
// process lives; seconds is the wall time accumulated so far and is flushed
// periodically so a crash loses at most one flush interval.
struct SessionRow {
  qint64 id = 0;
  // Stable identity that survives backup and restore, unlike the row id.
  QString sessionKey;
  QString gamePath;
  QString source;
  qint64 startedAt = 0;
  qint64 endedAt = 0;
  qint64 seconds = 0;
  qint64 pid = 0;
  qint64 procStart = -1;
  qint64 heartbeatAt = 0;
};

[[nodiscard]] QString defaultDatabasePath();
[[nodiscard]] QString defaultConfigPath();
[[nodiscard]] QString appServerName();

// Opens (or reuses) a tuned connection to the library database and creates the
// session tables. Returns false when opening or preparing the schema fails.
bool open(QSqlDatabase& database, const QString& path, const QString& connectionName);
bool ensureSchema(QSqlDatabase& database);

QVector<SessionRow> openSessions(QSqlDatabase& database);
qint64 beginSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                    qint64 startedAt, qint64 pid, qint64 procStart);
// Writes a session that is already over, used when storage refused the insert while the
// game was running: the row is written afterwards with its original boundaries, so a
// lasting write failure delays a session instead of losing it. A record with no game path,
// no start, or a negative total is refused. An end before the start is clamped, because the
// play total is monotonic and the wall clock is not.
bool insertClosedSession(QSqlDatabase& database, const QString& gamePath, const QString& source,
                         qint64 startedAt, qint64 endedAt, qint64 seconds, qint64 pid,
                         qint64 procStart);
bool updateProgress(QSqlDatabase& database, qint64 id, qint64 seconds, qint64 heartbeatAt);
bool endSession(QSqlDatabase& database, qint64 id, qint64 endedAt, qint64 seconds);
bool endAllSessions(QSqlDatabase& database, qint64 endedAt);

// Reads one recorded session by its stable key. An unknown key yields a row with
// id 0 so callers can refuse the request instead of guessing.
[[nodiscard]] SessionRow sessionByKey(QSqlDatabase& database, const QString& sessionKey);

// Removes one closed session. A session the recorder is still tracking
// (ended_at = 0) is refused, so history deletion can never orphan a live game.
// Imported playtime and captured baselines are never touched: deleting recorded
// time can only lower the displayed total toward what the emulator itself
// reports, never invent it.
bool deleteSession(QSqlDatabase& database, const QString& sessionKey);

// Removes the closed sessions of the given game paths. Returns how many rows
// went, or -1 when the delete failed. pathLimit caps how many paths are honoured;
// -1 means every path the caller passed, which is what a deliberate clear of one
// game's history needs.
int deleteSessionsForPaths(QSqlDatabase& database, const QStringList& gamePaths,
                           int pathLimit = 32);

// Closes open sessions whose tracked process is gone, using the last heartbeat as
// the end time so a dead daemon never invents play time. Returns the survivors.
QVector<SessionRow>
reconcileOpenSessions(QSqlDatabase& database,
                      const std::function<bool(qint64 pid, qint64 procStart)>& processAlive);

[[nodiscard]] QHash<QString, qint64> trackedSecondsByPath(QSqlDatabase& database);
[[nodiscard]] QHash<QString, qint64> lastPlayedByPath(QSqlDatabase& database);

// Capture once, including zero. Subtract already observed time conservatively: a late
// first import may already contain those sessions. Only baseline_seconds is written
// here and it is never rewritten; the recorded-time watermark on the same row moves as
// the emulator's counter is observed.
void captureBaseline(QSqlDatabase& database, const QString& gamePath, qint64 importedSeconds,
                     qint64 capturedAt);
[[nodiscard]] QHash<QString, qint64> baselinesByPath(QSqlDatabase& database);

// How many sessions for this game are still open. An import observation is only
// trustworthy when none are, because an open session's unflushed time is not yet in
// the recorded total the watermark stores.
[[nodiscard]] int openSessionsForPath(QSqlDatabase& database, const QString& gamePath);

// Takes recorded time off a game's recorded-time watermark after that much history was
// deleted from it. Without this, a deletion leaves the watermark above the recorded
// total forever and the game stops showing the play that happens afterwards.
void lowerObservedWatermark(QSqlDatabase& database, const QString& gamePath, qint64 seconds);

// One game's import watermark: the imported figure last observed and the recorded
// time that had already been seen at that point. Recorded time beyond it is new
// play the imported counter cannot know about yet.
struct ImportWatermark {
  qint64 importedSeconds = -1;
  qint64 observedSeconds = -1;
  // The figure the sources display today, which is what a first observation
  // reproduces so an upgrade never moves a number on its own.
  qint64 baselineSeconds = 0;
};

[[nodiscard]] QHash<QString, ImportWatermark> importWatermarksByPath(QSqlDatabase& database);

// Reconciles an imported counter with recorded sessions, returning the total to
// show. A negative import means the source has no counter of its own, so recorded
// time is the whole of it. baseline + tracked is what the sources display today;
// the watermark adds only recorded time that a stale imported figure could not
// already include, so the total can rise when genuinely new play is recorded and
// never double-counts a session the emulator has since written into its counter.
[[nodiscard]] qint64 reconcileImportedAndTracked(qint64 importedSeconds, qint64 baselineSeconds,
                                                 qint64 trackedSeconds,
                                                 const ImportWatermark& watermark);

// Records the imported figure just observed, together with the recorded time
// already stored with it. The recorded total is read from the database rather
// than taken from the caller, because a caller's cached total can lag a session
// that was just written, and a watermark that understates it makes time the
// import already includes look new. Returns the watermark to use now.
[[nodiscard]] ImportWatermark observeImport(QSqlDatabase& database, const QString& gamePath,
                                            qint64 importedSeconds, qint64 observedAt);

} // namespace SessionDatabase

#pragma once

#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionJournal.h"

#include <QHash>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>

// Turns emulator process sightings into play_sessions rows. One recorder owns
// the active sessions of one database connection. Elapsed time comes from a
// monotonic clock, so suspended wall time is never billed as play time, and
// every row is flushed periodically so a crash loses at most one interval.
class SessionRecorder final {
public:
  // elapsedMs must return monotonic milliseconds. The default uses the process
  // start; tests inject a controllable clock. The durable journal lives beside the
  // database file; it is disabled for an in-memory database, and journalPath overrides
  // the location for tests.
  explicit SessionRecorder(const QSqlDatabase& database,
                           const std::function<qint64()>& elapsedMs = {},
                           const QString& journalPath = {});
  ~SessionRecorder();

  void setFlushIntervalMs(int intervalMs);

  // Startup: closes open sessions whose process is gone (at the last heartbeat),
  // adopts survivors still running the same game, and closes any that now run a
  // different game.
  // `launched` adds matches found by other means than the profiles, such as a game's
  // Wine prefix.
  void recover(const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
               qint64 nowWall, const QVector<SessionMatch>& launched = {});

  // One poll: opens sessions for new matches, extends live ones, and closes
  // sessions whose process disappeared.
  //
  // When pauseUnfocused is on and `unfocused` is supplied, a session whose window
  // is not the compositor's focused one stops accumulating time. The mark still
  // moves forward, so the unfocused span is never billed retroactively when the
  // game comes back, and the periodic flush keeps the heartbeat current so a crash
  // during a pause still ends the row where it was last known to be playing.
  void sync(const QVector<SessionMatch>& matches, qint64 nowWall,
            const std::function<bool(qint64 pid)>& unfocused = {});

  // Bills play time only while the game keeps the compositor's focus. Off by
  // default; without a compositor the predicate is never supplied and nothing
  // changes.
  void setPauseUnfocused(bool value) { m_pauseUnfocused = value; }

  // Closes everything, used when tracking is switched off.
  void endAll(qint64 nowWall);

  // Omakade sources that should rescan, one entry per ended session with a
  // rescan mapping, deduplicated since the previous call.
  [[nodiscard]] QStringList takeRescanRequests();

  bool takeStorageFailure() { return std::exchange(m_storageFailure, false); }
  // Pending writes that storage refused: refused closes, and finished sessions whose
  // insert was refused while the game was still running. This is the in-memory retry
  // queue, bounded at 64; the durable journal holds every accepted operation, so a
  // queue entry past the cap is delayed, not lost.
  [[nodiscard]] int pendingCloseCount() const { return m_pendingCloses.size(); }

  // Durable recovery status. The journal refuses new records at its cap and reports that
  // once through takeJournalCapacityWarning, so the interface can warn without the
  // recorder silently dropping accepted work. A corrupt journal is set aside once and
  // reported through takeJournalCorruptRecovered.
  [[nodiscard]] bool journalAvailable() const {
    return m_journal != nullptr && m_journal->available();
  }
  bool takeJournalCapacityWarning() { return std::exchange(m_journalCapacity, false); }
  bool takeJournalCorruptRecovered() { return std::exchange(m_journalCorrupt, false); }

  [[nodiscard]] int activeCount() const { return static_cast<int>(m_active.size()); }

  // The sessions being tracked right now, in the order they started. Discord
  // presence and anything else that needs to describe the running game reads this
  // rather than the database, so it always agrees with what is being recorded.
  struct ActiveInfo {
    QString gamePath;
    QString emulator;
    qint64 startedAt = 0;
    qint64 elapsedMs = 0;
    bool paused = false;
  };
  [[nodiscard]] QVector<ActiveInfo> activeSessions() const;

private:
  struct ActiveSession {
    // 0 while storage has refused to create the row. The session is tracked anyway, so
    // the playtime is billed and the row can be written with its original start later.
    qint64 id = 0;
    // Stable identity minted before the first database write, so a replay after a crash
    // reuses it instead of creating a second session row.
    QString sessionKey;
    qint64 pid = 0;
    qint64 procStart = -1;
    QString gamePath;
    QString emulator;
    QString rescanSource;
    qint64 startedAt = 0;
    qint64 elapsedMs = 0;
    qint64 markMs = 0;
    qint64 lastFlushMs = 0;
    qint64 lastPollWall = 0;
    qint64 intervalRemainderMs = 0;
    QVector<SessionDatabase::SessionInterval> intervals;
    // The focus state at the last poll, so the span between that poll and a close
    // is billed for a game that was playing and skipped for one put aside.
    bool paused = false;
    // A session attributed by window title rather than by a verified process identity.
    // Its title can fail to resolve for a single poll (a save dialog, an Alt+Tab, a
    // slow compositor answer), which must not end the session: that would split one
    // play session into a row per flicker and rewrite the emulator's last-played each
    // time. Such a session is closed only after the title stays unresolved.
    bool titleMatched = false;
    int missedPolls = 0;
    // The session moved to another process of its game, and storage has not yet accepted
    // the row's new process identity. Retried on each flush, so a recorder restart finds
    // the row under the process that is still running.
    bool movePending = false;
  };

  QString keyFor(const SessionMatch& match) const;
  QHash<QString, ActiveSession>::Iterator
  closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs, qint64 nowWall);
  void flush(ActiveSession& session, qint64 nowMs, qint64 nowWall);
  void recordObservedSpan(ActiveSession& session, qint64 nowMs, qint64 nowWall);
  void retryClosed(qint64 nowMs, const QVector<SessionMatch>& matches);
  // Retries the row of a session whose insert storage refused, writing the play that
  // accumulated in the meantime so a crash before the next interval does not lose it.
  // Does nothing once the row exists.
  void retryInsert(ActiveSession& session, qint64 nowMs, qint64 nowWall);
  struct PendingClose {
    // 0 when the session never got a row, in which case the rest of the entry is the
    // whole record that has to be written once storage recovers.
    qint64 id = 0;
    QString key;
    qint64 endedAt = 0;
    qint64 seconds = 0;
    qint64 startedAt = 0;
    qint64 pid = 0;
    qint64 procStart = -1;
    QString gamePath;
    QString source;
    QVector<SessionDatabase::SessionInterval> intervals;
    // The wall time at which the state was observed, so a replayed checkpoint keeps the
    // observation time as its heartbeat instead of the replay instant.
    qint64 observedAt = 0;
    // True for an active-session checkpoint, false for a finished session. A failed
    // checkpoint must be retried as an open session, never written as a closed one.
    bool open = false;
    // The incarnation and clear epoch captured when the operation was accepted. The retry
    // validates them transactionally with the write, so a deletion or restore that happens
    // while the recorder is still running drops the operation instead of resurrecting it.
    QString incarnation;
    qint64 clearEpoch = 0;
  };
  // Adds a refused operation to the in-memory retry queue and to the durable journal,
  // holding the queue at its cap so a lasting storage failure cannot grow it without
  // bound. The journal holds every accepted operation, so a dropped queue entry is
  // delayed until the next recorder start, not lost.
  void queuePending(PendingClose pending);
  void trimPendingCloses();
  // Writes one refused operation to the durable journal, stamped with the generations the
  // database currently holds for the game. A record the journal refuses is reported through
  // the capacity flag rather than dropped in silence.
  void journalPending(const PendingClose& pending);
  // Replays operations a previous recorder could not write, transactionally, then compacts
  // the journal. The stable key makes a replay idempotent, so a crash between the database
  // commit and the acknowledgment cannot double a session.
  void replayJournal(const QVector<SessionMatch>& matches);
  // Applies one journaled operation, closed or active, through the guarded write path.
  [[nodiscard]] SessionDatabase::ReplayOutcome
  applyJournalOperation(const SessionJournal::Operation& operation,
                        const QVector<SessionMatch>& matches);
  // Commits the latest observed state and adopts a surviving same-game process under its
  // original stable key. A changed or dead process is closed atomically with the replay.
  [[nodiscard]] SessionDatabase::ReplayOutcome
  reconcileRecoveredOpen(const SessionJournal::Operation& operation,
                         const QVector<SessionMatch>& matches);
  // Binds anonymous journal records to the database only after both identity and ownership
  // can be established. False means the unresolved work must remain pending.
  bool resolveOwnership();
  // Writes an active session's observed state to the journal when the database refused its
  // insert or progress update, so a killed recorder loses at most one checkpoint interval.
  void checkpointActive(const ActiveSession& session, qint64 nowWall);
  // The database identity to stamp operations with. It reads the current incarnation and
  // caches it; when the database cannot be read (locked or read-only) it reuses the last
  // identity seen, so a record accepted during an outage still carries one instead of looking
  // like a record from a replaced database.
  [[nodiscard]] QString currentIncarnation();
  // Drains durable pending work that is not in the bounded in-memory queue, so records past
  // the memory cap still reach the database once storage recovers, without a restart.
  void drainJournal(const QVector<SessionMatch>& matches);
  QVector<PendingClose> m_pendingCloses;
  qint64 m_lastCloseAttemptMs = 0;
  bool m_storageFailure = false;
  bool m_pauseUnfocused = false;
  std::unique_ptr<SessionJournal> m_journal;
  bool m_journalCapacity = false;
  bool m_journalCorrupt = false;
  QString m_incarnation;

  QSqlDatabase m_database;
  std::function<qint64()> m_elapsedMs;
  int m_flushIntervalMs = 30000;
  QHash<QString, ActiveSession> m_active;
  QStringList m_rescanRequests;
};

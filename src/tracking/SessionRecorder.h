#pragma once

#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDatabase.h"

#include <QHash>
#include <QVector>

#include <functional>
#include <utility>

// Turns emulator process sightings into play_sessions rows. One recorder owns
// the active sessions of one database connection. Elapsed time comes from a
// monotonic clock, so suspended wall time is never billed as play time, and
// every row is flushed periodically so a crash loses at most one interval.
class SessionRecorder final {
public:
  // elapsedMs must return monotonic milliseconds. The default uses the process
  // start; tests inject a controllable clock.
  explicit SessionRecorder(const QSqlDatabase& database,
                           const std::function<qint64()>& elapsedMs = {});
  ~SessionRecorder();

  void setFlushIntervalMs(int intervalMs);

  // Startup: closes open sessions whose process is gone (at the last heartbeat),
  // adopts survivors still running the same game, and closes any that now run a
  // different game.
  void recover(const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
               qint64 nowWall);

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
  [[nodiscard]] int pendingCloseCount() const { return m_pendingCloses.size(); }

  [[nodiscard]] int activeCount() const { return static_cast<int>(m_active.size()); }

private:
  struct ActiveSession {
    qint64 id = 0;
    QString gamePath;
    QString emulator;
    QString rescanSource;
    qint64 elapsedMs = 0;
    qint64 markMs = 0;
    qint64 lastFlushMs = 0;
    // The focus state at the last poll, so the span between that poll and a close
    // is billed for a game that was playing and skipped for one put aside.
    bool paused = false;
  };

  QString keyFor(const SessionMatch& match) const;
  QHash<QString, ActiveSession>::Iterator
  closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs, qint64 nowWall);
  void flush(ActiveSession& session, qint64 nowMs, qint64 nowWall);
  void retryClosed(qint64 nowMs);
  struct PendingClose {
    qint64 id;
    qint64 endedAt;
    qint64 seconds;
  };
  QVector<PendingClose> m_pendingCloses;
  qint64 m_lastCloseAttemptMs = 0;
  bool m_storageFailure = false;
  bool m_pauseUnfocused = false;

  QSqlDatabase m_database;
  std::function<qint64()> m_elapsedMs;
  int m_flushIntervalMs = 30000;
  QHash<QString, ActiveSession> m_active;
  QStringList m_rescanRequests;
};

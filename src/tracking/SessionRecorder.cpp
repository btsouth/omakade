#include "tracking/SessionRecorder.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 kDefaultFlushIntervalMs = 30000;

// How many polls a title-attributed session may go unresolved before it is closed. The
// poll is 5 seconds, so this tolerates a save dialog, a menu or a slow compositor
// answer without inventing a boundary, while still ending the session promptly when the
// game really has gone.
constexpr int kTitleGracePolls = 3;

QElapsedTimer& defaultClock() {
  static QElapsedTimer clock;
  if (!clock.isValid()) {
    clock.start();
  }
  return clock;
}
} // namespace

SessionRecorder::SessionRecorder(const QSqlDatabase& database,
                                 const std::function<qint64()>& elapsedMs)
    : m_database(database), m_elapsedMs(elapsedMs) {
  if (!m_elapsedMs) {
    m_elapsedMs = [] { return defaultClock().elapsed(); };
  }
}

SessionRecorder::~SessionRecorder() = default;

void SessionRecorder::setFlushIntervalMs(int intervalMs) {
  if (intervalMs > 0) {
    m_flushIntervalMs = intervalMs;
  }
}

QString SessionRecorder::keyFor(const SessionMatch& match) const {
  return QStringLiteral("%1:%2").arg(match.pid).arg(match.procStart);
}

void SessionRecorder::recover(const QVector<ProcessSnapshot>& processes,
                              const ProcessProfileSet& profiles, qint64 nowWall) {
  Q_UNUSED(nowWall);
  const QVector<SessionDatabase::SessionRow> survivors =
      SessionDatabase::reconcileOpenSessions(m_database, &ProcFs::processAlive);
  if (survivors.isEmpty()) {
    return;
  }
  const QVector<SessionMatch> matches = ProcessMatcher::match(processes, profiles);
  const qint64 nowMs = m_elapsedMs();
  for (const SessionDatabase::SessionRow& row : survivors) {
    const SessionMatch* adopted = nullptr;
    for (const SessionMatch& match : matches) {
      // A window-title match has no recorded process identity, so it can never
      // be adopted after a restart and is only ever closed by a later poll.
      if (match.procStart <= 0) {
        continue;
      }
      if (match.pid == row.pid && match.procStart == row.procStart &&
          match.gamePath == row.gamePath) {
        adopted = &match;
        break;
      }
    }
    if (adopted != nullptr) {
      ActiveSession session;
      session.id = row.id;
      session.pid = row.pid;
      session.gamePath = row.gamePath;
      session.emulator = adopted->emulator;
      session.rescanSource = adopted->rescanSource;
      session.elapsedMs = row.seconds * 1000;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      m_active.insert(QStringLiteral("%1:%2").arg(row.pid).arg(row.procStart), session);
      continue;
    }
    // The process lives but no longer runs the same game; keep the recorded time
    // and stop where the last heartbeat proved it was still playing.
    if (!SessionDatabase::endSession(m_database, row.id, qMax(row.startedAt, row.heartbeatAt),
                                     row.seconds)) {
      queueClosed(row.id, qMax(row.startedAt, row.heartbeatAt), row.seconds);
      m_lastCloseAttemptMs = nowMs;
      m_storageFailure = true;
    }
  }
}

void SessionRecorder::flush(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  if (!SessionDatabase::updateProgress(m_database, session.id, session.elapsedMs / 1000, nowWall))
    m_storageFailure = true;
  session.lastFlushMs = nowMs;
}

QHash<QString, SessionRecorder::ActiveSession>::Iterator
SessionRecorder::closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs,
                              qint64 nowWall) {
  // A paused session stopped billing at the last poll, so the span since then is
  // not play time either.
  const qint64 totalMs =
      session->elapsedMs + (session->paused ? 0 : nowMs - session->markMs);
  if (!SessionDatabase::endSession(m_database, session->id, nowWall, totalMs / 1000)) {
    queueClosed(session->id, nowWall, totalMs / 1000);
    m_lastCloseAttemptMs = nowMs;
    m_storageFailure = true;
  }
  if (!session->rescanSource.isEmpty() && !m_rescanRequests.contains(session->rescanSource)) {
    m_rescanRequests.append(session->rescanSource);
  }
  return m_active.erase(session);
}

void SessionRecorder::queueClosed(qint64 id, qint64 endedAt, qint64 seconds) {
  // A storage failure that lasts (a full disk, a read-only database) would otherwise
  // queue one entry per session for the life of the daemon, and every poll would retry
  // all of them. The oldest are dropped once the queue is implausibly long: a session
  // that could not be closed after this many attempts is not going to be, and its row
  // still holds whatever was last flushed, so the recorded time is not lost, only the
  // tail of it.
  constexpr int kMaxPendingCloses = 64;
  m_pendingCloses.append({id, endedAt, seconds});
  while (m_pendingCloses.size() > kMaxPendingCloses) {
    m_pendingCloses.removeFirst();
  }
}

void SessionRecorder::retryClosed(qint64 nowMs) {
  if (m_pendingCloses.isEmpty() || nowMs - m_lastCloseAttemptMs < m_flushIntervalMs)
    return;
  m_lastCloseAttemptMs = nowMs;
  for (qsizetype i = 0; i < m_pendingCloses.size();) {
    const auto pending = m_pendingCloses.at(i);
    if (SessionDatabase::endSession(m_database, pending.id, pending.endedAt, pending.seconds))
      m_pendingCloses.removeAt(i);
    else {
      m_storageFailure = true;
      ++i;
    }
  }
}

void SessionRecorder::sync(const QVector<SessionMatch>& matches, qint64 nowWall,
                           const std::function<bool(qint64)>& unfocused) {
  const qint64 nowMs = m_elapsedMs();
  retryClosed(nowMs);
  const bool pause = m_pauseUnfocused && static_cast<bool>(unfocused);
  QSet<QString> matched;
  matched.reserve(matches.size());
  for (const SessionMatch& match : matches) {
    const QString key = keyFor(match);
    matched.insert(key);
    auto existing = m_active.find(key);
    // The same emulator process can report a different game on a later poll.
    if (existing != m_active.end() && existing->gamePath != match.gamePath) {
      closeSession(existing, nowMs, nowWall);
      existing = m_active.end();
    }
    if (existing == m_active.end()) {
      const qint64 id = SessionDatabase::beginSession(m_database, match.gamePath, match.emulator,
                                                      nowWall, match.pid, match.procStart);
      if (id <= 0) {
        m_storageFailure = true;
        continue;
      }
      ActiveSession session;
      session.id = id;
      session.pid = match.pid;
      session.gamePath = match.gamePath;
      session.emulator = match.emulator;
      session.rescanSource = match.rescanSource;
      session.startedAt = nowWall;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      // A title match carries no verified process identity (procStart <= 0), which is
      // exactly the case whose title can flicker.
      session.titleMatched = match.procStart <= 0;
      m_active.insert(key, session);
      continue;
    }
    // Time is only billed while the game holds the compositor's focus. The mark
    // moves forward either way, so a pause spans exactly the polls where the game
    // was unfocused and is never back-dated when focus returns.
    existing->paused = pause && unfocused(match.pid);
    if (!existing->paused) {
      existing->elapsedMs += nowMs - existing->markMs;
    }
    existing->markMs = nowMs;
    // The heartbeat keeps moving so a crash during a long pause ends the row at the
    // last poll instead of at a boundary reached after the game was put aside.
    if (nowMs - existing->lastFlushMs >= m_flushIntervalMs) {
      flush(*existing, nowMs, nowWall);
    }
  }
  for (auto it = m_active.begin(); it != m_active.end();) {
    if (matched.contains(it.key())) {
      it->missedPolls = 0;
      ++it;
      continue;
    }
    // A title-attributed session whose title did not resolve this poll is tolerated for
    // a short while before it is closed. The title is the only thing identifying the
    // game, and it is not stable: an emulator's window can be a save dialog or a menu
    // for a poll or two, and one missed poll used to end the session and start a new
    // row, splitting a single play session into fragments and rewriting the emulator's
    // last-played at every split.
    //
    // The grace is bounded by the process, not by the poll count alone: while the
    // recorded process is still alive the game really is running, so the time is billed
    // and the session continues. The moment it is gone the session closes, exactly as a
    // verified match would, so no time after the exit is ever billed.
    if (it->titleMatched && ProcFs::processRunning(it->pid) &&
        ++it->missedPolls <= kTitleGracePolls) {
      if (!it->paused) {
        it->elapsedMs += nowMs - it->markMs;
      }
      it->markMs = nowMs;
      if (nowMs - it->lastFlushMs >= m_flushIntervalMs) {
        flush(*it, nowMs, nowWall);
      }
      ++it;
      continue;
    }
    it = closeSession(it, nowMs, nowWall);
  }
}

void SessionRecorder::endAll(qint64 nowWall) {
  const qint64 nowMs = m_elapsedMs();
  retryClosed(nowMs);
  for (auto it = m_active.begin(); it != m_active.end();) {
    it = closeSession(it, nowMs, nowWall);
  }
  // Pending closures retain their original boundary. A blanket close must not
  // replace it with a later toggle/poll time while storage is unavailable.
  if (m_pendingCloses.isEmpty() && !SessionDatabase::endAllSessions(m_database, nowWall))
    m_storageFailure = true;
}

QStringList SessionRecorder::takeRescanRequests() { return std::move(m_rescanRequests); }

QVector<SessionRecorder::ActiveInfo> SessionRecorder::activeSessions() const {
  QVector<ActiveInfo> sessions;
  sessions.reserve(m_active.size());
  for (auto session = m_active.cbegin(); session != m_active.cend(); ++session) {
    sessions.append(ActiveInfo{.gamePath = session->gamePath,
                               .emulator = session->emulator,
                               .startedAt = session->startedAt,
                               .elapsedMs = session->elapsedMs,
                               .paused = session->paused});
  }
  // A long-running game is the one a player is watching, so it leads the list and
  // keeps the presence stable when a second game starts.
  std::sort(sessions.begin(), sessions.end(),
            [](const ActiveInfo& left, const ActiveInfo& right) {
              return left.startedAt < right.startedAt;
            });
  return sessions;
}

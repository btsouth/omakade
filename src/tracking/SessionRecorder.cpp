#include "tracking/SessionRecorder.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSet>
#include <QUuid>

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
                                 const std::function<qint64()>& elapsedMs,
                                 const QString& journalPath)
    : m_database(database), m_elapsedMs(elapsedMs) {
  if (!m_elapsedMs) {
    m_elapsedMs = [] { return defaultClock().elapsed(); };
  }
  // The journal lives beside the database file. An in-memory database has no file to sit
  // next to, and a test that wants a journal passes an explicit path.
  QString path = journalPath;
  if (path.isEmpty()) {
    const QString name = database.databaseName();
    if (!name.isEmpty() && name != QStringLiteral(":memory:")) {
      path = name + QStringLiteral(".journal");
    }
  }
  if (!path.isEmpty()) {
    m_journal = std::make_unique<SessionJournal>(path);
    if (!m_journal->open()) {
      // Storage that cannot hold the journal is reported through journalAvailable; the
      // in-memory queue and the database keep working exactly as they did.
      m_journal.reset();
    } else if (m_journal->recoveredCorrupt()) {
      m_journalCorrupt = true;
    }
  }
  // Learn the database identity once, while the database is normally readable, so a record
  // accepted during a later lock still carries it.
  if (database.isOpen()) {
    m_incarnation = SessionDatabase::journalIncarnation(m_database);
    if (!m_incarnation.isEmpty() && m_journal && !resolveOwnership())
      m_storageFailure = true;
  }
}

QString SessionRecorder::currentIncarnation() {
  const QString read = SessionDatabase::journalIncarnation(m_database);
  if (!read.isEmpty()) {
    m_incarnation = read;
    return read;
  }
  return m_incarnation;
}

SessionRecorder::~SessionRecorder() = default;

bool SessionRecorder::resolveOwnership() {
  const QString identity = currentIncarnation();
  if (identity.isEmpty() || m_journal == nullptr || m_journal->owner().isEmpty())
    return false;
  QString owner = SessionDatabase::journalOwnership(m_database);
  if (owner.isEmpty() && !SessionDatabase::bindJournalOwnership(m_database, m_journal->owner()))
    return false;
  owner = SessionDatabase::journalOwnership(m_database);
  if (owner == m_journal->owner())
    return true;
  // Replace installs the next owner in its database transaction. Old pending work must
  // first be rejected under its old token and durably removed, before an empty journal
  // can bind to the replacement generation.
  return !owner.isEmpty() && m_journal->bindEmptyOwner(owner);
}

void SessionRecorder::setFlushIntervalMs(int intervalMs) {
  if (intervalMs > 0) {
    m_flushIntervalMs = intervalMs;
  }
}

void SessionRecorder::recordObservedSpan(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  const qint64 elapsed = qMax<qint64>(0, nowMs - session.markMs);
  const qint64 wallEnd = qMax(session.lastPollWall, nowWall);
  if (elapsed > 0 && session.lastPollWall > 0) {
    if (!session.paused) {
      session.elapsedMs += elapsed;
    }
    qint64 billedSeconds = 0;
    if (!session.paused) {
      const qint64 total = session.intervalRemainderMs + elapsed;
      billedSeconds = total / 1000;
      session.intervalRemainderMs = total % 1000;
    }
    const QString kind = session.paused ? QStringLiteral("paused") : QStringLiteral("playing");
    if (!session.intervals.isEmpty() && session.intervals.constLast().kind == kind &&
        session.intervals.constLast().wallEnd <= session.lastPollWall) {
      SessionDatabase::SessionInterval& last = session.intervals.last();
      last.wallEnd = wallEnd;
      last.billedSeconds += billedSeconds;
    } else if (session.intervals.size() < 1024) {
      session.intervals.append({.wallStart = session.lastPollWall,
                                .wallEnd = wallEnd,
                                .billedSeconds = billedSeconds,
                                .kind = kind});
    } else if (!session.intervals.isEmpty()) {
      // One ordinary day cannot produce this many focus transitions. If a pathological
      // compositor does, keep the total rather than growing without bound.
      SessionDatabase::SessionInterval& last = session.intervals.last();
      last.wallEnd = wallEnd;
      last.billedSeconds += billedSeconds;
      if (billedSeconds > 0) {
        last.kind = QStringLiteral("playing");
      }
    }
  }
  session.markMs = nowMs;
  session.lastPollWall = wallEnd;
}

QString SessionRecorder::keyFor(const SessionMatch& match) const {
  return QStringLiteral("%1:%2").arg(match.pid).arg(match.procStart);
}

void SessionRecorder::recover(const QVector<ProcessSnapshot>& processes,
                              const ProcessProfileSet& profiles, qint64 nowWall,
                              const QVector<SessionMatch>& launched) {
  Q_UNUSED(nowWall);
  const QVector<SessionMatch> matches = ProcessMatcher::match(processes, profiles) + launched;
  // Replay anything a previous recorder could not write before adopting the sessions that
  // are still open, so recovered history and live sessions are reconciled against the
  // completed journal rather than the other way round.
  replayJournal(matches);
  const QVector<SessionDatabase::SessionRow> survivors =
      SessionDatabase::reconcileOpenSessions(m_database, &ProcFs::processAlive);
  if (survivors.isEmpty()) {
    return;
  }
  const qint64 nowMs = m_elapsedMs();
  for (const SessionDatabase::SessionRow& row : survivors) {
    const QString activeKey = QStringLiteral("%1:%2").arg(row.pid).arg(row.procStart);
    if (m_active.contains(activeKey))
      continue;
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
      session.sessionKey = row.sessionKey;
      session.startedAt = row.startedAt;
      session.pid = row.pid;
      session.procStart = row.procStart;
      session.gamePath = row.gamePath;
      session.emulator = adopted->emulator;
      session.rescanSource = adopted->rescanSource;
      session.elapsedMs = row.seconds * 1000;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      session.lastPollWall = nowWall;
      session.intervals = SessionDatabase::sessionIntervals(m_database, row.sessionKey);
      if (session.intervals.isEmpty() && row.seconds > 0) {
        session.intervals.append({.wallStart = row.startedAt,
                                  .wallEnd = qMax(row.startedAt, row.heartbeatAt),
                                  .billedSeconds = row.seconds,
                                  .kind = QStringLiteral("playing")});
      }
      m_active.insert(QStringLiteral("%1:%2").arg(row.pid).arg(row.procStart), session);
      continue;
    }
    // The process lives but no longer runs the same game; keep the recorded time
    // and stop where the last heartbeat proved it was still playing.
    if (!SessionDatabase::endSession(m_database, row.id, qMax(row.startedAt, row.heartbeatAt),
                                     row.seconds)) {
      PendingClose pending;
      pending.id = row.id;
      pending.key = row.sessionKey;
      pending.endedAt = qMax(row.startedAt, row.heartbeatAt);
      pending.seconds = row.seconds;
      pending.startedAt = row.startedAt;
      pending.pid = row.pid;
      pending.procStart = row.procStart;
      pending.gamePath = row.gamePath;
      pending.source = row.source;
      pending.intervals = SessionDatabase::sessionIntervals(m_database, row.sessionKey);
      queuePending(pending);
      m_lastCloseAttemptMs = nowMs;
      m_storageFailure = true;
    }
  }
}

void SessionRecorder::flush(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  // A session whose row storage refused has no row to update; a session whose progress update
  // was refused keeps its last flushed seconds. Either way the observed state is checkpointed
  // to the journal, bounded to one record per flush interval, so a killed recorder loses at
  // most one interval instead of the whole session.
  bool stored = false;
  if (session.id > 0) {
    stored =
        SessionDatabase::updateProgress(m_database, session.id, session.elapsedMs / 1000, nowWall);
    if (!stored) {
      m_storageFailure = true;
    }
  }
  if (stored && !session.intervals.isEmpty() &&
      !SessionDatabase::replaceSessionIntervals(m_database, session.sessionKey,
                                                 session.intervals)) {
    m_storageFailure = true;
  }
  if (!stored) {
    checkpointActive(session, nowWall);
  }
  session.lastFlushMs = nowMs;
}

void SessionRecorder::retryInsert(ActiveSession& session, qint64 nowMs, qint64 nowWall) {
  if (session.id > 0) {
    return;
  }
  if (session.sessionKey.isEmpty()) {
    session.sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  session.id = SessionDatabase::beginSession(m_database, session.gamePath, session.emulator,
                                             session.startedAt, session.pid, session.procStart,
                                             session.sessionKey);
  if (session.id <= 0) {
    m_storageFailure = true;
    return;
  }
  // The row starts at the original start time, so the play that happened while storage was
  // refusing it is written now rather than waiting for the next interval.
  flush(session, nowMs, nowWall);
}

QHash<QString, SessionRecorder::ActiveSession>::Iterator
SessionRecorder::closeSession(QHash<QString, ActiveSession>::Iterator session, qint64 nowMs,
                              qint64 nowWall) {
  recordObservedSpan(*session, nowMs, nowWall);
  const qint64 totalMs = session->elapsedMs;
  if (session->sessionKey.isEmpty()) {
    session->sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  if (session->id > 0) {
    const bool closed =
        SessionDatabase::endSession(m_database, session->id, nowWall, totalMs / 1000);
    const bool intervalsStored =
        !closed || session->intervals.isEmpty() ||
        SessionDatabase::replaceSessionIntervals(m_database, session->sessionKey,
                                                 session->intervals);
    if (!closed || !intervalsStored) {
      PendingClose pending;
      pending.id = session->id;
      pending.key = session->sessionKey;
      pending.endedAt = nowWall;
      pending.seconds = totalMs / 1000;
      pending.startedAt = session->startedAt;
      pending.pid = session->pid;
      pending.procStart = session->procStart;
      pending.gamePath = session->gamePath;
      pending.source = session->emulator;
      pending.intervals = session->intervals;
      queuePending(pending);
      m_lastCloseAttemptMs = nowMs;
      m_storageFailure = true;
    }
  } else {
    // The session never got a row at all. Queue the whole record, so storage recovering
    // after the game has already exited still writes the play that was observed.
    PendingClose pending;
    pending.key = session->sessionKey;
    pending.endedAt = nowWall;
    pending.seconds = totalMs / 1000;
    pending.startedAt = session->startedAt;
    pending.pid = session->pid;
    pending.procStart = session->procStart;
    pending.gamePath = session->gamePath;
    pending.source = session->emulator;
    pending.intervals = session->intervals;
    queuePending(pending);
    m_lastCloseAttemptMs = nowMs;
    m_storageFailure = true;
  }
  if (!session->rescanSource.isEmpty() && !m_rescanRequests.contains(session->rescanSource)) {
    m_rescanRequests.append(session->rescanSource);
  }
  return m_active.erase(session);
}

void SessionRecorder::queuePending(PendingClose pending) {
  if (pending.key.isEmpty()) {
    pending.key = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  // Stamp the operation with the identity last seen, so the guarded retry can tell later
  // whether the database or the game changed under it. The cached identity is used when the
  // database cannot be read right now, so a locked database does not produce an anonymous
  // record that a later restore would have to guess about.
  pending.incarnation = currentIncarnation();
  pending.clearEpoch = SessionDatabase::gameClearEpoch(m_database, pending.gamePath);
  m_pendingCloses.append(pending);
  journalPending(pending);
  trimPendingCloses();
}

void SessionRecorder::journalPending(const PendingClose& pending) {
  if (m_journal == nullptr || !m_journal->available() || pending.key.isEmpty()) {
    return;
  }
  SessionJournal::Operation operation;
  operation.key = pending.key;
  operation.gamePath = pending.gamePath;
  operation.source = pending.source;
  operation.startedAt = pending.startedAt;
  operation.endedAt = pending.endedAt;
  operation.seconds = pending.seconds;
  operation.pid = pending.pid;
  operation.procStart = pending.procStart;
  operation.observedAt = pending.observedAt;
  operation.open = pending.open;
  operation.intervals = pending.intervals;
  operation.incarnation = pending.incarnation;
  operation.clearEpoch = pending.clearEpoch;
  if (!m_journal->append(operation)) {
    // The record was refused, not evicted: report it so the interface can warn.
    m_journalCapacity = true;
    m_storageFailure = true;
    return;
  }
  if (!m_journal->full()) {
    m_journalCapacity = false;
  }
}

void SessionRecorder::checkpointActive(const ActiveSession& session, qint64 nowWall) {
  if (m_journal == nullptr || !m_journal->available() || session.sessionKey.isEmpty()) {
    return;
  }
  SessionJournal::Operation operation;
  operation.key = session.sessionKey;
  operation.gamePath = session.gamePath;
  operation.source = session.emulator;
  operation.startedAt = session.startedAt;
  operation.endedAt = 0;
  operation.seconds = session.elapsedMs / 1000;
  operation.pid = session.pid;
  operation.procStart = session.procStart;
  operation.observedAt = nowWall;
  operation.open = true;
  operation.intervals = session.intervals;
  operation.incarnation = currentIncarnation();
  operation.clearEpoch = SessionDatabase::gameClearEpoch(m_database, session.gamePath);
  if (!m_journal->append(operation)) {
    m_journalCapacity = true;
    m_storageFailure = true;
  }
}

SessionDatabase::ReplayOutcome
SessionRecorder::applyJournalOperation(const SessionJournal::Operation& operation,
                                       const QVector<SessionMatch>& matches) {
  if (operation.open) {
    return reconcileRecoveredOpen(operation, matches);
  }
  // The stable key makes this idempotent, so a record that was committed before a crash and
  // one that never reached the database both resolve to exactly one session.
  return SessionDatabase::replayClosedSession(
      m_database, operation.key, operation.gamePath, operation.source, operation.startedAt,
      operation.endedAt, operation.seconds, operation.pid, operation.procStart,
      operation.incarnation, operation.clearEpoch, m_journal ? m_journal->owner() : QString(),
      operation.intervals);
}

SessionDatabase::ReplayOutcome
SessionRecorder::reconcileRecoveredOpen(const SessionJournal::Operation& operation,
                                        const QVector<SessionMatch>& matches) {
  const SessionMatch* sameGame = nullptr;
  for (const SessionMatch& match : matches) {
    if (match.pid != operation.pid || match.procStart != operation.procStart)
      continue;
    if (match.gamePath == operation.gamePath)
      sameGame = &match;
    break;
  }
  const bool alive =
      operation.pid > 0 &&
      (operation.procStart > 0 ? ProcFs::processAlive(operation.pid, operation.procStart)
                               : ProcFs::processRunning(operation.pid));
  // A surviving process that changed games has already stopped playing the old game. Close
  // at the checkpoint boundary, not at the delayed replay instant.
  const bool close = !alive || sameGame == nullptr;
  SessionDatabase::ReplayOutcome outcome = SessionDatabase::replayOpenSession(
      m_database, operation.key, operation.gamePath, operation.source, operation.startedAt,
      operation.seconds, operation.pid, operation.procStart, operation.incarnation,
      operation.clearEpoch, operation.observedAt,
      close ? qMax(operation.startedAt, operation.observedAt) : 0, operation.seconds,
      m_journal ? m_journal->owner() : QString(), operation.intervals);
  if (outcome == SessionDatabase::ReplayOutcome::Stale || !alive || sameGame == nullptr)
    return outcome;

  // Reserve the original key even while replay is failing. Otherwise ordinary matched
  // polls create a second identity and checkpoint it while the first is still pending.
  const QString reservedKey = QStringLiteral("%1:%2").arg(operation.pid).arg(operation.procStart);
  if (outcome == SessionDatabase::ReplayOutcome::Error) {
    if (!m_active.contains(reservedKey)) {
      ActiveSession session;
      session.sessionKey = operation.key;
      session.startedAt = operation.startedAt;
      session.pid = operation.pid;
      session.procStart = operation.procStart;
      session.gamePath = operation.gamePath;
      session.emulator = sameGame->emulator;
      session.rescanSource = sameGame->rescanSource;
      session.elapsedMs = operation.seconds * 1000;
      session.markMs = m_elapsedMs();
      session.lastFlushMs = session.markMs;
      session.lastPollWall = operation.observedAt;
      session.intervals = operation.intervals;
      if (session.intervals.isEmpty() && operation.seconds > 0) {
        session.intervals.append({.wallStart = operation.startedAt,
                                  .wallEnd = qMax(operation.startedAt, operation.observedAt),
                                  .billedSeconds = operation.seconds,
                                  .kind = QStringLiteral("playing")});
      }
      session.titleMatched = operation.procStart <= 0;
      m_active.insert(reservedKey, session);
    }
    return outcome;
  }

  // The durable row is the stable state. Adopt it immediately, so the next poll updates the
  // same key instead of creating a second session after a delayed journal replay.
  const QString activeKey = QStringLiteral("%1:%2").arg(operation.pid).arg(operation.procStart);
  auto existing = m_active.find(activeKey);
  if (existing != m_active.end()) {
    if (existing->gamePath != operation.gamePath)
      return outcome;
    const auto row = SessionDatabase::sessionByKey(m_database, operation.key);
    if (row.id <= 0 || existing->sessionKey != operation.key)
      return SessionDatabase::ReplayOutcome::Error;
    existing->id = row.id;
    existing->elapsedMs = qMax(existing->elapsedMs, row.seconds * 1000);
    existing->intervals = operation.intervals;
    if (existing->intervals.isEmpty() && row.seconds > 0) {
      existing->intervals.append({.wallStart = row.startedAt,
                                  .wallEnd = qMax(row.startedAt, row.heartbeatAt),
                                  .billedSeconds = row.seconds,
                                  .kind = QStringLiteral("playing")});
    }
    return outcome;
  }
  const SessionDatabase::SessionRow row = SessionDatabase::sessionByKey(m_database, operation.key);
  if (row.id <= 0)
    return SessionDatabase::ReplayOutcome::Error;
  ActiveSession session;
  session.id = row.id;
  session.sessionKey = row.sessionKey;
  session.pid = row.pid;
  session.procStart = row.procStart;
  session.gamePath = row.gamePath;
  session.emulator = sameGame->emulator;
  session.rescanSource = sameGame->rescanSource;
  session.startedAt = row.startedAt;
  session.elapsedMs = row.seconds * 1000;
  session.markMs = m_elapsedMs();
  session.lastFlushMs = session.markMs;
  session.lastPollWall = operation.observedAt;
  session.intervals = operation.intervals;
  if (session.intervals.isEmpty() && row.seconds > 0) {
    session.intervals.append({.wallStart = row.startedAt,
                              .wallEnd = qMax(row.startedAt, row.heartbeatAt),
                              .billedSeconds = row.seconds,
                              .kind = QStringLiteral("playing")});
  }
  m_active.insert(activeKey, session);
  return outcome;
}

void SessionRecorder::drainJournal(const QVector<SessionMatch>& matches) {
  if (m_journal == nullptr || !m_journal->available()) {
    return;
  }
  const QVector<SessionJournal::Operation> operations = m_journal->pending(256);
  if (operations.isEmpty()) {
    return;
  }
  QStringList drop;
  for (const SessionJournal::Operation& operation : operations) {
    const SessionDatabase::ReplayOutcome outcome = applyJournalOperation(operation, matches);
    if (outcome == SessionDatabase::ReplayOutcome::Error) {
      m_storageFailure = true;
      continue;
    }
    // Written or stale: either way the journal no longer needs to keep it.
    drop.append(operation.key);
  }
  if (!drop.isEmpty()) {
    if (!m_journal->compact(drop))
      m_storageFailure = true;
  }
  if (!m_journal->full()) {
    m_journalCapacity = false;
  }
}

void SessionRecorder::replayJournal(const QVector<SessionMatch>& matches) {
  if (m_journal == nullptr || !m_journal->available()) {
    return;
  }
  const QVector<SessionJournal::Operation> operations = m_journal->pending(512);
  QStringList drop;
  for (const SessionJournal::Operation& operation : operations) {
    const SessionDatabase::ReplayOutcome outcome = applyJournalOperation(operation, matches);
    if (outcome == SessionDatabase::ReplayOutcome::Error) {
      m_storageFailure = true;
      // Keep it available for retry, bounded by the memory cap.
      PendingClose pending;
      pending.key = operation.key;
      pending.endedAt = operation.endedAt;
      pending.seconds = operation.seconds;
      pending.startedAt = operation.startedAt;
      pending.pid = operation.pid;
      pending.procStart = operation.procStart;
      pending.gamePath = operation.gamePath;
      pending.source = operation.source;
      pending.intervals = operation.intervals;
      pending.observedAt = operation.observedAt;
      pending.open = operation.open;
      pending.incarnation = operation.incarnation;
      pending.clearEpoch = operation.clearEpoch;
      bool queued = false;
      for (const PendingClose& existing : m_pendingCloses) {
        if (existing.key == pending.key) {
          queued = true;
          break;
        }
      }
      if (!queued) {
        m_pendingCloses.append(pending);
      }
      continue;
    }
    drop.append(operation.key);
  }
  if (!drop.isEmpty()) {
    if (!m_journal->compact(drop))
      m_storageFailure = true;
  }
  if (!m_journal->full()) {
    m_journalCapacity = false;
  }
  trimPendingCloses();
}

void SessionRecorder::trimPendingCloses() {
  // A storage failure that lasts (a full disk, a read-only database) would otherwise
  // queue one entry per session for the life of the daemon, and every poll would retry
  // all of them. The oldest are dropped once the queue is implausibly long. This is the
  // in-memory retry queue only: the durable journal holds every accepted operation, so a
  // dropped queue entry is delayed until the next recorder start, not lost. One bound
  // covers both kinds of pending write.
  constexpr int kMaxPendingCloses = 64;
  while (m_pendingCloses.size() > kMaxPendingCloses) {
    m_pendingCloses.removeFirst();
  }
}

void SessionRecorder::retryClosed(qint64 nowMs, const QVector<SessionMatch>& matches) {
  if (nowMs - m_lastCloseAttemptMs < m_flushIntervalMs) {
    return;
  }
  if (m_pendingCloses.isEmpty() && (m_journal == nullptr || !m_journal->available())) {
    return;
  }
  m_lastCloseAttemptMs = nowMs;
  resolveOwnership();
  // A live session may have checkpointed newer progress while this queue retained its
  // initial failed replay. Never acknowledge a newer disk checkpoint using stale memory.
  const auto durable = m_journal ? m_journal->pending(512) : QVector<SessionJournal::Operation>{};
  QStringList drop;
  for (qsizetype i = 0; i < m_pendingCloses.size();) {
    PendingClose pending = m_pendingCloses.at(i);
    if (pending.open) {
      for (const auto& operation : durable) {
        if (operation.key != pending.key)
          continue;
        pending.seconds = operation.seconds;
        pending.observedAt = operation.observedAt;
        pending.endedAt = operation.endedAt;
        pending.open = operation.open;
        pending.intervals = operation.intervals;
        pending.incarnation = operation.incarnation;
        pending.clearEpoch = operation.clearEpoch;
        break;
      }
    }
    // A close for a session that already has a row only moves the boundary that row was
    // carrying; an open row is never deleted, so this cannot race a deletion. An entry with no
    // row is replayed through the guarded path, which validates the identity and tombstone in
    // the same transaction as the write.
    SessionDatabase::ReplayOutcome outcome = SessionDatabase::ReplayOutcome::Error;
    if (pending.id > 0) {
      outcome = SessionDatabase::finalizeSession(m_database, pending.id, pending.key,
                                                 pending.endedAt, pending.seconds);
      if (outcome == SessionDatabase::ReplayOutcome::Written &&
          !pending.intervals.isEmpty() &&
          !SessionDatabase::replaceSessionIntervals(m_database, pending.key,
                                                    pending.intervals)) {
        outcome = SessionDatabase::ReplayOutcome::Error;
      }
    } else if (pending.open) {
      // A failed active checkpoint is retried as an open session. Writing it as a closed one
      // would clamp its end to its start and record a session with no span.
      outcome = applyJournalOperation(SessionJournal::Operation{.key = pending.key,
                                                                .gamePath = pending.gamePath,
                                                                .source = pending.source,
                                                                .startedAt = pending.startedAt,
                                                                .endedAt = pending.endedAt,
                                                                .seconds = pending.seconds,
                                                                .pid = pending.pid,
                                                                .procStart = pending.procStart,
                                                                .observedAt = pending.observedAt,
                                                                .incarnation = pending.incarnation,
                                                                .clearEpoch = pending.clearEpoch,
                                                                .open = pending.open,
                                                                .intervals = pending.intervals},
                                      matches);
    } else {
      outcome = SessionDatabase::replayClosedSession(
          m_database, pending.key, pending.gamePath, pending.source, pending.startedAt,
          pending.endedAt, pending.seconds, pending.pid, pending.procStart, pending.incarnation,
          pending.clearEpoch, m_journal ? m_journal->owner() : QString(), pending.intervals);
    }
    if (outcome == SessionDatabase::ReplayOutcome::Error) {
      m_storageFailure = true;
      ++i;
      continue;
    }
    // Written or stale: either way the operation is done with and must not be retried.
    if (!pending.key.isEmpty()) {
      drop.append(pending.key);
    }
    m_pendingCloses.removeAt(i);
  }
  if (!drop.isEmpty() && m_journal != nullptr && m_journal->available()) {
    m_journal->compact(drop);
  }
  // Records beyond the in-memory cap are still durable. Drain them too, so storage recovery
  // does not need a recorder restart to write them.
  drainJournal(matches);
  if (m_journal != nullptr && !m_journal->full()) {
    m_journalCapacity = false;
  }
}

void SessionRecorder::sync(const QVector<SessionMatch>& matches, qint64 nowWall,
                           const std::function<bool(qint64)>& unfocused) {
  const qint64 nowMs = m_elapsedMs();
  retryClosed(nowMs, matches);
  const bool pause = m_pauseUnfocused && static_cast<bool>(unfocused);
  QSet<QString> matched;
  matched.reserve(matches.size());
  // Pids with a verified match this poll: a command line that names the game, or the emulator's
  // own record of it. A session attributed from a window title for one of these is superseded,
  // and has to end at this boundary. The title grace below would otherwise keep it billing
  // alongside the verified session, which is the same play counted twice and an extra row for it.
  QSet<qint64> verifiedPids;
  for (const SessionMatch& match : matches) {
    if (match.procStart > 0) {
      verifiedPids.insert(match.pid);
    }
  }
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
      // A session for this process that was attributed from its window title has just been proved
      // by stronger evidence. This is one play session, so it keeps its row and takes the verified
      // identity rather than ending here and starting a second row for the same game. The row's
      // recorded process identity is not rewritten; only what the recorder tracks from now on is.
      if (match.procStart > 0) {
        for (auto candidate = m_active.begin(); candidate != m_active.end(); ++candidate) {
          if (!candidate->titleMatched || candidate->pid != match.pid ||
              candidate->gamePath != match.gamePath) {
            continue;
          }
          ActiveSession adopted = *candidate;
          m_active.erase(candidate);
          adopted.procStart = match.procStart;
          adopted.titleMatched = false;
          adopted.missedPolls = 0;
          existing = m_active.insert(key, adopted);
          break;
        }
      }
    }
    if (existing == m_active.end()) {
      // The stable identity is minted before the first write, so a refused insert and a
      // later replay both resolve to this one session.
      const QString sessionKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const qint64 id =
          SessionDatabase::beginSession(m_database, match.gamePath, match.emulator, nowWall,
                                        match.pid, match.procStart, sessionKey);
      // A refused insert used to drop the match on the spot, so a session that could not
      // even be created went unrecorded while one that got a row and then failed to close
      // was queued and retried. The game is running either way: the session is tracked with
      // no row yet, which bills its playtime and lets the row be written with its original
      // start once storage accepts writes, or queued as a finished session if the game
      // exits first.
      if (id <= 0) {
        m_storageFailure = true;
      }
      ActiveSession session;
      session.id = id;
      session.sessionKey = sessionKey;
      session.pid = match.pid;
      session.procStart = match.procStart;
      session.gamePath = match.gamePath;
      session.emulator = match.emulator;
      session.rescanSource = match.rescanSource;
      session.startedAt = nowWall;
      session.markMs = nowMs;
      session.lastFlushMs = nowMs;
      session.lastPollWall = nowWall;
      // A title match carries no verified process identity (procStart <= 0), which is
      // exactly the case whose title can flicker.
      session.titleMatched = match.procStart <= 0;
      m_active.insert(key, session);
      continue;
    }
    // A session whose row storage has not accepted is retried before it is billed, so the
    // row it eventually writes carries the play that has already happened.
    retryInsert(*existing, nowMs, nowWall);
    // Time is only billed while the game holds the compositor's focus. The mark
    // moves forward either way, so a pause spans exactly the polls where the game
    // was unfocused and is never back-dated when focus returns.
    existing->paused = pause && unfocused(match.pid);
    recordObservedSpan(*existing, nowMs, nowWall);
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
    // verified match would, so no time after the exit is ever billed. A verified match for
    // the same process ends it at once, because that match has taken over the recording.
    if (it->titleMatched && ProcFs::processRunning(it->pid) &&
        !verifiedPids.contains(it->pid) && ++it->missedPolls <= kTitleGracePolls) {
      retryInsert(*it, nowMs, nowWall);
      recordObservedSpan(*it, nowMs, nowWall);
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
  retryClosed(nowMs, {});
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
  std::sort(sessions.begin(), sessions.end(), [](const ActiveInfo& left, const ActiveInfo& right) {
    return left.startedAt < right.startedAt;
  });
  return sessions;
}

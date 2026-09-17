#include "tracking/PlaySessionStore.h"

#include "tracking/ProcFs.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionDisplay.h"
#include "tracking/SessionStopper.h"
#include "library/GameRoles.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLockFile>
#include <QSqlQuery>
#include <QSysInfo>
#include <unistd.h>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <iterator>

namespace {
constexpr int kRefreshIntervalMs = 20000;
// The Now Playing list polls faster than the playtime totals: it is a tiny query
// and the view has to notice a game starting or exiting while it is on screen.
constexpr int kNowPlayingIdleIntervalMs = 3000;
constexpr int kNowPlayingActiveIntervalMs = 1000;
// How long a game gets to act on a graceful stop before the view offers force.
constexpr qint64 kStopGraceSeconds = 8;
} // namespace

PlaySessionStore::PlaySessionStore(const QString& databasePath, QObject* parent)
    : QObject(parent),
      m_connectionName(QStringLiteral("omakade-sessions-%1").arg(QUuid::createUuid().toString())) {
  m_databasePath = databasePath;
  m_valid = SessionDatabase::open(m_database, databasePath, m_connectionName);
  refresh();
  m_baselines = SessionDatabase::baselinesByPath(m_database);
  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setInterval(kRefreshIntervalMs);
  connect(m_refreshTimer, &QTimer::timeout, this, &PlaySessionStore::refresh);
  m_refreshTimer->start();
  m_nowPlayingTimer = new QTimer(this);
  m_nowPlayingTimer->setInterval(kNowPlayingIdleIntervalMs);
  connect(m_nowPlayingTimer, &QTimer::timeout, this, &PlaySessionStore::refreshNowPlaying);
  m_nowPlayingTimer->start();
  refreshNowPlaying();
}

PlaySessionStore::~PlaySessionStore() {
  m_refreshTimer->stop();
  m_nowPlayingTimer->stop();
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

bool PlaySessionStore::recorderOwnsDatabase(const QString& databasePath) {
  QLockFile owner(databasePath + QStringLiteral(".sessiond.lock"));
  qint64 pid = 0;
  QString hostname, application;
  if (!owner.getLockInfo(&pid, &hostname, &application) || pid <= 0 ||
      hostname != QSysInfo::machineHostName() || application != QStringLiteral("omakade-sessiond"))
    return false;
  const QFileInfo process(QStringLiteral("/proc/%1").arg(pid));
  const QFileInfo executable(QStringLiteral("/proc/%1/exe").arg(pid));
  return process.ownerId() == static_cast<uint>(geteuid()) &&
         QFileInfo(executable.symLinkTarget()).fileName() == QStringLiteral("omakade-sessiond");
}

void PlaySessionStore::refreshRecorderStatus() {
  const bool running = recorderOwnsDatabase(m_databasePath);
  if (running == m_recorderRunning) return;
  m_recorderRunning = running;
  emit recorderStatusChanged();
}

QVariantList PlaySessionStore::historyForPaths(const QStringList& gamePaths, int limit) const {
  QVariantList history;
  if (!m_valid) return history;

  QStringList paths;
  for (const QString& path : gamePaths) {
    const QString clean = path.trimmed();
    if (!clean.isEmpty() && clean.size() <= 4096 && !paths.contains(clean)) paths.append(clean);
    if (paths.size() == 32) break;
  }
  if (paths.isEmpty()) return history;

  QStringList placeholders;
  for (qsizetype index = 0; index < paths.size(); ++index) placeholders.append("?");
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
                    "SELECT source, started_at, ended_at, seconds, session_key FROM play_sessions "
                    "WHERE game_path IN (%1) ORDER BY started_at DESC, id DESC LIMIT ?")
                    .arg(placeholders.join(',')));
  for (const QString& path : paths) query.addBindValue(path);
  query.addBindValue(qBound(1, limit, 20));
  if (!query.exec()) return history;

  while (query.next()) {
    const qint64 endedAt = query.value(2).toLongLong();
    history.append(QVariantMap{{"sessionKey", query.value(4).toString()},
                               {"source", query.value(0).toString()},
                               {"startedAt", query.value(1).toLongLong()},
                               {"endedAt", endedAt},
                               {"seconds", query.value(3).toLongLong()},
                               {"active", endedAt == 0}});
  }
  return history;
}

bool PlaySessionStore::deleteSession(const QString& sessionKey, const QStringList& gamePaths) {
  if (!m_valid || sessionKey.trimmed().isEmpty()) {
    return false;
  }
  // The row must still belong to the game the view is showing. That keeps a
  // stale menu from removing a session after the recorder moved on.
  const SessionDatabase::SessionRow row = SessionDatabase::sessionByKey(m_database, sessionKey);
  if (row.id <= 0 || row.endedAt <= 0 || !gamePaths.contains(row.gamePath)) {
    return false;
  }
  if (!SessionDatabase::deleteSession(m_database, sessionKey)) {
    return false;
  }
  // refresh() recomputes tracked totals; historyChanged tells the view even when
  // the deleted row held no seconds.
  refresh();
  ++m_historyRevision;
  emit historyChanged();
  return true;
}

int PlaySessionStore::deleteHistoryForPaths(const QStringList& gamePaths) {
  if (!m_valid) {
    return -1;
  }
  const int removed = SessionDatabase::deleteSessionsForPaths(m_database, gamePaths);
  if (removed < 0) {
    return -1;
  }
  if (removed > 0) {
    refresh();
    ++m_historyRevision;
    emit historyChanged();
  }
  return removed;
}

void PlaySessionStore::refreshNowPlaying() {
  QVariantList rows;
  QVector<SessionDatabase::SessionRow> open;
  if (m_valid) {
    open = SessionDatabase::openSessions(m_database);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const SessionDatabase::SessionRow& session : open) {
      // A session counts as running only while its recorded process is. That also
      // keeps the list honest when the recorder itself stopped.
      //
      // A window-title session has no procfs start time to verify against, since
      // a reused pid can never be told apart, so it is only checked for being
      // alive. Its stop control is withheld for the same reason: an unverified
      // process must never be signalled.
      const bool titled = session.procStart <= 0;
      const bool alive = titled ? ProcFs::processRunning(session.pid)
                                : ProcFs::processAlive(session.pid, session.procStart);
      if (!alive) {
        continue;
      }
      const auto pending = m_pendingStops.constFind(session.pid);
      const bool stopping = pending != m_pendingStops.cend();
      // Prefer the recorder's own clock, the accumulated seconds plus whatever has
      // elapsed since the last flush, so the view agrees with what gets recorded.
      const qint64 sinceFlush =
          session.heartbeatAt > 0 ? qMax<qint64>(0, now - session.heartbeatAt)
                                  : qMax<qint64>(0, now - session.startedAt);
      rows.append(QVariantMap{
          {QStringLiteral("path"), session.gamePath},
          {QStringLiteral("name"), SessionDisplay::titleForGamePath(session.gamePath)},
          {QStringLiteral("source"), session.source},
          {QStringLiteral("pid"), session.pid},
          {QStringLiteral("procStart"), session.procStart},
          {QStringLiteral("startedAt"), session.startedAt},
          {QStringLiteral("elapsedSeconds"), session.seconds + sinceFlush},
          {QStringLiteral("stopping"), stopping},
          {QStringLiteral("stoppable"), !titled},
          {QStringLiteral("forceReady"),
           stopping && pending->deadline > 0 && now >= pending->deadline},
      });
    }
    for (auto attempt = m_pendingStops.begin(); attempt != m_pendingStops.end();) {
      const bool stillOpen = std::any_of(
          open.cbegin(), open.cend(), [&attempt](const SessionDatabase::SessionRow& session) {
            return session.pid == attempt.key() && session.procStart == attempt.value().procStart;
          });
      attempt = stillOpen ? std::next(attempt) : m_pendingStops.erase(attempt);
    }
  } else {
    m_pendingStops.clear();
  }
  if (m_nowPlayingTimer != nullptr) {
    m_nowPlayingTimer->setInterval(rows.isEmpty() ? kNowPlayingIdleIntervalMs
                                                  : kNowPlayingActiveIntervalMs);
  }
  if (rows == m_nowPlaying) {
    return;
  }
  m_nowPlaying = rows;
  emit nowPlayingChanged();
}

bool PlaySessionStore::trackedSessionOpen(qint64 pid, qint64 procStart) {
  // A session recorded from a window title has no verified process identity, so
  // it is never a stop candidate.
  if (pid <= 0 || procStart <= 0) {
    return false;
  }
  const QVector<SessionDatabase::SessionRow> open = SessionDatabase::openSessions(m_database);
  return std::any_of(open.cbegin(), open.cend(), [pid, procStart](const SessionDatabase::SessionRow& session) {
    return session.pid == pid && session.procStart == procStart;
  });
}

bool PlaySessionStore::stopSession(qint64 pid, qint64 procStart) {
  // Only a game the recorder is tracking right now can be stopped from here.
  if (!m_valid || !trackedSessionOpen(pid, procStart)) {
    return false;
  }
  const SessionStopper::Result result =
      SessionStopper::terminate(pid, procStart, ProcFs::processAlive, ProcFs::sendSignal);
  if (result == SessionStopper::Result::Signalled) {
    m_pendingStops.insert(
        pid, StopAttempt{procStart, QDateTime::currentSecsSinceEpoch() + kStopGraceSeconds});
  } else {
    m_pendingStops.remove(pid);
  }
  refreshNowPlaying();
  return result == SessionStopper::Result::Signalled;
}

bool PlaySessionStore::forceStopSession(qint64 pid, qint64 procStart) {
  if (!m_valid || !trackedSessionOpen(pid, procStart)) {
    return false;
  }
  const SessionStopper::Result result =
      SessionStopper::forceKill(pid, procStart, ProcFs::processAlive, ProcFs::sendSignal);
  if (result != SessionStopper::Result::Signalled) {
    m_pendingStops.remove(pid);
  }
  refreshNowPlaying();
  return result == SessionStopper::Result::Signalled;
}

QString PlaySessionStore::provenance(const PlaySessionStore* store, const QString& path,
                                     qint64 importedSeconds) {
  const QString imported = importedSeconds < 0
      ? QStringLiteral("No imported emulator playtime")
      : QStringLiteral("Imported from emulator: %1").arg(GameRoles::formatPlaytime(importedSeconds));
  if (!store || !store->m_valid) return imported;
  return imported + QStringLiteral(" · Recorded by Omakade: %1%2")
      .arg(GameRoles::formatPlaytime(store->m_trackedSeconds.value(path, 0)),
           store->enabled() ? QString{} : QStringLiteral(" (not applied while recording is off)"));
}

bool PlaySessionStore::enabled() const { return m_enabled; }

void PlaySessionStore::setEnabled(bool value) {
  if (m_enabled == value) {
    return;
  }
  m_enabled = value;
  emit enabledChanged();
  refresh();
  emit totalsChanged();
}

void PlaySessionStore::captureBaseline(const QString& gamePath, qint64 importedSeconds) {
  if (!m_valid || !m_enabled || m_baselines.contains(gamePath)) {
    return;
  }
  SessionDatabase::captureBaseline(m_database, gamePath, importedSeconds,
                                   QDateTime::currentSecsSinceEpoch());
  m_baselines = SessionDatabase::baselinesByPath(m_database);
}

qint64 PlaySessionStore::displaySeconds(const QString& gamePath, qint64 importedSeconds) const {
  if (!m_valid || !m_enabled || gamePath.isEmpty()) {
    return importedSeconds;
  }
  return merge(importedSeconds, m_baselines.value(gamePath, 0),
               m_trackedSeconds.value(gamePath, 0));
}

qint64 PlaySessionStore::sessionLastPlayed(const QString& gamePath) const {
  if (!m_valid || !m_enabled || gamePath.isEmpty()) {
    return 0;
  }
  return m_lastPlayed.value(gamePath, 0);
}

qint64 PlaySessionStore::merge(qint64 importedSeconds, qint64 baselineSeconds,
                               qint64 trackedSeconds) {
  return qMax(importedSeconds, baselineSeconds + trackedSeconds);
}

qint64 PlaySessionStore::displayedSeconds(const PlaySessionStore* store, const QString& gamePath,
                                          qint64 importedSeconds) {
  return store == nullptr ? importedSeconds : store->displaySeconds(gamePath, importedSeconds);
}

qint64 PlaySessionStore::displayedLastPlayed(const PlaySessionStore* store, const QString& gamePath,
                                             qint64 importedLastPlayed) {
  return store == nullptr ? importedLastPlayed
                          : qMax(importedLastPlayed, store->sessionLastPlayed(gamePath));
}

void PlaySessionStore::refresh() {
  refreshRecorderStatus();
  if (!m_valid) {
    return;
  }
  const QHash<QString, qint64> tracked =
      SessionDatabase::trackedSecondsByPath(m_database);
  const QHash<QString, qint64> lastPlayed =
      SessionDatabase::lastPlayedByPath(m_database);
  if (tracked == m_trackedSeconds && lastPlayed == m_lastPlayed) {
    return;
  }
  m_trackedSeconds = tracked;
  m_lastPlayed = lastPlayed;
  ++m_revision;
  emit totalsChanged();
}

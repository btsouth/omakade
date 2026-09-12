#include "tracking/PlaySessionStore.h"

#include "tracking/SessionDatabase.h"
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

namespace {
constexpr int kRefreshIntervalMs = 20000;
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
}

PlaySessionStore::~PlaySessionStore() {
  m_refreshTimer->stop();
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
                    "SELECT source, started_at, ended_at, seconds FROM play_sessions "
                    "WHERE game_path IN (%1) ORDER BY started_at DESC, id DESC LIMIT ?")
                    .arg(placeholders.join(',')));
  for (const QString& path : paths) query.addBindValue(path);
  query.addBindValue(qBound(1, limit, 20));
  if (!query.exec()) return history;

  while (query.next()) {
    const qint64 endedAt = query.value(2).toLongLong();
    history.append(QVariantMap{{"source", query.value(0).toString()},
                               {"startedAt", query.value(1).toLongLong()},
                               {"endedAt", endedAt},
                               {"seconds", query.value(3).toLongLong()},
                               {"active", endedAt == 0}});
  }
  return history;
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

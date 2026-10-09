#include "tracking/AppNotify.h"
#include "guide/ResidentGuide.h"
#include <QThread>
#include "tracking/AttributionAdapter.h"
#include "tracking/DiscordPresence.h"
#include "tracking/HyprlandWindows.h"
#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionDisplay.h"
#include "tracking/SessionRecorder.h"
#include "tracking/SessionTitleIndex.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
#include <vector>

namespace {
constexpr int kPollIntervalMs = 5000;

// The record switch lives in Omakade's own config so one toggle controls the
// display and the recording. Read with the file's mtime so the poll loop stays
// cheap when nothing changed.
class ConfigToggle {
public:
  bool load() {
    // The application id can come from the environment alone, so it is read before
    // the config file is considered: a development build with no config at all can
    // still publish under its own Discord application.
    const QString fromEnvironment = qEnvironmentVariable("OMAKADE_DISCORD_CLIENT_ID").trimmed();
    if (!fromEnvironment.isEmpty()) {
      m_discordClientId = fromEnvironment;
    }
    const QString path = SessionDatabase::defaultConfigPath();
    QFileInfo info(path);
    if (!info.exists()) {
      return false;
    }
    if (m_checked == info.lastModified()) {
      return m_enabled;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return false;
    }
    m_checked = info.lastModified();
    const QString contents = QString::fromUtf8(file.readAll());
    const QRegularExpression pattern(
        QStringLiteral("(?m)^track_play_sessions\\s*=\\s*(true|false)\\s*$"));
    const QRegularExpressionMatch match = pattern.match(contents);
    m_enabled = !match.hasMatch() || match.captured(1) == QStringLiteral("true");
    // Pause-on-unfocus is opt-in, so an absent key means off.
    const QRegularExpression pausePattern(
        QStringLiteral("(?m)^pause_unfocused_sessions\\s*=\\s*(true|false)\\s*$"));
    const QRegularExpressionMatch pauseMatch = pausePattern.match(contents);
    m_pauseUnfocused = pauseMatch.hasMatch() && pauseMatch.captured(1) == QStringLiteral("true");
    // Discord Rich Presence is opt-in too, and only meaningful with an application
    // id to publish under.
    const QRegularExpression presencePattern(
        QStringLiteral("(?m)^discord_presence\\s*=\\s*(true|false)\\s*$"));
    const QRegularExpressionMatch presenceMatch = presencePattern.match(contents);
    m_discordPresence =
        presenceMatch.hasMatch() && presenceMatch.captured(1) == QStringLiteral("true");
    // The Discord application id the presence is published under. Discord shows the
    // application's own name, so this only selects which one it is. The environment
    // variable read at the top of this method wins over the config file.
    if (fromEnvironment.isEmpty()) {
      const QRegularExpression idPattern(
          QStringLiteral("(?m)^discord_client_id\\s*=\\s*\"([0-9]{5,32})\"\\s*$"));
      const QRegularExpressionMatch idMatch = idPattern.match(contents);
      m_discordClientId = idMatch.hasMatch() ? idMatch.captured(1) : QString{};
    }
    return m_enabled;
  }

  [[nodiscard]] bool pauseUnfocused() const { return m_pauseUnfocused; }
  [[nodiscard]] bool discordPresence() const { return m_discordPresence; }
  [[nodiscard]] QString discordClientId() const { return m_discordClientId; }

private:
  QDateTime m_checked;
  bool m_enabled = true;
  bool m_pauseUnfocused = false;
  bool m_discordPresence = false;
  QString m_discordClientId;
};

} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("omakade-sessiond"));

  // Own the control endpoint before any thread can listen or unlink it. This also
  // protects guide-only startup and survives unavailable recording storage.
  QLockFile instance(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/omakade-sessiond.lock");
  instance.setStaleLockTime(0);
  if (!instance.tryLock(0)) { qWarning("omakade-sessiond: recorder already running or guide instance lock is unavailable"); return 1; }

  // The recorder uses synchronous procfs/database polls. Keep guide input and IPC on
  // an independent event loop, with no GUI application or library models.
  QThread guideThread;
  QObject guideOwner; guideOwner.moveToThread(&guideThread);
  QObject::connect(&guideThread, &QThread::started, &guideOwner, [&guideOwner] { new ResidentGuide(&guideOwner); });
  guideThread.start();
  const auto stopGuide = [&] {
    QMetaObject::invokeMethod(&guideOwner, [&guideOwner] { qDeleteAll(guideOwner.children()); }, Qt::BlockingQueuedConnection);
    guideThread.quit(); guideThread.wait();
  };
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, stopGuide);
  if (app.arguments().contains("--guide-only")) return app.exec();

  const QString databasePath = SessionDatabase::defaultDatabasePath();
  if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
    qWarning("omakade-sessiond: could not create the data directory");
    return app.exec(); // Recording is unavailable; the guide remains usable.
  }
  // One owner per database, including manually started copies of the daemon.
  QLockFile owner(databasePath + QStringLiteral(".sessiond.lock"));
  owner.setStaleLockTime(0);
  if (!owner.tryLock(0)) {
    qWarning("omakade-sessiond: recorder already running or its lock is unavailable");
    return app.exec(); // Recording is unavailable; the guide remains usable.
  }

  QString profileError;
  const ProcessProfileSet profiles = ProcessMatcher::load(ProcessMatcher::profilesPath(), &profileError);
  if (!profileError.isEmpty()) {
    qWarning("omakade-sessiond: %s", qPrintable(profileError));
  }

  QSqlDatabase database;
  if (!SessionDatabase::open(database, SessionDatabase::defaultDatabasePath(),
                             QStringLiteral("omakade-sessiond"))) {
    qWarning("omakade-sessiond: could not open the play session database");
    return app.exec(); // Recording is unavailable; the guide remains usable.
  }

  ConfigToggle toggle;
  SessionRecorder recorder(database);
  // Titles for games an emulator loaded from its own file picker. The index is
  // rebuilt when the library database changes on disk (a scan or a source load),
  // and it is empty on any failure, which leaves attribution exactly as it was.
  SessionTitleIndex titleIndex;
  qint64 libraryTitleToken = SessionTitleIndex::cacheChangeToken(database);
  bool indexed = titleIndex.refresh(database);
  if (indexed && titleIndex.isEmpty()) {
    qInfo("omakade-sessiond: no game titles available for window-title attribution");
  }
  const auto refreshTitles = [&] {
    // Rebuild only when something the index reads has changed. Watching the database
    // file is not usable: the recorder shares that file, and its own session writes move
    // the write-ahead log's timestamp, so a file-based guard would rebuild the whole
    // index on every poll while a game runs. The token fingerprints the cache contents instead,
    // which the recorder never touches.
    const qint64 token = SessionTitleIndex::cacheChangeToken(database);
    if (indexed && token == libraryTitleToken) {
      return;
    }
    if (titleIndex.refresh(database)) {
      libraryTitleToken = token;
      indexed = true;
    }
  };
  // One poll's matches, plus the focus check that goes with the same window
  // snapshot so matches and focus can never disagree.
  struct Poll {
    QVector<SessionMatch> matches;
    std::function<bool(qint64)> unfocused;
  };
  // Evidence from an emulator's own records for a game it loaded from its own file picker.
  // The daemon owns the instance so its per-process state spans polls; a fresh process is
  // baselined on first sight and attributed only once its record shows the game advancing.
  std::vector<std::unique_ptr<AttributionAdapter::Adapter>> attribution;
  attribution.push_back(AttributionAdapter::dolphinTimePlayed());
  attribution.push_back(AttributionAdapter::pcsx2Emulog());
  const auto pollOnce = [&] {
    Poll result;
    // The index the adapter resolves identities through has to stay current whether or not a
    // compositor is available: a game the library gained after the daemon started is exactly
    // what a session with no title still needs to resolve. The rebuild is guarded by a cache
    // fingerprint, so this is a cheap check rather than a reload.
    refreshTitles();
    const QVector<ProcessSnapshot> processes = ProcFs::listProcesses();
    // The file freshness window is measured against the poll's own wall clock, so one poll
    // cannot read evidence as fresher than the moment it was observed.
    const qint64 pollWall = QDateTime::currentSecsSinceEpoch();
    const bool compositor = HyprlandWindows::available();
    const QVector<HyprlandWindows::Window> windows =
        compositor ? HyprlandWindows::list() : QVector<HyprlandWindows::Window>{};
    const ProcessMatcher::AttributionResolver attribute =
        [&](qint64 pid, qint64 procStart, const QString& emulator) {
          for (const auto& adapter : attribution) {
            AttributionAdapter::Result attributed = adapter->attribute(
                emulator, pid, procStart, pollWall,
                [&titleIndex](const QString& identity, const QString& forEmulator) {
                  return titleIndex.pathForGameId(identity, forEmulator);
                });
            if (!attributed.attributed() && !attributed.refused) {
              continue;
            }
            if (attributed.stale && indexed && !windows.isEmpty()) {
              // A stopped record can mean paused play or a return to the emulator's own menu.
              // The window separates them, and only the same game keeps the session running.
              attributed = AttributionAdapter::resolveStoppedHeartbeat(
                  attributed,
                  titleIndex.pathForWindowTitle(
                      HyprlandWindows::titleForPid(windows, pid), emulator));
            }
            if (attributed.attributed() || attributed.refused) {
              return attributed;
            }
          }
          return AttributionAdapter::Result{};
        };
    if (!indexed || !compositor) {
      result.matches =
          ProcessMatcher::matchWithAttribution(processes, profiles, {}, {}, attribute);
      return result;
    }
    result.matches = ProcessMatcher::matchWithAttribution(
        processes, profiles,
        [&windows](qint64 pid) { return HyprlandWindows::titleForPid(windows, pid); },
        [&titleIndex](const QString& title, const QString& emulator) {
          return titleIndex.pathForWindowTitle(title, emulator);
        },
        attribute);
    // The snapshot is copied into the predicate, so the answer describes the poll
    // that produced these matches rather than a later moment.
    result.unfocused = [windows](qint64 pid) {
      return HyprlandWindows::isUnfocused(windows, pid);
    };
    return result;
  };
  const qint64 nowWall = QDateTime::currentSecsSinceEpoch();
  if (toggle.load()) recorder.recover(ProcFs::listProcesses(), profiles, nowWall);
  else recorder.endAll(nowWall);

  // Discord Rich Presence, when it is switched on and an application id is
  // configured. The client is rebuilt when the application id changes, because the
  // daemon starts before any config exists and the id can appear later. It is inert
  // with no id, so nothing is attempted until one is set.
  std::unique_ptr<DiscordPresence::Client> presence;
  QString presenceClientId;
  const auto publishPresence = [&] {
    const QString clientId = toggle.discordClientId();
    if (presence == nullptr || clientId != presenceClientId) {
      // A different application, or the first one seen, plus anything already
      // published belongs to the old client and has to be re-sent.
      presenceClientId = clientId;
      presence = std::make_unique<DiscordPresence::Client>(
          clientId, DiscordPresence::socketCandidates(
                        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)));
    }
    // Switching the toggle off, switching recording off or losing sight of every
    // game clears the presence rather than leaving a stale game showing.
    const QVector<SessionRecorder::ActiveInfo> active =
        toggle.discordPresence() ? recorder.activeSessions()
                                 : QVector<SessionRecorder::ActiveInfo>{};
    QJsonObject activity;
    if (!active.isEmpty()) {
      activity = DiscordPresence::sessionActivity(
          SessionDisplay::titleForGamePath(active.first().gamePath), active.first().emulator,
          active.first().startedAt, static_cast<int>(active.size()));
    }
    presence->publishActivity(activity);
  };

  QTimer poll;
  // Notify once when the journal first becomes unavailable, rather than every poll.
  bool journalWasAvailable = recorder.journalAvailable();
  QObject::connect(&poll, &QTimer::timeout, [&] {
    if (!toggle.load()) {
      recorder.endAll(QDateTime::currentSecsSinceEpoch());
    } else {
      recorder.setPauseUnfocused(toggle.pauseUnfocused());
      const Poll result = pollOnce();
      recorder.sync(result.matches, QDateTime::currentSecsSinceEpoch(),
                    toggle.pauseUnfocused() ? result.unfocused
                                            : std::function<bool(qint64)>{});
    }
    publishPresence();
    if (recorder.takeStorageFailure()) {
      qWarning("omakade-sessiond: session storage failed; pending progress may be lost if the "
               "recorder exits");
      AppNotify::send("tracking-storage-error");
    }
    // The durable journal reports its own trouble rather than dropping accepted work in
    // silence: a corrupt file was set aside once, and a full journal refuses new records.
    if (recorder.takeJournalCorruptRecovered()) {
      qWarning("omakade-sessiond: a corrupt session journal was preserved and replaced");
      AppNotify::send("tracking-journal-corrupt");
    }
    if (recorder.takeJournalCapacityWarning()) {
      qWarning("omakade-sessiond: the session journal is at capacity; further refused writes are "
               "not durable until it drains");
      AppNotify::send("tracking-journal-full");
    }
    const bool journalAvailable = recorder.journalAvailable();
    if (journalWasAvailable && !journalAvailable) {
      qWarning("omakade-sessiond: the session journal storage is unavailable");
      AppNotify::send("tracking-journal-corrupt");
    }
    journalWasAvailable = journalAvailable;
    const QStringList rescans = recorder.takeRescanRequests();
    for (const QString& source : rescans) {
      AppNotify::send(QStringLiteral("rescan %1").arg(source).toUtf8());
    }
  });
  poll.start(kPollIntervalMs);
  return app.exec();
}

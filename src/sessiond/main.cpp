#include "tracking/AppNotify.h"
#include "tracking/HyprlandWindows.h"
#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionRecorder.h"
#include "tracking/SessionTitleIndex.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int kPollIntervalMs = 5000;

// The record switch lives in Omakade's own config so one toggle controls the
// display and the recording. Read with the file's mtime so the poll loop stays
// cheap when nothing changed.
class ConfigToggle {
public:
  bool load() {
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
    const QRegularExpression pattern(
        QStringLiteral("(?m)^track_play_sessions\\s*=\\s*(true|false)\\s*$"));
    const QRegularExpressionMatch match = pattern.match(QString::fromUtf8(file.readAll()));
    m_enabled = !match.hasMatch() || match.captured(1) == QStringLiteral("true");
    return m_enabled;
  }

private:
  QDateTime m_checked;
  bool m_enabled = true;
};

QString profilesPath() {
  const QString userPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                           QStringLiteral("/omakade/sessiond-profiles.json");
  if (QFileInfo::exists(userPath)) {
    return userPath;
  }
  return QStringLiteral(OMAKADE_SESSIOND_PROFILES);
}
} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("omakade-sessiond"));

  const QString databasePath = SessionDatabase::defaultDatabasePath();
  if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
    qWarning("omakade-sessiond: could not create the data directory");
    return 1;
  }
  // One owner per database, including manually started copies of the daemon.
  QLockFile owner(databasePath + QStringLiteral(".sessiond.lock"));
  owner.setStaleLockTime(0);
  if (!owner.tryLock(0)) {
    qWarning("omakade-sessiond: recorder already running or its lock is unavailable");
    return 1;
  }

  QString profileError;
  const ProcessProfileSet profiles = ProcessMatcher::load(profilesPath(), &profileError);
  if (!profileError.isEmpty()) {
    qWarning("omakade-sessiond: %s", qPrintable(profileError));
  }

  QSqlDatabase database;
  if (!SessionDatabase::open(database, SessionDatabase::defaultDatabasePath(),
                             QStringLiteral("omakade-sessiond"))) {
    qWarning("omakade-sessiond: could not open the play session database");
    return 1;
  }

  ConfigToggle toggle;
  SessionRecorder recorder(database);
  // Titles for games an emulator loaded from its own file picker. The index is
  // rebuilt when the library database changes on disk (a scan or a source load),
  // and it is empty on any failure, which leaves attribution exactly as it was.
  SessionTitleIndex titleIndex;
  QFileInfo libraryInfo(SessionDatabase::defaultDatabasePath());
  const bool indexed = titleIndex.refresh(database);
  if (indexed && titleIndex.isEmpty()) {
    qInfo("omakade-sessiond: no game titles available for window-title attribution");
  }
  const auto refreshTitles = [&] {
    QFileInfo info(SessionDatabase::defaultDatabasePath());
    if (info.exists() && info.lastModified() == libraryInfo.lastModified()) {
      return;
    }
    libraryInfo = info;
    titleIndex.refresh(database);
  };
  const auto matchProcesses = [&] {
    if (!indexed || !HyprlandWindows::available()) {
      return ProcessMatcher::match(ProcFs::listProcesses(), profiles);
    }
    refreshTitles();
    const QVector<HyprlandWindows::Window> windows = HyprlandWindows::list();
    return ProcessMatcher::matchWithWindowTitles(
        ProcFs::listProcesses(), profiles,
        [&windows](qint64 pid) { return HyprlandWindows::titleForPid(windows, pid); },
        [&titleIndex](const QString& title, const QString& emulator) {
          return titleIndex.pathForWindowTitle(title, emulator);
        });
  };
  const qint64 nowWall = QDateTime::currentSecsSinceEpoch();
  if (toggle.load()) recorder.recover(ProcFs::listProcesses(), profiles, nowWall);
  else recorder.endAll(nowWall);

  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, [&] {
    if (!toggle.load()) {
      recorder.endAll(QDateTime::currentSecsSinceEpoch());
    } else {
      recorder.sync(matchProcesses(), QDateTime::currentSecsSinceEpoch());
    }
    if (recorder.takeStorageFailure()) {
      qWarning("omakade-sessiond: session storage failed; pending progress may be lost if the "
               "recorder exits");
      AppNotify::send("tracking-storage-error");
    }
    const QStringList rescans = recorder.takeRescanRequests();
    for (const QString& source : rescans) {
      AppNotify::send(QStringLiteral("rescan %1").arg(source).toUtf8());
    }
  });
  poll.start(kPollIntervalMs);
  return app.exec();
}

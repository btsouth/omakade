#include "library/GameRoles.h"
#include "library/PlayStats.h"
#include "library/SteamGameModel.h"
#include "tracking/HyprlandWindows.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/ProcFs.h"
#include "tracking/ProcessMatcher.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionRecorder.h"
#include "tracking/SteamInstalls.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <csignal>
#include <unistd.h>

namespace {
const QString kReaper = QStringLiteral("/home/player/.local/share/Steam/ubuntu12_32/reaper");
const QString kEldenRing = QStringLiteral("1245620");

ProcessSnapshot process(qint64 pid, qint64 parent, qint64 start, const QStringList& arguments) {
  return ProcessSnapshot{.pid = pid, .procStart = start,
                         .comm = QFileInfo(arguments.value(0)).fileName(), .arguments = arguments,
                         .exePath = {}, .winePrefix = {}, .flatpakAppId = {}, .steamAppId = {},
                         .parentPid = parent};
}

QStringList reaper(const QString& appId, const QStringList& extra = {}) {
  QStringList arguments{kReaper, QStringLiteral("SteamLaunch"), QStringLiteral("AppId=") + appId};
  arguments += extra;
  arguments += {QStringLiteral("--"),
                QStringLiteral("/home/player/.local/share/Steam/ubuntu12_32/steam-launch-wrapper"),
                QStringLiteral("--"), QStringLiteral("/games/steam/steamapps/common/Proton/proton"),
                QStringLiteral("waitforexitandrun"), QStringLiteral("game.exe")};
  return arguments;
}

ProcessProfileSet retroArchProfile() {
  ProcessProfileSet profiles;
  profiles.emulators.append({.name = QStringLiteral("RetroArch"),
                             .binaries = {QStringLiteral("retroarch")},
                             .executablePaths = {},
                             .rescanSource = QStringLiteral("RetroArch")});
  profiles.romExtensions.insert(QStringLiteral("sfc"));
  return profiles;
}

// Steam's own tables in a library database, as the Steam source writes them.
void addSteamGame(const QString& databasePath, const QString& appId, const QString& title,
                  const QString& libraryPath, const QString& installDirectory) {
  const QString connection = QUuid::createUuid().toString();
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO games(app_id, title) VALUES(?, ?)"));
    query.addBindValue(appId);
    query.addBindValue(title);
    QVERIFY(query.exec());
    query.prepare(QStringLiteral(
        "INSERT INTO installations(app_id, install_dir, library_path, manifest_path, "
        "observed_at) VALUES(?, ?, ?, ?, 1)"));
    query.addBindValue(appId);
    query.addBindValue(installDirectory);
    query.addBindValue(libraryPath);
    query.addBindValue(libraryPath + QStringLiteral("/steamapps/appmanifest_") + appId +
                       QStringLiteral(".acf"));
    QVERIFY(query.exec());
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

// A real process shaped like Steam's reaper: its command line is
// `<dir>/reaper SteamLaunch AppId=<id> -- game.exe`, and it runs a child the way the
// reaper runs the game. The "reaper" is a link to sh, which reads the SteamLaunch script.
bool startReaper(QProcess& reaperProcess, const QTemporaryDir& directory, const QString& appId) {
  QFile script(directory.filePath(QStringLiteral("SteamLaunch")));
  if (!script.open(QIODevice::WriteOnly)) return false;
  script.write("sleep 30\nexit 0\n");
  script.close();
  const QString link = directory.filePath(QStringLiteral("reaper"));
  if (!QFile::exists(link) && !QFile::link(QStringLiteral("/bin/sh"), link)) return false;
  reaperProcess.setWorkingDirectory(directory.path());
  reaperProcess.start(link, {QStringLiteral("SteamLaunch"), QStringLiteral("AppId=") + appId,
                             QStringLiteral("--"), QStringLiteral("game.exe")});
  return reaperProcess.waitForStarted(5000);
}

// Waits until procfs shows the reaper's child, so the tree is complete before it is read.
QVector<ProcessSnapshot> processesWithChildOf(qint64 pid) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    const QVector<ProcessSnapshot> processes = ProcFs::listProcesses();
    if (ProcFs::processTree(processes, pid).size() > 1) return processes;
    QTest::qWait(20);
  }
  return ProcFs::listProcesses();
}
} // namespace

class SteamRecordingTests : public QObject {
  Q_OBJECT
private slots:
  void reaperOptionsGiveTheAppId() {
    QCOMPARE(ProcessMatcher::steamLaunchAppId(reaper(kEldenRing)), kEldenRing);
    // Steam's first-launch setup runs under the game's app id too, and is not play.
    QVERIFY(ProcessMatcher::steamLaunchAppId(reaper(kEldenRing, {"Install=1"})).isEmpty());
    // Only the reaper's own options count, never the game's command after `--`.
    QVERIFY(ProcessMatcher::steamLaunchAppId(
                {kReaper, "SteamLaunch", "--", "game", "AppId=620"}).isEmpty());
    QVERIFY(ProcessMatcher::steamLaunchAppId({"/usr/bin/bash", "SteamLaunch", "AppId=620"}).isEmpty());
    QVERIFY(ProcessMatcher::steamLaunchAppId({kReaper, "AppId=620", "SteamLaunch"}).isEmpty());
    QVERIFY(ProcessMatcher::steamLaunchAppId({kReaper, "SteamLaunch", "AppId=0", "--"}).isEmpty());
    QVERIFY(ProcessMatcher::steamLaunchAppId({kReaper, "SteamLaunch", "AppId=abc", "--"}).isEmpty());
    QCOMPARE(ProcessMatcher::steamLaunchAppId({"reaper", "SteamLaunch", "AppId=620"}),
             QStringLiteral("620"));
  }

  void steamLaunchesRecordAgainstTheInstallFolder() {
    const QString eldenRing = QStringLiteral("/games/steam/steamapps/common/ELDEN RING");
    const QString portal = QStringLiteral("/games/steam/steamapps/common/Portal 2");
    const QString retroArch = QStringLiteral("/games/steam/steamapps/common/RetroArch");
    const QHash<QString, QString> installs{{kEldenRing, eldenRing},
                                           {QStringLiteral("620"), portal},
                                           {QStringLiteral("1118310"), retroArch}};
    const QVector<ProcessSnapshot> processes{
        process(100, 1, 10, {"/home/player/.local/share/Steam/ubuntu12_32/steam"}),
        process(200, 100, 50, reaper(kEldenRing)),
        process(201, 200, 51, {"steam-launch-wrapper", "--", "proton"}),
        process(203, 201, 53, {"Z:\\games\\steam\\steamapps\\common\\ELDEN RING\\eldenring.exe"}),
        // A second reaper for the same game, as a nested launch could show: one session only.
        process(210, 203, 60, reaper(kEldenRing)),
        // A non-Steam shortcut has an app id with no install folder.
        process(300, 100, 70, reaper("3141592653")),
        // Setup Steam runs before a first launch is not play.
        process(400, 100, 80, reaper("620", {"Install=1"})),
        // Steam's own build of RetroArch: the emulator profile owns that play.
        process(500, 100, 90, reaper("1118310")),
        process(501, 500, 91, {"/games/steam/steamapps/common/RetroArch/retroarch", "-L",
                               "snes9x_libretro.so", "/roms/Chrono Trigger.sfc"}),
        // A process that only mentions a launch in its arguments is not a reaper.
        process(600, 100, 95, {"/usr/bin/bash", "reaper", "SteamLaunch", "AppId=620"})};
    const ProcessProfileSet profiles = retroArchProfile();
    const QVector<SessionMatch> matches =
        ProcessMatcher::matchSteamLaunches(processes, installs, profiles);
    QCOMPARE(matches.size(), 1);
    const SessionMatch& match = matches.first();
    QCOMPARE(match.pid, qint64(200));
    QCOMPARE(match.procStart, qint64(50));
    QCOMPARE(match.gamePath, eldenRing);
    QCOMPARE(match.emulator, QStringLiteral("Steam"));
    QVERIFY(!match.handsOver);
    QVERIFY(!ProcessMatcher::matchCameFromWindowTitle(match));
    // The emulator under Steam's reaper is recorded once, by its own profile.
    const QVector<SessionMatch> emulator = ProcessMatcher::match(processes, profiles);
    QCOMPARE(emulator.size(), 1);
    QCOMPARE(emulator.first().pid, qint64(501));
    QCOMPARE(emulator.first().gamePath, QStringLiteral("/roms/Chrono Trigger.sfc"));
    // Without the library's install table nothing is recorded.
    QVERIFY(ProcessMatcher::matchSteamLaunches(processes, {}, profiles).isEmpty());
    // A process without a verified identity is never matched.
    QVector<ProcessSnapshot> unverified{process(200, 100, -1, reaper(kEldenRing))};
    QVERIFY(ProcessMatcher::matchSteamLaunches(unverified, installs, profiles).isEmpty());
  }

  // Steam starts an emulator such as its own RetroArch through the launch wrapper and the
  // runtime container, so the reaper runs for a while before the emulator shows in its tree,
  // and it can outlive the emulator while the container is cleaned up. Neither gap is
  // recorded as Steam play.
  void emulatorFromSteamIsNeverRecordedAsSteam() {
    const QString retroArch = QStringLiteral("/games/steam/steamapps/common/RetroArch");
    const QString portal = QStringLiteral("/games/steam/steamapps/common/Portal 2");
    const QHash<QString, QString> installs{{QStringLiteral("1118310"), retroArch},
                                           {QStringLiteral("620"), portal}};
    const ProcessProfileSet profiles = retroArchProfile();
    const ProcessSnapshot steam =
        process(100, 1, 10, {"/home/player/.local/share/Steam/ubuntu12_32/steam"});
    const ProcessSnapshot reaperProcess = process(500, 100, 1000, reaper("1118310"));
    const ProcessSnapshot wrapper =
        process(501, 500, 1001, {"steam-launch-wrapper", "--", "pressure-vessel-wrap"});
    const ProcessSnapshot emulator =
        process(502, 501, 1150, {"/games/steam/steamapps/common/RetroArch/retroarch"});
    // Clock ticks: the reaper started at 1000 and is matched once it is older than 1000.
    const qint64 young = 999;
    const qint64 settled = 2000;
    ProcessMatcher::SteamLaunchState state;

    // The first poll lands before the container has started RetroArch.
    QVERIFY(ProcessMatcher::matchSteamLaunches({steam, reaperProcess, wrapper}, installs,
                                               profiles, young, &state)
                .isEmpty());
    // The next one sees it, and the reaper is remembered as the emulator's.
    QVERIFY(ProcessMatcher::matchSteamLaunches({steam, reaperProcess, wrapper, emulator},
                                               installs, profiles, settled, &state)
                .isEmpty());
    QCOMPARE(state.emulatorReapers.size(), 1);
    // RetroArch exits while the reaper waits on the container: still not Steam play.
    QVERIFY(ProcessMatcher::matchSteamLaunches({steam, reaperProcess, wrapper}, installs,
                                               profiles, settled, &state)
                .isEmpty());
    // Once the reaper is gone it is forgotten, so a reused pid starts fresh.
    QVERIFY(ProcessMatcher::matchSteamLaunches({steam}, installs, profiles, settled, &state)
                .isEmpty());
    QVERIFY(state.emulatorReapers.isEmpty());

    // A game that runs no emulator waits out the same delay, then is recorded from its reaper.
    const ProcessSnapshot portalReaper = process(600, 100, 1000, reaper("620"));
    const ProcessSnapshot portalGame = process(601, 600, 1005, {"/games/portal2_linux"});
    QVERIFY(ProcessMatcher::matchSteamLaunches({steam, portalReaper, portalGame}, installs,
                                               profiles, young, &state)
                .isEmpty());
    const QVector<SessionMatch> matches = ProcessMatcher::matchSteamLaunches(
        {steam, portalReaper, portalGame}, installs, profiles, settled, &state);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().pid, qint64(600));
    QCOMPARE(matches.first().gamePath, portal);
    QVERIFY(state.emulatorReapers.isEmpty());
  }

  void installsReadTellsFailureFromNoGames() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
    const QString connection = QUuid::createUuid().toString();
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, path, connection));
      // No Steam tables yet: the read fails, which the recorder must not take as no games.
      QVERIFY(!SteamInstalls::read(database).has_value());
      database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    { SteamGameModel schema(path); }
    const QString again = QUuid::createUuid().toString();
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, path, again));
      const std::optional<QHash<QString, QString>> installs = SteamInstalls::read(database);
      QVERIFY(installs.has_value());
      QVERIFY(installs->isEmpty());
      database.close();
    }
    QSqlDatabase::removeDatabase(again);
  }

  void steamSessionsAreNamedByTheLibrary() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
    const QString library = directory.filePath(QStringLiteral("SteamLibrary"));
    { SteamGameModel schema(path); }
    addSteamGame(path, QStringLiteral("1643320"), QStringLiteral("S.T.A.L.K.E.R. 2: Heart of Chornobyl"),
                 library, QStringLiteral("S.T.A.L.K.E.R. 2"));
    const QString connection = QUuid::createUuid().toString();
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, path, connection));
      const QString installed =
          SteamInstalls::installPath(library, QStringLiteral("S.T.A.L.K.E.R. 2"));
      QCOMPARE(SteamInstalls::sessionTitle(database, QStringLiteral("Steam"), installed),
               QStringLiteral("S.T.A.L.K.E.R. 2: Heart of Chornobyl"));
      // A folder the library no longer lists keeps its whole name, dots and all.
      QCOMPARE(SteamInstalls::sessionTitle(database, QStringLiteral("Steam"),
                                           library + QStringLiteral("/steamapps/common/S.T.A.L.K.E.R. 3")),
               QStringLiteral("S.T.A.L.K.E.R. 3"));
      // Other sessions are named from their game path as before.
      QCOMPARE(SteamInstalls::sessionTitle(database, QStringLiteral("RetroArch"),
                                           QStringLiteral("/roms/Chrono_Trigger.sfc")),
               QStringLiteral("Chrono Trigger"));
      database.close();
    }
    QSqlDatabase::removeDatabase(connection);
  }

  void steamSourceSwitchControlsRecording() {
    QVERIFY(SteamInstalls::sourceEnabled(QString{}));
    QVERIFY(SteamInstalls::sourceEnabled(QStringLiteral("steam_enabled = true\n")));
    QVERIFY(!SteamInstalls::sourceEnabled(QStringLiteral("lutris_enabled = true\nsteam_enabled = false\n")));
    // Only the whole key counts.
    QVERIFY(SteamInstalls::sourceEnabled(QStringLiteral("not_steam_enabled = false\n")));
  }

  void focusFollowsTheReaperTree() {
    const QVector<ProcessSnapshot> processes{
        process(200, 100, 50, reaper(kEldenRing)),
        process(201, 200, 51, {"steam-launch-wrapper"}),
        process(203, 201, 53, {"Z:\\games\\eldenring.exe"}),
        process(300, 100, 60, {"/usr/bin/foot"}),
        // A parent loop in a damaged snapshot cannot hang the walk.
        process(401, 402, 1, {"a"}), process(402, 401, 2, {"b"})};
    const QSet<qint64> tree = ProcFs::processTree(processes, 200);
    QCOMPARE(tree, (QSet<qint64>{200, 201, 203}));
    QCOMPARE(ProcFs::processTree(processes, 401), (QSet<qint64>{401, 402}));
    QCOMPARE(ProcFs::processTree(processes, 999), QSet<qint64>{999});

    const auto windows = [](int gameFocus, int otherFocus) {
      return HyprlandWindows::parse(
          QStringLiteral(R"([{"address":"0x1","pid":203,"title":"ELDEN RING","focusHistoryID":%1},)"
                         R"({"address":"0x2","pid":300,"title":"foot","focusHistoryID":%2}])")
              .arg(gameFocus)
              .arg(otherFocus)
              .toUtf8());
    };
    // The reaper owns no window; the game it started does.
    QVERIFY(!HyprlandWindows::isUnfocused(windows(0, 1), tree));
    QVERIFY(HyprlandWindows::isUnfocused(windows(1, 0), tree));
    // Asked about the reaper alone, as before, it is never paused.
    QVERIFY(!HyprlandWindows::isUnfocused(windows(1, 0), qint64(200)));
    // A tree that has not opened a window yet is not unfocused.
    QVERIFY(!HyprlandWindows::isUnfocused(windows(1, 0), QSet<qint64>{200, 201}));
    // An unknown focus id pauses nothing.
    QVERIFY(!HyprlandWindows::isUnfocused(
        HyprlandWindows::parse(R"([{"pid":203,"title":"ELDEN RING"}])"), tree));
  }

  void procfsReportsTheParent() {
    const qint64 self = getpid();
    for (const ProcessSnapshot& snapshot : ProcFs::listProcesses()) {
      if (snapshot.pid != self) continue;
      QCOMPARE(snapshot.parentPid, qint64(getppid()));
      // Start times and uptime count the same ticks, which is how a reaper's age is told.
      QVERIFY(snapshot.procStart > 0 && snapshot.procStart <= ProcFs::uptimeTicks());
      return;
    }
    QFAIL("the test process was not listed");
  }

  void recordedSteamSessionShowsUnderItsGame() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("library.sqlite3"));
    const QString library = directory.filePath(QStringLiteral("SteamLibrary"));
    { SteamGameModel schema(path); }
    // The folder name differs from the title, so a session that failed to link would show
    // under the folder's name instead.
    addSteamGame(path, kEldenRing, QStringLiteral("ELDEN RING"), library,
                 QStringLiteral("ELDEN RING game"));
    addSteamGame(path, QStringLiteral("620"), QStringLiteral("Portal 2"), library,
                 QStringLiteral("Portal 2"));

    QTemporaryDir launchDirectory;
    QProcess steamLaunch;
    QVERIFY(startReaper(steamLaunch, launchDirectory, kEldenRing));
    const qint64 pid = steamLaunch.processId();
    const QVector<ProcessSnapshot> processes = processesWithChildOf(pid);
    QVERIFY(ProcFs::processTree(processes, pid).size() > 1);

    const QString connection = QUuid::createUuid().toString();
    QString recordedPath;
    {
      QSqlDatabase database;
      QVERIFY(SessionDatabase::open(database, path, connection));
      const std::optional<QHash<QString, QString>> read = SteamInstalls::read(database);
      QVERIFY(read.has_value());
      const QHash<QString, QString> installs = *read;
      QCOMPARE(installs.size(), 2);
      const QVector<SessionMatch> matches =
          ProcessMatcher::matchSteamLaunches(processes, installs, ProcessProfileSet{});
      QCOMPARE(matches.size(), 1);
      QCOMPARE(matches.first().pid, pid);
      recordedPath = matches.first().gamePath;

      const qint64 start = QDateTime(QDate(2026, 3, 4), QTime(20, 0)).toSecsSinceEpoch();
      qint64 clock = 0;
      {
        SessionRecorder recorder(database, [&clock] { return clock; });
        recorder.setFlushIntervalMs(1);
        recorder.sync(matches, start);
        clock += 600000;
        recorder.sync(matches, start + 600);
        // The recorder stops here without closing, as when it is restarted.
      }
      // A new recorder adopts the open Steam session under the same rules as any other.
      SessionRecorder recorder(database, [&clock] { return clock; });
      recorder.setFlushIntervalMs(1);
      recorder.recover(processes, ProcessProfileSet{}, start + 600, matches);
      QCOMPARE(recorder.activeCount(), 1);

      // The Steam session is listed as playing, and is stopped through the library and
      // the guide, which stop the whole game rather than Steam's reaper alone.
      {
        PlaySessionStore store(path);
        store.refreshNowPlaying();
        QCOMPARE(store.nowPlaying().size(), 1);
        const QVariantMap row = store.nowPlaying().first().toMap();
        QCOMPARE(row.value(QStringLiteral("source")).toString(), QStringLiteral("Steam"));
        QCOMPARE(row.value(QStringLiteral("path")).toString(), recordedPath);
        QVERIFY(!row.value(QStringLiteral("stoppable")).toBool());
        QVERIFY(!row.value(QStringLiteral("fromWindowTitle")).toBool());
        // Named by the library's title, not the install folder.
        QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("ELDEN RING"));
      }

      clock += 1200000;
      recorder.sync(matches, start + 1800);
      for (const qint64 member : ProcFs::processTree(ProcFs::listProcesses(), pid))
        if (member != pid) ::kill(static_cast<pid_t>(member), SIGKILL);
      steamLaunch.kill();
      QVERIFY(steamLaunch.waitForFinished(5000));
      recorder.sync({}, start + 1800);
      QCOMPARE(recorder.activeCount(), 0);
      QSqlQuery query(database);
      QVERIFY(query.exec(QStringLiteral(
          "SELECT COUNT(*), MAX(seconds), MAX(source), MAX(game_path), MIN(ended_at) "
          "FROM play_sessions")));
      QVERIFY(query.next());
      QCOMPARE(query.value(0).toInt(), 1);
      QCOMPARE(query.value(1).toLongLong(), qint64(1800));
      QCOMPARE(query.value(2).toString(), QStringLiteral("Steam"));
      QCOMPARE(query.value(3).toString(), recordedPath);
      QVERIFY(query.value(4).toLongLong() > 0);
      query.finish();
      database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    // The library row for the game, read the way Stats reads it.
    SteamGameModel steam(path);
    QVector<PlayStats::LibraryGame> rows;
    for (int row = 0; row < steam.rowCount(); ++row) {
      const QModelIndex index = steam.index(row, 0);
      PlayStats::LibraryGame game;
      game.title = index.data(GameRoles::Title).toString();
      game.source = index.data(GameRoles::Source).toString();
      game.appId = index.data(GameRoles::AppId).toString();
      game.identity = game.source + QChar(0x1f) + game.appId;
      game.paths = {index.data(GameRoles::InstallPath).toString()};
      game.playtimeSeconds = index.data(GameRoles::PlaytimeSeconds).toLongLong();
      if (game.appId == kEldenRing) QCOMPARE(game.paths.first(), recordedPath);
      rows.append(game);
    }
    QCOMPARE(rows.size(), 2);

    PlayStats::Snapshot snapshot;
    snapshot.databasePath = path;
    snapshot.period = QStringLiteral("year");
    snapshot.year = 2026;
    snapshot.library = rows;
    snapshot.today = QDate(2026, 12, 31);
    const PlayStats::Figures figures = PlayStats::compute(snapshot);
    QVERIFY2(figures.error.isEmpty(), qPrintable(figures.error));
    QCOMPARE(figures.headline.value(QStringLiteral("recordedSeconds")).toLongLong(), qint64(1800));
    QCOMPARE(figures.headline.value(QStringLiteral("gamesPlayed")).toInt(), 1);
    QCOMPARE(figures.headline.value(QStringLiteral("topGameTitle")).toString(),
             QStringLiteral("ELDEN RING"));
    QCOMPARE(figures.topGames.size(), 1);
    QCOMPARE(figures.topGames.first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("ELDEN RING"));
    QCOMPARE(figures.bySource.size(), 1);
    QCOMPARE(figures.bySource.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Steam"));
  }
};

QTEST_GUILESS_MAIN(SteamRecordingTests)
#include "SteamRecordingTests.moc"

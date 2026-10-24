#include "library/GameStopService.h"
#include "tracking/GameStop.h"
#include "tracking/ProcFs.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QSemaphore>

namespace {

class FakeSink final : public GameStop::SignalSink {
public:
  struct Call {
    QString lever;
    qint64 pid = 0;
    QString text;
    bool forced = false;
  };

  QVector<Call> calls;
  GameStop::LeverResult terminateResult = GameStop::LeverResult::Done;
  GameStop::LeverResult forceResult = GameStop::LeverResult::Done;

  GameStop::LeverResult terminate(qint64 pid) override {
    calls.append({QStringLiteral("terminate"), pid, {}, false});
    return terminateResult;
  }
  GameStop::LeverResult forceTerminate(qint64 pid) override {
    calls.append({QStringLiteral("force"), pid, {}, false});
    return forceResult;
  }
  GameStop::LeverResult stopWinePrefix(const QString& prefix, bool force) override {
    calls.append({QStringLiteral("prefix"), 0, prefix, force});
    return GameStop::LeverResult::Done;
  }
  GameStop::LeverResult stopFlatpakApp(const QString& appId) override {
    calls.append({QStringLiteral("flatpak"), 0, appId, false});
    return GameStop::LeverResult::Done;
  }

  [[nodiscard]] QStringList levers() const {
    QStringList names;
    for (const Call& call : calls) {
      names.append(call.forced ? call.lever + QStringLiteral("-forced") : call.lever);
    }
    return names;
  }
};

ProcessSnapshot process(qint64 pid, qint64 procStart, const QString& comm,
                        const QStringList& arguments, const QString& exePath = {}) {
  ProcessSnapshot snapshot;
  snapshot.pid = pid;
  snapshot.procStart = procStart;
  snapshot.comm = comm;
  snapshot.arguments = arguments;
  snapshot.exePath = exePath;
  return snapshot;
}

QVariantMap row(const QString& source, const QString& appId, const QString& title) {
  return QVariantMap{{QStringLiteral("source"), source},
                     {QStringLiteral("appId"), appId},
                     {QStringLiteral("title"), title}};
}

bool anyLineContains(const QVariantList& lines, const QString& fragment) {
  for (const QVariant& line : lines) {
    if (line.toString().contains(fragment)) {
      return true;
    }
  }
  return false;
}

bool anyStringContains(const QStringList& strings, const QString& fragment) {
  for (const QString& value : strings) {
    if (value.contains(fragment)) {
      return true;
    }
  }
  return false;
}

} // namespace

class GameStopServiceTests : public QObject {
  Q_OBJECT

private slots:
  void discoveryDoesNotBlockAndSupersededResultsAreDiscarded() {
    GameStopService service;
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<QSemaphore>();
    auto calls = std::make_shared<std::atomic_int>(0);
    service.setRowsProvider([] { return QVariantList{}; });
    service.setSnapshotProvider([gate, entered, calls] {
      ++*calls;
      entered->release();
      gate->tryAcquire(1, 3000);
      return QVector<ProcessSnapshot>{};
    });
    QSignalSpy changed(&service, &GameStopService::liveGamesChanged);
    service.refreshLiveGames();
    QVERIFY(service.scanning());
    QVERIFY(entered->tryAcquire(1, 1000));
    // Refresh requests coalesce, but a post-stop request must not use the old scan.
    service.refreshLiveGames();
    service.refreshLiveGames(true);
    gate->release();
    QTRY_VERIFY(calls->load() == 2);
    QVERIFY(service.scanning());
    gate->release();
    QTRY_VERIFY(!service.scanning());
    QCOMPARE(calls->load(), 2);
    QVERIFY(service.runningGames().isEmpty());
    QCOMPARE(changed.size(), 3); // scan start, replacement start, fresh result
  }

  void confirmedListIsRevalidatedOffTheUiThread() {
    GameStopService service;
    auto gate = std::make_shared<QSemaphore>();
    auto entered = std::make_shared<QSemaphore>();
    service.setSnapshotProvider([gate, entered] {
      entered->release();
      gate->tryAcquire(1, 3000);
      return QVector<ProcessSnapshot>{};
    });
    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stopListedGames({row("Manual", "gone", "Already closed")}));
    QVERIFY(service.busy());
    QVERIFY(entered->tryAcquire(1, 1000));
    gate->release();
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(!service.busy());
    QVERIFY(!finished.at(0).at(0).toBool());
  }

  // The mapping from a library row to an identity is the part that can be
  // wrong, so each source is pinned.
  void identityForDerivesThePrefixPerSource() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);
    const GameStop::GameIdentity steamIdentity = GameStopService::identityFor(steam);
    QCOMPARE(steamIdentity.winePrefixes, QStringList{prefix});
    QVERIFY(steamIdentity.flatpakAppId.isEmpty());
    QCOMPARE(steamIdentity.installPath, steamInstall);

    // A Battle.net row stores its prefix in the launch target.
    QVariantMap battleNet = row(QStringLiteral("Battle.net"), QStringLiteral("wow"),
                                QStringLiteral("World of Warcraft"));
    battleNet.insert(QStringLiteral("launchTarget"),
                     QStringLiteral("/home/user/.local/share/bottles/bottles/battlenet"));
    const GameStop::GameIdentity battleNetIdentity = GameStopService::identityFor(battleNet);
    QCOMPARE(battleNetIdentity.winePrefixes,
             QStringList{QStringLiteral("/home/user/.local/share/bottles/bottles/battlenet")});

    // So does an Xodus Nexus row.
    QVariantMap xodus = row(QStringLiteral("Xodus Nexus"), QStringLiteral("43802bcbb9d5e7ea"),
                            QStringLiteral("Valor Mortis"));
    const QString xodusPrefix =
        QStringLiteral("/home/user/.local/share/xodus-nexus/compatdata/43802bcbb9d5e7ea/pfx");
    xodus.insert(QStringLiteral("launchTarget"), xodusPrefix);
    const GameStop::GameIdentity xodusIdentity = GameStopService::identityFor(xodus);
    QCOMPARE(xodusIdentity.winePrefixes, QStringList{xodusPrefix});
    QVERIFY(xodusIdentity.flatpakAppId.isEmpty());

    // An emulator row's content path is what a session is matched on.
    QVariantMap retroArch = row(QStringLiteral("RetroArch"), QStringLiteral("snes"),
                                QStringLiteral("Chrono Trigger"));
    retroArch.insert(QStringLiteral("installPath"), QStringLiteral("/games/snes/Chrono.sfc"));
    retroArch.insert(QStringLiteral("launchTarget"), QStringLiteral("/cores/snes9x_lr.so"));
    const GameStop::GameIdentity emulatorIdentity = GameStopService::identityFor(retroArch);
    QCOMPARE(emulatorIdentity.gamePaths,
             (QStringList{QStringLiteral("/games/snes/Chrono.sfc"),
                          QStringLiteral("/cores/snes9x_lr.so")}));
    QCOMPARE(emulatorIdentity.emulator, QStringLiteral("RetroArch"));

    // A flatpak emulator keeps its app id in the runner role, and a wine runner
    // name is not an app id.
    QVariantMap ryujinx = row(QStringLiteral("Ryujinx"), QStringLiteral("zelda"),
                              QStringLiteral("Zelda"));
    ryujinx.insert(QStringLiteral("flatpak"), true);
    ryujinx.insert(QStringLiteral("runner"), QStringLiteral("io.github.ryubing.Ryujinx"));
    QCOMPARE(GameStopService::identityFor(ryujinx).flatpakAppId,
             QStringLiteral("io.github.ryubing.Ryujinx"));
    ryujinx.insert(QStringLiteral("runner"), QStringLiteral("proton"));
    QCOMPARE(GameStopService::identityFor(ryujinx).flatpakAppId,
             QStringLiteral("io.github.ryubing.Ryujinx"));
    // Without the flatpak flag the app id is not used at all.
    ryujinx.insert(QStringLiteral("flatpak"), false);
    QVERIFY(GameStopService::identityFor(ryujinx).flatpakAppId.isEmpty());
  }

  void previewNamesTheTargetsAndNothingElse() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);

    GameStopService service;
    service.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    const QVariantList lines = service.preview(steam);
    QVERIFY(lines.isEmpty()); // A configured prefix with no processes is not a running game.

    // A row with nothing to attribute says so rather than showing an empty list.
    QVariantMap nothing = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                              QStringLiteral("Thing"));
    nothing.insert(QStringLiteral("installPath"), QStringLiteral("/usr/bin/wine"));
    QVERIFY(service.preview(nothing).isEmpty());
    QVERIFY(anyStringContains(service.notesFor(nothing), QStringLiteral("Nothing attributable")));
  }

  void liveGamesKeepsOnlyWhatHasATarget() {
    GameStopService service;
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{process(90, 9000, QStringLiteral("game"),
                                             {QStringLiteral("game")},
                                             QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap live = row(QStringLiteral("Manual"), QStringLiteral("live"),
                           QStringLiteral("Live Game"));
    live.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));
    QVariantMap idle = row(QStringLiteral("Manual"), QStringLiteral("idle"),
                           QStringLiteral("Not Running"));
    idle.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Other"));
    service.setRowsProvider([live, idle] { return QVariantList{live, idle}; });

    const QVariantList games = service.liveGames();
    QCOMPARE(games.size(), 1);
    const QVariantMap found = games.first().toMap();
    QCOMPARE(found.value(QStringLiteral("title")).toString(), QStringLiteral("Live Game"));
    QVERIFY(anyLineContains(found.value(QStringLiteral("lines")).toList(),
                            QStringLiteral("pid 90")));
  }

  // The point of the two-phase stop in the service: the interface thread is not
  // the one that runs the levers, and the report says what actually happened.
  void stopRunsOffTheCallingThreadAndReportsWhatClosed() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    // It ignores the graceful signal and dies to the forced one: the first two
    // reads see it alive (the graceful step and the forced step), and the read
    // the post-check makes sees it gone.
    int reads = 0;
    QThread* livenessThread = nullptr;
    service.setLiveness([&reads, &livenessThread](qint64, qint64) {
      livenessThread = QThread::currentThread();
      return ++reads <= 2;
    });
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(91, 9100, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    // The call returned before the levers ran, so the interface stays responsive.
    QVERIFY(service.busy());
    // The process ignores the graceful signal, so the forced step reaches it.
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QVERIFY2(livenessThread != nullptr, "the levers never checked liveness");
    QVERIFY2(livenessThread != QThread::currentThread(),
             "the levers ran on the calling thread");
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QVERIFY2(anyLineContains(finished.first().at(2).toList(), QStringLiteral("closed")),
             "a process that died to the forced signal was not reported as closed");
    QVERIFY2(!anyLineContains(finished.first().at(2).toList(), QStringLiteral("still running")),
             "a process that died was reported as still running");
  }

  void aProcessThatSurvivesIsReportedRatherThanCalledDone() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    service.setLiveness([](qint64, qint64) { return true; });  // it never dies
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(92, 9200, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), false);
    const QVariantList lines = finished.first().at(2).toList();
    QVERIFY2(anyLineContains(lines, QStringLiteral("still running")),
             "a surviving process was reported as closed");
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("Some processes could not be stopped."));
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
  }

  void aForcedProcessMayNeedTimeToExitBeforeTheResultIsChecked() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    int reads = 0;
    service.setLiveness([&reads](qint64, qint64) { return ++reads <= 4; });
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(92, 9200, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    QVERIFY(finished.wait(5000));
    QCOMPARE(sink.levers(), (QStringList{QStringLiteral("terminate"), QStringLiteral("force")}));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QVERIFY(!anyLineContains(finished.first().at(2).toList(), QStringLiteral("still running")));
  }

  void aScopeTargetGoesToTheMatchingLever() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString library = temporary.path() + QStringLiteral("/Library");
    const QString steamInstall = library + QStringLiteral("/steamapps/common/Frostpunk");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/323190/pfx");
    QVERIFY(QDir().mkpath(steamInstall));
    QVERIFY(QDir().mkpath(prefix));

    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    auto reads = std::make_shared<int>(0);
    service.setLiveness([reads](qint64, qint64) { return ++*reads < 4; });
    service.setSnapshotProvider([steamInstall, prefix] {
      auto running = process(42, 4200, "game", {"game"}, steamInstall + "/game");
      running.winePrefix = prefix;
      return QVector<ProcessSnapshot>{running};
    });
    QVariantMap steam = row(QStringLiteral("Steam"), QStringLiteral("323190"),
                            QStringLiteral("Frostpunk"));
    steam.insert(QStringLiteral("installPath"), steamInstall);

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(steam));
    QVERIFY(finished.wait(5000));
    // The prefix takes the graceful step and, since the wineserver reported
    // success and a prefix is still pending, the forced one after the wait.
    QCOMPARE(sink.calls.size(), 2);
    QCOMPARE(sink.calls.first().lever, QStringLiteral("prefix"));
    QCOMPARE(sink.calls.first().text, prefix);
    QCOMPARE(sink.calls.first().forced, false);
    QCOMPARE(sink.calls.at(1).lever, QStringLiteral("force"));
    QCOMPARE(finished.first().at(0).toBool(), true);
  }

  void sharedFlatpakScopeIsListedAndStoppedOnce() {
    GameStopService service; FakeSink sink;
    service.setSignalSink(&sink); service.setGracePeriodMs(0);
    QVariantMap first = row("Steam", "one", "One");
    first.insert("flatpak", true); first.insert("installPath", "/games/one");
    QVariantMap second = row("Steam", "two", "Two");
    second.insert("flatpak", true); second.insert("installPath", "/games/two");
    service.setRowsProvider([first, second] { return QVariantList{first, second}; });
    service.setSnapshotProvider([] {
      auto a = process(42, 4200, "game1", {"game1"}, "/games/one/game1");
      auto b = process(43, 4300, "game2", {"game2"}, "/games/two/game2");
      a.flatpakAppId = b.flatpakAppId = "com.valvesoftware.Steam";
      return QVector<ProcessSnapshot>{a, b};
    });
    auto reads = std::make_shared<int>(0);
    service.setLiveness([reads](qint64, qint64) { return ++*reads == 1; });
    QCOMPARE(service.liveGames().size(), 1);
    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stopAll()); QVERIFY(finished.wait(5000));
    QCOMPARE(sink.calls.size(), 1); QCOMPARE(sink.calls.first().lever, QString("flatpak"));
    QVERIFY(finished.first().at(0).toBool());
  }

  void stopRefusesWhenNothingIsAttributable() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    QVariantMap idle = row(QStringLiteral("Manual"), QStringLiteral("idle"),
                           QStringLiteral("Not Running"));
    idle.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Other"));
    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(!service.stop(idle));
    QVERIFY(!service.busy());
    QVERIFY(sink.calls.isEmpty());
    QCOMPARE(finished.count(), 0);
  }

  void stopAllReportsEveryLiveGameOnce() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString first = temporary.path() + QStringLiteral("/One");
    const QString second = temporary.path() + QStringLiteral("/Two");
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));

    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(0);
    // Each game survives its graceful request and exits after the forced one.
    // Track each PID separately because the settling check can poll more than
    // once before the next game is handled.
    QHash<qint64, int> reads;
    service.setLiveness([&reads](qint64 pid, qint64) { return ++reads[pid] <= 2; });
    const qint64 firstPid = QCoreApplication::applicationPid() + 100000;
    service.setSnapshotProvider([first, second, firstPid] {
      return QVector<ProcessSnapshot>{
          process(firstPid, 9300, QStringLiteral("one"), {QStringLiteral("one")}, first + "/one"),
          process(firstPid + 1, 9400, QStringLiteral("two"), {QStringLiteral("two")}, second + "/two")};
    });
    QVariantMap one = row(QStringLiteral("Manual"), QStringLiteral("one"), QStringLiteral("One"));
    one.insert(QStringLiteral("installPath"), first);
    QVariantMap two = row(QStringLiteral("Manual"), QStringLiteral("two"), QStringLiteral("Two"));
    two.insert(QStringLiteral("installPath"), second);
    // The same game seen twice, as a linked row would be, must not double count.
    service.setRowsProvider([one, two] { return QVariantList{one, two, one}; });

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stopAll());
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    // Each live game takes the graceful step and then the forced one, because
    // the stub only lets go at the post-check.
    QCOMPARE(sink.levers(),
             (QStringList{QStringLiteral("terminate"), QStringLiteral("force"),
                          QStringLiteral("terminate"), QStringLiteral("force")}));
    int titles = 0;
    for (const QVariant& line : finished.first().at(2).toList()) {
      if (line.toString() == QStringLiteral("One")) {
        ++titles;
      }
    }
    QCOMPARE(titles, 1);
  }

  void stopRefusesToStartTwiceAtOnce() {
    GameStopService service;
    FakeSink sink;
    service.setSignalSink(&sink);
    service.setGracePeriodMs(400);
    service.setLiveness([](qint64, qint64) { return false; });
    service.setSnapshotProvider([] {
      return QVector<ProcessSnapshot>{
          process(95, 9500, QStringLiteral("game"), {QStringLiteral("game")},
                  QStringLiteral("/games/native/Thing/game"))};
    });
    QVariantMap game = row(QStringLiteral("Manual"), QStringLiteral("thing"),
                           QStringLiteral("Thing"));
    game.insert(QStringLiteral("installPath"), QStringLiteral("/games/native/Thing"));

    QSignalSpy finished(&service, &GameStopService::finished);
    QVERIFY(service.stop(game));
    QVERIFY(!service.stop(game));
    QVERIFY(finished.wait(5000));
    QVERIFY(!service.busy());
  }
};

QTEST_GUILESS_MAIN(GameStopServiceTests)
#include "GameStopServiceTests.moc"

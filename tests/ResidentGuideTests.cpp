#include "guide/GuideClient.h"
#include "guide/GuidePayload.h"
#include "tracking/SessionDatabase.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QLocalServer>
#include "guide/GuideEnvironment.h"
#include <fcntl.h>
#include <linux/input.h>
#include <QSqlQuery>
#include <sys/stat.h>
#include <unistd.h>
#include <QScopeGuard>
#include <time.h>

class ResidentGuideTests final : public QObject {
  Q_OBJECT
private slots:
  void residentOwnsShortcutWithoutGui();
  void retryWaitsForNewSocket();
  void missingServiceHomeFallback();
  void recordingFailureKeepsGuide();
  void upgradeAndMergedRequests();
  void reconnectRefreshesEnvironment();
  void preparingDeadlineOwnsDecision();
  void failedProvisionRetries();
  void justStartedResidentIsNotRestarted();
  void staleResidentIsRestarted();
  void ambiguousEnvironmentIsUnavailable();
  void gameLaunchCancelsRescan();
};

void ResidentGuideTests::residentOwnsShortcutWithoutGui() {
  QTemporaryDir root; QVERIFY(root.isValid());
  const auto bin = root.path() + "/bin", runtime = root.path() + "/runtime", config = root.path() + "/config", data = root.path() + "/data";
  for (const auto& path : {bin, runtime, config + "/omarchy/plugins/omakade.guide", data + "/omakade"}) QVERIFY(QDir().mkpath(path));
  ::chmod(QFile::encodeName(runtime).constData(), 0700);
  const auto write = [](const QString& path, const QByteArray& contents, bool executable = false) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(contents) != contents.size()) return false; file.close();
    return !executable || QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  };
  QVERIFY(write(config + "/omarchy/plugins/omakade.guide/manifest.json", "{}"));
  QVERIFY(write(config + "/omarchy/shell.json", R"({"plugins":[{"id":"omakade.guide"}]})"));
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  const auto cleanupGame = qScopeGuard([&] { game.kill(); game.waitForFinished(); });
  QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId())); QVERIFY(stat.open(QIODevice::ReadOnly));
  const auto raw = stat.readAll(); const auto start = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ')[19].toLongLong();
  const auto databasePath = data + "/omakade/library.sqlite3";
  QSqlDatabase database; QVERIFY(SessionDatabase::open(database, databasePath, "resident-test"));
  QVERIFY(SessionDatabase::beginSession(database, "/games/test.rom", "Dolphin", QDateTime::currentSecsSinceEpoch(), game.processId(), start) > 0);
  database.close(); database = {}; QSqlDatabase::removeDatabase("resident-test");
  const auto active = QJsonDocument(QJsonObject{{"pid", game.processId()}, {"address", "0x123"}, {"fullscreen", 1}}).toJson(QJsonDocument::Compact);
  const auto clients = QJsonDocument(QJsonArray{QJsonObject{{"pid", game.processId()}, {"address", "0x123"}, {"monitor", 0}}}).toJson(QJsonDocument::Compact);
  const auto queryLog = root.path() + "/queries";
  QVERIFY(write(bin + "/systemctl", "#!/bin/sh\n[ \"$2\" = show-environment ] || exit 1\necho HYPRLAND_INSTANCE_SIGNATURE=late-test\necho WAYLAND_DISPLAY=wayland-test\n", true));
  QVERIFY(write(bin + "/hyprctl", "#!/bin/sh\n[ \"$HYPRLAND_INSTANCE_SIGNATURE\" = late-test ] || exit 1\necho \"$@\" >> '" + queryLog.toUtf8() + "'\nif [ \"$1\" = eval ]; then echo ok; exit; fi\ncase \"$2\" in\nactivewindow) echo '" + active + "';;\nclients) echo '" + clients + "';;\nmonitors) echo '[{\"id\":0,\"name\":\"TEST-1\"}]';;\neval) echo ok;;\nesac\n", true));
  const auto summonFile = root.path() + "/summon.json", shellLog = root.path() + "/shell.log";
  const auto slowSummon = root.path() + "/slow-summon";
  QVERIFY(write(bin + "/omarchy-shell", QString(R"(#!/bin/sh
if [ "$2" = summon ]; then
  {
    [ ! -e '%3' ] || sleep 0.1
    printf '%s\n' "$4"
  } > '%1.tmp' || exit 1
  mv '%1.tmp' '%1' || exit 1
fi
printf '%s %s %s\n' "$1" "$2" "$3" >> '%2'
echo ok
)").arg(summonFile, shellLog, slowSummon).toUtf8(), true));
  auto env = QProcessEnvironment::systemEnvironment();
  env.remove("HYPRLAND_INSTANCE_SIGNATURE"); env.remove("WAYLAND_DISPLAY");
  env.insert("PATH", bin + ':' + env.value("PATH")); env.insert("XDG_RUNTIME_DIR", runtime); env.insert("TMPDIR", runtime);
  env.insert("XDG_CONFIG_HOME", config); env.insert("XDG_DATA_HOME", data); env.insert("XDG_STATE_HOME", root.path() + "/state");
  // Any accidental QGuiApplication path fails. The shortcut must use Qt Core alone.
  env.insert("QT_QPA_PLATFORM", "invalid-platform-for-resident-test");
  env.insert("QT_FORCE_STDERR_LOGGING", "1");
  QProcess daemon; daemon.setProcessEnvironment(env); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only", "--guide-input-test"});
  QVERIFY(daemon.waitForStarted());
  const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  // An early default.target service acquires its desktop environment after login.
  QVERIFY(QDir().mkpath(runtime + "/hypr/late-test"));
  QVERIFY(write(runtime + "/hypr/late-test/hyprland.lock", QByteArray::number(::getpid()) + "\nwayland-test\n"));
  QVERIFY(write(runtime + "/wayland-test", ""));
  QLocalServer events; QVERIFY(events.listen(runtime + "/hypr/late-test/.socket2.sock"));
  QPointer<QLocalSocket> eventPeer;
  connect(&events, &QLocalServer::newConnection, &events, [&] {
    while (auto* peer = events.nextPendingConnection()) eventPeer = peer;
  });
  // Environment discovery may first make a short reachability probe.
  QTRY_VERIFY(eventPeer && eventPeer->state() == QLocalSocket::ConnectedState);
  QTRY_VERIFY(!events.hasPendingConnections());
  QTest::qWait(50);
  QTRY_VERIFY(eventPeer && eventPeer->state() == QLocalSocket::ConnectedState);
  QTRY_VERIFY(QFileInfo::exists(runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid())));
  // A second daemon must fail before either guide socket is removed.
  QProcess second; second.setProcessEnvironment(env); second.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(second.waitForFinished()); QCOMPARE(second.exitCode(), 1);
  const auto marker = root.path() + "/state/omakade/guide-plugin-enabled";
  QVERIFY(!QFileInfo::exists(marker));
  QProcess shortcut; shortcut.setProcessEnvironment(env);
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--game-mode-toggle"});
  QVERIFY(shortcut.waitForFinished(5000));
  QCOMPARE(shortcut.exitCode(), 0);
  QVERIFY2(!shortcut.readAllStandardError().contains("platform plugin"), "Shortcut initialized Qt GUI");
  QTRY_VERIFY(QFileInfo::exists(summonFile));
  QFile summon(summonFile); QVERIFY(summon.open(QIODevice::ReadOnly));
  const auto payload = QJsonDocument::fromJson(summon.readAll()).object();
  QCOMPARE(payload.value("output").toString(), "TEST-1");
  QCOMPARE(payload.value("version").toInt(), GuidePayload::kVersion);
  QCOMPARE(payload.value("data").toObject().value("game").toObject().value("source").toString(), "Dolphin");
  const auto backend = payload.value("backend").toObject();
  QLocalSocket plugin; plugin.connectToServer(backend.value("socket").toString()); QVERIFY(plugin.waitForConnected());
  const auto send = [&](const QString& action) {
    plugin.write(QJsonDocument(QJsonObject{{"version", GuidePayload::kVersion}, {"token", backend.value("token")}, {"action", action}}).toJson(QJsonDocument::Compact) + '\n');
    QVERIFY(plugin.waitForBytesWritten());
  };
  send("opened");
  QTest::qWait(30);
  plugin.readAll();
  plugin.write(QJsonDocument(QJsonObject{{"version", GuidePayload::kVersion}, {"token", backend.value("token")},
    {"action", "inject"}, {"value", QJsonObject{{"type", 1}, {"code", 0x221}, {"value", 1}}}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(plugin.waitForBytesWritten());
  QByteArray inputMessages;
  QTRY_VERIFY(([&] { inputMessages += plugin.readAll(); return inputMessages.contains("\"type\":\"input\""); })());
  // Family changes and input share a socket so a late shell update cannot reset
  // the cursor after navigation has already arrived.
  QVERIFY(inputMessages.indexOf("\"type\":\"update\"") >= 0);
  QVERIFY(inputMessages.indexOf("\"type\":\"update\"") < inputMessages.indexOf("\"type\":\"input\""));
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--guide-toggle"}); QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(([&] { QFile log(shellLog); return log.open(QIODevice::ReadOnly) && log.readAll().contains("shell hide omakade.guide"); })());
  QFile log(shellLog); QVERIFY(log.open(QIODevice::ReadOnly));
  QTRY_VERIFY(([&] { QFile queries(queryLog); return queries.open(QIODevice::ReadOnly) && queries.readAll().contains("hl.dispatch(hl.dsp.focus({window=\"address:0x123\"}))"); })());
  const auto shellCalls = log.readAll(); QVERIFY(!shellCalls.contains("rescanPlugins")); QVERIFY(!shellCalls.contains("call omakade.guide update"));
  const auto control = [&](const QString& action, QJsonObject data = {}) {
    data.insert("action", action);
    QLocalSocket socket; socket.connectToServer(runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid()));
    if (!socket.waitForConnected()) return QJsonObject{};
    socket.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForReadyRead()) return QJsonObject{};
    return QJsonDocument::fromJson(socket.readAll()).object();
  };
  // Clock-only publisher changes must not turn the library's one-second updates
  // back into compositor process spawning.
  QJsonObject published{{"pid", game.processId()}, {"procStart", start}, {"source", "Dolphin"}, {"path", "/games/test.rom"}, {"elapsedSeconds", 1}};
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}}).value("result").toString(), "handled");
  QTest::qWait(100);
  QFile publishedQueries(queryLog); QVERIFY(publishedQueries.open(QIODevice::ReadOnly)); const auto publishCalls = publishedQueries.readAll().count('\n'); publishedQueries.close();
  for (int second = 2; second < 5; ++second) {
    published.insert("elapsedSeconds", second);
    control("publish", {{"sessions", QJsonArray{published}}}); QTest::qWait(40);
  }
  QVERIFY(publishedQueries.open(QIODevice::ReadOnly)); QCOMPARE(publishedQueries.readAll().count('\n'), publishCalls);
  control("publish", {{"sessions", QJsonArray{}}});
  // Publish a game immediately after a cached no-game snapshot. A cache miss must
  // reconcile asynchronously, rather than routing this live game into the library.
  QVERIFY(SessionDatabase::open(database, databasePath, "resident-test-miss"));
  { QSqlQuery clear(database); QVERIFY(clear.exec("UPDATE play_sessions SET ended_at = 1")); }
  eventPeer->write("closewindow>>0x123\n"); eventPeer->flush();
  QTRY_VERIFY(!control("status").value("hasGame").toBool());
  QTRY_VERIFY(QFileInfo::exists(marker));
  QTest::qWait(100);
  QFile queries(queryLog); QVERIFY(queries.open(QIODevice::ReadOnly)); const auto idleCalls = queries.readAll().count('\n'); queries.close();
  QTest::qWait(1100);
  QVERIFY(queries.open(QIODevice::ReadOnly)); QCOMPARE(queries.readAll().count('\n'), idleCalls);
  QVERIFY(SessionDatabase::beginSession(database, "/games/new.rom", "Dolphin", QDateTime::currentSecsSinceEpoch(), game.processId(), start) > 0);
  database.close(); database = {}; QSqlDatabase::removeDatabase("resident-test-miss");
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--game-mode-toggle"});
  QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(([&] { QFile events(shellLog); return events.open(QIODevice::ReadOnly) && events.readAll().count("shell summon omakade.guide") == 2; })());
  QFile secondSummon(summonFile); QVERIFY(secondSummon.open(QIODevice::ReadOnly));
  const auto secondBackend = QJsonDocument::fromJson(secondSummon.readAll()).object().value("backend").toObject();
  QLocalSocket secondPlugin; secondPlugin.connectToServer(secondBackend.value("socket").toString()); QVERIFY(secondPlugin.waitForConnected());
  secondPlugin.write(QJsonDocument(QJsonObject{{"version", 1}, {"token", secondBackend.value("token")}, {"action", "opened"}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(secondPlugin.waitForBytesWritten());
  const auto stopped = [&] {
    QFile state(QStringLiteral("/proc/%1/stat").arg(game.processId()));
    if (!state.open(QIODevice::ReadOnly)) return false;
    const auto raw = state.readAll(); return raw.mid(raw.lastIndexOf(')') + 2).startsWith('T');
  };
  QTRY_VERIFY(stopped());
  QCOMPARE(control("close").value("result").toString(), "handled");
  QTRY_VERIFY(!stopped());
  // A disabled plugin returns Home to its 1.15 Game Mode behavior.
  QVERIFY(write(config + "/omarchy/shell.json", R"({"plugins":[]})"));
  QCOMPARE(control("shortcut").value("result").toString(), "fallback");
  // So does a parked Game Mode session, so Home resumes it exactly as before.
  QVERIFY(write(config + "/omarchy/shell.json", R"({"plugins":[{"id":"omakade.guide"}]})"));
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", QJsonObject{{"gameModeParked", true}}}}).value("result").toString(), "handled");
  QCOMPARE(control("shortcut").value("result").toString(), "fallback");
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", QJsonObject{{"gameModeParked", false}}}}).value("result").toString(), "handled");
  QVERIFY(write(config + "/omarchy/shell.json", R"({"plugins":[{"id":"omakade.guide"}]})"));
  // Desktop from the guide parks Game Mode; the next Home resumes it and comes back to the
  // game with the guide open, not to the library or a 1.15 toggle.
  const auto summons = [&] { QFile calls(shellLog); return calls.open(QIODevice::ReadOnly) ? int(calls.readAll().count("shell summon omakade.guide")) : -1; };
  const auto queried = [&](const QByteArray& text) { QFile calls(queryLog); return calls.open(QIODevice::ReadOnly) && calls.readAll().contains(text); };
  const auto inGameMode = QJsonObject{{"gameModeActive", true}, {"gameModeParked", false}};
  const auto parkedContext = QJsonObject{{"gameModeActive", false}, {"gameModeParked", true}};
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", inGameMode}}).value("result").toString(), "handled");
  QCOMPARE(control("shortcut").value("result").toString(), "handled");
  QTRY_COMPARE(summons(), 3);
  {
    QFile desktopSummon(summonFile); QVERIFY(desktopSummon.open(QIODevice::ReadOnly));
    const auto desktopBackend = QJsonDocument::fromJson(desktopSummon.readAll()).object().value("backend").toObject();
    QLocalSocket desktopPlugin; desktopPlugin.connectToServer(desktopBackend.value("socket").toString()); QVERIFY(desktopPlugin.waitForConnected());
    for (const auto* action : {"opened", "desktop"}) {
      desktopPlugin.write(QJsonDocument(QJsonObject{{"version", GuidePayload::kVersion}, {"token", desktopBackend.value("token")}, {"action", action}}).toJson(QJsonDocument::Compact) + '\n');
      QVERIFY(desktopPlugin.waitForBytesWritten()); QTest::qWait(30);
    }
  }
  QTRY_VERIFY(queried("--game-mode-desktop"));
  QTRY_VERIFY(!stopped()); // Game Mode parks a running game, as when Home is held.
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", parkedContext}}).value("result").toString(), "handled");
  QCOMPARE(control("shortcut", {{"node", "event29"}}).value("result").toString(), "handled");
  QTRY_VERIFY(queried("--game-mode-return"));
  QCOMPARE(summons(), 3);
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", inGameMode}}).value("result").toString(), "handled");
  QTRY_COMPARE(summons(), 4);
  QTRY_VERIFY(stopped());
  QCOMPARE(control("close").value("result").toString(), "handled");
  QTRY_VERIFY(!stopped());
  // Back in Game Mode, a later park is not the guide's: Home resumes it the 1.15 way.
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", parkedContext}}).value("result").toString(), "handled");
  QCOMPARE(control("shortcut").value("result").toString(), "fallback");
  QCOMPARE(control("publish", {{"sessions", QJsonArray{published}}, {"context", inGameMode}}).value("result").toString(), "handled");
  // Force a scheduling gap while the next payload is being written. Observing
  // the summon log must imply that its complete payload has been published.
  QVERIFY(write(slowSummon, ""));
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--game-mode-toggle"}); QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(([&] { QFile calls(shellLog); return calls.open(QIODevice::ReadOnly) && calls.readAll().count("shell summon omakade.guide") == 5; })());
  QFile thirdSummon(summonFile); QVERIFY(thirdSummon.open(QIODevice::ReadOnly));
  QJsonParseError payloadError;
  const auto thirdPayload = QJsonDocument::fromJson(thirdSummon.readAll(), &payloadError).object();
  QCOMPARE(payloadError.error, QJsonParseError::NoError);
  const auto thirdBackend = thirdPayload.value("backend").toObject();
  QCOMPARE(thirdBackend.value("socket"), backend.value("socket"));
  QVERIFY(!thirdBackend.value("token").toString().isEmpty());
  QVERIFY(thirdBackend.value("token") != secondBackend.value("token"));
  QLocalSocket thirdPlugin; thirdPlugin.connectToServer(thirdBackend.value("socket").toString()); QVERIFY(thirdPlugin.waitForConnected());
  thirdPlugin.write(QJsonDocument(QJsonObject{{"version", 1}, {"token", thirdBackend.value("token")}, {"action", "opened"}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(thirdPlugin.waitForBytesWritten()); QTest::qWait(30);
  QCOMPARE(control("close").value("result").toString(), "handled");
  QByteArray messages;
  QTRY_VERIFY2(([&] {
    messages += daemon.readAllStandardError();
    return messages.contains("summon dispatched") && messages.contains("opened elapsed_ms=") && messages.contains("closed elapsed_ms=");
  })(), qPrintable(QString::fromUtf8(messages + daemon.readAllStandardOutput())));
  // A missing resident endpoint routes the shortcut to the 1.15 library command.
  daemon.kill(); QVERIFY(daemon.waitForFinished());
  QLocalServer library; QVERIFY(library.listen(runtime + QStringLiteral("/omakade-%1").arg(::getuid())));
  env.insert("QT_QPA_PLATFORM", "offscreen"); shortcut.setProcessEnvironment(env);
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--game-mode-toggle"});
  QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(library.hasPendingConnections()); auto* peer = library.nextPendingConnection();
  QTRY_VERIFY(peer->bytesAvailable() > 0); QCOMPARE(peer->readAll(), QByteArray("game-mode toggle game-mode-fallback"));

}

namespace {
bool writeTestFile(const QString& path, const QByteArray& data, bool executable = false) {
  QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(data); file.close();
  return !executable || QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
struct Fixture {
  QTemporaryDir root;
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  QString runtime, bin, config, data;
  QLocalServer events;
  Fixture() {
    runtime = root.filePath("runtime"); bin = root.filePath("bin"); config = root.filePath("config"); data = root.filePath("data");
    for (const auto& path : {runtime, bin, config + "/omarchy/plugins/omakade.guide", data + "/omakade"}) QDir().mkpath(path);
    ::chmod(QFile::encodeName(runtime).constData(), 0700);
    environment.insert("XDG_RUNTIME_DIR", runtime); environment.insert("TMPDIR", runtime);
    environment.insert("QT_FORCE_STDERR_LOGGING", "1"); environment.insert("XDG_CONFIG_HOME", config);
    environment.insert("XDG_DATA_HOME", data); environment.insert("XDG_STATE_HOME", root.filePath("state"));
    environment.insert("PATH", bin + ':' + environment.value("PATH"));
    environment.insert("HYPRLAND_INSTANCE_SIGNATURE", "test"); environment.insert("WAYLAND_DISPLAY", "test");
    QDir().mkpath(runtime + "/hypr/test");
    writeTestFile(runtime + "/hypr/test/hyprland.lock", QByteArray::number(::getpid()) + "\ntest\n");
    writeTestFile(runtime + "/test", "");
    events.listen(runtime + "/hypr/test/.socket2.sock");
    writeTestFile(config + "/omarchy/plugins/omakade.guide/manifest.json", "{}");
    writeTestFile(config + "/omarchy/shell.json", R"({"plugins":[{"id":"omakade.guide"}]})");
    writeTestFile(bin + "/systemctl", "#!/bin/sh\nexit 1\n", true);
    writeTestFile(bin + "/hyprctl", "#!/bin/sh\necho '[]'\n", true);
    writeTestFile(bin + "/omarchy-shell", "#!/bin/sh\necho ok\n", true);
  }
  QString endpoint() const { return runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid()); }
};
}

void ResidentGuideTests::retryWaitsForNewSocket() {
  Fixture fixture;
  const auto oldRuntime = qgetenv("XDG_RUNTIME_DIR"), oldPath = qgetenv("PATH");
  qputenv("XDG_RUNTIME_DIR", fixture.runtime.toUtf8()); qputenv("PATH", fixture.environment.value("PATH").toUtf8());
  const auto cleanup = qScopeGuard([&] { qputenv("XDG_RUNTIME_DIR", oldRuntime); qputenv("PATH", oldPath); });
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\nexit 0\n", true));
  QLocalServer server;
  connect(&server, &QLocalServer::newConnection, &server, [&] {
    auto* peer = server.nextPendingConnection();
    connect(peer, &QLocalSocket::readyRead, peer, [peer] {
      peer->readAll(); peer->write("{\"result\":\"handled\"}\n"); peer->flush();
    });
  });
  // A successful systemctl start precedes the daemon opening its socket.
  QTimer::singleShot(350, &server, [&] { QVERIFY(server.listen(fixture.endpoint())); });
  QString result; QElapsedTimer elapsed; elapsed.start();
  GuideClient::requestShortcut({}, this, [&](const QString& reply, const QJsonObject&) { result = reply; });
  QTRY_COMPARE(result, QString("handled")); QVERIFY(elapsed.elapsed() >= 350); QVERIFY(elapsed.elapsed() < 2500);
}

void ResidentGuideTests::missingServiceHomeFallback() {
  Fixture fixture;
  const auto log = fixture.root.filePath("home-fallback");
  QVERIFY(writeTestFile(fixture.bin + "/hyprctl", "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\necho ok\n", true));
  const auto dev = fixture.root.filePath("dev"), sys = fixture.root.filePath("sys/event0/device");
  QVERIFY(QDir().mkpath(dev)); QVERIFY(QDir().mkpath(sys + "/capabilities"));
  QVERIFY(writeTestFile(sys + "/name", "Microsoft X-Box 360 pad\n"));
  QVERIFY(writeTestFile(sys + "/capabilities/key", "7cdb000000000000 0 0 0 0\n"));
  QVERIFY(writeTestFile(sys + "/capabilities/abs", "3003f\n"));
  const auto node = dev + "/event0"; QCOMPARE(::mkfifo(QFile::encodeName(node).constData(), 0600), 0);
  const int writer = ::open(QFile::encodeName(node).constData(), O_RDWR | O_NONBLOCK); QVERIFY(writer >= 0);
  const auto cleanupNode = qScopeGuard([&] { ::close(writer); });
  QProcess button; button.setProcessEnvironment(fixture.environment);
  button.start(QStringLiteral(OMAKADE_GUIDE_BUTTON), {"--dev-dir", dev, "--sys-dir", fixture.root.filePath("sys"), "--command", "omakade --game-mode-toggle"});
  QVERIFY(button.waitForStarted());
  const auto cleanup = qScopeGuard([&] { button.kill(); button.waitForFinished(); });
  QTest::qWait(1200); QCOMPARE(button.state(), QProcess::Running);
  for (int value : {1, 0, 1, 0}) {
    input_event event{}; event.type = EV_KEY; event.code = BTN_MODE; event.value = value;
    QCOMPARE(::write(writer, &event, sizeof(event)), ssize_t(sizeof(event)));
    QTest::qWait(30);
  }
  QTRY_VERIFY_WITH_TIMEOUT(([&] { QFile output(log); return output.open(QIODevice::ReadOnly) && output.readAll().contains("--game-mode-fallback --guide-device"); })(), 5000);
  QTest::qWait(500);
  QFile fallbackLog(log); QVERIFY(fallbackLog.open(QIODevice::ReadOnly)); QCOMPARE(fallbackLog.readAll().count("--game-mode-fallback --guide-device"), 1);
  QFile unit(QStringLiteral(OMAKADE_SOURCE_DIR "/packaging/omakade-guide-button.service")); QVERIFY(unit.open(QIODevice::ReadOnly));
  const auto text = unit.readAll(); QVERIFY(text.contains("Wants=omakade-sessiond.service")); QVERIFY(!text.contains("Requires="));
}

void ResidentGuideTests::recordingFailureKeepsGuide() {
  Fixture fixture;
  QVERIFY(writeTestFile(fixture.data + "/omakade/library.sqlite3", "not a database"));
  QProcess daemon; daemon.setProcessEnvironment(fixture.environment); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {});
  QVERIFY(daemon.waitForStarted());
  const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(fixture.endpoint()));
  QLocalSocket peer; peer.connectToServer(fixture.endpoint()); QVERIFY(peer.waitForConnected());
  peer.write("{\"action\":\"status\"}\n"); peer.flush(); QVERIFY(peer.waitForReadyRead());
  QCOMPARE(QJsonDocument::fromJson(peer.readAll()).object().value("result").toString(), "handled");
  QTest::qWait(100); QCOMPARE(daemon.state(), QProcess::Running);
  QByteArray diagnostic;
  QTRY_VERIFY(([&] { diagnostic += daemon.readAllStandardError(); return diagnostic.contains("could not open the play session database"); })());
}


void ResidentGuideTests::upgradeAndMergedRequests() {
  Fixture fixture;
  const auto oldRuntime = qgetenv("XDG_RUNTIME_DIR"), oldPath = qgetenv("PATH"), oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_RUNTIME_DIR", fixture.runtime.toUtf8()); qputenv("PATH", fixture.environment.value("PATH").toUtf8());
  qputenv("XDG_CONFIG_HOME", fixture.config.toUtf8());
  const auto cleanup = qScopeGuard([&] { qputenv("XDG_RUNTIME_DIR", oldRuntime); qputenv("PATH", oldPath); qputenv("XDG_CONFIG_HOME", oldConfig); });
  const auto log = fixture.root.filePath("systemctl.log");
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\nexit 0\n", true));
  const auto wants = fixture.config + "/systemd/user/default.target.wants"; QVERIFY(QDir().mkpath(wants));
  QVERIFY(QFile::link("/usr/lib/systemd/user/omakade-sessiond.service", wants + "/omakade-sessiond.service"));
  // An old status protocol is insufficient even when the service is active.
  QLocalServer server; QVERIFY(server.listen(fixture.endpoint()));
  connect(&server, &QLocalServer::newConnection, &server, [&] {
    auto* peer = server.nextPendingConnection();
    connect(peer, &QLocalSocket::readyRead, peer, [peer] { peer->readAll(); peer->write("{\"result\":\"handled\"}\n"); peer->flush(); });
  });
  GuideClient::ensureResident(this);
  QTRY_VERIFY(([&] { QFile file(log); return file.open(QIODevice::ReadOnly) && file.readAll().contains("try-restart omakade-sessiond.service"); })());
  QFile file(log); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(file.readAll().contains("reenable omakade-sessiond.service"));
  server.close(); QLocalServer::removeServer(fixture.endpoint());
  QString first, merged;
  GuideClient::requestShortcut({}, this, [&](const QString& reply, const QJsonObject&) { first = reply; });
  GuideClient::requestShortcut({}, this, [&](const QString& reply, const QJsonObject&) { merged = reply; });
  QCOMPARE(merged, QString("handled")); QTRY_COMPARE(first, QString("unavailable"));
}

void ResidentGuideTests::reconnectRefreshesEnvironment() {
  Fixture fixture;
  QVERIFY(QDir().mkpath(fixture.runtime + "/hypr/new-test"));
  QVERIFY(writeTestFile(fixture.runtime + "/hypr/new-test/hyprland.lock", QByteArray::number(::getpid()) + "\nwayland-new\n"));
  QVERIFY(writeTestFile(fixture.runtime + "/wayland-new", ""));
  QLocalServer newer; QVERIFY(newer.listen(fixture.runtime + "/hypr/new-test/.socket2.sock"));
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\necho HYPRLAND_INSTANCE_SIGNATURE=new-test\necho WAYLAND_DISPLAY=wayland-new\n", true));
  auto resolved = GuideEnvironment::resolve(fixture.environment);
  QCOMPARE(resolved.value("HYPRLAND_INSTANCE_SIGNATURE"), "new-test");
  QCOMPARE(resolved.value("WAYLAND_DISPLAY"), "wayland-new");
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\nexit 1\n", true));
  fixture.events.close(); newer.close();
  QVERIFY(QDir().mkpath(fixture.runtime + "/hypr/discovered"));
  QVERIFY(writeTestFile(fixture.runtime + "/hypr/discovered/hyprland.lock", QByteArray::number(::getpid()) + "\nwayland-paired\n"));
  QVERIFY(writeTestFile(fixture.runtime + "/wayland-paired", ""));
  QVERIFY(writeTestFile(fixture.runtime + "/wayland-newest", ""));
  QLocalServer events; QVERIFY(events.listen(fixture.runtime + "/hypr/discovered/.socket2.sock"));
  resolved = GuideEnvironment::resolve(fixture.environment);
  QCOMPARE(resolved.value("HYPRLAND_INSTANCE_SIGNATURE"), "discovered");
  QCOMPARE(resolved.value("WAYLAND_DISPLAY"), "wayland-paired");
}

void ResidentGuideTests::preparingDeadlineOwnsDecision() {
  Fixture fixture;
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  const auto cleanupGame = qScopeGuard([&] { game.kill(); game.waitForFinished(); });
  QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId())); QVERIFY(stat.open(QIODevice::ReadOnly));
  const auto raw = stat.readAll(); const auto start = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ')[19].toLongLong();
  const auto clients = QJsonDocument(QJsonArray{QJsonObject{{"pid", game.processId()}, {"address", "0x123"}, {"monitor", 0}}}).toJson(QJsonDocument::Compact);
  // Two serial reconciliations take 1.9 s. The late second snapshot must not
  // process the request after the 1.5 s preparing response.
  QVERIFY(writeTestFile(fixture.bin + "/hyprctl", "#!/bin/sh\nsleep .95\ncase \"$2\" in\nclients) echo '" + clients + "';;\nactivewindow) echo '{}';;\n*) echo '[]';;\nesac\n", true));
  const auto shellLog = fixture.root.filePath("shell.log");
  QVERIFY(writeTestFile(fixture.bin + "/omarchy-shell", "#!/bin/sh\necho \"$@\" >> '" + shellLog.toUtf8() + "'\necho ok\n", true));
  QProcess daemon; daemon.setProcessEnvironment(fixture.environment); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(daemon.waitForStarted()); const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(fixture.endpoint()));
  QLocalSocket publish; publish.connectToServer(fixture.endpoint()); QVERIFY(publish.waitForConnected());
  publish.write(QJsonDocument(QJsonObject{{"action", "publish"}, {"sessions", QJsonArray{QJsonObject{{"pid", game.processId()}, {"procStart", start}, {"source", "Fixture"}, {"path", "test"}}}}}).toJson(QJsonDocument::Compact) + '\n'); publish.flush();
  QVERIFY(publish.waitForReadyRead());
  QLocalSocket socket; socket.connectToServer(fixture.endpoint()); QVERIFY(socket.waitForConnected());
  QElapsedTimer elapsed; elapsed.start(); socket.write("{\"action\":\"shortcut\"}\n"); socket.flush();
  QVERIFY(socket.waitForReadyRead(1900));
  QCOMPARE(QJsonDocument::fromJson(socket.readAll()).object().value("result").toString(), "preparing");
  QVERIFY(elapsed.elapsed() < 1900);
  QTest::qWait(1100);
  QFile log(shellLog); if (log.open(QIODevice::ReadOnly)) QVERIFY(!log.readAll().contains("summon"));
}

void ResidentGuideTests::failedProvisionRetries() {
  Fixture fixture;
  QFile::remove(fixture.config + "/omarchy/shell.json");
  const auto log = fixture.root.filePath("ensure.log"), ready = fixture.root.filePath("shell-ready");
  QVERIFY(writeTestFile(fixture.bin + "/hyprctl", "#!/bin/sh\n[ \"$2\" = activewindow ] && echo '{}' || echo '[]'\n", true));
  QVERIFY(writeTestFile(fixture.bin + "/omarchy-shell", "#!/bin/sh\necho call >> '" + log.toUtf8() + "'\n[ -f '" + ready.toUtf8() + "' ] && echo ok || exit 1\n", true));
  QProcess daemon; daemon.setProcessEnvironment(fixture.environment); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(daemon.waitForStarted()); const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(log)); QTest::qWait(1800);
  QVERIFY(writeTestFile(ready, "ready"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fixture.root.filePath("state/omakade/guide-plugin-enabled")), 10000);
}


void ResidentGuideTests::justStartedResidentIsNotRestarted() {
  Fixture fixture;
  const auto log = fixture.root.filePath("systemctl.log");
  timespec now{}; ::clock_gettime(CLOCK_MONOTONIC, &now);
  const auto entered = qint64(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n[ \"$2\" = show ] && echo " + QByteArray::number(entered) + "\nexit 0\n", true));
  const auto oldRuntime = qgetenv("XDG_RUNTIME_DIR"), oldPath = qgetenv("PATH");
  qputenv("XDG_RUNTIME_DIR", fixture.runtime.toUtf8()); qputenv("PATH", fixture.environment.value("PATH").toUtf8());
  const auto cleanup = qScopeGuard([&] { qputenv("XDG_RUNTIME_DIR", oldRuntime); qputenv("PATH", oldPath); });
  QLocalServer server;
  connect(&server, &QLocalServer::newConnection, &server, [&] {
    auto* peer = server.nextPendingConnection();
    connect(peer, &QLocalSocket::readyRead, peer, [peer] { peer->readAll(); peer->write("{\"result\":\"handled\",\"ready\":true}\n"); peer->flush(); });
  });
  QTimer::singleShot(400, &server, [&] { QVERIFY(server.listen(fixture.endpoint())); });
  QString result;
  GuideClient::requestShortcut({}, this, [&](const QString& reply, const QJsonObject&) { result = reply; });
  QTRY_COMPARE(result, QString("handled"));
  QFile file(log); QVERIFY(file.open(QIODevice::ReadOnly)); const auto calls = file.readAll();
  QVERIFY(calls.contains("ActiveEnterTimestampMonotonic")); QVERIFY(!calls.contains("try-restart")); QVERIFY(!calls.contains("--user start"));
}
void ResidentGuideTests::staleResidentIsRestarted() {
  Fixture fixture;
  const auto log = fixture.root.filePath("systemctl.log");
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n[ \"$2\" = show ] && echo 1\nexit 0\n", true));
  const auto oldRuntime = qgetenv("XDG_RUNTIME_DIR"), oldPath = qgetenv("PATH");
  qputenv("XDG_RUNTIME_DIR", fixture.runtime.toUtf8()); qputenv("PATH", fixture.environment.value("PATH").toUtf8());
  const auto cleanup = qScopeGuard([&] { qputenv("XDG_RUNTIME_DIR", oldRuntime); qputenv("PATH", oldPath); });
  GuideClient::ensureResident(this);
  QTRY_VERIFY(([&] { QFile file(log); return file.open(QIODevice::ReadOnly) && file.readAll().contains("try-restart"); })());
  QTRY_VERIFY(!property("guideStarting").toBool());
}
void ResidentGuideTests::ambiguousEnvironmentIsUnavailable() {
  Fixture fixture;
  QVERIFY(QDir().mkpath(fixture.runtime + "/hypr/second"));
  QVERIFY(writeTestFile(fixture.runtime + "/hypr/second/hyprland.lock", QByteArray::number(::getpid()) + "\nwayland-second\n"));
  QVERIFY(writeTestFile(fixture.runtime + "/wayland-second", ""));
  QLocalServer second; QVERIFY(second.listen(fixture.runtime + "/hypr/second/.socket2.sock"));
  auto environment = fixture.environment; environment.remove("HYPRLAND_INSTANCE_SIGNATURE"); environment.remove("WAYLAND_DISPLAY");
  auto resolved = GuideEnvironment::resolve(environment);
  QVERIFY(resolved.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()); QVERIFY(resolved.value("WAYLAND_DISPLAY").isEmpty());
  // A manager Wayland hint selects its paired instance, irrespective of mtime.
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\necho WAYLAND_DISPLAY=wayland-second\n", true));
  resolved = GuideEnvironment::resolve(environment);
  QCOMPARE(resolved.value("HYPRLAND_INSTANCE_SIGNATURE"), "second"); QCOMPARE(resolved.value("WAYLAND_DISPLAY"), "wayland-second");
  // Reachable sockets with a dead owner are not compositor candidates.
  QVERIFY(writeTestFile(fixture.runtime + "/hypr/second/hyprland.lock", "999999999\nwayland-second\n"));
  resolved = GuideEnvironment::resolve(environment);
  QCOMPARE(resolved.value("HYPRLAND_INSTANCE_SIGNATURE"), "test"); QCOMPARE(resolved.value("WAYLAND_DISPLAY"), "test");
  QVERIFY(writeTestFile(fixture.runtime + "/hypr/second/hyprland.lock", QByteArray::number(::getpid()) + "\nwayland-second\n"));
  QVERIFY(writeTestFile(fixture.bin + "/systemctl", "#!/bin/sh\nexit 1\n", true));
  QProcess daemon; daemon.setProcessEnvironment(environment); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(daemon.waitForStarted()); const auto cleanupDaemon = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(fixture.endpoint()));
  QLocalSocket shortcut; shortcut.connectToServer(fixture.endpoint()); QVERIFY(shortcut.waitForConnected());
  QElapsedTimer elapsed; elapsed.start(); shortcut.write("{\"action\":\"shortcut\"}\n"); shortcut.flush();
  QVERIFY(shortcut.waitForReadyRead(500));
  QCOMPARE(QJsonDocument::fromJson(shortcut.readAll()).object().value("result").toString(), "fallback");
  QVERIFY(elapsed.elapsed() < 500);
}

void ResidentGuideTests::gameLaunchCancelsRescan() {
  Fixture fixture;
  QFile::remove(fixture.config + "/omarchy/shell.json");
  const auto log = fixture.root.filePath("shell.log"), clientsFile = fixture.root.filePath("clients.json");
  // The fake shell answers ping once the launch is published, within the daemon's 500 ms
  // shell timeout, so the launch lands between ping and rescan even on a loaded machine.
  const auto flag = fixture.root.filePath("published");
  QVERIFY(writeTestFile(clientsFile, "[]"));
  QVERIFY(writeTestFile(fixture.bin + "/hyprctl", "#!/bin/sh\ncase \"$2\" in\nclients) cat '" + clientsFile.toUtf8() + "';;\nactivewindow) echo '{}';;\n*) echo '[]';;\nesac\n", true));
  QVERIFY(writeTestFile(fixture.bin + "/omarchy-shell", "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n[ \"$2\" = ping ] && { for i in $(seq 45); do [ -e '" + flag.toUtf8() + "' ] && break; sleep .01; done; echo ok; exit; }\necho unknown\n", true));
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  const auto cleanupGame = qScopeGuard([&] { game.kill(); game.waitForFinished(); });
  QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId())); QVERIFY(stat.open(QIODevice::ReadOnly));
  const auto raw = stat.readAll(); const auto start = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ')[19].toLongLong();
  QProcess daemon; daemon.setProcessEnvironment(fixture.environment); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(daemon.waitForStarted()); const auto cleanupDaemon = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(log));
  QVERIFY(writeTestFile(clientsFile, QJsonDocument(QJsonArray{QJsonObject{{"pid", game.processId()}, {"address", "0x123"}}}).toJson(QJsonDocument::Compact)));
  QLocalSocket publish; publish.connectToServer(fixture.endpoint()); QVERIFY(publish.waitForConnected());
  publish.write(QJsonDocument(QJsonObject{{"action", "publish"}, {"sessions", QJsonArray{QJsonObject{{"pid", game.processId()}, {"procStart", start}, {"source", "Fixture"}, {"path", "test"}}}}}).toJson(QJsonDocument::Compact) + '\n'); publish.flush();
  QVERIFY(publish.waitForReadyRead());
  QVERIFY(writeTestFile(flag, ""));
  QTest::qWait(700);
  QFile calls(log); QVERIFY(calls.open(QIODevice::ReadOnly)); QVERIFY(!calls.readAll().contains("rescanPlugins"));
}

QTEST_GUILESS_MAIN(ResidentGuideTests)
#include "ResidentGuideTests.moc"

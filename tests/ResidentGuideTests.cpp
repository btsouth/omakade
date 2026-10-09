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

class ResidentGuideTests final : public QObject {
  Q_OBJECT
private slots:
  void residentOwnsShortcutWithoutGui();
  void retryWaitsForNewSocket();
  void missingServiceHomeFallback();
  void recordingFailureKeepsGuide();
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
  QVERIFY(write(bin + "/hyprctl", "#!/bin/sh\n[ \"$HYPRLAND_INSTANCE_SIGNATURE\" = late-test ] || exit 1\necho \"$@\" >> '" + queryLog.toUtf8() + "'\ncase \"$2\" in\nactivewindow) echo '" + active + "';;\nclients) echo '" + clients + "';;\nmonitors) echo '[{\"id\":0,\"name\":\"TEST-1\"}]';;\nesac\n", true));
  const auto summonFile = root.path() + "/summon.json", shellLog = root.path() + "/shell.log";
  QVERIFY(write(bin + "/omarchy-shell", "#!/bin/sh\necho \"$1 $2 $3\" >> '" + shellLog.toUtf8() + "'\n[ \"$2\" = summon ] && echo \"$4\" > '" + summonFile.toUtf8() + "'\necho ok\n", true));
  auto env = QProcessEnvironment::systemEnvironment();
  env.remove("HYPRLAND_INSTANCE_SIGNATURE"); env.remove("WAYLAND_DISPLAY");
  env.insert("PATH", bin + ':' + env.value("PATH")); env.insert("XDG_RUNTIME_DIR", runtime);
  env.insert("XDG_CONFIG_HOME", config); env.insert("XDG_DATA_HOME", data); env.insert("XDG_STATE_HOME", root.path() + "/state");
  // Any accidental QGuiApplication path fails. The shortcut must use Qt Core alone.
  env.insert("QT_QPA_PLATFORM", "invalid-platform-for-resident-test");
  env.insert("QT_FORCE_STDERR_LOGGING", "1");
  QProcess daemon; daemon.setProcessEnvironment(env); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only", "--guide-input-test"});
  QVERIFY(daemon.waitForStarted());
  const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  // An early default.target service acquires its desktop environment after login.
  QVERIFY(QDir().mkpath(runtime + "/hypr/late-test"));
  QLocalServer events; QVERIFY(events.listen(runtime + "/hypr/late-test/.socket2.sock"));
  QTRY_VERIFY(events.hasPendingConnections());
  auto* eventPeer = events.nextPendingConnection(); QVERIFY(eventPeer);
  QTRY_VERIFY(QFileInfo::exists(runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid())));
  // A second daemon must fail before either guide socket is removed.
  QProcess second; second.setProcessEnvironment(env); second.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(second.waitForFinished()); QCOMPARE(second.exitCode(), 1);
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
  QTRY_VERIFY(([&] { QFile queries(queryLog); return queries.open(QIODevice::ReadOnly) && queries.readAll().contains("dispatch focuswindow address:0x123"); })());
  const auto shellCalls = log.readAll(); QVERIFY(!shellCalls.contains("rescanPlugins")); QVERIFY(!shellCalls.contains("call omakade.guide update"));
  const auto control = [&](const QString& action) {
    QLocalSocket socket; socket.connectToServer(runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid()));
    if (!socket.waitForConnected()) return QJsonObject{};
    socket.write(QJsonDocument(QJsonObject{{"action", action}}).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForReadyRead()) return QJsonObject{};
    return QJsonDocument::fromJson(socket.readAll()).object();
  };
  // Publish a game immediately after a cached no-game snapshot. A cache miss must
  // reconcile asynchronously, rather than routing this live game into the library.
  QVERIFY(SessionDatabase::open(database, databasePath, "resident-test-miss"));
  { QSqlQuery clear(database); QVERIFY(clear.exec("UPDATE play_sessions SET ended_at = 1")); }
  eventPeer->write("closewindow>>0x123\n"); eventPeer->flush();
  QTRY_VERIFY(!control("status").value("hasGame").toBool());
  QTest::qWait(100);
  QFile queries(queryLog); QVERIFY(queries.open(QIODevice::ReadOnly)); const auto idleCalls = queries.readAll().count('\n'); queries.close();
  QTest::qWait(1100);
  QVERIFY(queries.open(QIODevice::ReadOnly)); QCOMPARE(queries.readAll().count('\n'), idleCalls);
  QVERIFY(SessionDatabase::beginSession(database, "/games/new.rom", "Dolphin", QDateTime::currentSecsSinceEpoch(), game.processId(), start) > 0);
  database.close(); database = {}; QSqlDatabase::removeDatabase("resident-test-miss");
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--game-mode-toggle"});
  QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(([&] { QFile events(shellLog); return events.open(QIODevice::ReadOnly) && events.readAll().count("shell summon omakade.guide") == 2; })());
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
  Fixture() {
    runtime = root.filePath("runtime"); bin = root.filePath("bin"); config = root.filePath("config"); data = root.filePath("data");
    for (const auto& path : {runtime, bin, config + "/omarchy/plugins/omakade.guide", data + "/omakade"}) QDir().mkpath(path);
    ::chmod(QFile::encodeName(runtime).constData(), 0700);
    environment.insert("XDG_RUNTIME_DIR", runtime); environment.insert("XDG_CONFIG_HOME", config);
    environment.insert("XDG_DATA_HOME", data); environment.insert("XDG_STATE_HOME", root.filePath("state"));
    environment.insert("PATH", bin + ':' + environment.value("PATH"));
    environment.insert("HYPRLAND_INSTANCE_SIGNATURE", "test"); environment.insert("WAYLAND_DISPLAY", "test");
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
  for (int value : {1, 0}) {
    input_event event{}; event.type = EV_KEY; event.code = BTN_MODE; event.value = value;
    QCOMPARE(::write(writer, &event, sizeof(event)), ssize_t(sizeof(event)));
    QTest::qWait(30);
  }
  QTRY_VERIFY_WITH_TIMEOUT(([&] { QFile output(log); return output.open(QIODevice::ReadOnly) && output.readAll().contains("--game-mode-fallback --guide-device"); })(), 5000);
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
  QVERIFY(daemon.readAllStandardError().contains("could not open the play session database"));
}

QTEST_GUILESS_MAIN(ResidentGuideTests)
#include "ResidentGuideTests.moc"

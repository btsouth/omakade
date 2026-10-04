#include "guide/GuideInput.h"
#include "guide/GuideActions.h"
#include <QTemporaryDir>
#include <sys/eventfd.h>
#include "guide/GuidePayload.h"
#include "tracking/ProcFs.h"

#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QProcess>
#include <QtTest>
#include <csignal>
#include <linux/input.h>
#include <sys/wait.h>
#include <unistd.h>

class InGameGuideTests final : public QObject {
  Q_OBJECT
private slots:
  void payloadUnknowns();
  void payloadRoundTrip();
  void pluginParser();
  void buttons();
  void axes();
  void families();
  void guardResumesOnOwnerDeath();
  void guardTreeAndIdentity();
  void protocolExtension();
  void mangoBuilding();
  void notesStorage();
  void couchScale();
  void perDeviceGrab();
  void guardDeathResume();
  void quitEscalation();
  void guardDiesDuringPause();
  void failedPinRetainsRecovery();
};

void InGameGuideTests::payloadUnknowns() {
  const auto empty = GuidePayload::build({}, {}, "DP-2", "keyboard", true, false);
  QVERIFY(!empty.value("data").toObject().contains("game"));
  const auto game = GuidePayload::build({{"name", "Game"}, {"source", "Manual"}}, {}, "DP-2", "xbox", false, false).value("data").toObject().value("game").toObject();
  QVERIFY(!game.contains("totalMinutes"));
  QVERIFY(!game.contains("sessionMinutes"));
  QVERIFY(!game.contains("achievements"));
  QVERIFY(!game.contains("notes"));
}

void InGameGuideTests::payloadRoundTrip() {
  const auto payload = GuidePayload::build({{"name", "fallback"}, {"source", "Steam"}, {"elapsedSeconds", 125}},
      {{"title", "Quoted \"game\" 🕹"}, {"playtimeSeconds", 3600}, {"coverPath", "file:///tmp/a.png"},
       {"achievementsTotal", 12}, {"achievementsUnlocked", 4}}, "HDMI-A-1", "playstation", true, true);
  QJsonObject parsed;
  QVERIFY(GuidePayload::parse(QJsonDocument(payload).toJson(), &parsed));
  QCOMPARE(parsed, payload);
  const auto game = parsed.value("data").toObject().value("game").toObject();
  QCOMPARE(game.value("sessionMinutes").toInteger(), 2);
  QCOMPARE(game.value("totalMinutes").toInteger(), 60);
  QCOMPARE(game.value("kind").toString(), "steam");
  QVERIFY(!GuidePayload::parse("{}", &parsed));
  auto newer = payload; newer.insert("version", 2);
  QVERIFY(!GuidePayload::parse(QJsonDocument(newer).toJson(), &parsed));
  auto invalid = payload; invalid.insert("data", QJsonObject{{"game", "wrong"}});
  QVERIFY(!GuidePayload::parse(QJsonDocument(invalid).toJson(), &parsed));
}

void InGameGuideTests::pluginParser() {
  QFile script(QStringLiteral(OMAKADE_SOURCE_DIR "/omarchy-plugin/GuideProtocol.js"));
  QVERIFY(script.open(QIODevice::ReadOnly));
  auto code = QString::fromUtf8(script.readAll());
  code.remove(".pragma library");
  QJSEngine engine;
  QVERIFY(!engine.evaluate(code).isError());
  const auto parse = engine.globalObject().property("parse");
  const auto valid = GuidePayload::build({{"name", "Live game"}, {"source", "RetroArch"}}, {}, "DP-2", "deck", true, false);
  const auto result = parse.call({QString::fromUtf8(QJsonDocument(valid).toJson())});
  QVERIFY(!result.isNull());
  QCOMPARE(result.property("output").toString(), "DP-2");
  for (const QString invalid : {"{", "{}", "{\"version\":2}", "{\"version\":1,\"output\":\"a\",\"pad\":\"xbox\",\"data\":{\"game\":{}}}"})
    QVERIFY(parse.call({invalid}).isNull());
}

void InGameGuideTests::buttons() {
  GuideInputMap map;
  for (const auto& pair : {qMakePair(BTN_SOUTH, "a"), qMakePair(BTN_EAST, "b"), qMakePair(BTN_WEST, "x"),
      qMakePair(BTN_NORTH, "y"), qMakePair(BTN_TL, "lb"), qMakePair(BTN_TR, "rb"),
      qMakePair(BTN_MODE, "guide"), qMakePair(BTN_START, "start"), qMakePair(BTN_DPAD_DOWN, "down")}) {
    QCOMPARE(map.event(EV_KEY, pair.first, 1), QLatin1String(pair.second));
    QVERIFY(map.event(EV_KEY, pair.first, 1).isEmpty());
    QVERIFY(map.event(EV_KEY, pair.first, 2).isEmpty());
    QVERIFY(map.event(EV_KEY, pair.first, 0).isEmpty());
    QCOMPARE(map.event(EV_KEY, pair.first, 1), QLatin1String(pair.second));
    map.reset();
  }
  QVERIFY(map.event(EV_KEY, BTN_SELECT, 1).isEmpty());
}

void InGameGuideTests::axes() {
  GuideInputMap map;
  map.setAxis(ABS_X, 0, 255, 8);
  QVERIFY(map.event(EV_ABS, ABS_X, 128).isEmpty());
  QCOMPARE(map.event(EV_ABS, ABS_X, 255), "right");
  QVERIFY(map.event(EV_ABS, ABS_X, 230).isEmpty());
  QCOMPARE(map.heldDirections(), QStringList{"right"});
  QVERIFY(map.event(EV_ABS, ABS_X, 128).isEmpty());
  QVERIFY(map.heldDirections().isEmpty());
  QCOMPARE(map.event(EV_ABS, ABS_X, 0), "left");
  QCOMPARE(map.event(EV_ABS, ABS_Y, -32768), "up");
  QCOMPARE(map.event(EV_ABS, ABS_HAT0Y, 1), "down");
  QVERIFY(map.event(EV_ABS, ABS_HAT0Y, 1).isEmpty());
  map.reset();
  QVERIFY(map.heldDirections().isEmpty());
  QVERIFY(map.event(EV_ABS, ABS_Z, 32767).isEmpty());
}

void InGameGuideTests::families() {
  QCOMPARE(GuidePayload::padFamily("Sony DualSense"), "playstation");
  QCOMPARE(GuidePayload::padFamily("Nintendo Switch Pro"), "nintendo");
  QCOMPARE(GuidePayload::padFamily("Steam Deck"), "deck");
  QCOMPARE(GuidePayload::padFamily("Microsoft X-Box"), "xbox");
  QCOMPARE(GuidePayload::padFamily("USB gamepad"), "generic");
}

void InGameGuideTests::guardResumesOnOwnerDeath() {
  QProcess game;
  game.start("sleep", {"30"});
  QVERIFY(game.waitForStarted());
  qint64 start = -1;
  for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  QVERIFY(start > 0);
  QProcess guard;
  guard.start(QStringLiteral(OMAKADE_GUIDE_GUARD));
  QVERIFY(guard.waitForStarted());
  guard.write(QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", game.processId()}, {"start", start}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(guard.waitForReadyRead(3000));
  const auto response = QJsonDocument::fromJson(guard.readAllStandardOutput()).object();
  QVERIFY2(response.value("ok").toBool(), qPrintable(response.value("error").toString()));
  auto state = [&game] {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId()));
    if (!stat.open(QIODevice::ReadOnly)) return QByteArray{};
    const auto data = stat.readAll();
    return data.mid(data.lastIndexOf(')') + 2, 1);
  };
  QCOMPARE(state(), QByteArray("T"));
  guard.closeWriteChannel();
  QVERIFY(guard.waitForFinished(3000));
  QTRY_VERIFY(state() != "T");
  QVERIFY(ProcFs::processAlive(game.processId(), start));
  game.terminate();
  QVERIFY(game.waitForFinished());
}

void InGameGuideTests::guardTreeAndIdentity() {
  QProcess game;
  game.start("python3", {"-u", "-c", "import subprocess; child = subprocess.Popen(['sleep','30']); print(child.pid); child.wait()"});
  QVERIFY(game.waitForStarted());
  QVERIFY(game.waitForReadyRead());
  const auto child = game.readAllStandardOutput().trimmed().toLongLong();
  QVERIFY(child > 1);
  qint64 start = -1;
  for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  QVERIFY(start > 0);
  QProcess guard;
  guard.start(QStringLiteral(OMAKADE_GUIDE_GUARD));
  QVERIFY(guard.waitForStarted());
  auto send = [&guard, &game](qint64 identity) {
    guard.write(QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", game.processId()}, {"start", identity}}).toJson(QJsonDocument::Compact) + '\n');
    if (!guard.waitForReadyRead(3000)) return false;
    return QJsonDocument::fromJson(guard.readAllStandardOutput()).object().value("ok").toBool();
  };
  QVERIFY(!send(start + 1));
  QVERIFY(send(start));
  auto state = [](qint64 pid) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return QByteArray{};
    const auto data = stat.readAll();
    return data.mid(data.lastIndexOf(')') + 2, 1);
  };
  QCOMPARE(state(game.processId()), QByteArray("T"));
  QCOMPARE(state(child), QByteArray("T"));
  guard.closeWriteChannel();
  QVERIFY(guard.waitForFinished(3000));
  QTRY_VERIFY(state(game.processId()) != "T");
  QTRY_VERIFY(state(child) != "T");
  ::kill(child, SIGTERM);
  QVERIFY(game.waitForFinished(3000));
}


void InGameGuideTests::protocolExtension() {
  auto payload = GuidePayload::build({{"name", "Game"}, {"source", "Manual"}}, {}, "DP-2", "xbox", true, false);
  auto data = payload.value("data").toObject(); auto game = data.value("game").toObject();
  game.insert("note", "hello"); game.insert("forceReady", true); data.insert("game", game);
  data.insert("performance", QJsonObject{{"nextLaunch", true}}); payload.insert("data", data);
  QJsonObject result; QVERIFY(GuidePayload::parse(QJsonDocument(payload).toJson(), &result)); QCOMPARE(result, payload);
  QFile file(QStringLiteral(OMAKADE_SOURCE_DIR "/omarchy-plugin/GuideProtocol.js")); QVERIFY(file.open(QIODevice::ReadOnly));
  auto script = QString::fromUtf8(file.readAll()); script.remove(".pragma library"); QJSEngine engine; engine.evaluate(script);
  QVERIFY(!engine.globalObject().property("parse").call({QString::fromUtf8(QJsonDocument(payload).toJson())}).isNull());
}
void InGameGuideTests::mangoBuilding() {
  QProcessEnvironment base; base.insert("KEEP", "yes"); base.insert("MANGOHUD_CONFIG", "full"); base.insert("MANGOHUD_FPS_LIMIT", "144");
  QCOMPARE(GuideActions::mangoEnvironment(base, false, "/tmp/c"), base);
  const auto env = GuideActions::mangoEnvironment(base, true, "/tmp/c");
  QCOMPARE(env.value("MANGOHUD"), "1"); QCOMPARE(env.value("MANGOHUD_CONFIGFILE"), "/tmp/c"); QCOMPARE(env.value("KEEP"), "yes");
  QCOMPARE(env.value("MANGOHUD_CONFIG"), "read_cfg,no_display"); QVERIFY(!env.contains("MANGOHUD_FPS_LIMIT"));
  for (const auto& level : {"off", "fps", "frametime", "full"}) {
    const auto config = GuideActions::mangoConfig("omakade-test", level, 60);
    QVERIFY(config.startsWith("no_display\ncontrol=omakade-test\nfps_limit=60\n"));
    if (QString(level) == "full") QVERIFY(config.contains("full\n"));
    if (QString(level) == "frametime") QVERIFY(config.contains("frame_timing=1"));
  }
  QCOMPARE(GuideActions::mangoVisibilityCommand(false, true), QByteArray(":hud;"));
  QCOMPARE(GuideActions::mangoVisibilityCommand(true, false), QByteArray(":hud;"));
  QVERIFY(GuideActions::mangoVisibilityCommand(true, true).isEmpty());
}
void InGameGuideTests::notesStorage() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  const auto a = GuideActions::key({{"source", "Manual"}, {"path", "../game"}});
  const auto b = GuideActions::key({{"source", "Steam"}, {"path", "../game"}});
  QVERIFY(a != b); QVERIFY(!a.contains('/'));
  QVERIFY(GuideActions::saveNotes(directory.path(), a, "A note 🕹\nSecond line"));
  QCOMPARE(GuideActions::notes(directory.path(), a), QString("A note 🕹\nSecond line"));
  QVERIFY(GuideActions::notes(directory.path(), b).isEmpty());
  QVERIFY(!GuideActions::saveNotes(directory.path(), a, QString(8193, 'a')));
  QVERIFY(GuideActions::saveNotes(directory.path(), a, "")); QVERIFY(GuideActions::notes(directory.path(), a).isEmpty());
}
void InGameGuideTests::couchScale() {
  QFile file(QStringLiteral(OMAKADE_SOURCE_DIR "/omarchy-plugin/GuideSettings.js")); QVERIFY(file.open(QIODevice::ReadOnly));
  auto script = QString::fromUtf8(file.readAll()); script.remove(".pragma library"); QJSEngine engine; QVERIFY(!engine.evaluate(script).isError());
  auto scale = engine.globalObject().property("couchScale");
  QCOMPARE(scale.call({"auto", 800}).toNumber(), 1.5); QCOMPARE(scale.call({"auto", 801}).toNumber(), 1.5);
  QCOMPARE(scale.call({"auto", 200}).toNumber(), 1.25); QCOMPARE(scale.call({"auto", 199}).toNumber(), 1.25);
  QCOMPARE(scale.call({"auto", 201}).toNumber(), 1.0); QCOMPARE(scale.call({"auto", 0}).toNumber(), 1.0);
  QCOMPARE(scale.call({"2", 100}).toNumber(), 2.0);
}
void InGameGuideTests::perDeviceGrab() {
  GuideInput input; int opens = 0;
  input.setAccess({[] { return QList<GuideListener::Controller>{{"event0", "a", "Busy pad", false}, {"event1", "b", "Xbox pad", false}}; },
    [&opens](const QString&) { return opens++ == 0 ? -1 : ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); }, [](int) { return true; }});
  QString family, warning;
  QVERIFY(input.grab("event0", &family, &warning)); QCOMPARE(input.grabbedCount(), size_t(1));
  QVERIFY(warning.contains("Busy pad may still reach the game"));
  input.release(); QCOMPARE(input.grabbedCount(), size_t(0));
}
void InGameGuideTests::guardDeathResume() {
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  qint64 start = -1; for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  QProcess guard; guard.start(QStringLiteral(OMAKADE_GUIDE_GUARD)); QVERIFY(guard.waitForStarted());
  guard.write(QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", game.processId()}, {"start", start}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(guard.waitForReadyRead(3000)); const auto response = QJsonDocument::fromJson(guard.readAllStandardOutput()).object();
  QVERIFY(response.value("ok").toBool()); GuideActions::Tree recovery; QVERIFY(recovery.adopt(response.value("stopped").toArray()));
  guard.kill(); QVERIFY(guard.waitForFinished()); recovery.signal(SIGCONT);
  QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId())); QVERIFY(stat.open(QIODevice::ReadOnly));
  QTRY_VERIFY_WITH_TIMEOUT([&] { stat.seek(0); auto bytes = stat.readAll(); return bytes.mid(bytes.lastIndexOf(')') + 2, 1) != "T"; }(), 3000);
  game.terminate(); QVERIFY(game.waitForFinished());
}
void InGameGuideTests::failedPinRetainsRecovery() {
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  qint64 start = -1; for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  GuideActions::Tree recovery; QVERIFY(recovery.pin(game.processId(), start));
  recovery.signal(SIGSTOP);
  auto ids = recovery.identities(); ids.append(QJsonObject{{"pid", 1}, {"start", 1}});
  QVERIFY(!recovery.adopt(ids)); QVERIFY(!recovery.identities().isEmpty()); recovery.signal(SIGCONT);
  game.terminate(); QVERIFY(game.waitForFinished());
}

void InGameGuideTests::guardDiesDuringPause() {
  QProcess game; game.start("python3", {"-u", "-c", "import subprocess; child=subprocess.Popen(['sleep','30']); print(child.pid); child.wait()"});
  QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead()); const auto child = game.readAllStandardOutput().trimmed().toLongLong();
  qint64 start = -1; for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  QProcess guard; guard.start(QStringLiteral(OMAKADE_GUIDE_GUARD)); QVERIFY(guard.waitForStarted());
  guard.write(QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", game.processId()}, {"start", start}, {"recoverable", true}}).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(guard.waitForReadyRead(3000)); auto reply = QJsonDocument::fromJson(guard.readAllStandardOutput()).object();
  QJsonArray ids{reply.value("pin")}; GuideActions::Tree recovery; QVERIFY(recovery.adopt(ids));
  guard.write("pin-ok\n"); QVERIFY(guard.waitForReadyRead(3000)); reply = QJsonDocument::fromJson(guard.readAllStandardOutput()).object();
  QVERIFY(reply.contains("pin")); ids.append(reply.value("pin")); QVERIFY(recovery.adopt(ids));
  guard.kill(); QVERIFY(guard.waitForFinished()); recovery.signal(SIGCONT);
  QFile stat(QStringLiteral("/proc/%1/stat").arg(game.processId())); QVERIFY(stat.open(QIODevice::ReadOnly));
  QTRY_VERIFY_WITH_TIMEOUT([&] { stat.seek(0); auto bytes = stat.readAll(); return bytes.mid(bytes.lastIndexOf(')') + 2, 1) != "T"; }(), 3000);
  ::kill(child, SIGTERM); QVERIFY(game.waitForFinished());
}

void InGameGuideTests::quitEscalation() {
  QProcess game; game.start("python3", {"-u", "-c", "import signal,time,subprocess; children=[]; signal.signal(signal.SIGTERM,lambda *args: (children.append(subprocess.Popen(['sleep','30'])),print(children[-1].pid))); print('ready'); time.sleep(30)"});
  QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead()); qint64 start = -1;
  for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  GuideActions::Tree tree; QVERIFY(!tree.pin(game.processId(), start + 1)); QVERIFY(tree.pin(game.processId(), start));
  tree.signal(SIGTERM); QVERIFY(game.waitForReadyRead(1000));
  const auto child = game.readAllStandardOutput().split('\n');
  qint64 childPid = 0; for (const auto& line : child) if (line.toLongLong() > 1) childPid = line.toLongLong();
  QVERIFY(childPid > 1); QVERIFY(!game.waitForFinished(100)); QVERIFY(tree.alive());
  QVERIFY(tree.pin(game.processId(), start)); QVERIFY(tree.identities().size() >= 2);
  tree.signal(SIGKILL); QVERIFY(game.waitForFinished());
  QTRY_VERIFY(!tree.alive());
}

QTEST_GUILESS_MAIN(InGameGuideTests)
#include "InGameGuideTests.moc"

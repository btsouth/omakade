#include "guide/GuideInput.h"
#include "guide/GuideActions.h"
#include "guide/GuidePlugin.h"
#include "guide/InGameGuide.h"
#include <QTemporaryDir>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QUrl>
#include <fcntl.h>
#include "tracking/PlaySessionStore.h"
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
  void steamArtSelection();
  void steamArtRejections();
  void payloadUnknowns();
  void payloadRoundTrip();
  void pluginParser();
  void buttons();
  void axes();
  void reportArbitrationAndRepeat();
  void mirroredReportsAndRecovery();
  void families();
  void guardResumesOnOwnerDeath();
  void guardTreeAndIdentity();
  void protocolExtension();
  void mangoBuilding();
  void couchScale();
  void perDeviceGrab();
  void padsChangingWhileOpen();
  void identifyOnGrabbedDevice();
  void trackedQuit();
  void guardDeathResume();
  void quitEscalation();
  void guardDiesDuringPause();
  void failedPinRetainsRecovery();
  void quitKeepsItsOriginalGame();
  void pluginLinksAndEnablesOnce();
  void pluginKeepsUserCopyAndWaitsForShell();
  void pluginFallsBackWhenSummonFails();
  void provisioningAndPauseDoNotBlock();
};

void InGameGuideTests::steamArtSelection() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  const auto cache = directory.path() + "/268910/";
  QVERIFY(QDir().mkpath(cache));
  QImage hero(1920, 620, QImage::Format_RGB32); hero.fill(Qt::blue);
  QImage logo(400, 200, QImage::Format_ARGB32); logo.fill(Qt::transparent);
  QVERIFY(hero.save(cache + "library_hero.jpg")); QVERIFY(logo.save(cache + "logo.png"));
  auto build = [&](const QVariantMap& metadata) {
    return GuidePayload::build({{"source", "Steam"}, {"name", "Cuphead"}}, metadata,
        "DP-2", "xbox", false, false, directory.path()).value("data").toObject().value("game").toObject();
  };
  for (const auto& path : {QString{}, QString("file:///steam/header.jpg"), QString("/steam/header.jpg"), QString("/missing/library_hero.jpg")}) {
    const auto game = build({{"appId", "268910"}, {"heroPath", path}});
    QCOMPARE(game.value("banner").toString(), QUrl::fromLocalFile(cache + "library_hero.jpg").toString());
    QCOMPARE(game.value("logo").toString(), QUrl::fromLocalFile(cache + "logo.png").toString());
    QVERIFY(!game.contains("bannerKind"));
  }
  const auto localHero = directory.path() + "/library_hero.jpg";
  QVERIFY(hero.save(localHero));
  const auto local = build({{"appId", "268910"}, {"heroPath", QUrl::fromLocalFile(localHero).toString()}, {"logoPath", "file:///steam/logo.png"}});
  QCOMPARE(local.value("banner").toString(), QUrl::fromLocalFile(localHero).toString());
  QCOMPARE(local.value("logo").toString(), QString("file:///steam/logo.png"));
  QVERIFY(QFile::remove(cache + "library_hero.jpg"));
  const auto fallback = build({{"appId", "268910"}, {"heroPath", "file:///steam/header.jpg"}});
  QCOMPARE(fallback.value("banner").toString(), QString("file:///steam/header.jpg"));
  QCOMPARE(fallback.value("bannerKind").toString(), QString("header"));
}

void InGameGuideTests::steamArtRejections() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  const auto cache = directory.path() + "/268910/"; QVERIFY(QDir().mkpath(cache));
  QImage small(460, 215, QImage::Format_RGB32); small.fill(Qt::red);
  QVERIFY(small.save(cache + "library_hero.jpg"));
  QFile corrupt(cache + "logo.png"); QVERIFY(corrupt.open(QIODevice::WriteOnly));
  corrupt.write("not an image"); corrupt.close();
  auto build = [&](const QString& source, const QString& id) {
    return GuidePayload::build({{"source", source}, {"name", "Game"}}, {{"appId", id}, {"heroPath", "file:///steam/header.jpg"}},
        "DP-2", "xbox", false, false, directory.path()).value("data").toObject().value("game").toObject();
  };
  const auto invalidArt = build("Steam", "268910");
  QCOMPARE(invalidArt.value("bannerKind").toString(), QString("header")); QVERIFY(!invalidArt.contains("logo"));
  QImage wide(1920, 620, QImage::Format_RGB32); wide.fill(Qt::blue); QVERIFY(wide.save(cache + "library_hero.jpg"));
  for (const auto& source : {QString("Manual"), QString("Heroic"), QString("steam")})
    QCOMPARE(build(source, "268910").value("banner").toString(), QString("file:///steam/header.jpg"));
  for (const auto& id : {QString{}, QString("../268910"), QString("268910/"), QString("268910\n"), QString("٢٦٨٩١٠")})
    QCOMPARE(build("Steam", id).value("banner").toString(), QString("file:///steam/header.jpg"));
}

void InGameGuideTests::payloadUnknowns() {
  const auto empty = GuidePayload::build({}, {}, "DP-2", "keyboard", true, false);
  QVERIFY(!empty.value("data").toObject().contains("game"));
  const auto game = GuidePayload::build({{"name", "Game"}, {"source", "Manual"}}, {}, "DP-2", "xbox", false, false).value("data").toObject().value("game").toObject();
  QVERIFY(!game.contains("totalMinutes"));
  QVERIFY(!game.contains("sessionMinutes"));
  QVERIFY(!game.contains("achievements"));
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
  QVERIFY(!game.contains("bannerKind"));
  const auto steamArt = GuidePayload::build({{"name", "Cuphead"}, {"source", "Steam"}},
      {{"heroPath", "file:///steam/librarycache/268910/header.jpg"}, {"logoPath", "file:///steam/librarycache/268910/logo.png"}},
      "DP-2", "xbox", true, true).value("data").toObject().value("game").toObject();
  QCOMPARE(steamArt.value("bannerKind").toString(), QStringLiteral("header"));
  QCOMPARE(steamArt.value("logo").toString(), QStringLiteral("file:///steam/librarycache/268910/logo.png"));
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
  for (const auto& pair : {qMakePair(BTN_SOUTH, "a"), qMakePair(BTN_EAST, "b"), qMakePair(BTN_MODE, "guide")}) {
    QVERIFY(map.event(EV_KEY, pair.first, 1).isEmpty());
    QCOMPARE(map.report(0), QStringList{pair.second});
    map.event(EV_KEY, pair.first, 1); map.event(EV_KEY, pair.first, 2);
    QVERIFY(map.report(1).isEmpty());
    map.event(EV_KEY, pair.first, 0); QVERIFY(map.report(2).isEmpty());
    map.event(EV_KEY, pair.first, 1); QCOMPARE(map.report(3), QStringList{pair.second});
    map.reset();
  }
}

void InGameGuideTests::axes() {
  GuideInputMap map;
  map.setAxis(ABS_X, 0, 255, 8);
  map.event(EV_ABS, ABS_X, 128); QVERIFY(map.report(0).isEmpty());
  map.event(EV_ABS, ABS_X, 255); QCOMPARE(map.report(1), QStringList{"right"});
  map.event(EV_ABS, ABS_X, 230); QVERIFY(map.report(2).isEmpty());
  QCOMPARE(map.heldDirections(), QStringList{"right"});
  map.event(EV_ABS, ABS_X, 0); QVERIFY(map.report(3).isEmpty()); // reversal requires neutral
  QVERIFY(map.heldDirections().isEmpty());
  map.event(EV_ABS, ABS_X, 128); QVERIFY(map.report(4).isEmpty());
  map.event(EV_ABS, ABS_X, 0); QCOMPARE(map.report(5), QStringList{"left"});
  map.reset(); QVERIFY(map.heldDirections().isEmpty());
}

void InGameGuideTests::reportArbitrationAndRepeat() {
  GuideInputMap map;
  map.event(EV_ABS, ABS_X, 18000); map.event(EV_ABS, ABS_Y, 18000);
  QCOMPARE(map.report(0), QStringList{"down"}); // radial diagonal, dominant-axis tie
  QVERIFY(map.repeat(349).isEmpty()); QCOMPARE(map.repeat(350), QStringList{"down"});
  QVERIFY(map.repeat(449).isEmpty()); QCOMPARE(map.repeat(450), QStringList{"down"});
  QCOMPARE(map.repeat(1000), QStringList{"down"});
  QVERIFY(map.repeat(1079).isEmpty()); QCOMPARE(map.repeat(1080), QStringList{"down"});
  QCOMPARE(map.repeat(9000), QStringList{"down"}); QVERIFY(map.repeat(9000).isEmpty());
  map.reset();
  map.event(EV_ABS, ABS_X, 32767); map.event(EV_ABS, ABS_HAT0Y, -1); map.event(EV_KEY, BTN_DPAD_DOWN, 1);
  QCOMPARE(map.report(0), QStringList{"down"}); // buttons take precedence over hat and stick
  map.reset();
  map.event(EV_ABS, ABS_X, 11000); map.event(EV_ABS, ABS_Y, 11000);
  QCOMPARE(map.report(0), QStringList{"down"}); // each axis below threshold, radius above it
  map.reset(); map.event(EV_KEY, BTN_DPAD_DOWN, 1);
  QVERIFY(map.report(0, true).isEmpty()); QVERIFY(map.repeat(5000).isEmpty());
  QVERIFY(map.report(5001).isEmpty()); // a stale held gesture cannot repeat later
  map.event(EV_KEY, BTN_DPAD_DOWN, 0); map.report(5002);
  map.event(EV_KEY, BTN_DPAD_DOWN, 1); QCOMPARE(map.report(5003), QStringList{"down"});
}

void InGameGuideTests::mirroredReportsAndRecovery() {
  GuideInput input;
  QHash<QString, int> writers;
  GuideInput::Access access;
  access.scan = [] { return QList<GuideListener::Controller>{
    {"event15", "physical", "Microsoft X-Box 360 pad", false},
    {"event16", "virtual", "Microsoft X-Box 360 pad 0", true}}; };
  access.open = [&writers](const QString& node) { int fds[2]; if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC)) return -1; writers[node] = fds[1]; return fds[0]; };
  access.grab = [](int) { return true; }; access.ungrab = [](int) {};
  input.setAccess(access); input.grab("event15", nullptr, nullptr);
  QSignalSpy actions(&input, &GuideInput::action);
  const auto report = [&writers](const QString& node, int type, int code, int value, qint64 age = 0) {
    input_event events[2]{};
    events[0].type = type; events[0].code = code; events[0].value = value;
    events[1].type = EV_SYN; events[1].code = SYN_REPORT;
    if (age) { const auto at = QDateTime::currentMSecsSinceEpoch() - age; for (auto& event : events) { event.input_event_sec = at / 1000; event.input_event_usec = (at % 1000) * 1000; } }
    QCOMPARE(::write(writers[node], events, sizeof(events)), ssize_t(sizeof(events)));
  };
  QTest::qWait(20);
  report("event16", EV_KEY, BTN_MODE, 1, 40); // delayed copy of the opening Home, before grab
  QTest::qWait(20); QCOMPARE(actions.size(), 0);
  report("event16", EV_KEY, BTN_MODE, 0); QTest::qWait(20);
  report("event15", EV_KEY, BTN_DPAD_DOWN, 1); report("event16", EV_KEY, BTN_DPAD_DOWN, 1);
  QTRY_COMPARE(actions.size(), 1); QCOMPARE(actions.first().first().toString(), "down");
  report("event15", EV_KEY, BTN_DPAD_DOWN, 0); report("event16", EV_KEY, BTN_DPAD_DOWN, 0);
  QTest::qWait(20);
  report("event15", EV_KEY, BTN_SOUTH, 1); report("event16", EV_KEY, BTN_SOUTH, 1);
  QTRY_COMPARE(actions.size(), 2);
  report("event15", EV_KEY, BTN_SOUTH, 0); QTest::qWait(20);
  report("event15", EV_KEY, BTN_SOUTH, 1); QTest::qWait(20); QCOMPARE(actions.size(), 2); // mirror still held
  report("event15", EV_KEY, BTN_SOUTH, 0); report("event16", EV_KEY, BTN_SOUTH, 0); QTest::qWait(20);
  report("event15", EV_KEY, BTN_SOUTH, 1); QTRY_COMPARE(actions.size(), 3);
  report("event15", EV_KEY, BTN_DPAD_DOWN, 1, 5000); report("event15", EV_KEY, BTN_DPAD_DOWN, 0, 4000);
  report("event15", EV_KEY, BTN_DPAD_UP, 1, 3000); report("event15", EV_KEY, BTN_DPAD_UP, 0, 2000);
  QTest::qWait(20); QCOMPARE(actions.size(), 3);
  report("event15", EV_SYN, SYN_DROPPED, 0); report("event15", EV_KEY, BTN_EAST, 1);
  QTest::qWait(20); QCOMPARE(actions.size(), 3); // recovery is sampled, never dispatched
  report("event15", EV_KEY, BTN_EAST, 0); QTest::qWait(20);
  report("event15", EV_KEY, BTN_EAST, 1); QTRY_COMPARE(actions.size(), 4);
  // Home must first be neutral on every source, including a delayed virtual mirror.
  report("event16", EV_KEY, BTN_MODE, 1); QTest::qWait(20); // currently armed, one close
  QTRY_COMPARE(actions.size(), 5);
  report("event15", EV_KEY, BTN_MODE, 1); QTest::qWait(20); QCOMPARE(actions.size(), 5);
  report("event15", EV_KEY, BTN_MODE, 0); QTest::qWait(20);
  report("event15", EV_KEY, BTN_MODE, 1); QTest::qWait(20); QCOMPARE(actions.size(), 5);
  input.release(); for (const int fd : writers) ::close(fd);
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
  QTRY_VERIFY(([&] {
    for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
    return start > 0;
  })());
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
  game.insert("forceReady", true); data.insert("game", game);
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
  GuideInput input;
  int writer = -1, ungrabs = 0;
  GuideInput::Access access;
  access.scan = [] { return QList<GuideListener::Controller>{{"event0", "a", "Busy pad", false}, {"event1", "b", "Unavailable pad", false}, {"event2", "c", "Xbox pad", false}}; };
  access.open = [&writer](const QString& node) {
    if (node == "event1") return -1;
    int pipe[2];
    if (::pipe2(pipe, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
    if (node == "event0") writer = pipe[1];
    else ::close(pipe[1]);
    return pipe[0];
  };
  int grabs = 0;
  access.grab = [&grabs](int) { return ++grabs % 2 == 0; };
  access.ungrab = [&ungrabs](int) { ++ungrabs; };
  input.setAccess(access);
  QString family, warning;
  QSignalSpy actions(&input, &GuideInput::action);
  QVERIFY(input.grab("event0", &family, &warning));
  QCOMPARE(input.deviceCount(), size_t(2)); QCOMPARE(input.grabbedCount(), size_t(1));
  QVERIFY(warning.contains("Busy pad may still reach the game"));
  QVERIFY(warning.contains("Unavailable pad could not be opened"));
  input_event event{}; event.type = EV_KEY; event.code = BTN_DPAD_DOWN; event.value = 1;
  input_event syn{}; syn.type = EV_SYN; syn.code = SYN_REPORT;
  QCOMPARE(::write(writer, &event, sizeof(event)), ssize_t(sizeof(event)));
  QCOMPARE(::write(writer, &syn, sizeof(syn)), ssize_t(sizeof(syn)));
  QTRY_COMPARE(actions.size(), 1); QCOMPARE(actions.first().first().toString(), "down");
  input.release(); ::close(writer);
  QCOMPARE(input.deviceCount(), size_t(0)); QCOMPARE(ungrabs, 1);
  QVERIFY(input.grab("event-missing", &family, &warning));
  QVERIFY(warning.contains("Busy pad may still reach the game"));
  QVERIFY(warning.contains("Unavailable pad could not be opened"));
  QVERIFY(warning.contains("controller that opened the guide disconnected"));
  input.release(); ::close(writer); QCOMPARE(ungrabs, 2);
}

void InGameGuideTests::padsChangingWhileOpen() {
  // Steam Input replaces its virtual pad while a game runs; the replacement must be held
  // too, and losing a pad must not close the guide.
  GuideInput input;
  QList<GuideListener::Controller> pads{{"event15", "a", "Microsoft X-Box 360 pad", false}};
  QHash<QString, int> writers;
  int grabs = 0;
  GuideInput::Access access;
  access.scan = [&pads] { return pads; };
  access.open = [&writers](const QString& node) {
    int pipe[2];
    if (::pipe2(pipe, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
    writers.insert(node, pipe[1]);
    return pipe[0];
  };
  access.grab = [&grabs](int) { ++grabs; return true; };
  access.ungrab = [](int) {};
  input.setAccess(access);
  QString family, warning;
  QSignalSpy actions(&input, &GuideInput::action);
  QVERIFY(input.grab("event15", &family, &warning));
  QCOMPARE(input.deviceCount(), size_t(1));
  pads.append({"event16", "b", "Microsoft X-Box 360 pad 0", true});
  input.rescan();
  QCOMPARE(input.deviceCount(), size_t(2)); QCOMPARE(input.grabbedCount(), size_t(2)); QCOMPARE(grabs, 2);
  input.rescan();
  QCOMPARE(input.deviceCount(), size_t(2));
  input_event event{}; event.type = EV_KEY; event.code = BTN_SOUTH; event.value = 1;
  input_event syn{}; syn.type = EV_SYN; syn.code = SYN_REPORT;
  QCOMPARE(::write(writers.value("event16"), &event, sizeof(event)), ssize_t(sizeof(event)));
  QCOMPARE(::write(writers.value("event16"), &syn, sizeof(syn)), ssize_t(sizeof(syn)));
  QTRY_COMPARE(actions.size(), 1); QCOMPARE(actions.first().first().toString(), "a");
  pads.removeLast();
  ::close(writers.take("event16"));
  QTRY_COMPARE(input.deviceCount(), size_t(1));
  event.code = BTN_EAST;
  QCOMPARE(::write(writers.value("event15"), &event, sizeof(event)), ssize_t(sizeof(event)));
  QCOMPARE(::write(writers.value("event15"), &syn, sizeof(syn)), ssize_t(sizeof(syn)));
  QTRY_COMPARE(actions.size(), 2); QCOMPARE(actions.last().first().toString(), "b");
  input.release();
  for (const int fd : writers) ::close(fd);
}

void InGameGuideTests::identifyOnGrabbedDevice() {
  InGameGuide guide(nullptr, nullptr, nullptr, nullptr, nullptr, false);
  int fd = -1, writer = -1, opens = 0, uploads = 0, plays = 0, erases = 0;
  bool supported = true, canUpload = true, canPlay = true;
  GuideInput::Access access;
  access.scan = [] { return QList<GuideListener::Controller>{{"event0", "a", "Xbox pad", false}}; };
  access.open = [&](const QString&) { int pipe[2]; if (::pipe2(pipe, O_NONBLOCK | O_CLOEXEC) != 0) return -1; ++opens; writer = pipe[1]; return fd = pipe[0]; };
  access.grab = [&](int grabbed) { return grabbed == fd; };
  access.supportsRumble = [&](int device) { return device == fd && supported; };
  access.upload = [&](int device, ff_effect* effect) {
    if (device != fd || effect->type != FF_RUMBLE || effect->id != -1 || effect->replay.length != 500 || effect->u.rumble.strong_magnitude != 0x7000) return false;
    ++uploads; effect->id = 7; return canUpload;
  };
  access.play = [&](int device, int effect) { if (device != fd || effect != 7) return false; ++plays; return canPlay; };
  access.erase = [&](int device, int effect) { if (device == fd && effect == 7) ++erases; };
  guide.m_input.setAccess(access);
  QString family, error;
  QVERIFY(guide.m_input.grab("event0", &family, &error)); QCOMPARE(guide.m_input.grabbedCount(), size_t(1));
  guide.m_opened = true;
  guide.message({{"action", "identify"}, {"value", "/dev/input/event0"}});
  QCOMPARE(opens, 1); QCOMPARE(uploads, 1); QCOMPARE(plays, 1);
  QTRY_COMPARE_WITH_TIMEOUT(erases, 1, 1000);
  supported = false;
  QVERIFY(!guide.m_input.identify("/dev/input/event0", &error)); QVERIFY(error.contains("no rumble support"));
  QCOMPARE(uploads, 1); QCOMPARE(plays, 1);
  supported = true; canUpload = false;
  QVERIFY(!guide.m_input.identify("/dev/input/event0", &error)); QVERIFY(error.contains("upload"));
  canUpload = true; canPlay = false;
  QVERIFY(!guide.m_input.identify("/dev/input/event0", &error)); QVERIFY(error.contains("play")); QCOMPARE(erases, 2);
  canPlay = true;
  QVERIFY(guide.m_input.identify("/dev/input/event0", &error));
  guide.m_input.release(); QCOMPARE(erases, 3); ::close(writer);
  QVERIFY(!guide.m_input.identify("/dev/input/event0", &error)); QVERIFY(error.contains("no longer connected"));
}

void InGameGuideTests::trackedQuit() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  QProcess game;
  game.start("python3", {"-u", "-c", "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('ready'); time.sleep(30)"});
  QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead());
  const auto pid = game.processId();
  qint64 start = -1;
  for (const auto& process : ProcFs::listProcesses()) if (process.pid == pid) start = process.procStart;
  QVERIFY(start > 0);
  const auto path = directory.filePath("sessions.sqlite3");
  {
    QSqlDatabase database;
    const QString connection = "guide-tracked-quit";
    QVERIFY(SessionDatabase::open(database, path, connection));
    QVERIFY(SessionDatabase::beginSession(database, "tracked-game", "Manual", QDateTime::currentSecsSinceEpoch(), pid, start) > 0);
    database.close(); database = {}; QSqlDatabase::removeDatabase(connection);
  }
  PlaySessionStore store(path); store.refreshNowPlaying(); QCOMPARE(store.nowPlaying().size(), 1);
  InGameGuide guide(&store, nullptr, nullptr, nullptr, nullptr, false);
  guide.m_opened = true; guide.m_token = "tracked";
  guide.m_session = store.nowPlaying().first().toMap();
  guide.message({{"action", "quit-confirmed"}});
  QCOMPARE(store.nowPlaying().size(), 1);
  QVERIFY(store.nowPlaying().first().toMap().value("stopping").toBool());
  QVERIFY(!store.nowPlaying().first().toMap().value("forceReady").toBool());
  QVERIFY(!guide.m_forceReady);
  guide.message({{"action", "force-quit"}}); QVERIFY(!game.waitForFinished(50));
  // The existing original-game test waits for the real five-second gate.
  guide.m_forceReady = true;
  guide.message({{"action", "force-quit"}}); QVERIFY(game.waitForFinished());
  store.refreshNowPlaying(); QVERIFY(store.nowPlaying().isEmpty());
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
  QVERIFY(game.waitForStarted()); QVERIFY(game.waitForReadyRead()); game.readAllStandardOutput(); qint64 start = -1;
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

void InGameGuideTests::quitKeepsItsOriginalGame() {
  QProcess game, other;
  game.start("python3", {"-u", "-c", "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('ready'); time.sleep(30)"});
  other.start("sleep", {"30"});
  QVERIFY(game.waitForStarted()); QVERIFY(other.waitForStarted()); QVERIFY(game.waitForReadyRead());
  qint64 start = -1; for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  InGameGuide guide(nullptr, nullptr, nullptr, nullptr, nullptr, false);
  guide.m_opened = true; guide.m_token = "test";
  guide.m_session = {{"pid", game.processId()}, {"procStart", start}, {"source", "Manual"}, {"path", "original"}};
  guide.message({{"action", "quit-confirmed"}});
  QVERIFY(!guide.m_forceReady); QVERIFY(!game.waitForFinished(50));
  QTRY_VERIFY_WITH_TIMEOUT(guide.m_forceReady, 6000);
  guide.m_session.insert("pid", other.processId());
  guide.message({{"action", "force-quit"}});
  QVERIFY(game.waitForFinished()); QVERIFY(other.state() == QProcess::Running);
  other.terminate(); QVERIFY(other.waitForFinished());
}

namespace {
struct PluginFixture {
  QTemporaryDir root;
  GuidePlugin::Paths paths;
  QString log;
  PluginFixture() {
    const QString base = root.path();
    QDir().mkpath(base + "/bundled");
    QFile manifest(base + "/bundled/manifest.json");
    if (manifest.open(QIODevice::WriteOnly)) manifest.write(R"({"id":"omakade.guide"})");
    paths.pluginsDir = base + "/config/omarchy/plugins";
    paths.shellConfig = base + "/config/omarchy/shell.json";
    paths.bundledDir = base + "/bundled";
    paths.markerPath = base + "/state/omakade/guide-plugin-enabled";
    paths.shellProgram = base + "/omarchy-shell";
    log = base + "/shell.log";
    QDir().mkpath(base + "/config/omarchy");
  }
  // A shell that records its arguments and answers enablePlugin with `reply` ("" = not running).
  void fakeShell(const QString& reply, bool writeConfig) {
    QFile script(paths.shellProgram);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QString body = "#!/bin/sh\necho \"$@\" >> '" + log + "'\n";
    if (reply.isEmpty()) body += "exit 1\n";
    else {
      if (writeConfig) body += "[ \"$2\" = enablePlugin ] && echo '{\"plugins\":[{\"id\":\"omakade.guide\"}]}' > '" + paths.shellConfig + "'\n";
      body += "[ \"$2\" = enablePlugin ] && echo " + reply + "\n[ \"$2\" = rescanPlugins ] && echo ok\nexit 0\n";
    }
    script.write(body.toUtf8()); script.close();
    QFile::setPermissions(paths.shellProgram, QFile::permissions(paths.shellProgram) | QFile::ExeOwner);
  }
  QStringList calls() const { QFile f(log); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts) : QStringList{}; }
};
}  // namespace

void InGameGuideTests::pluginLinksAndEnablesOnce() {
  PluginFixture fixture; fixture.fakeShell("ok", true);
  QVERIFY(!GuidePlugin::usable(fixture.paths));
  QVERIFY(GuidePlugin::ensure(fixture.paths));
  const QString link = fixture.paths.pluginsDir + "/omakade.guide";
  QVERIFY(QFileInfo(link).isSymLink());
  QCOMPARE(QFileInfo(link).symLinkTarget(), fixture.paths.bundledDir);
  QVERIFY(GuidePlugin::usable(fixture.paths));
  QVERIFY(QFileInfo::exists(fixture.paths.markerPath));
  QCOMPARE(fixture.calls(), (QStringList{"shell rescanPlugins", "shell enablePlugin omakade.guide {}"}));
  // A later launch asks the shell nothing, and a plugin the user disabled stays disabled.
  QVERIFY(QFile::remove(fixture.paths.shellConfig));
  QVERIFY(!GuidePlugin::ensure(fixture.paths));
  QVERIFY(!GuidePlugin::usable(fixture.paths));
  QCOMPARE(fixture.calls().size(), 2);
}

void InGameGuideTests::pluginKeepsUserCopyAndWaitsForShell() {
  PluginFixture own; own.fakeShell("ok", true);
  QVERIFY(QDir().mkpath(own.paths.pluginsDir + "/omakade.guide"));
  QFile mine(own.paths.pluginsDir + "/omakade.guide/manifest.json");
  QVERIFY(mine.open(QIODevice::WriteOnly)); mine.write("{}"); mine.close();
  QVERIFY(GuidePlugin::ensure(own.paths));
  QVERIFY(!QFileInfo(own.paths.pluginsDir + "/omakade.guide").isSymLink());
  PluginFixture dangling; dangling.fakeShell("ok", true);
  QVERIFY(QDir().mkpath(dangling.paths.pluginsDir));
  QVERIFY(QFile::link(dangling.root.path() + "/gone", dangling.paths.pluginsDir + "/omakade.guide"));
  QVERIFY(!GuidePlugin::ensure(dangling.paths));
  QCOMPARE(QFileInfo(dangling.paths.pluginsDir + "/omakade.guide").symLinkTarget(), dangling.root.path() + "/gone");
  // The shell is not running: the plugin is linked but nothing is marked, so the next launch retries.
  PluginFixture down; down.fakeShell("", false);
  QVERIFY(!GuidePlugin::ensure(down.paths));
  QVERIFY(QFileInfo(down.paths.pluginsDir + "/omakade.guide").isSymLink());
  QVERIFY(!QFileInfo::exists(down.paths.markerPath));
  down.fakeShell("ok", true);
  QVERIFY(GuidePlugin::ensure(down.paths));
  QVERIFY(QFileInfo::exists(down.paths.markerPath));
  // The shell does not know the plugin: no marker either.
  PluginFixture unknown; unknown.fakeShell("unknown", false);
  QVERIFY(!GuidePlugin::ensure(unknown.paths));
  QVERIFY(!QFileInfo::exists(unknown.paths.markerPath));
  // No bundled copy (a source build): nothing is linked.
  PluginFixture source; source.fakeShell("ok", true);
  source.paths.bundledDir = source.root.path() + "/missing";
  QVERIFY(!GuidePlugin::ensure(source.paths));
  QVERIFY(!QFileInfo(source.paths.pluginsDir + "/omakade.guide").isSymLink());
}

void InGameGuideTests::pluginFallsBackWhenSummonFails() {
  PluginFixture fixture; fixture.fakeShell("ok", true);
  QDir().mkpath(fixture.root.path() + "/bin");
  QFile shell(fixture.root.path() + "/bin/omarchy-shell");
  QVERIFY(shell.open(QIODevice::WriteOnly)); shell.write("#!/bin/sh\necho unknown\n"); shell.close();
  QFile::setPermissions(shell.fileName(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  const QByteArray originalPath = qgetenv("PATH");
  qputenv("PATH", (fixture.root.path() + "/bin:" + originalPath).toUtf8());
  InGameGuide guide(nullptr, nullptr, nullptr, nullptr, nullptr, false);
  guide.m_enabled = true;
  QSignalSpy failed(&guide, &InGameGuide::summonFailed);
  // Opened from inside Omakade: no fallback, so Game Mode is left alone.
  QVERIFY(guide.toggle());
  QTRY_VERIFY(!guide.m_opening);
  QCOMPARE(failed.count(), 0);
  QVERIFY(guide.toggle({}, true));
  QTRY_COMPARE(failed.count(), 1);
  qputenv("PATH", originalPath);
  QVERIFY(!guide.opened()); QVERIFY(!guide.m_paused);
  // No plugin, no guide: the shortcut keeps its Game Mode behavior.
  QVERIFY(!guide.usable());
  guide.setPluginPaths(fixture.paths);
  QVERIFY(!guide.usable());
  QVERIFY(GuidePlugin::ensure(fixture.paths));
  QVERIFY(guide.usable());
  guide.m_enabled = false;
  QVERIFY(!guide.usable());
}

void InGameGuideTests::provisioningAndPauseDoNotBlock() {
  PluginFixture fixture; fixture.fakeShell("ok", true);
  bool done = false; QElapsedTimer elapsed; elapsed.start();
  GuidePlugin::ensureAsync(fixture.paths, this, [&done](bool ready) { done = ready; });
  QVERIFY(elapsed.elapsed() < 30);
  QTRY_VERIFY(done); QVERIFY(GuidePlugin::usable(fixture.paths));
  QProcess game; game.start("sleep", {"30"}); QVERIFY(game.waitForStarted());
  qint64 start = -1;
  for (const auto& process : ProcFs::listProcesses()) if (process.pid == game.processId()) start = process.procStart;
  QVERIFY(start > 0);
  InGameGuide guide(nullptr, nullptr, nullptr, nullptr, nullptr, false);
  guide.m_session = {{"pid", game.processId()}, {"procStart", start}};
  // The test executable sits in tests/, beside which the guard is not installed.
  const auto originalPath = qgetenv("PATH");
  qputenv("PATH", (QFileInfo(QStringLiteral(OMAKADE_GUIDE_GUARD)).absolutePath() + ':' + originalPath).toUtf8());
  elapsed.restart(); QVERIFY(guide.setPaused(true)); QVERIFY(elapsed.elapsed() < 30);
  QTRY_VERIFY(guide.m_paused);
  elapsed.restart(); guide.close(); QVERIFY(elapsed.elapsed() < 30);
  QVERIFY(!guide.m_paused); QVERIFY(!guide.m_guard);
  QTRY_VERIFY(ProcFs::processAlive(game.processId(), start));
  game.terminate(); QVERIFY(game.waitForFinished());
  qputenv("PATH", originalPath);
}

QTEST_GUILESS_MAIN(InGameGuideTests)
#include "InGameGuideTests.moc"

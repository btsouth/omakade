#include "guide/GuideInput.h"
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

QTEST_GUILESS_MAIN(InGameGuideTests)
#include "InGameGuideTests.moc"

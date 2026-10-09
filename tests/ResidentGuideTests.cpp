#include "guide/GuideClient.h"
#include "guide/GuidePayload.h"
#include "tracking/SessionDatabase.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QSqlQuery>
#include <sys/stat.h>

class ResidentGuideTests final : public QObject {
  Q_OBJECT
private slots:
  void residentOwnsShortcutWithoutGui();
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
  QVERIFY(write(bin + "/hyprctl", "#!/bin/sh\necho \"$@\" >> '" + queryLog.toUtf8() + "'\ncase \"$2\" in\nactivewindow) echo '" + active + "';;\nclients) echo '" + clients + "';;\nmonitors) echo '[{\"id\":0,\"name\":\"TEST-1\"}]';;\nesac\n", true));
  const auto summonFile = root.path() + "/summon.json", shellLog = root.path() + "/shell.log";
  QVERIFY(write(bin + "/omarchy-shell", "#!/bin/sh\necho \"$1 $2 $3\" >> '" + shellLog.toUtf8() + "'\n[ \"$2\" = summon ] && echo \"$4\" > '" + summonFile.toUtf8() + "'\necho ok\n", true));
  auto env = QProcessEnvironment::systemEnvironment();
  env.insert("PATH", bin + ':' + env.value("PATH")); env.insert("XDG_RUNTIME_DIR", runtime);
  env.insert("XDG_CONFIG_HOME", config); env.insert("XDG_DATA_HOME", data); env.insert("XDG_STATE_HOME", root.path() + "/state");
  // Any accidental QGuiApplication path fails. The shortcut must use Qt Core alone.
  env.insert("QT_QPA_PLATFORM", "invalid-platform-for-resident-test");
  QProcess daemon; daemon.setProcessEnvironment(env); daemon.start(QStringLiteral(OMAKADE_SESSIOND), {"--guide-only"});
  QVERIFY(daemon.waitForStarted());
  const auto cleanup = qScopeGuard([&] { daemon.kill(); daemon.waitForFinished(); });
  QTRY_VERIFY(QFileInfo::exists(runtime + QStringLiteral("/omakade-guide-control-%1").arg(::getuid())));
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
  shortcut.start(QStringLiteral(OMAKADE_APP), {"--guide-toggle"}); QVERIFY(shortcut.waitForFinished(5000)); QCOMPARE(shortcut.exitCode(), 0);
  QTRY_VERIFY(([&] { QFile log(shellLog); return log.open(QIODevice::ReadOnly) && log.readAll().contains("shell hide omakade.guide"); })());
  QFile log(shellLog); QVERIFY(log.open(QIODevice::ReadOnly)); QVERIFY(!log.readAll().contains("rescanPlugins"));
  const auto messages = daemon.readAllStandardError();
  QVERIFY(messages.contains("summon dispatched")); QVERIFY(messages.contains("opened elapsed_ms=")); QVERIFY(messages.contains("closed elapsed_ms="));
}

QTEST_GUILESS_MAIN(ResidentGuideTests)
#include "ResidentGuideTests.moc"

#include "app/AppSettings.h"
#include "launch/GameLauncher.h"
#include "library/GameRoles.h"
#include "library/LibraryRepair.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameMetadata.h"
#include "saves/SaveBackups.h"
#include "saves/SaveProtection.h"
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace {
void put(const QString& path, const QByteArray& bytes = "fixture") {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    qFatal("Cannot write fixture");
}
class Games : public QAbstractListModel {
public:
  QVariantList games;
  int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : games.size(); }
  QVariant data(const QModelIndex& i, int role) const override {
    return i.isValid()
               ? games[i.row()].toMap().value(QString::fromUtf8(GameRoles::names().value(role)))
               : QVariant{};
  }
  QHash<int, QByteArray> roleNames() const override { return GameRoles::names(); }
};
QVariantMap game(const QString& id, const QString& system = "snes") {
  return {{"source", "RetroArch"}, {"runner", ""},     {"appId", id},
          {"title", "Same Name"},  {"system", system}, {"installed", true}};
}
} // namespace
class FeatureWorkflowTests : public QObject {
  Q_OBJECT
private slots:
  void liveEmulatorRecordingAndSaveRecovery() {
    const auto root=qEnvironmentVariable("OMAKADE_LIVE_GAME_ROOT");
    if(root.isEmpty()) QSKIP("Opt-in live emulator fixture is not configured");
    QCOMPARE(root,QString("/tmp/omakade-live-game"));
    const auto content=root+"/Omakade QA.sfc", core=QString("/usr/lib/libretro/snes9x_libretro.so");
    const auto save=root+"/saves/Omakade QA.srm";
    QVERIFY(QFileInfo(content).isFile());QVERIFY(QFileInfo(core).isFile());
    put(save,QByteArray(2048,0));
    const auto database=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/omakade/library.sqlite3";
    QProcess recorder;
    recorder.setProgram(qEnvironmentVariable("OMAKADE_LIVE_RECORDER"));
    QVERIFY(!recorder.program().isEmpty());recorder.start();QVERIFY(recorder.waitForStarted());
    const auto cleanup=qScopeGuard([&] { recorder.terminate();recorder.waitForFinished(5000); });
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(database).isFile(),10000);
    GameLauncher launcher;launcher.setSetupDatabase(database);launcher.setRommLibraryRoot(root);
    SaveBackups backups(root,root+"/retroarch.cfg",root+"/backups",[&] {return launcher.gameRunning();});
    launcher.setSaveBackups(&backups);
    const QVariantMap installation{{"source","RomM"},{"runner",""},{"appId","live-qa"},
      {"installPath",content},{"system","snes"}};
    QVERIFY(launcher.saveSetup(installation,"RetroArch",core,false,{}));
    QVERIFY2(launcher.inspect(installation)["available"].toBool(),qPrintable(launcher.inspect(installation)["error"].toString()));
    backups.selectLaunch("RetroArch",content,core,false,"live-qa",{},{});
    QVERIFY2(launcher.launch("RomM","live-qa",false,{},content,{},"snes"),qPrintable(launcher.lastError()));
    QVERIFY(launcher.gameRunning());
    QCOMPARE(backups.versions().size(),1);
    const auto before=backups.versions().first().toMap()["id"].toString();
    QVERIFY(!backups.restore(before));
    QTRY_VERIFY_WITH_TIMEOUT(!launcher.gameRunning(),40000);
    QFile changed(save);QVERIFY(changed.open(QIODevice::ReadOnly));
    const auto played=changed.readAll();changed.close();
    QCOMPARE(played.size(),2048);QCOMPARE(static_cast<unsigned char>(played[0]),1);
    QVERIFY2(backups.restore(before),qPrintable(backups.message()));
    QFile restored(save);QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(),QByteArray(2048,0));restored.close();
    QString undo;
    for(const auto& v:backups.versions()) if(v.toMap()["id"].toString()!=before) {undo=v.toMap()["id"].toString();break;}
    QVERIFY(!undo.isEmpty());QVERIFY2(backups.restore(undo),qPrintable(backups.message()));
    QFile undone(save);QVERIFY(undone.open(QIODevice::ReadOnly));QCOMPARE(undone.readAll(),played);undone.close();
    const auto connection=QString("live-recording-verify");
    {
      auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(database);QVERIFY(db.open());
      auto recorded=[&] {
        QSqlQuery q(db);q.prepare("SELECT seconds,ended_at FROM play_sessions WHERE game_path=?");q.addBindValue(content);
        return q.exec() && q.next() && q.value(0).toInt()>0 && q.value(1).toLongLong()>0;
      };
      QTRY_VERIFY_WITH_TIMEOUT(recorded(),12000);
      db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    SaveBackups reopened(root,root+"/retroarch.cfg",root+"/backups",[]{return false;});
    reopened.selectLaunch("RetroArch",content,core,false,"live-qa",{},{});
    QVERIFY(reopened.versions().size()>=2);
    qInfo("Real emulator save changed 0 -> 1, restore 1 -> 0, undo 0 -> 1; closed session recorded.");
  }
  void installationSetupPersistsAndLaunchUsesRepair() {
    QTemporaryDir tmp;
    const auto old = qgetenv("PATH");
    const auto guard = qScopeGuard([&] { qputenv("PATH", old); });
    qputenv("PATH", tmp.path().toUtf8());
    const auto exe = tmp.filePath("snes9x"), output = tmp.filePath("launched");
    put(exe, "#!/bin/sh\nprintf '%s' \"$1\" > '" + output.toUtf8() + "'\n");
    QVERIFY(QFile::setPermissions(exe, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto path = tmp.filePath("Pokémon #1.sfc");
    put(path);
    auto installation = game("stable");
    installation["installPath"] = tmp.filePath("missing.sfc");
    {
      GameLauncher launcher;
      launcher.setSetupDatabase(tmp.filePath("library.sqlite"));
      QVERIFY(!launcher.inspect(installation)["available"].toBool());
      QVERIFY(launcher.saveSetup(installation, "snes9x", {}, false, path));
      QVERIFY(launcher.inspect(installation)["available"].toBool());
    }
    GameLauncher launcher;
    launcher.setSetupDatabase(tmp.filePath("library.sqlite"));
    QCOMPARE(launcher.inspect(installation)["gamePath"].toString(), path);
    auto rescanned = installation;
    rescanned["appId"] = "rescanned-path-id";
    rescanned["installPath"] = path;
    QCOMPARE(launcher.inspect(rescanned)["mode"].toString(), QString("snes9x"));
    QVERIFY(launcher.launch("RetroArch", "stable", false, {},
                            installation["installPath"].toString(), {}, "snes"));
    // The shell creates the file before printf fills it, so wait for the content.
    const auto launched = [&] {
      QFile result(output);
      return result.open(QIODevice::ReadOnly) ? QString::fromUtf8(result.readAll()) : QString();
    };
    QTRY_COMPARE(launched(), path);
    QVERIFY(launcher.saveSetup(installation, "RetroArch", {}, false, path));
    QVERIFY(!launcher.inspect(installation)["available"]
                 .toBool()); // explicit RetroArch cannot fall back
    QVERIFY(launcher.resetSetup(installation));
    QVERIFY(!launcher.inspect(installation)["available"].toBool());
  }
  void automaticNativeFallbackClearsFlatpakContext() {
    QTemporaryDir tmp;
    const auto old = qgetenv("PATH");
    const auto guard = qScopeGuard([&] { qputenv("PATH", old); });
    qputenv("PATH", tmp.path().toUtf8());
    const auto exe = tmp.filePath("snes9x");
    put(exe, "#!/bin/sh\nexit 0\n");
    QVERIFY(QFile::setPermissions(exe, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto path = tmp.filePath("game.sfc");
    put(path);
    auto installation = game("fallback");
    installation["installPath"] = path;
    installation["flatpak"] = true;
    GameLauncher launcher;
    launcher.setPreferStandaloneEmulators(true);
    const auto plan = launcher.inspect(installation);
    QVERIFY2(plan["available"].toBool(), qPrintable(plan["error"].toString()));
    QCOMPARE(plan["program"].toString(), QString("snes9x"));
    QVERIFY(!plan["resolvedFlatpak"].toBool());
    QVERIFY(!plan["saveContext"].toMap()["flatpak"].toBool());
  }
  void customSaveLayoutPolicyAndRecovery() {
    QTemporaryDir tmp;
    const auto root = tmp.filePath("backups"), path = tmp.filePath("saves/test.sav");
    put(path, "original");
    const QVariantMap context{
        {"source", "mgba"}, {"game", tmp.filePath("test.gba")}, {"flatpak", false}};
    put(context["game"].toString());
    bool running = false;
    SaveBackups backups(tmp.path(), tmp.filePath("retroarch.cfg"), root, [&] { return running; });
    QVERIFY(!backups.previewCustomFiles(context, {tmp.path()}, false)["valid"].toBool());
    QVERIFY(backups.setCustomFiles(context, {path}, true));
    QVERIFY(backups.coverage(context)["valid"].toBool());
    QVERIFY(backups.coverage(context)["shared"].toBool());
    backups.selectLaunch("mgba", context["game"].toString(), {}, false, {}, {}, {});
    QVERIFY(backups.protectLaunch("mgba", context["game"].toString()));
    QCOMPARE(backups.versions().size(), 1);
    const auto version = backups.versions().first().toMap()["id"].toString();
    QVERIFY(!backups.coverage(context)["latestVerified"].toString().isEmpty());
    put(path, "changed");
    QVERIFY(backups.restore(version));
    QFile restored(path);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("original"));
    restored.close();
    QVERIFY(backups.setPolicy(2, 256));
    QCOMPARE(backups.versions().size(), 2);
    running = true;
    QVERIFY(!backups.setPolicy(3, 512));
    QVERIFY(!backups.resetCustomFiles(context));
    running = false;
    SaveBackups reopened(tmp.path(), tmp.filePath("retroarch.cfg"), root, [] { return false; });
    QCOMPARE(reopened.retention(), 2);
    QCOMPARE(reopened.storageLimitMiB(), 256);
    QVERIFY(reopened.coverage(context)["valid"].toBool());
  }
  void sharedCleanupIsReviewedAndBlockedDuringEmulation() {
    QTemporaryDir tmp;
    bool running = false;
    SaveBackups backups(tmp.path(), tmp.filePath("ra.cfg"), tmp.filePath("backups"),
                        [&] { return running; });
    Games source;
    const auto save = tmp.filePath("shared.sav");
    put(save, "initial");
    for (const auto& id : {"a", "b"}) {
      auto entry = game(id);
      entry["installPath"] = tmp.filePath(QString(id) + ".sfc");
      put(entry["installPath"].toString());
      source.games.append(entry);
      QVERIFY(backups.setCustomFiles({{"source", "RetroArch"}, {"game", entry["installPath"]}},
                                     {save}, true));
    }
    const auto first = source.games.first().toMap()["installPath"].toString();
    const auto second = source.games.last().toMap()["installPath"].toString();
    for (int n = 0; n < 5; ++n) {
      put(save, QByteArray("v") + QByteArray::number(n));
      QVERIFY(backups.protectLaunch("RetroArch", first));
    }
    QVERIFY(backups.protectLaunch("RetroArch", second));
    UnifiedGameModel games(tmp.filePath("library.sqlite"));
    games.addSourceModel(&source);
    GameLauncher launcher;
    SaveProtection overview(&games, &launcher, &backups);
    overview.refresh();
    QTRY_VERIFY(!overview.busy());
    QCOMPARE(overview.entries().size(), 2);
    QCOMPARE(overview.storageBytes(), 10);
    QVERIFY(backups.setPolicy(2, 256));
    QCOMPARE(backups.count(first), 5);
    overview.previewCleanup();
    QCOMPARE(overview.cleanup().size(), 3);
    running = true;
    QVERIFY(!overview.applyCleanup());
    QCOMPARE(backups.count(first), 5);
    running = false;
    overview.previewCleanup();
    QVERIFY(overview.applyCleanup());
    QTRY_VERIFY(!overview.busy());
    QCOMPARE(backups.count(first), 2);
    QCOMPARE(backups.count(second), 2);
    QFile current(save);
    QVERIFY(current.open(QIODevice::ReadOnly));
    QCOMPARE(current.readAll(), QByteArray("v4"));
  }
  void flatpakSelectionAndRommMountAreRevalidated() {
    QTemporaryDir tmp;
    const auto old = qgetenv("PATH");
    const auto guard = qScopeGuard([&] { qputenv("PATH", old); });
    qputenv("PATH", tmp.path().toUtf8());
    for (const auto& name : {"Ryujinx", "flatpak"}) {
      const auto path = tmp.filePath(name);
      put(path, "#!/bin/sh\nexit 0\n");
      QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    auto installation = game("switch-game", "switch");
    installation["source"] = "Ryujinx";
    installation["installPath"] = tmp.filePath("Game.nsp");
    put(installation["installPath"].toString());
    GameLauncher launcher;
    launcher.setSetupDatabase(tmp.filePath("library.sqlite"));
    QVERIFY(launcher.saveSetup(installation, "Ryujinx", {}, true, {}));
    QCOMPARE(launcher.inspect(installation)["program"].toString(), QString("flatpak"));
    QVERIFY(launcher.saveSetup(installation, "Ryujinx", {}, false, {}));
    QCOMPARE(launcher.inspect(installation)["program"].toString(), QString("Ryujinx"));
    installation["source"] = "RomM";
    QVERIFY(!launcher.inspect(installation)["available"].toBool());
    launcher.setRommLibraryRoot(tmp.path());
    QVERIFY(launcher.inspect(installation)["available"].toBool());
    QTemporaryDir outside;
    put(outside.filePath("Other.nsp"));
    QVERIFY(QFile::remove(installation["installPath"].toString()));
    QVERIFY(QFile::link(outside.filePath("Other.nsp"), installation["installPath"].toString()));
    QVERIFY(!launcher.inspect(installation)["available"].toBool());
  }
  void reviewPositionAndPlatformSpecificSuggestions() {
    QTemporaryDir tmp;
    Games source;
    source.games = {game("a"), game("b", "gba"), game("c")};
    UnifiedGameModel games(tmp.filePath("library.sqlite"));
    games.addSourceModel(&source);
    QVERIFY(games.reviewReasons(0).contains("duplicates"));
    QVERIFY(!games.reviewReasons(1).contains("duplicates"));
    QString key;
    {
      LibraryRepair repair(&games, nullptr, tmp.filePath("review.ini"));
      repair.refresh();
      repair.setReason("duplicates");
      QCOMPARE(repair.entries().size(), 2);
      repair.toggleSelected();
      QCOMPARE(repair.selectedTitles().size(), 1);
      repair.move(1);
      key = repair.current()["metadataKey"].toString();
    }
    LibraryRepair reopened(&games, nullptr, tmp.filePath("review.ini"));
    reopened.refresh();
    QCOMPARE(reopened.current()["metadataKey"].toString(), key);
    QVERIFY(games.linkGames(0, "RetroArch", "", "c"));
    reopened.refresh();
    QCOMPARE(reopened.entries().size(), 0);
  }
  void identityAndArtworkUndoSurviveRestart() {
    QTemporaryDir tmp;
    const auto dbPath = tmp.filePath("library.sqlite");
    Games source;
    source.games = {game("a")};
    UnifiedGameModel games(dbPath);
    games.addSourceModel(&source);
    GameMetadata metadata(dbPath, nullptr);
    games.setMetadata(&metadata);
    QTRY_VERIFY(metadata.reviewWritable());
    const auto key = games.reviewGame(0)["metadataKey"].toString();
    const auto connection = QString("workflow-edit");
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
      db.setDatabaseName(dbPath);
      QVERIFY(db.open());
      auto set = [&](int id, const QString& portrait) {
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO game_metadata VALUES(?,?)");
        q.addBindValue(key);
        q.addBindValue(
            QJsonDocument::fromVariant(QVariantMap{{"igdbId", id}, {"portrait", portrait}})
                .toJson());
        return q.exec();
      };
      const auto original = tmp.filePath("original.png"), newArt = tmp.filePath("new.png");
      QImage image(600, 900, QImage::Format_RGB32);
      image.fill(Qt::red);
      QVERIFY(image.save(original));
      image.fill(Qt::blue);
      QVERIFY(image.save(newArt));
      QVERIFY(set(1, original));
      metadata.reloadReviewEntry(key);
      QVERIFY(games.repairCheckpoint(key, "identity", false));
      QVERIFY(games.repairCheckpoint(key, "artwork", false));
      QVERIFY(set(2, newArt));
      metadata.reloadReviewEntry(key);
      QVERIFY(QFile::remove(original));
      QVERIFY(games.repairCheckpoint(key, "identity", true));
      QCOMPARE(metadata.entry(key)["igdbId"].toInt(), 1);
      QCOMPARE(metadata.entry(key)["portrait"].toString(), newArt);
      db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    games.setMetadata(nullptr);
    UnifiedGameModel reopened(dbPath);
    reopened.addSourceModel(&source);
    GameMetadata reloaded(dbPath, nullptr);
    reopened.setMetadata(&reloaded);
    QTRY_VERIFY(reloaded.reviewWritable());
    QVERIFY(reopened.hasRepairCheckpoint(key, "artwork"));
    QVERIFY(reopened.repairCheckpoint(key, "artwork", true));
    QCOMPARE(reloaded.entry(key)["igdbId"].toInt(), 1);
    const auto preserved = reloaded.entry(key)["portrait"].toString();
    QVERIFY(QFileInfo::exists(preserved));
    QCOMPARE(QImage(preserved).pixelColor(0, 0), QColor(Qt::red));
    QVERIFY(!reopened.hasRepairCheckpoint(key, "artwork"));
    reopened.setMetadata(nullptr);
  }
};
QTEST_GUILESS_MAIN(FeatureWorkflowTests)
#include "FeatureWorkflowTests.moc"

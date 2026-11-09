#include "saves/SaveBackups.h"
#include "launch/GameLauncher.h"
#include "app/AppSettings.h"
#include "tracking/ProcFs.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {
void put(const QString& path, const QByteArray& bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) qFatal("Cannot create fixture directory");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write fixture");
}
QByteArray get(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
QMap<QString, QByteArray> treeHashes(const QString& root) {
  QMap<QString, QByteArray> hashes;
  QDirIterator files(root, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                      QDirIterator::Subdirectories);
  while (files.hasNext()) {
    const QString path = files.next();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
      hashes.insert(QDir(root).relativeFilePath(path),
                    QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
  }
  return hashes;
}
QString keyHash(const QString& game) {
  return QString::fromLatin1(QCryptographicHash::hash(game.toUtf8(), QCryptographicHash::Sha256).toHex());
}
struct Fixture {
  QTemporaryDir temp;
  QString home = temp.path(), game = home + "/roms/Test Game.sfc", core = home + "/snes9x_libretro.so";
  QString cfg = home + "/retroarch.cfg", root = home + "/backups", save = home + "/saves/Snes9x/Test Game.srm";
  bool running = false;
  SaveBackups backups{home, cfg, root, [&] { return running; }};
  Fixture() {
    put(game, "rom"); put(core, "core"); put(save, "previous save");
    put(cfg, "savefile_directory = \"~/saves\"\nsavefiles_in_content_dir = \"false\"\nsort_savefiles_enable = \"true\"\nsort_savefiles_by_content_enable = \"false\"\nauto_overrides_enable = \"true\"\nrgui_config_directory = \"~/config\"\n");
  }
  QString gameRoot() const { return gameRootFor(game); }
  QString gameRootFor(const QString& path) const { return root + '/' + keyHash(path); }
  QString setRoot(const QString& path) const { return root + "/sets/" + keyHash(path); }
  QString version() { backups.selectGame(game); return backups.versions().first().toMap().value("id").toString(); }
};
}
class SaveBackupsTests : public QObject {
  Q_OBJECT
private slots:
  void preferencePersistsAndStaysLocalToThisMachine() {
    Fixture f;
    const auto path = f.home + "/omakade.toml";
    AppSettings settings(path);
    QVERIFY(settings.protectRetroArchSaves());
    settings.setProtectRetroArchSaves(false);
    AppSettings reloaded(path);
    QVERIFY(!reloaded.protectRetroArchSaves());
    QVERIFY(!reloaded.backupSettings().contains("protect_retroarch_saves"));
  }
  void runningGuardMatchesEmulatorProcessNames_data() {
    QTest::addColumn<QString>("comm");
    QTest::addColumn<QString>("argv0");
    QTest::newRow("PPSSPPSDL comm") << "PPSSPPSDL" << "";
    QTest::newRow("PPSSPPSDL path") << "" << "/usr/bin/PPSSPPSDL";
    QTest::newRow("PPSSPPQt") << "PPSSPPQt" << "/usr/bin/PPSSPPQt";
    QTest::newRow("ppsspp-qt") << "ppsspp-qt" << "/usr/bin/ppsspp-qt";
    QTest::newRow("ppsspp") << "ppsspp" << "ppsspp";
    QTest::newRow("PPSSPP") << "PPSSPP" << "";
    QTest::newRow("RetroArch") << "RetroArch" << "/usr/bin/RetroArch";
    QTest::newRow("dolphin argv0 fallback") << "dolphin-emu-nog" << "/usr/bin/dolphin-emu-nogui";
  }
  void runningGuardMatchesEmulatorProcessNames() {
    QFETCH(QString, comm); QFETCH(QString, argv0);
    QVERIFY(SaveBackups::isEmulatorProcess(comm, argv0));
  }
  void runningGuardIgnoresUnrelatedProcesses() {
    QVERIFY(!SaveBackups::isEmulatorProcess("bash", "/usr/bin/bash"));
    QVERIFY(!SaveBackups::isEmulatorProcess("ppsspp-launcher", "/opt/ppsspp-launcher"));
    QVERIFY(!SaveBackups::isEmulatorProcess({}, {}));
  }
  void runningGuardSeesTheNativePpssppLaunchCommand() {
    // The exact binary GameLauncher starts must be what the restore guard looks for.
    const QString program = GameLauncher::ppssppCommand("/roms/Game.iso", false).program;
    QCOMPARE(program, QStringLiteral("PPSSPPSDL"));
    const QString sleep = QStandardPaths::findExecutable("sleep");
    if (sleep.isEmpty()) QSKIP("sleep is not available");
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    const QString fake = bin.path() + '/' + program;
    QVERIFY(QFile::copy(sleep, fake));
    QVERIFY(QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QProcess process;
    process.start(fake, {"30"});
    QVERIFY(process.waitForStarted());
    const auto stop = qScopeGuard([&] { process.kill(); process.waitForFinished(); });
    const qint64 pid = process.processId();
    bool seen = false;
    for (int attempt = 0; attempt < 50 && !seen; ++attempt) {
      for (const auto& snapshot : ProcFs::listProcesses()) {
        if (snapshot.pid != pid || snapshot.arguments.value(0) != fake) continue;
        QCOMPARE(snapshot.comm, program);
        QVERIFY(SaveBackups::isEmulatorProcess(snapshot.comm, snapshot.arguments.value(0)));
        seen = true;
      }
      if (!seen) QTest::qWait(20);
    }
    QVERIFY(seen);
  }
  void discoversOnlySupportedLayouts() {
    Fixture f;
    QCOMPARE(f.backups.discover(f.game, f.core), f.save);
    QVERIFY(f.backups.discover(f.game, "/cores/mgba_libretro.so").isEmpty());
    QVERIFY(f.backups.discover(f.game + "#inner.sfc", f.core).isEmpty());
    QVERIFY(f.backups.discover("relative.sfc", f.core).isEmpty());
    put(f.home + "/config/Snes9x/Test Game.cfg", "input_player1_b_btn = \"0\"\n");
    QCOMPARE(f.backups.discover(f.game, f.core), f.save);
    put(f.home + "/config/Snes9x/Test Game.cfg", "savefile_directory = \"/elsewhere\"\n");
    QVERIFY(f.backups.discover(f.game, f.core).isEmpty());
    QVERIFY(QFile::remove(f.home + "/config/Snes9x/Test Game.cfg"));
    put(f.home + "/config/Snes9x/roms.cfg", "#include \"extra.cfg\"\n");
    QVERIFY(f.backups.discover(f.game, f.core).isEmpty());
    QVERIFY(QFile::remove(f.home + "/config/Snes9x/roms.cfg"));
    put(f.cfg, get(f.cfg).replace("sort_savefiles_by_content_enable = \"false\"", "sort_savefiles_by_content_enable = \"true\""));
    QVERIFY(f.backups.discover(f.game, f.core).isEmpty());
  }
  void additionalCartridgeSaves_data() {
    QTest::addColumn<QString>("core");
    QTest::addColumn<QString>("folder");
    QTest::addColumn<QString>("extension");
    QTest::newRow("gba") << "mgba" << "mGBA" << "gba";
    for (const QString ext : {"md", "gen", "smd", "sms", "gg"})
      QTest::newRow(qPrintable(ext)) << "genesis_plus_gx" << "Genesis Plus GX" << ext;
  }
  void additionalCartridgeSaves() {
    QFETCH(QString, core); QFETCH(QString, folder); QFETCH(QString, extension);
    Fixture f;
    const QString game = f.home + "/roms/Cartridge." + extension;
    const QString corePath = f.home + '/' + core + "_libretro.so";
    const QString save = f.home + "/saves/" + folder + "/Cartridge.srm";
    put(game, "rom"); put(save, "previous progress");
    QCOMPARE(f.backups.discover(game, corePath), save);
    QVERIFY(f.backups.protect(game, corePath));
    f.backups.selectGame(game);
    const QString version = f.backups.versions().first().toMap()["id"].toString();
    put(save, "new progress");
    QVERIFY(f.backups.restore(version));
    QCOMPARE(get(save), QByteArray("previous progress"));
    QCOMPARE(f.backups.count(game), 2);
    put(f.home + "/config/" + folder + "/Cartridge.cfg", "savefile_directory = \"/elsewhere\"\n");
    QVERIFY(f.backups.discover(game, corePath).isEmpty());
    QVERIFY(!f.backups.restore(version));
  }
  void melondsSaveDiscoveryUsesBatterySaveAndRefusesRelocatedState() {
    QTemporaryDir first;
    QVERIFY(first.isValid());
    const QString firstHome = first.path();
    const QString firstGame = firstHome + "/roms/Homebrew.nds";
    const QString firstSave = firstHome + "/roms/Homebrew.sav";
    put(firstGame, "fixture ROM");
    put(firstSave, "battery progress");
    SaveBackups firstBackups(firstHome, firstHome + "/retroarch.cfg",
                             firstHome + "/backups", [] { return false; });
    QVERIFY(firstBackups.protectLaunch("melonDS", firstGame, {}, false,
                                       "path:" + firstGame, {}, firstGame));
    QCOMPARE(firstBackups.count(firstGame), 1);
    firstBackups.selectGame(firstGame);
    const QString firstVersion = firstBackups.versions().first().toMap()["id"].toString();
    put(firstSave, "newer progress");
    QVERIFY(firstBackups.restore(firstVersion));
    QCOMPARE(get(firstSave), QByteArray("battery progress"));

    QTemporaryDir second;
    QVERIFY(second.isValid());
    const QString secondHome = second.path();
    const QString secondGame = secondHome + "/roms/Overridden.nds";
    const QString configRoot = secondHome + "/.config/melonDS";
    const QString overriddenSave = configRoot + "/saves/Overridden.sav";
    put(secondGame, "fixture ROM");
    put(overriddenSave, "configured progress");
    put(configRoot + "/melonDS.toml",
        "[Instance0]\nSaveFilePath = \"saves\"\n");
    SaveBackups secondBackups(secondHome, secondHome + "/retroarch.cfg",
                              secondHome + "/backups", [] { return false; });
    QVERIFY(secondBackups.protectLaunch("melonDS", secondGame, {}, false,
                                        "path:" + secondGame, {}, secondGame));
    QCOMPARE(secondBackups.count(secondGame), 1);
    secondBackups.selectGame(secondGame);
    const QString secondVersion = secondBackups.versions().first().toMap()["id"].toString();
    put(overriddenSave, "newer configured progress");
    QVERIFY(secondBackups.restore(secondVersion));
    QCOMPARE(get(overriddenSave), QByteArray("configured progress"));

    QTemporaryDir relocated;
    QVERIFY(relocated.isValid());
    const QString relocatedGame = relocated.path() + "/roms/Relocated.nds";
    put(relocatedGame, "fixture ROM");
    put(relocated.path() + "/roms/Relocated.ml1.sav", "state-scoped battery save");
    SaveBackups relocatedBackups(relocated.path(), relocated.path() + "/retroarch.cfg",
                                 relocated.path() + "/backups", [] { return false; });
    QVERIFY(relocatedBackups.protectLaunch("melonDS", relocatedGame, {}, false,
                                           "path:" + relocatedGame, {}, relocatedGame));
    QCOMPARE(relocatedBackups.count(relocatedGame), 0);
  }
  void rpcs3WithoutAMatchingSaveIsAValidNoOp() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString game = directory.path() + "/game/PS3_GAME/USRDIR/EBOOT.BIN";
    put(game, "elf");
    SaveBackups backups(directory.path(), directory.path() + "/retroarch.cfg",
                        directory.path() + "/backups", [] { return false; });
    QVERIFY(backups.protectLaunch("RPCS3", game, {}, false, "BLUS00002", {}, game));
    QCOMPARE(backups.count(game), 0);
  }
  void additionalCoresRejectMultiFileAndAmbiguousContent() {
    Fixture f;
    for (const QString ext : {"gb", "gbc", "zip", "7z"}) {
      const QString game = f.home + "/Game." + ext;
      put(game, "rom"); put(f.home + "/saves/mGBA/Game.srm", "save");
      QVERIFY(f.backups.discover(game, "mgba_libretro.so").isEmpty());
    }
    for (const QString ext : {"cue", "chd", "iso", "bin", "m3u", "zip"}) {
      const QString game = f.home + "/Game." + ext;
      put(game, "rom"); put(f.home + "/saves/Genesis Plus GX/Game.srm", "save");
      QVERIFY(f.backups.discover(game, "genesis_plus_gx_libretro.so").isEmpty());
    }
  }
  void duplicatesAndRetentionNeverChangeTheSave() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    QCOMPARE(f.backups.count(f.game), 1);
    QVERIFY(f.backups.protect(f.game, f.core));
    QCOMPARE(f.backups.count(f.game), 1);
    for (int i = 0; i < 12; ++i) {
      put(f.save, "save " + QByteArray::number(i));
      QVERIFY(f.backups.protect(f.game, f.core));
      QTest::qSleep(2);
    }
    QCOMPARE(f.backups.count(f.game), 10);
    QCOMPARE(get(f.save), QByteArray("save 11"));
    QCOMPARE(get(f.gameRoot() + '/' + f.version() + "/save.srm"), get(f.save));
  }
  void manualSnapshotsReportStorageAndDeleteOnlyTheBackup() {
    Fixture f;
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    QVERIFY(f.backups.canSnapshot());
    QVERIFY(f.backups.snapshotSelected());
    QCOMPARE(f.backups.count(f.game), 1);
    QCOMPARE(f.backups.storageBytes(), qint64(QByteArray("previous save").size()));
    QVERIFY(f.backups.snapshotSelected());
    QCOMPARE(f.backups.count(f.game), 1);
    QVERIFY(f.backups.message().contains("No changes"));

    put(f.save, "new progress");
    QVERIFY(f.backups.snapshotSelected());
    QCOMPARE(f.backups.count(f.game), 2);
    const QString newest = f.backups.versions().first().toMap()["id"].toString();
    const qint64 expectedBytes =
        QByteArray("previous save").size() + QByteArray("new progress").size();
    QCOMPARE(f.backups.storageBytes(), expectedBytes);
    QVERIFY(!f.backups.deleteVersion("../../outside"));
    QCOMPARE(f.backups.count(f.game), 2);

    f.running = true;
    QVERIFY(!f.backups.deleteVersion(newest));
    QCOMPARE(f.backups.count(f.game), 2);
    f.running = false;
    QVERIFY(f.backups.deleteVersion(newest));
    QCOMPARE(f.backups.count(f.game), 1);
    QCOMPARE(get(f.save), QByteArray("new progress"));
    QCOMPARE(f.backups.storageBytes(), qint64(QByteArray("previous save").size()));
  }
  void manualSnapshotsReportCreationAtRetentionLimit() {
    Fixture f;
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    for (int i = 0; i < 11; ++i) {
      put(f.save, "progress " + QByteArray::number(i));
      QVERIFY(f.backups.snapshotSelected());
      QCOMPARE(f.backups.message(), QString("Save backup created."));
      QTest::qSleep(2);
    }
    QCOMPARE(f.backups.count(f.game), 10);
    QVERIFY(f.backups.snapshotSelected());
    QCOMPARE(f.backups.message(), QString("No changes since the latest backup."));
  }
  void manualSnapshotReportsMissingSaves() {
    Fixture f;
    QVERIFY(QFile::remove(f.save));
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    QVERIFY(f.backups.snapshotSelected());
    QCOMPARE(f.backups.count(f.game), 0);
    QCOMPARE(f.backups.message(), QString("No existing saves to back up."));
  }
  void legacyBackupDeletionLeavesTheCurrentSaveUntouched() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    f.backups.selectGame(f.game);
    const QString version = f.backups.versions().first().toMap()["id"].toString();
    QVERIFY(f.backups.deleteVersion(version));
    QCOMPARE(f.backups.count(f.game), 0);
    QCOMPARE(get(f.save), QByteArray("previous save"));
    QVERIFY(!f.backups.deleteVersion(version));
  }
  void restoreProtectsCurrentSaveAndCanUndo() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString old = f.version();
    put(f.save, "new progress");
    QVERIFY(f.backups.restore(old));
    QCOMPARE(get(f.save), QByteArray("previous save"));
    QCOMPARE(f.backups.count(f.game), 2);
    const auto current = f.version();
    QVERIFY(f.backups.restore(current));
    QCOMPARE(get(f.save), QByteArray("new progress"));
  }
  void damagedManifestDoesNotSuppressProtectionOfTheCurrentSave() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString old = f.version();
    put(f.save, "new progress");
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString latest = f.version();
    const QString file = f.gameRoot() + '/' + latest + "/manifest.json";
    auto item = QJsonDocument::fromJson(get(file)).object();
    item["sha256"] = "damaged";
    put(file, QJsonDocument(item).toJson());
    QVERIFY(f.backups.restore(old));
    QCOMPARE(get(f.save), QByteArray("previous save"));
    const auto undo = f.version();
    QVERIFY(undo != latest);
    QVERIFY(f.backups.restore(undo));
    QCOMPARE(get(f.save), QByteArray("new progress"));
  }
  void deletedSaveCanBeRecovered() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString version = f.version();
    QVERIFY(QFile::remove(f.save));
    QVERIFY(f.backups.restore(version));
    QCOMPARE(get(f.save), QByteArray("previous save"));
  }
  void damagedAndWrongGameBackupsNeverOverwrite() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString version = f.version();
    put(f.gameRoot() + '/' + version + "/save.srm", "damaged");
    QVERIFY(!f.backups.restore(version));
    QVERIFY(f.backups.message().contains("damaged"));
    QCOMPARE(get(f.save), QByteArray("previous save"));
    QVERIFY(!f.backups.restore("../../outside"));
    f.backups.selectGame(f.home + "/different.sfc");
    QVERIFY(!f.backups.restore(version));
    QCOMPARE(get(f.save), QByteArray("previous save"));
  }
  void changedLocationAndRunningEmulatorBlockRestore() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString version = f.version();
    f.running = true;
    QVERIFY(!f.backups.restore(version));
    QVERIFY(f.backups.message().contains("Close emulators"));
    put(f.save, "playing");
    QVERIFY(f.backups.protect(f.game, f.core));
    QCOMPARE(f.backups.count(f.game), 1);
    f.running = false;
    put(f.cfg, get(f.cfg).replace("~/saves", "~/new-saves"));
    put(f.home + "/new-saves/Snes9x/Test Game.srm", "different location");
    QVERIFY(!f.backups.restore(version));
    QCOMPARE(get(f.save), QByteArray("playing"));
    QCOMPARE(get(f.home + "/new-saves/Snes9x/Test Game.srm"), QByteArray("different location"));
  }
  void disabledFlatpakAndSymlinksAreSkipped() {
    Fixture f;
    f.backups.setEnabled(false);
    QVERIFY(f.backups.protect(f.game, f.core));
    QCOMPARE(f.backups.count(f.game), 0);
    f.backups.setEnabled(true);
    QVERIFY(f.backups.protect(f.game, f.core, true));
    QCOMPARE(f.backups.count(f.game), 0);
    QVERIFY(QFile::rename(f.save, f.save + ".original"));
    QVERIFY(QFile::link(f.save + ".original", f.save));
    QVERIFY(f.backups.discover(f.game, f.core).isEmpty());
    QCOMPARE(get(f.save + ".original"), QByteArray("previous save"));
  }
  void storageFailurePreservesCurrentAndPreviousSaves() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString version = f.version();
    put(f.save, "new progress");
    QFile occupied(f.root + "/occupied");
    QVERIFY(occupied.open(QIODevice::WriteOnly));
    QVERIFY(occupied.resize(256 * 1024 * 1024)); occupied.close();
    QVERIFY(!f.backups.protect(f.game, f.core));
    QVERIFY(!f.backups.restore(version));
    QCOMPARE(get(f.save), QByteArray("new progress"));
    QCOMPARE(get(f.gameRoot() + '/' + version + "/save.srm"), QByteArray("previous save"));
  }
  void emulatorStartingDuringSnapshotAbortsIt() {
    Fixture f;
    int checks = 0;
    SaveBackups backups(f.home, f.cfg, f.root, [&] { return ++checks > 1; });
    QVERIFY(!backups.protect(f.game, f.core));
    QCOMPARE(backups.count(f.game), 0);
    QCOMPARE(get(f.save), QByteArray("previous save"));
  }
  void normalLaunchProtectsSaveBeforeStartingProcess() {
    Fixture f;
    const auto previous = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previous); });
    const QString executable = f.home + "/retroarch";
    put(executable, "#!/bin/sh\nprintf 'next session' > '" + f.save.toUtf8() + "'\n");
    QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    qputenv("PATH", f.home.toUtf8());
    GameLauncher launcher;
    launcher.setSaveBackups(&f.backups);
    QVERIFY(launcher.launch("RetroArch", "fixture", false, {}, f.game, f.core, "snes"));
    QTRY_COMPARE(get(f.save), QByteArray("next session"));
    QCOMPARE(f.backups.count(f.game), 1);
    const auto version = f.version();
    QVERIFY(f.backups.restore(version));
    QCOMPARE(get(f.save), QByteArray("previous save"));
  }
  void relocationCopiesLegacyAndSetBackupsWithoutChangingOldKeys() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    QVERIFY(f.backups.snapshotSelected());
    const QString newGame = f.home + "/moved/New Game.sfc";
    const QString newSave = f.home + "/saves/Snes9x/New Game.srm";
    put(newGame, "new rom");
    put(newSave, "current new progress");
    const auto oldLegacy = treeHashes(f.gameRoot());
    const auto oldSets = treeHashes(f.setRoot(f.game));
    const auto preview = f.backups.previewRelocationBackups(f.game, newGame);
    QVERIFY(preview.value("ok").toBool());
    QVERIFY(preview.value("hasLegacy").toBool());
    QVERIFY(preview.value("hasSets").toBool());
    QVariantMap receipt;
    QString error;
    QVERIFY2(f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error), qPrintable(error));
    QCOMPARE(treeHashes(f.gameRoot()), oldLegacy);
    QCOMPARE(treeHashes(f.setRoot(f.game)), oldSets);
    QCOMPARE(get(f.save), QByteArray("previous save"));
    f.backups.selectGame(newGame);
    QCOMPARE(f.backups.versions().size(), 2);
    QString legacyVersion;
    QString setVersion;
    for (const QVariant& value : f.backups.versions()) {
      const QString id = value.toMap().value("id").toString();
      if (id.startsWith("set-"))
        setVersion = id;
      else
        legacyVersion = id;
    }
    QVERIFY(!legacyVersion.isEmpty());
    QVERIFY(!setVersion.isEmpty());
    QVERIFY(f.backups.restore(legacyVersion));
    QCOMPARE(get(newSave), QByteArray("previous save"));
    put(newSave, "current new progress");
    QVERIFY(f.backups.restore(setVersion));
    QCOMPARE(get(newSave), QByteArray("previous save"));
    QCOMPARE(get(f.save), QByteArray("previous save"));
    QCOMPARE(treeHashes(f.gameRoot()), oldLegacy);
    QCOMPARE(treeHashes(f.setRoot(f.game)), oldSets);
  }
  void relocationRefusesAmbiguousConfiguredSaveMappings_data() {
    QTest::addColumn<QString>("change");
    QTest::newRow("different-files") << "files";
    QTest::newRow("different-trees") << "trees";
    QTest::newRow("different-filter") << "filter";
    QTest::newRow("becomes-shared") << "shared";
  }
  void relocationRefusesAmbiguousConfiguredSaveMappings() {
    QFETCH(QString, change);
    Fixture f;
    const QString oldA = f.home + "/old-saves/a.sav";
    const QString oldB = f.home + "/old-saves/b.sav";
    const QString newA = f.home + "/new-saves/a.sav";
    const QString newB = f.home + "/new-saves/b.sav";
    const QString newGame = f.home + "/moved/New.sfc";
    put(oldA, "slot A"); put(oldB, "slot B");
    put(newA, "current A"); put(newB, "current B"); put(newGame, "rom");
    const auto rule = [](const QString& game, const QString& a, const QString& b) {
      return QJsonObject{{"source", "RetroArch"}, {"game", game},
                         {"files", QJsonArray{a, b}}};
    };
    auto oldRule = rule(f.game, oldA, oldB);
    auto newRule = rule(newGame, newB, newA);
    if (change == "trees") {
      oldRule.remove("files"); newRule.remove("files");
      oldRule.insert("trees", QJsonArray{f.home + "/old-saves"});
      newRule.insert("trees", QJsonArray{f.home + "/new-saves"});
    } else if (change == "filter" || change == "shared") {
      newRule = rule(newGame, oldA, oldB);
      if (change == "filter")
        newRule.insert("relativePattern", "^a[.]sav$");
      else
        newRule.insert("shared", true);
    }
    put(f.home + "/.config/omakade/save-layouts.json",
        QJsonDocument(QJsonObject{{"format", 1}, {"layouts", QJsonArray{
          oldRule, newRule}}}).toJson());
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    QVERIFY(f.backups.snapshotSelected());
    const auto oldSets = treeHashes(f.setRoot(f.game));
    QVariantMap receipt;
    QString error;
    QVERIFY(!f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error));
    QVERIFY(error.contains("mapped"));
    QVERIFY(!QFileInfo::exists(f.setRoot(newGame)));
    QCOMPARE(treeHashes(f.setRoot(f.game)), oldSets);
    QCOMPARE(get(oldA), QByteArray("slot A")); QCOMPARE(get(oldB), QByteArray("slot B"));
    QCOMPARE(get(newA), QByteArray("current A")); QCOMPARE(get(newB), QByteArray("current B"));
  }
  void failedRelocationCopyLeavesNoDestinationAndPreservesOldKeys() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    f.backups.selectLaunch("RetroArch", f.game, f.core, false, "fixture", {}, {});
    QVERIFY(f.backups.snapshotSelected());
    const QString newGame = f.home + "/moved/Failure.sfc";
    put(newGame, "new rom");
    const auto oldLegacy = treeHashes(f.gameRoot());
    const auto oldSets = treeHashes(f.setRoot(f.game));
    int copied = 0;
    const SaveSetStore::CopyFile failMidCopy = [&copied](const QString& source,
                                                         const QString& destination) {
      if (++copied == 2)
        return false;
      return QFile::copy(source, destination);
    };
    QVariantMap receipt;
    QString error;
    QVERIFY(!f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error, failMidCopy));
    QVERIFY(copied >= 2);
    QVERIFY(!QFileInfo::exists(f.gameRootFor(newGame)));
    QVERIFY(!QFileInfo::exists(f.setRoot(newGame)));
    QCOMPARE(treeHashes(f.gameRoot()), oldLegacy);
    QCOMPARE(treeHashes(f.setRoot(f.game)), oldSets);
  }
  void relocationCopyHonorsRunningAndRecoveryGuards() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString newGame = f.home + "/roms/New.sfc";
    const auto oldFiles = treeHashes(f.gameRoot());
    f.running = true;
    QVERIFY(!f.backups.previewRelocationBackups(f.game, newGame).value("ok").toBool());
    QVariantMap receipt;
    QString error;
    QVERIFY(!f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error));
    f.running = false;
    QVERIFY(QDir().mkpath(f.root + "/sets/.restore"));
    QVERIFY(!f.backups.previewRelocationBackups(f.game, newGame).value("ok").toBool());
    QVERIFY(!f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error));
    QVERIFY(!QFileInfo::exists(f.gameRootFor(newGame)));
    QVERIFY(!QFileInfo::exists(f.setRoot(newGame)));
    QCOMPARE(treeHashes(f.gameRoot()), oldFiles);
  }
  void relocationRefusesDestinationBackups() {
    Fixture f;
    QVERIFY(f.backups.protect(f.game, f.core));
    const QString newGame = f.home + "/moved/Occupied.sfc";
    const QString newSave = f.home + "/saves/Snes9x/Occupied.srm";
    put(newGame, "new rom");
    put(newSave, "new save");
    QVERIFY(f.backups.protect(newGame, f.core));
    const auto preview = f.backups.previewRelocationBackups(f.game, newGame);
    QVERIFY(!preview.value("ok").toBool());
    QVERIFY(preview.value("refusal").toString().contains("already exist"));
  }
  void relocationMirrorsSharedAliasesWithoutCopyingSharedSets() {
    Fixture f;
    SaveSetStore store(f.root + "/sets", [&f] { return f.running; });
    SaveLayout layout{{f.save}, {}, "Shared fixture saves", {}, true};
    const QJsonObject context{{"source", "RetroArch"}, {"game", f.game}, {"core", f.core}};
    QString error;
    QVERIFY2(store.snapshot(f.game, context, layout, &error), qPrintable(error));
    const auto original = store.versions(f.game).first().toMap();
    const QString sharedKey = original.value("storageKey").toString();
    const QString sharedRoot = f.root + "/sets/" + keyHash(sharedKey);
    const auto sharedFiles = treeHashes(sharedRoot);
    const QString oldAlias = f.setRoot(f.game) + "/shared.json";
    const QByteArray oldAliasBytes = get(oldAlias);
    const QString newGame = f.home + "/moved/Shared.sfc";
    put(newGame, "new rom");
    const auto preview = f.backups.previewRelocationBackups(f.game, newGame);
    QVERIFY(preview.value("ok").toBool());
    QVERIFY(preview.value("hasShared").toBool());
    QVERIFY(!preview.value("copyable").toBool());
    QVariantMap receipt;
    QVERIFY2(f.backups.copyRelocationBackups(f.game, newGame, &receipt, &error), qPrintable(error));
    const auto mirrored = store.versions(newGame);
    QCOMPARE(mirrored.size(), 1);
    QCOMPARE(mirrored.first().toMap().value("id").toString(), original.value("id").toString());
    QCOMPARE(mirrored.first().toMap().value("storageKey").toString(), sharedKey);
    QCOMPARE(treeHashes(sharedRoot), sharedFiles);
    QCOMPARE(get(oldAlias), oldAliasBytes);
    const auto copiedAlias = QJsonDocument::fromJson(get(f.setRoot(newGame) + "/shared.json"))
                                 .object().value("keys").toArray();
    QCOMPARE(copiedAlias, QJsonDocument::fromJson(oldAliasBytes).object().value("keys").toArray());
    const auto secondPreview = f.backups.previewRelocationBackups(f.game, newGame);
    QVERIFY(!secondPreview.value("ok").toBool());
    QVERIFY2(f.backups.rollbackRelocationBackups(newGame, receipt, &error), qPrintable(error));
    QVERIFY(store.versions(newGame).isEmpty());
    QCOMPARE(treeHashes(sharedRoot), sharedFiles);
    QCOMPARE(get(oldAlias), oldAliasBytes);
  }
};
QTEST_GUILESS_MAIN(SaveBackupsTests)
#include "SaveBackupsTests.moc"

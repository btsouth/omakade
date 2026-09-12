#include "saves/SaveBackups.h"
#include "launch/GameLauncher.h"
#include "app/AppSettings.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

namespace {
void put(const QString& path, const QByteArray& bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) qFatal("Cannot create fixture directory");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Cannot write fixture");
}
QByteArray get(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
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
  QString gameRoot() const { return root + '/' + QString::fromLatin1(QCryptographicHash::hash(game.toUtf8(), QCryptographicHash::Sha256).toHex()); }
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
};
QTEST_GUILESS_MAIN(SaveBackupsTests)
#include "SaveBackupsTests.moc"

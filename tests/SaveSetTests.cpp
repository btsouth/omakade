#include "launch/GameLauncher.h"
#include "library/ConsoleCatalog.h"
#include "saves/SaveBackups.h"
#include "saves/SaveLayouts.h"
#include "saves/SaveSetStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
namespace {
void put(const QString& path, const QByteArray& data) {
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(data), data.size());
}
QByteArray get(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
struct Fixture {
  QTemporaryDir dir;
  QString home = dir.path(), root = home + "/backups", game = home + "/game.gbc";
  SaveLayout layout{
      {home + "/game.srm", home + "/game.rtc"}, {home + "/savefolder"}, "Fixture saves", {}, false};
  QJsonObject context{{"source", "fixture"}, {"game", game}};
  bool running = false;
  SaveSetStore store{root, [this] { return running; }};
  SaveSetStore::Resolver resolver = [this](const QJsonObject& c) {
    return c == context ? layout : SaveLayout{};
  };
  QString error;
  Fixture() {
    put(game, "rom");
    put(home + "/game.srm", "original SRAM");
    put(home + "/game.rtc", "original RTC");
    put(home + "/savefolder/nested/progress", "original progress");
  }
  QString version() { return store.versions(game).first().toMap()["id"].toString(); }
  QString manifestPath(const QString& version) const {
    return root + '/' +
           QString::fromLatin1(
               QCryptographicHash::hash(game.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           '/' + version.mid(4) + "/manifest.json";
  }
};
} // namespace
class SaveSetTests : public QObject {
  Q_OBJECT
private slots:
  void unreadableFoldersRefuseSnapshotAndRestore_data() {
    QTest::addColumn<QString>("relative");
    QTest::newRow("save-root") << "savefolder";
    QTest::newRow("nested-save-folder") << "savefolder/nested";
  }
  void unreadableFoldersRefuseSnapshotAndRestore() {
    QFETCH(QString, relative);
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    put(f.home + "/game.srm", "new progress");
    const QString folder = f.home + '/' + relative;
    const auto permissions = QFileInfo(folder).permissions();
    const auto restorePermissions =
        qScopeGuard([&] { QFile::setPermissions(folder, permissions); });
    QVERIFY(QFile::setPermissions(folder, QFile::WriteOwner | QFile::ExeOwner));
    if (QFileInfo(folder).isReadable())
      QSKIP("This user bypasses directory read permissions");
    QVERIFY(!f.store.snapshot(f.game, f.context, f.layout, &f.error));
    QVERIFY(f.error.contains("read completely"));
    QCOMPARE(f.store.versions(f.game).size(), 1);
    QVERIFY(!f.store.restore(f.game, version, f.resolver, &f.error));
    QVERIFY(!f.store.pending());
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new progress"));
    QVERIFY(QFile::setPermissions(folder, permissions));
    QCOMPARE(get(f.home + "/savefolder/nested/progress"), QByteArray("original progress"));
    QVERIFY(f.store.restore(f.game, version, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("original SRAM"));
    QCOMPARE(get(f.home + "/savefolder/nested/progress"), QByteArray("original progress"));
  }
  void malformedSnapshotIsNeverRestored_data() {
    QTest::addColumn<QString>("field");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("missing-entries") << "entries" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("null-entries") << "entries" << QJsonValue();
    QTest::newRow("object-entries") << "entries" << QJsonValue(QJsonObject{});
    QTest::newRow("truncated-entries") << "entries" << QJsonValue(QJsonArray{});
    QTest::newRow("missing-bytes") << "bytes" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("string-bytes") << "bytes" << QJsonValue("0");
    QTest::newRow("fractional-bytes") << "bytes" << QJsonValue(0.5);
    QTest::newRow("wrong-total") << "bytes" << QJsonValue(0);
    QTest::newRow("missing-context") << "context" << QJsonValue(QJsonValue::Undefined);
  }
  void malformedSnapshotIsNeverRestored() {
    QFETCH(QString, field);
    QFETCH(QJsonValue, value);
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    auto manifest = QJsonDocument::fromJson(get(f.manifestPath(version))).object();
    manifest.insert(field, value);
    put(f.manifestPath(version), QJsonDocument(manifest).toJson());
    put(f.home + "/game.srm", "new progress");
    QVERIFY(!f.store.restore(f.game, version, f.resolver, &f.error));
    QVERIFY(!f.error.isEmpty());
    QVERIFY(!f.store.pending());
    QCOMPARE(f.store.versions(f.game).size(), 1);
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new progress"));
    QCOMPARE(get(f.home + "/game.rtc"), QByteArray("original RTC"));
    QCOMPARE(get(f.home + "/savefolder/nested/progress"), QByteArray("original progress"));
  }
  void missingSizeOnEmptyMemberIsRejected() {
    Fixture f;
    put(f.home + "/empty", {});
    f.layout.files = {f.home + "/empty"};
    f.layout.trees.clear();
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    auto manifest = QJsonDocument::fromJson(get(f.manifestPath(version))).object();
    auto entries = manifest["entries"].toArray();
    auto member = entries[0].toObject();
    member.remove("bytes");
    entries[0] = member;
    manifest["entries"] = entries;
    put(f.manifestPath(version), QJsonDocument(manifest).toJson());
    put(f.home + "/empty", "current progress");
    QVERIFY(!f.store.restore(f.game, version, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/empty"), QByteArray("current progress"));
  }
  void malformedRecoveryKeepsCopiesAndCurrentSaves_data() {
    QTest::addColumn<QString>("field");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("missing-before") << "before" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("null-after") << "after" << QJsonValue();
    QTest::newRow("missing-committed") << "committed" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("string-committed") << "committed" << QJsonValue("true");
  }
  void malformedRecoveryKeepsCopiesAndCurrentSaves() {
    QFETCH(QString, field);
    QFETCH(QJsonValue, value);
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    put(f.home + "/game.srm", "new SRAM");
    put(f.home + "/game.rtc", "new RTC");
    int checks = 0;
    bool interrupt = true;
    SaveSetStore store(f.root, [&] { return interrupt && ++checks >= 6; });
    QVERIFY(!store.restore(f.game, version, f.resolver, &f.error));
    QVERIFY(store.pending());
    const QString path = f.root + "/.restore/manifest.json";
    const auto original = get(path);
    auto journal = QJsonDocument::fromJson(original).object();
    journal.insert(field, value);
    put(path, QJsonDocument(journal).toJson());
    const auto sram = get(f.home + "/game.srm"), rtc = get(f.home + "/game.rtc");
    interrupt = false;
    QVERIFY(!store.recover(f.resolver, &f.error));
    QVERIFY(store.pending());
    QCOMPARE(get(f.home + "/game.srm"), sram);
    QCOMPARE(get(f.home + "/game.rtc"), rtc);
    put(path, original);
    QVERIFY(store.recover(f.resolver, &f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new SRAM"));
    QCOMPARE(get(f.home + "/game.rtc"), QByteArray("new RTC"));
  }
  void retroArchDefaultPaths_data() {
    QTest::addColumn<bool>("flatpak");
    QTest::newRow("native") << false;
    QTest::newRow("flatpak") << true;
  }
  void retroArchDefaultPaths() {
    QFETCH(bool, flatpak);
    Fixture f;
    const QString cfg = f.home + (flatpak ? "/.var/app/org.libretro.RetroArch/config" : "/.config");
    const QString config = cfg + "/retroarch/retroarch.cfg";
    const QByteArray settings =
        "savefile_directory = \"default\"\n"
        "sort_savefiles_enable = \"true\"\nauto_overrides_enable = \"true\"\n"
        "rgui_config_directory = \"default\"\nsystem_directory = \"default\"\n";
    put(config, settings);
    QJsonObject c{{"source", "RetroArch"},
                  {"game", f.game},
                  {"core", "gambatte_libretro.so"},
                  {"flatpak", flatpak}};
    auto resolve = [&](const QJsonObject& context) {
      return resolveSaveLayout(context, f.home, config);
    };
    const QString save = cfg + "/retroarch/saves/Gambatte/game.srm";
    put(save, "original save");
    auto l = resolve(c);
    QVERIFY(l.valid());
    QVERIFY(l.files.contains(save));
    QVERIFY(f.store.snapshot(f.game, c, l, &f.error));
    const auto version = f.version();
    put(save, "new save");
    QVERIFY(f.store.restore(f.game, version, resolve, &f.error));
    QCOMPARE(get(save), QByteArray("original save"));
    put(cfg + "/retroarch/config/Gambatte/game.cfg", "savefile_directory = \"~/custom\"\n");
    QVERIFY(resolve(c).files.contains(f.home + "/custom/Gambatte/game.srm"));
    put(config, settings + "savefiles_in_content_dir = \"true\"\n");
    QVERIFY(resolve(c).files.contains(f.home + "/Gambatte/game.srm"));
    c["core"] = "flycast_libretro.so";
    l = resolve(c);
    QVERIFY(l.files.contains(f.home + "/dc/vmu_save_A1.bin"));
    for (const auto& path : l.files)
      QVERIFY(!path.startsWith("/default/"));
  }
  void completeSetRestoreAndUndo() {
    Fixture f;
    QVERIFY2(f.store.snapshot(f.game, f.context, f.layout, &f.error), qPrintable(f.error));
    const auto old = f.version();
    put(f.home + "/game.srm", "new SRAM");
    QVERIFY(QFile::remove(f.home + "/game.rtc"));
    put(f.home + "/savefolder/newfile", "new file");
    QVERIFY2(f.store.restore(f.game, old, f.resolver, &f.error), qPrintable(f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("original SRAM"));
    QCOMPARE(get(f.home + "/game.rtc"), QByteArray("original RTC"));
    QVERIFY(!QFileInfo::exists(f.home + "/savefolder/newfile"));
    const auto undo = f.version();
    QVERIFY(undo != old);
    QVERIFY(f.store.restore(f.game, undo, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new SRAM"));
    QVERIFY(!QFileInfo::exists(f.home + "/game.rtc"));
    QCOMPARE(get(f.home + "/savefolder/newfile"), QByteArray("new file"));
  }
  void deletedFoldersAndEmptyFiles() {
    Fixture f;
    put(f.home + "/savefolder/empty", {});
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    auto id = f.version();
    QVERIFY(QDir(f.home + "/savefolder").removeRecursively());
    QVERIFY(f.store.restore(f.game, id, f.resolver, &f.error));
    QVERIFY(QFileInfo::exists(f.home + "/savefolder/empty"));
    QCOMPARE(get(f.home + "/savefolder/nested/progress"), QByteArray("original progress"));
  }
  void restoringAnEntirelyDeletedSetCanBeUndone() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto old = f.version();
    QVERIFY(QFile::remove(f.home + "/game.srm"));
    QVERIFY(QFile::remove(f.home + "/game.rtc"));
    QVERIFY(QDir(f.home + "/savefolder").removeRecursively());
    QVERIFY(f.store.restore(f.game, old, f.resolver, &f.error));
    const auto absent = f.version();
    QVERIFY(absent != old);
    QVERIFY(f.store.restore(f.game, absent, f.resolver, &f.error));
    QVERIFY(!QFileInfo::exists(f.home + "/game.srm"));
    QVERIFY(!QFileInfo::exists(f.home + "/savefolder/nested/progress"));
  }
  void corruptBackupNeverChangesCurrentFiles() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto id = f.version();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(f.game.toUtf8(), QCryptographicHash::Sha256).toHex());
    put(f.root + '/' + hash + '/' + id.mid(4) + "/0", "corrupt");
    put(f.home + "/game.srm", "new");
    QVERIFY(!f.store.restore(f.game, id, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new"));
  }
  void interruptedRestoreRecoversWholeSet() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto id = f.version();
    put(f.home + "/game.srm", "new SRAM");
    put(f.home + "/game.rtc", "new RTC");
    int checks = 0;
    bool interrupt = true;
    SaveSetStore interrupted(f.root, [&] { return interrupt && ++checks >= 6; });
    QVERIFY(!interrupted.restore(f.game, id, f.resolver, &f.error));
    QVERIFY(interrupted.pending());
    interrupt = false;
    QVERIFY2(interrupted.recover(f.resolver, &f.error), qPrintable(f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("new SRAM"));
    QCOMPARE(get(f.home + "/game.rtc"), QByteArray("new RTC"));
    QVERIFY(!interrupted.pending());
  }
  void recoveryRefusesUnrelatedNewProgress() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto id = f.version();
    put(f.home + "/game.srm", "new SRAM");
    put(f.home + "/game.rtc", "new RTC");
    int checks = 0;
    bool interrupt = true;
    SaveSetStore interrupted(f.root, [&] { return interrupt && ++checks >= 6; });
    QVERIFY(!interrupted.restore(f.game, id, f.resolver, &f.error));
    QVERIFY(interrupted.pending());
    put(f.home + "/game.srm", "external progress");
    interrupt = false;
    QVERIFY(!interrupted.recover(f.resolver, &f.error));
    QVERIFY(interrupted.pending());
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("external progress"));
  }
  void changedScopeRunningAndSymlinksAreRefused() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto id = f.version();
    f.running = true;
    QVERIFY(!f.store.restore(f.game, id, f.resolver, &f.error));
    f.running = false;
    f.layout.files << f.home + "/other";
    QVERIFY(!f.store.restore(f.game, id, f.resolver, &f.error));
    f.layout.files.removeLast();
    QVERIFY(QFile::rename(f.home + "/game.srm", f.home + "/original"));
    QVERIFY(QFile::link(f.home + "/original", f.home + "/game.srm"));
    QVERIFY(!f.store.snapshot(f.game, f.context, f.layout, &f.error));
    QVERIFY(!f.store.restore(f.game, id, f.resolver, &f.error));
  }
  void dedupAndRetention() {
    Fixture f;
    for (int i = 0; i < 12; ++i) {
      put(f.home + "/game.srm", QByteArray::number(i));
      QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    }
    QCOMPARE(f.store.versions(f.game).size(), 10);
    const auto id = f.version();
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    QCOMPARE(f.version(), id);
  }
  void deletionIsScopedAndBlockedWhileRunning() {
    Fixture f;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto id = f.version();
    f.running = true;
    QVERIFY(!f.store.remove(f.game, id, &f.error));
    f.running = false;
    QVERIFY(!f.store.remove(f.game, "../../outside", &f.error));
    QCOMPARE(f.store.versions(f.game).size(), 1);
    QVERIFY(f.store.remove(f.game, id, &f.error));
    QVERIFY(f.store.versions(f.game).isEmpty());
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("original SRAM"));
    QVERIFY(!f.store.remove(f.game, id, &f.error));
  }
  void sharedBanksReuseHistoryAcrossGames() {
    Fixture f;
    f.layout.shared = true;
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    const QString second = f.home + "/other.gbc";
    auto otherContext = f.context;
    otherContext["game"] = second;
    QVERIFY(f.store.snapshot(second, otherContext, f.layout, &f.error));
    QCOMPARE(f.store.versions(second).size(), 1);
    QCOMPARE(f.store.versions(second).first().toMap()["id"].toString(), version);
    put(f.home + "/game.srm", "new progress");
    QVERIFY(f.store.restore(second, version, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("original SRAM"));
    QCOMPARE(f.store.versions(f.game).size(), 2);
    QCOMPARE(f.store.versions(second).size(), 2);
    QVERIFY(f.store.remove(second, version, &f.error));
    QCOMPARE(f.store.versions(f.game).size(), 1);
    QCOMPARE(f.store.versions(second).size(), 1);
    QCOMPARE(get(f.home + "/game.srm"), QByteArray("original SRAM"));
  }
  void explicitPortableLayouts() {
    Fixture f;
    const QString config = f.home + "/retroarch.cfg";
    QJsonObject rule{{"source", "Ryujinx"},
                     {"game", f.game},
                     {"trees", QJsonArray{f.home + "/portable/saves"}},
                     {"description", "Portable Eden saves"}};
    put(f.home + "/.config/omakade/save-layouts.json",
        QJsonDocument(QJsonObject{{"format", 1}, {"layouts", QJsonArray{rule}}}).toJson());
    auto l = resolveSaveLayout({{"source", "Ryujinx"}, {"game", f.game}}, f.home, config);
    QCOMPARE(l.trees, QStringList{f.home + "/portable/saves"});
    auto other =
        resolveSaveLayout({{"source", "Ryujinx"}, {"game", f.home + "/other.nsp"}}, f.home, config);
    QVERIFY(!other.trees.contains(f.home + "/portable/saves"));
  }
  void pendingRecoveryBlocksLaunchBeforeStartingEmulator() {
    Fixture f;
    const QString config = f.home + "/retroarch.cfg";
    put(config, "savefile_directory = \"~/saves\"\n");
    put(f.home + "/saves/game.srm", "original SRAM");
    put(f.home + "/saves/game.rtc", "original RTC");
    const QJsonObject context{
        {"source", "RetroArch"}, {"game", f.game}, {"core", "gambatte_libretro.so"}};
    auto resolve = [&](const QJsonObject& c) { return resolveSaveLayout(c, f.home, config); };
    auto layout = resolve(context);
    int checks = 0;
    bool interrupt = false;
    SaveSetStore store(f.root + "/sets", [&] { return interrupt && ++checks >= 6; });
    QVERIFY(store.snapshot(f.game, context, layout, &f.error));
    const auto version = store.versions(f.game).first().toMap()["id"].toString();
    put(f.home + "/saves/game.srm", "new SRAM");
    put(f.home + "/saves/game.rtc", "new RTC");
    interrupt = true;
    QVERIFY(!store.restore(f.game, version, resolve, &f.error));
    QVERIFY(store.pending());
    put(f.home + "/saves/game.srm", "external progress");
    SaveBackups backups(f.home, config, f.root, [] { return false; });
    QVERIFY(!backups.protectLaunch("RetroArch", f.game, "gambatte_libretro.so"));
    QVERIFY(backups.message().contains("Launch is paused"));
    QCOMPARE(get(f.home + "/saves/game.srm"), QByteArray("external progress"));
  }
  void retroArchClockAndOverrides() {
    Fixture f;
    const QString config = f.home + "/retroarch.cfg";
    put(config, "savefile_directory = \"~/saves\"\nsort_savefiles_enable = "
                "\"true\"\nrgui_config_directory = \"~/overrides\"\n");
    QJsonObject c{{"source", "RetroArch"}, {"game", f.game}, {"core", "gambatte_libretro.so"}};
    auto l = resolveSaveLayout(c, f.home, config);
    QVERIFY(l.valid());
    QVERIFY(l.files.contains(f.home + "/saves/Gambatte/game.rtc"));
    put(f.home + "/overrides/Gambatte/game.cfg", "savefile_directory = \"~/custom\"\n");
    l = resolveSaveLayout(c, f.home, config);
    QVERIFY(l.files.contains(f.home + "/custom/Gambatte/game.srm"));
  }
  void catalogCartridgeEmulatorsHaveAdapters() {
    Fixture f;
    const QString config = f.home + "/retroarch.cfg";
    put(config, "savefile_directory = \"~/saves\"\n");
    for (const auto& console : ConsoleCatalog::all()) {
      for (const auto& executable : console.standaloneExecutables) {
        auto l = resolveSaveLayout({{"source", executable}, {"game", f.game}}, f.home, config);
        QVERIFY2(l.valid(), qPrintable(executable + ": " + l.error));
      }
      for (const auto& core : console.retroArchCores) {
        auto l = resolveSaveLayout(
            {{"source", "RetroArch"}, {"game", f.game}, {"core", core + ".so"}}, f.home, config);
        QVERIFY2(l.valid(), qPrintable(core + ": " + l.error));
      }
    }
  }
  void dedicatedSources_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("relative");
    QTest::newRow("pcsx2") << "PCSX2" << "SLUS-00000" << ".config/PCSX2/memcards/card.ps2";
    QTest::newRow("dolphin") << "Dolphin" << "GM8E01"
                             << ".local/share/dolphin-emu/GC/USA/Card A/game.gci";
    QTest::newRow("dolphin-compressed")
        << "Dolphin" << "path:/game.ciso" << ".local/share/dolphin-emu/GC/USA/Card A/game.gci";
    QTest::newRow("wii") << "Dolphin" << "RMGE01"
                         << ".local/share/dolphin-emu/Wii/title/00010000/524d4745/data/save.dat";
    QTest::newRow("cemu")
        << "Cemu" << "0005000010101c00"
        << ".local/share/Cemu/mlc01/usr/save/00050000/10101c00/user/80000001/save.dat";
    QTest::newRow("shadps4") << "shadPS4" << "CUSA00900"
                             << ".local/share/shadPS4/home/1000/savedata/CUSA00900/userdata";
    QTest::newRow("ryujinx") << "Ryujinx" << "0100000000000000"
                             << ".config/Ryujinx/bis/user/save/0000000000000001/0/save.dat";
  }
  void dedicatedSources() {
    QFETCH(QString, source);
    QFETCH(QString, id);
    QFETCH(QString, relative);
    const QMap<QString, QString> apps{{"PCSX2", "net.pcsx2.PCSX2"},
                                      {"Dolphin", "org.DolphinEmu.dolphin-emu"},
                                      {"Cemu", "info.cemu.Cemu"},
                                      {"shadPS4", "net.shadps4.shadPS4"},
                                      {"Ryujinx", "io.github.ryubing.Ryujinx"}};
    for (bool flatpak : {false, true}) {
      Fixture f;
      QString location = relative;
      if (flatpak) {
        location.replace(".config/", ".var/app/" + apps[source] + "/config/");
        location.replace(".local/share/", ".var/app/" + apps[source] + "/data/");
      }
      const QString path = f.home + '/' + location;
      put(path, "old");
      QJsonObject c{{"source", source}, {"game", f.game}, {"id", id}, {"flatpak", flatpak}};
      auto l = resolveSaveLayout(c, f.home, f.home + "/retroarch.cfg");
      QVERIFY2(l.valid(), qPrintable(l.error));
      auto resolver = [&](const QJsonObject& v) {
        return resolveSaveLayout(v, f.home, f.home + "/retroarch.cfg");
      };
      QVERIFY2(f.store.snapshot(f.game, c, l, &f.error), qPrintable(f.error));
      QVERIFY(!f.store.versions(f.game).isEmpty());
      const auto version = f.version();
      put(path, "new");
      QVERIFY2(f.store.restore(f.game, version, resolver, &f.error), qPrintable(f.error));
      QCOMPARE(get(path), QByteArray("old"));
    }
  }
  void dedicatedLaunches_data() { dedicatedSources_data(); }
  void dedicatedLaunches() {
    QFETCH(QString, source);
    QFETCH(QString, id);
    QFETCH(QString, relative);
    Fixture f;
    const QString path = f.home + '/' + relative;
    put(path, "previous progress");
    const QMap<QString, QString> binaries{{"PCSX2", "pcsx2-qt"},
                                          {"Dolphin", "dolphin-emu"},
                                          {"Cemu", "cemu"},
                                          {"Ryujinx", "Ryujinx"},
                                          {"shadPS4", "shadps4"}};
    const QString executable = f.home + "/bin/" + binaries[source];
    put(executable, "#!/bin/sh\nprintf 'new progress' > '" + path.toUtf8() + "'\n");
    QVERIFY(
        QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    const auto reset = qScopeGuard([&] { qputenv("PATH", oldPath); });
    qputenv("PATH", (f.home + "/bin").toUtf8());
    QString game = f.home + "/game.iso";
    if (source == "shadPS4")
      game = f.home + "/CUSA00900/eboot.bin";
    else if (source == "Ryujinx")
      game = f.home + "/game.nsp";
    else if (source == "Cemu")
      game = f.home + "/code/game.rpx";
    put(game, "rom");
    SaveBackups backups(f.home, f.home + "/retroarch.cfg", f.root, [] { return false; });
    GameLauncher launcher;
    launcher.setSaveBackups(&backups);
    QVERIFY2(
        launcher.launch(source, source == "PCSX2" ? "path:" + game : id, false, {}, game, game),
        qPrintable(launcher.lastError()));
    QTRY_COMPARE(get(path), QByteArray("new progress"));
    backups.selectGame(game);
    QVERIFY(!backups.versions().isEmpty());
    QVERIFY2(backups.restore(backups.versions().first().toMap()["id"].toString()),
             qPrintable(backups.message()));
    QCOMPARE(get(path), QByteArray("previous progress"));
  }
  void rawMemoryCardAndStorageFailure() {
    Fixture f;
    const QString card = f.home + "/card.ps2";
    QFile file(card);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(8650752));
    file.close();
    f.layout.files = {card};
    f.layout.trees.clear();
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.seek(8650751));
    QCOMPARE(file.write("x", 1), 1);
    file.close();
    QVERIFY(f.store.restore(f.game, version, f.resolver, &f.error));
    QCOMPARE(QFileInfo(card).size(), qint64(8650752));
    QCOMPARE(get(card).back(), char(0));
    put(card, "new progress");
    QFile full(f.root + "/occupied");
    QVERIFY(full.open(QIODevice::WriteOnly));
    QVERIFY(full.resize(2LL * 1024 * 1024 * 1024));
    full.close();
    QVERIFY(!f.store.snapshot(f.game, f.context, f.layout, &f.error));
    QVERIFY(!f.store.restore(f.game, version, f.resolver, &f.error));
    QCOMPARE(get(card), QByteArray("new progress"));
  }
  void standaloneBanksExcludeStatesAndIncludePerGameVmus() {
    Fixture f;
    for (const QString source : {"mupen64plus", "flycast"}) {
      const QString folder =
          f.home + "/.local/share/" + source + (source == "mupen64plus" ? "/save" : "");
      const QString save =
          folder + (source == "mupen64plus" ? "/Game.eep" : "/T12345_vmu_save_A1.bin");
      const QString state = folder + "/Game.state";
      put(save, "old");
      put(state, "old state");
      const QJsonObject c{{"source", source}, {"game", f.game}};
      auto resolver = [&](const QJsonObject& v) {
        return resolveSaveLayout(v, f.home, f.home + "/retroarch.cfg");
      };
      const auto layout = resolver(c);
      QVERIFY(f.store.snapshot(f.game, c, layout, &f.error));
      const auto version = f.version();
      put(save, "new");
      put(state, "new state");
      QVERIFY(f.store.restore(f.game, version, resolver, &f.error));
      QCOMPARE(get(save), QByteArray("old"));
      QCOMPARE(get(state), QByteArray("new state"));
    }
  }
  void filteredBanksKeepUnrelatedSaves() {
    Fixture f;
    f.layout.files.clear();
    f.layout.relativePattern = "^nested/.+$";
    put(f.home + "/savefolder/unrelated", "other game");
    QVERIFY(f.store.snapshot(f.game, f.context, f.layout, &f.error));
    const auto version = f.version();
    put(f.home + "/savefolder/nested/progress", "new progress");
    put(f.home + "/savefolder/unrelated", "other game progressed");
    QVERIFY(f.store.restore(f.game, version, f.resolver, &f.error));
    QCOMPARE(get(f.home + "/savefolder/unrelated"), QByteArray("other game progressed"));
  }
};
QTEST_GUILESS_MAIN(SaveSetTests)
#include "SaveSetTests.moc"

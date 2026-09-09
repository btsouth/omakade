#include "launch/GameLauncher.h"
#include "launch/PlayRequest.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"

#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

namespace {
void executable(const QString& directory, const QString& name) {
  QFile file(directory + '/' + name);
  if (!file.open(QIODevice::WriteOnly)) qFatal("Cannot write test executable");
  file.write("#!/bin/sh\nprintf '%s\\n' '" + name.toUtf8() + "' \"$@\" > \"$OMAKADE_LAUNCH_TEST_OUTPUT\"\n");
  file.close();
  if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    qFatal("Cannot make test fixture executable");
}
QByteArray contents(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
class CartridgeModel : public QAbstractListModel {
public:
  QString path, system;
  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
  QHash<int, QByteArray> roleNames() const override { return GameRoles::names(); }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid()) return {};
    switch (role) {
    case GameRoles::Source: return QString("RetroArch");
    case GameRoles::AppId: return QString("test-disc");
    case GameRoles::Title: return QString("Test game");
    case GameRoles::Runner: case GameRoles::LaunchTarget: return QString("");
    case GameRoles::InstallPath: return path;
    case GameRoles::System: return system;
    case GameRoles::Installed: return true;
    default: return {};
    }
  }
};
}

class AutomaticLaunchTests : public QObject {
  Q_OBJECT
private slots:
  void declaredSystemWinsOverSharedFormats() {
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.chd", "Sega - Dreamcast"), QString("dreamcast"));
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.chd", "PlayStation"), QString("psx"));
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.iso", "ps2"), QString("ps2"));
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.zip", "snes"), QString("snes"));
    QVERIFY(GameLauncher::cartridgeSystem("/games/Game.chd").isEmpty());
    QVERIFY(GameLauncher::cartridgeSystem("/games/Game.iso").isEmpty());
    QVERIFY(GameLauncher::cartridgeSystem("/games/Game.zip").isEmpty());
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.SFC"), QString("snes"));
    QCOMPARE(GameLauncher::cartridgeSystem("/games/Game.zip#Game.gba"), QString("gba"));
    // An explicitly unsupported system must not become SNES based on a suffix.
    QVERIFY(GameLauncher::cartridgeSystem("/games/Game.sfc", "unknown").isEmpty());
  }
  void automaticFallbackRespectsConfiguredCore() {
    const QString path = "/games/Game.sfc", core = "/cores/snes9x_libretro.so";
    auto command = GameLauncher::resolvedCartridgeCommand(path, {}, false, false, "snes9x", core, false);
    QCOMPARE(command.program, QString("snes9x"));
    QCOMPARE(command.arguments, QStringList{path});
    QVERIFY(!GameLauncher::resolvedCartridgeCommand(path, core, false, true, "snes9x", core, false).isValid());
    command = GameLauncher::resolvedCartridgeCommand(path, core, false, true, "snes9x", {}, true);
    QCOMPARE(command.program, QString("retroarch"));
    QCOMPARE(command.arguments, (QStringList{"--fullscreen", "-L", core, path}));
    QVERIFY(!GameLauncher::resolvedCartridgeCommand({}, {}, false, true, "snes9x", {}, true).isValid());
  }
  void archiveEntriesKeepRetroArchSyntaxWithRetroArch() {
    const QString entry = "/games/Collection.zip#Game.sfc";
    const QString core = "/cores/snes9x_libretro.so";
    auto command = GameLauncher::resolvedCartridgeCommand(entry, {}, false, true, "snes9x", core, true);
    QCOMPARE(command.program, QString("retroarch"));
    QCOMPARE(command.arguments.constLast(), entry);
    QVERIFY(!GameLauncher::resolvedCartridgeCommand(entry, {}, false, true, "snes9x", {}, false).isValid());
    QVERIFY(!GameLauncher::resolvedCartridgeCommand(entry, {}, false, true, "snes9x", {}, true).isValid());
    command = GameLauncher::resolvedCartridgeCommand("/games/Set.7Z#Game.sfc", {}, false, true, "snes9x", core, true);
    QCOMPARE(command.program, QString("retroarch"));
    command = GameLauncher::resolvedCartridgeCommand("/games/Game #1.sfc", {}, false, true, "snes9x", core, true);
    QCOMPARE(command.program, QString("snes9x"));
  }
  void archivePreflightRejectsMissingTargets() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto previousPath = qgetenv("PATH");
    const auto previousOutput = qgetenv("OMAKADE_LAUNCH_TEST_OUTPUT");
    const auto restore = qScopeGuard([&] {
      qputenv("PATH", previousPath);
      if (previousOutput.isNull()) qunsetenv("OMAKADE_LAUNCH_TEST_OUTPUT");
      else qputenv("OMAKADE_LAUNCH_TEST_OUTPUT", previousOutput);
    });
    qputenv("PATH", temp.path().toUtf8());
    const auto output = temp.filePath("arguments.txt");
    qputenv("OMAKADE_LAUNCH_TEST_OUTPUT", output.toUtf8());
    executable(temp.path(), "retroarch");
    QFile core(temp.filePath("core.so"));
    QVERIFY(core.open(QIODevice::WriteOnly)); core.close();
    QFile archive(temp.filePath("Set #1.zip"));
    QVERIFY(archive.open(QIODevice::WriteOnly)); archive.close();
    GameLauncher launcher;
    const QString entry = archive.fileName() + "#Game #2.sfc";
    QVERIFY(launcher.launch("RetroArch", "entry", false, {}, entry, core.fileName(), "snes"));
    QTRY_VERIFY(contents(output).contains(entry.toUtf8()));
    QVERIFY(QFile::remove(output));
    QVERIFY(!launcher.launch("RetroArch", "entry", false, {}, archive.fileName() + "#", core.fileName(), "snes"));
    QVERIFY(launcher.lastError().contains("installed files are missing"));
    QVERIFY(QDir().mkdir(temp.filePath("folder.zip")));
    QVERIFY(!launcher.launch("RetroArch", "entry", false, {}, temp.filePath("folder.zip#Game.sfc"), core.fileName(), "snes"));
    QVERIFY(launcher.lastError().contains("installed files are missing"));
    QVERIFY(!launcher.launch("RetroArch", "entry", false, {}, temp.path() + "#missing.sfc", core.fileName(), "snes"));
    QVERIFY(launcher.lastError().contains("installed files are missing"));
    // Archive syntax belongs to RetroArch, not arbitrary launcher installation paths.
    QVERIFY(!launcher.launch("Steam", "123", false, {}, entry));
    QVERIFY(launcher.lastError().contains("installed files are missing"));
    QVERIFY(!QFile::exists(output));
  }
  void missingConfiguredCoreIsReportedBeforeLaunch() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", temp.path().toUtf8());
    executable(temp.path(), "retroarch");
    executable(temp.path(), "snes9x");
    GameLauncher launcher;
    launcher.setPreferStandaloneEmulators(true);
    QString error;
    const QString missingCore = temp.filePath("missing_libretro.so");
    QVERIFY(!launcher.plannedCartridgeCommand("/games/Game.sfc", missingCore, false, "snes", &error).isValid());
    QVERIFY(error.contains(missingCore));
    QVERIFY(error.contains("Restore this core"));
    QVERIFY(!launcher.plannedCartridgeCommand("/games/Game.sfc", temp.path(), false, "snes", &error).isValid());
    QFile core(temp.filePath("custom_libretro.so"));
    QVERIFY(core.open(QIODevice::WriteOnly));
    core.close();
    const auto command = launcher.plannedCartridgeCommand("/games/Game.sfc", core.fileName(), false, "snes", &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(command.program, QString("retroarch"));
    QCOMPARE(command.arguments.at(2), core.fileName());
    QVERIFY(QFile::remove(temp.filePath("retroarch")));
    QVERIFY(!launcher.plannedCartridgeCommand("/games/Game.sfc", core.fileName(), false, "snes", &error).isValid());
    QCOMPARE(error, QString("RetroArch is not installed."));
    // With no pinned core, the installed standalone is a valid automatic choice.
    QCOMPARE(launcher.plannedCartridgeCommand("/games/Game.sfc", {}, false, "snes", &error).program, QString("snes9x"));
    QVERIFY(error.isEmpty());
  }
  void uiAndHeadlessLaunchUseTheDeclaredConsole() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto previousPath = qgetenv("PATH");
    const auto previousOutput = qgetenv("OMAKADE_LAUNCH_TEST_OUTPUT");
    const auto restore = qScopeGuard([&] {
      qputenv("PATH", previousPath);
      if (previousOutput.isNull()) qunsetenv("OMAKADE_LAUNCH_TEST_OUTPUT");
      else qputenv("OMAKADE_LAUNCH_TEST_OUTPUT", previousOutput);
    });
    qputenv("PATH", temp.path().toUtf8());
    const auto output = temp.filePath("arguments.txt");
    qputenv("OMAKADE_LAUNCH_TEST_OUTPUT", output.toUtf8());
    executable(temp.path(), "flycast");
    executable(temp.path(), "duckstation-qt");
    // Both emulators exist and the extension is shared. No real emulator is started.
    QFile rom(temp.filePath("Game & Friends.chd"));
    QVERIFY(rom.open(QIODevice::WriteOnly));
    rom.close();
    GameLauncher launcher;
    QString error;
    const QByteArray dreamcast = "flycast\n" + rom.fileName().toUtf8() + "\n";
    bool launched = false;
    QVERIFY(QMetaObject::invokeMethod(&launcher, "launch", Q_RETURN_ARG(bool, launched),
        Q_ARG(QString, QString("RetroArch")), Q_ARG(QString, QString("test-disc")),
        Q_ARG(bool, false), Q_ARG(QString, QString()), Q_ARG(QString, rom.fileName()),
        Q_ARG(QString, QString()), Q_ARG(QString, QString("dreamcast"))));
    QVERIFY(launched);
    QTRY_COMPARE(contents(output), dreamcast);
    QVERIFY(QFile::remove(output));
    CartridgeModel source;
    source.path = rom.fileName();
    source.system = "psx";
    UnifiedGameModel games(temp.filePath("library.sqlite3"));
    games.addSourceModel(&source);
    QVERIFY2(PlayRequest::perform(games, launcher, {"RetroArch", "", "test-disc"}, &error), qPrintable(error));
    QTRY_COMPARE(contents(output), "duckstation-qt\n" + rom.fileName().toUtf8() + "\n");
    QVERIFY(QFile::remove(output));
    QVERIFY(!launcher.launch("RetroArch", "test-disc", false, {}, rom.fileName(), {}));
    QVERIFY(launcher.lastError().contains("cannot determine"));
    QVERIFY(!QFile::exists(output));
  }
};
QTEST_GUILESS_MAIN(AutomaticLaunchTests)
#include "AutomaticLaunchTests.moc"

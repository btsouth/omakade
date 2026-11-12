#include "achievements/AchievementModel.h"
#include "achievements/XodusAchievementsService.h"
#include "achievements/RetroAchievementsService.h"
#include "achievements/SteamAccountService.h"
#include "app/AppHarness.h"
#include "app/CommandLine.h"
#include "app/OmakadeApp.h"
#include "app/RestoreStartup.h"
#include "app/AppSettings.h"
#include "app/CardExport.h"
#include "app/IdleInhibitor.h"
#include "app/SingleInstance.h"
#include "artwork/CoverImageProvider.h"
#include "backup/BackupManager.h"
#include "backup/BackupSnapshot.h"
#include "backup/BackupStartup.h"
#include "input/ControllerFocusGuard.h"
#include "input/ControllerInput.h"
#include "input/CouchCursorManager.h"
#include "launch/GameLauncher.h"
#include "launch/PlayRequest.h"
#include "launch/SteamLauncher.h"
#include "library/BattleNetGameModel.h"
#include "library/XodusGameModel.h"
#include "library/CemuGameModel.h"
#include "library/RommGameModel.h"
#include "saves/SaveProtection.h"
#include "library/LibraryRepair.h"
#include "library/ConsolePortalModel.h"
#include "library/DolphinGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/HeroicGameModel.h"
#include "library/HomeModel.h"
#include "library/PlayStats.h"
#include "library/UserDateFormat.h"
#include "library/LibraryFilterModel.h"
#include "library/GameStopService.h"
#include "library/LutrisGameModel.h"
#include "library/ManualGameModel.h"
#include "library/MelondsGameModel.h"
#include "library/MockGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/PpssppGameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/Rpcs3GameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/Shadps4GameModel.h"
#include "library/SteamGameModel.h"
#include "library/UnifiedGameModel.h"
#include "library/XeniaGameModel.h"
#include "library/LauncherGameModel.h"
#include "library/NativePortAdvisor.h"
#include "sources/kyty/KytyScanner.h"
#include "sources/ports/NativePortScanner.h"
#include "sources/ports/PortCatalog.h"
#include "metadata/GameInsightsService.h"
#include "metadata/GameMetadata.h"
#include "metadata/ProtonDbService.h"
#include <QQmlProperty>
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeOverlay.h"
#include "guide/GuidePlugin.h"
#include "guide/InGameGuide.h"
#include "guide/GuideClient.h"
#include "gamemode/GameModeSession.h"
#include "gamemode/LaunchHold.h"
#include "gamemode/LaunchWatch.h"
#include "gamemode/GameModeGuideButton.h"
#include "gamemode/GameModeShortcut.h"
#include "highlights/HighlightClips.h"
#include "highlights/ClipLibrary.h"
#include <QLinearGradient>
#include "streaming/SunshineIntegration.h"
#include "theme/OmarchyTheme.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/PrivateStorage.h"
#include "tracking/ProcFs.h"
#include "tracking/SessionDatabase.h"
#include "saves/SaveBackups.h"

#include <QAbstractItemModel>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QThread>
#include <QThreadPool>
#include <cstdio>
#include <QGuiApplication>
#include <QIcon>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLockFile>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <memory>

int runOmakade(int argc, char* argv[], AppHarness* harness) {
  QStringList rawArguments;
  for (int index = 0; index < argc; ++index) rawArguments.append(QString::fromLocal8Bit(argv[index]));
  const bool guideShortcut = rawArguments.contains("--game-mode-toggle") || rawArguments.contains("--guide-toggle");
  bool guideFallback = rawArguments.contains("--game-mode-fallback");
  // A controller's Home with no game running does what Start does in Omakade.
  bool fullscreenToggleRequest = rawArguments.contains("--fullscreen-toggle");
  if (guideShortcut && !guideFallback && !fullscreenToggleRequest) {
    // This path exits before loading a Qt platform, theme, library, models or QML.
    QCoreApplication commandApplication(argc, argv);
    const auto result = GuideClient::routeShortcut(optionValue(rawArguments, "--guide-device"));
    if (result == "handled" || result == "locked") return EXIT_SUCCESS;
    if (result == "fullscreen") {
      fullscreenToggleRequest = true;
    } else {
      // Missing/old/masked services retain the 1.15 Game Mode path.
      if (result != "fallback" && result != "unavailable" && result != "preparing") return EXIT_FAILURE;
      guideFallback = true;
    }
  }
  if (rawArguments.contains("--clip")) {
    // Saves a highlight from the running game through omakade-sessiond, with no window.
    QCoreApplication commandApplication(argc, argv);
    QEventLoop loop;
    QString result, reason;
    GuideClient::request({{"action", "clip"}}, &loop, [&](const QString& reply, const QJsonObject& data) {
      result = reply;
      reason = data.value("reason").toString();
      loop.quit();
    });
    loop.exec();
    if (result == "handled") return EXIT_SUCCESS;
    if (reason.isEmpty()) {
      // The service answers for itself when it runs; when it does not, say so here.
      reason = QStringLiteral("Omakade's background service is not running.");
      const QString sender = QStandardPaths::findExecutable(QStringLiteral("omarchy-notification-send"));
      if (!sender.isEmpty()) QProcess::startDetached(sender, {QStringLiteral("Clip not saved"), reason});
    }
    fprintf(stderr, "omakade: %s\n", qPrintable(reason));
    return EXIT_FAILURE;
  }
  QElapsedTimer startupTimer;
  startupTimer.start();

  QGuiApplication::setApplicationName(QStringLiteral("Omakade"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Omakade"));
  QGuiApplication::setApplicationVersion(QStringLiteral(OMAKADE_VERSION));
  QGuiApplication::setOrganizationName(QStringLiteral("Omakade"));
  QGuiApplication::setDesktopFileName(QStringLiteral("io.github.tsouth89.Omakade"));
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  QGuiApplication application(argc, argv);
  QIcon applicationIcon = QIcon::fromTheme(QStringLiteral("io.github.tsouth89.Omakade"));
  if (applicationIcon.isNull()) {
    applicationIcon =
        QIcon(QStringLiteral(":/icons/resources/icons/io.github.tsouth89.Omakade.svg"));
  }
  application.setWindowIcon(applicationIcon);

  OmarchyTheme theme;
  const auto applyThemeFont = [&application, &theme] {
    QFont font = application.font();
    font.setFamily(theme.fontFamily());
    application.setFont(font);
  };
  QObject::connect(&theme, &OmarchyTheme::themeChanged, &application, applyThemeFont);
  applyThemeFont();
  // omakade-uitest reads its own options here; the shipped binary has no harness.
  AppContext app;
  app.application = &application;
  app.theme = &theme;
  app.startupTimer = &startupTimer;
  if (harness) {
    if (auto exit = harness->start(app)) return *exit;
  }
  const AppHarnessModes& modes = app.modes;
  // `--play Source:runner:id` launches one library game, through the running window when
  // there is one, and `--quit` closes the running window. Sunshine app entries use both.
  const QString playKey = optionValue(application.arguments(), QStringLiteral("--play"));
  const bool quitRequest = application.arguments().contains(QStringLiteral("--quit"));
  // `--game-mode` starts Game Mode in the running window or in a new one. `--game-mode-exit`
  // leaves it, and with no window running it undoes what an interrupted session left changed.
  // `--game-mode-toggle` does whichever applies, which is what a key binding wants.
  const bool gameModeToggleRequest =
      guideFallback;
  bool gameModeRequest = application.arguments().contains(QStringLiteral("--game-mode"));
  bool gameModeExitRequest = application.arguments().contains(QStringLiteral("--game-mode-exit"));
  if (!quitRequest && optionSupplied(application.arguments(), QStringLiteral("--play")) &&
      playKey.isEmpty()) {
    qCritical() << "--play requires a launch key";
    return EXIT_FAILURE;
  }
  const bool renderMode = modes.renderMode;
  const bool gameModeTest = modes.gameModeCompositor != nullptr;
  const bool smokeTest = modes.selfTest || application.arguments().contains(QStringLiteral("--smoke-test"));
  const bool startupNavigationTest = modes.startupNavigationTest;
  const bool navigationTest = modes.navigationTest;
  const bool demoMode = smokeTest || renderMode || modes.demoLibrary ||
                        application.arguments().contains(QStringLiteral("--demo"));
  // The demo library, and the harness's checks that use it without being the demo. None of
  // them touch the user's library, services or saves.
  const bool syntheticLibrary = demoMode || modes.testLibrary;
  const bool isolatedTest = smokeTest || renderMode || modes.isolated;
  app.isolatedTest = &isolatedTest;
  if (isolatedTest) {
    // Test runs drive SDL's virtual pads, one posing as Steam Input's. Hide physical gamepads
    // so a controller plugged into a developer machine cannot take focus or change counts.
    qputenv("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT", "0xffff/0xffff,0x28de/0x11ff");
  }
  const bool reducedMotionRequest =
      application.arguments().contains(QStringLiteral("--reduced-motion"));
  const bool couchRequest = application.arguments().contains(QStringLiteral("--couch")) ||
                            SunshineIntegration::streaming() || fullscreenToggleRequest;
  if (quitRequest) {
    return SingleInstance::sendCommand({}, "quit") ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  const QString gameModeStatePath =
      QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)) +
      QStringLiteral("/omakade/game-mode.json");
  if (application.arguments().contains(QStringLiteral("--game-mode-return"))) {
    if (SingleInstance::sendCommand({}, "game-mode return")) return EXIT_SUCCESS;
    qCritical() << "No running Game Mode session to return to.";
    return EXIT_FAILURE;
  }
  if (application.arguments().contains(QStringLiteral("--game-mode-desktop"))) {
    if (SingleInstance::sendCommand({}, "game-mode desktop")) return EXIT_SUCCESS;
    qCritical() << "No running Game Mode session to return from.";
    return EXIT_FAILURE;
  }
  // With no window running, this falls through to a fullscreen start, as --couch does.
  if (fullscreenToggleRequest && SingleInstance::sendCommand({}, "fullscreen toggle")) {
    return EXIT_SUCCESS;
  }
  if (gameModeToggleRequest) {
    if (SingleInstance::sendCommand({}, "game-mode toggle game-mode-fallback")) {
      return EXIT_SUCCESS;
    }
    // With no window running, a record left by an interrupted session means Game Mode is
    // still on as far as the desktop goes, so the toggle puts the desktop back.
    gameModeExitRequest = QFile::exists(gameModeStatePath);
    gameModeRequest = !gameModeExitRequest;
  }
  if (gameModeExitRequest) {
    if (SingleInstance::sendCommand({}, "game-mode exit")) {
      return EXIT_SUCCESS;
    }
    HyprlandGameModeCompositor compositor;
    PactlGameModeAudio audio;
    OmarchyGameModeNotifications notifications;
    GameModeController recovery(&compositor, &audio, &notifications, gameModeStatePath);
    const GameModeController::Result result = recovery.recover();
    for (const QString& note : result.notes) {
      qWarning().noquote() << note;
    }
    if (!result.ok) {
      qCritical().noquote() << result.error;
    }
    return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  SingleInstance singleInstance;
  const QByteArray instanceCommand =
      !playKey.isEmpty()                 ? QByteArray("play ") + playKey.toUtf8()
      : gameModeRequest                  ? QByteArray("game-mode enter")
      : fullscreenToggleRequest          ? QByteArray("fullscreen toggle")
      : couchRequest                     ? QByteArray("activate stream")
                                         : QByteArray("activate");
  if (!isolatedTest && !singleInstance.claimOrNotify(instanceCommand)) {
    return EXIT_SUCCESS;
  }
  // Before any database or settings file is opened, and on every start so an older
  // installation's readable folders are tightened too.
  if (!isolatedTest) PrivateStorage::secureDefaults();
  const QString settingsPath =
      isolatedTest
          ? QDir::tempPath() +
                QStringLiteral("/omakade-test-%1.toml").arg(QCoreApplication::applicationPid())
          : QString{};
  // Recovery insists on canonical paths. Qt passes XDG_DATA_HOME and
  // XDG_CONFIG_HOME through untouched, so a trailing slash there must be
  // cleaned here rather than rejected at restore time.
  const QString dataRoot =
      QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
  const QString configRoot =
      QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
  const BackupRecovery::Paths backupPaths{dataRoot + "/omakade/library.sqlite3",
                                          configRoot + "/omakade/config.toml",
                                          dataRoot + "/omakade/restore-recovery"};
  // All synthetic libraries skip recovery entirely. The normal path owns the
  // SingleInstance claim above before any source database or service is opened.
  if (!syntheticLibrary &&
      !runRestoreStartup(application, theme, backupPaths, couchRequest)) return EXIT_FAILURE;
  AppSettings preferences(settingsPath);
  app.settingsPath = &settingsPath;
  app.preferences = &preferences;
  if (reducedMotionRequest) {
    preferences.setReducedMotion(true);
  } else if (!isolatedTest) {
    preferences.detectSystemReducedMotion();
  }
  if (!isolatedTest) preferences.detectSystemTextSize();
  const bool startInCouchMode = couchRequest || preferences.couchModeEnabled();
  ControllerInput controller;
  app.controller = &controller;
  std::unique_ptr<QAbstractItemModel> games;
  std::unique_ptr<LutrisGameModel> lutrisGames;
  std::unique_ptr<HeroicGameModel> heroicGames;
  std::unique_ptr<FaugusGameModel> faugusGames;
  std::unique_ptr<RetroArchGameModel> retroArchGames;
  std::unique_ptr<Pcsx2GameModel> pcsx2Games;
  std::unique_ptr<Rpcs3GameModel> rpcs3Games;
  std::unique_ptr<PpssppGameModel> ppssppGames;
  std::unique_ptr<RyujinxGameModel> ryujinxGames;
  std::unique_ptr<Shadps4GameModel> shadps4Games;
  std::unique_ptr<CemuGameModel> cemuGames;
  std::unique_ptr<MelondsGameModel> melondsGames;
  std::unique_ptr<RommGameModel> rommGames;
  std::unique_ptr<XeniaGameModel> xeniaGames;
  std::unique_ptr<LauncherGameModel> kytyGames;
  std::unique_ptr<LauncherGameModel> nativePortGames;
  std::unique_ptr<DolphinGameModel> dolphinGames;
  std::unique_ptr<BattleNetGameModel> battleNetGames;
  std::unique_ptr<XodusGameModel> xodusGames;
  std::unique_ptr<PlaySessionStore> playSessionStore;
  std::unique_ptr<ConsolePortalModel> consolePortals;
  SteamGameModel* steamLibrary = nullptr;
  LutrisGameModel* lutrisLibrary = nullptr;
  HeroicGameModel* heroicLibrary = nullptr;
  FaugusGameModel* faugusLibrary = nullptr;
  RetroArchGameModel* retroArchLibrary = nullptr;
  Pcsx2GameModel* pcsx2Library = nullptr;
  Rpcs3GameModel* rpcs3Library = nullptr;
  PpssppGameModel* ppssppLibrary = nullptr;
  RyujinxGameModel* ryujinxLibrary = nullptr;
  Shadps4GameModel* shadps4Library = nullptr;
  CemuGameModel* cemuLibrary = nullptr;
  MelondsGameModel* melondsLibrary = nullptr;
  XeniaGameModel* xeniaLibrary = nullptr;
  LauncherGameModel* kytyLibrary = nullptr;
  LauncherGameModel* nativePortLibrary = nullptr;
  DolphinGameModel* dolphinLibrary = nullptr;
  BattleNetGameModel* battleNetLibrary = nullptr;
  XodusGameModel* xodusLibrary = nullptr;
  app.kytyGames = &kytyGames;
  app.nativePortGames = &nativePortGames;
  app.kytyLibrary = &kytyLibrary;
  app.nativePortLibrary = &nativePortLibrary;
  app.retroArchGames = &retroArchGames;
  app.retroArchLibrary = &retroArchLibrary;
  app.consolePortals = &consolePortals;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::BeforeSources, app)) return *exit;
  }
  QString libraryDatabasePath;
  if (syntheticLibrary) {
    games = std::make_unique<MockGameModel>(nullptr, modes.demoGameCount,
                                            modes.demoFirstUninstalled, modes.demoStatsFixture);
  } else {
    auto steam = std::make_unique<SteamGameModel>(QString{}, &preferences);
    steamLibrary = steam.get();
    libraryDatabasePath = steamLibrary->databasePath();
    playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
    playSessionStore->setEnabled(preferences.trackPlaySessions());
    games = std::move(steam);
    lutrisGames = std::make_unique<LutrisGameModel>(steamLibrary->databasePath());
    lutrisLibrary = lutrisGames.get();
    heroicGames = std::make_unique<HeroicGameModel>(steamLibrary->databasePath());
    heroicLibrary = heroicGames.get();
    heroicLibrary->setGogLibraryPaths(preferences.gogLibraryPaths());
    QObject::connect(&preferences, &AppSettings::gogLibraryPathsChanged, heroicLibrary,
                     [&preferences, heroicLibrary] {
                       heroicLibrary->setGogLibraryPaths(preferences.gogLibraryPaths());
                       if (preferences.gogEnabled()) heroicLibrary->refresh();
                     });
    faugusGames = std::make_unique<FaugusGameModel>(steamLibrary->databasePath());
    faugusLibrary = faugusGames.get();
    retroArchGames = std::make_unique<RetroArchGameModel>(steamLibrary->databasePath(),
                                                          &preferences, playSessionStore.get());
    retroArchLibrary = retroArchGames.get();
    retroArchLibrary->setConfiguredRomFolders(preferences.romFolders());
    pcsx2Games =
        std::make_unique<Pcsx2GameModel>(steamLibrary->databasePath(), playSessionStore.get());
    pcsx2Library = pcsx2Games.get();
    rpcs3Games = std::make_unique<Rpcs3GameModel>(steamLibrary->databasePath(),
                                                  playSessionStore.get());
    rpcs3Library = rpcs3Games.get();
    rpcs3Library->setConfiguredRomFolders(preferences.romFolders());
    ppssppGames = std::make_unique<PpssppGameModel>(steamLibrary->databasePath(),
                                                    playSessionStore.get());
    ppssppLibrary = ppssppGames.get();
    ppssppLibrary->setConfiguredRomFolders(preferences.romFolders());
    ryujinxGames =
        std::make_unique<RyujinxGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    ryujinxLibrary = ryujinxGames.get();
    shadps4Games =
        std::make_unique<Shadps4GameModel>(steamLibrary->databasePath(), playSessionStore.get());
    shadps4Library = shadps4Games.get();
    cemuGames =
        std::make_unique<CemuGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    cemuLibrary = cemuGames.get();
    melondsGames = std::make_unique<MelondsGameModel>(steamLibrary->databasePath(),
                                                      playSessionStore.get());
    melondsLibrary = melondsGames.get();
    melondsLibrary->setConfiguredRomFolders(preferences.romFolders());
    rommGames = std::make_unique<RommGameModel>(QFileInfo(libraryDatabasePath).dir().filePath("romm-catalog.sqlite3"), &preferences, playSessionStore.get());
    xeniaGames =
        std::make_unique<XeniaGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    xeniaLibrary = xeniaGames.get();
    kytyGames = std::make_unique<LauncherGameModel>(
        LauncherGameModel::Definition{
            .source = QStringLiteral("Kyty"),
            .key = QStringLiteral("kyty"),
            .description = QStringLiteral("PlayStation 5 game launched through Kyty."),
            .scan = [] { return KytyScanner::scan(KytyScanner::defaultOptions()); }},
        steamLibrary->databasePath(), playSessionStore.get());
    kytyLibrary = kytyGames.get();
    nativePortGames = std::make_unique<LauncherGameModel>(
        LauncherGameModel::Definition{
            .source = QStringLiteral("Ports"),
            .key = QStringLiteral("native_port"),
            .description = QStringLiteral("Native PC port of a console game, run from your own dump."),
            .scan = [] {
              return NativePortScanner::scan(NativePortScanner::defaultOptions(),
                                             PortCatalog::bundled());
            }},
        steamLibrary->databasePath(), playSessionStore.get());
    nativePortLibrary = nativePortGames.get();
    dolphinGames =
        std::make_unique<DolphinGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    dolphinLibrary = dolphinGames.get();
    battleNetGames =
        std::make_unique<BattleNetGameModel>(steamLibrary->databasePath(), &preferences);
    battleNetLibrary = battleNetGames.get();
    xodusGames = std::make_unique<XodusGameModel>(steamLibrary->databasePath(), &preferences,
                                                  playSessionStore.get());
    xodusLibrary = xodusGames.get();
    consolePortals = std::make_unique<ConsolePortalModel>();
    consolePortals->addRomModel(retroArchGames.get());
    consolePortals->addRomModel(dolphinGames.get());
    consolePortals->addRomModel(ryujinxGames.get());
    consolePortals->addRomModel(cemuGames.get());
    consolePortals->addRomModel(melondsGames.get());
    consolePortals->addRomModel(rommGames.get());
    consolePortals->addRomModel(xeniaGames.get());
    consolePortals->addRomModel(pcsx2Games.get());
    consolePortals->addRomModel(rpcs3Games.get());
    consolePortals->addRomModel(ppssppGames.get());
    consolePortals->addRomModel(shadps4Games.get());
    consolePortals->addRomModel(kytyGames.get());
    consolePortals->addRomModel(nativePortGames.get());
  }
  if (consolePortals != nullptr) {
    consolePortals->setCardSystems(preferences.cardSystems());
    QObject::connect(&preferences, &AppSettings::consoleLayoutsChanged, consolePortals.get(),
                     [&preferences, portals = consolePortals.get()] {
                       portals->setCardSystems(preferences.cardSystems());
                     });
  }
  app.games = &games;
  app.heroicLibrary = &heroicLibrary;
  app.playSessionStore = &playSessionStore;
  app.libraryDatabasePath = &libraryDatabasePath;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::SourcesBuilt, app)) return *exit;
  }
  ManualGameModel manualGames(libraryDatabasePath.isEmpty() ? QStringLiteral(":memory:") : libraryDatabasePath);
  UnifiedGameModel unifiedGames(libraryDatabasePath);
  app.manualGames = &manualGames;
  app.unifiedGames = &unifiedGames;
  unifiedGames.addSourceModel(games.get());
  unifiedGames.addSourceModel(&manualGames);
  if (rommGames) unifiedGames.addSourceModel(rommGames.get());
  if (lutrisGames != nullptr) {
    unifiedGames.addSourceModel(lutrisGames.get());
  }
  if (heroicGames != nullptr) {
    unifiedGames.addSourceModel(heroicGames.get());
  }
  if (faugusGames != nullptr) {
    unifiedGames.addSourceModel(faugusGames.get());
  }
  if (retroArchGames != nullptr) {
    unifiedGames.addSourceModel(retroArchGames.get());
  }
  if (pcsx2Games != nullptr) {
    unifiedGames.addSourceModel(pcsx2Games.get());
  }
  if (rpcs3Games != nullptr) {
    unifiedGames.addSourceModel(rpcs3Games.get());
  }
  if (ppssppGames != nullptr) {
    unifiedGames.addSourceModel(ppssppGames.get());
  }
  if (ryujinxGames != nullptr) {
    unifiedGames.addSourceModel(ryujinxGames.get());
  }
  if (shadps4Games != nullptr) {
    unifiedGames.addSourceModel(shadps4Games.get());
  }
  if (cemuGames != nullptr) {
    unifiedGames.addSourceModel(cemuGames.get());
  }
  if (melondsGames != nullptr) {
    unifiedGames.addSourceModel(melondsGames.get());
  }
  if (xeniaGames != nullptr) {
    unifiedGames.addSourceModel(xeniaGames.get());
  }
  if (kytyGames != nullptr) {
    unifiedGames.addSourceModel(kytyGames.get());
  }
  if (nativePortGames != nullptr) {
    unifiedGames.addSourceModel(nativePortGames.get());
  }
  if (dolphinGames != nullptr) {
    unifiedGames.addSourceModel(dolphinGames.get());
  }
  if (xodusGames != nullptr) {
    unifiedGames.addSourceModel(xodusGames.get());
  }
  if (battleNetGames != nullptr) {
    unifiedGames.addSourceModel(battleNetGames.get());
  }
  if (consolePortals != nullptr) {
    unifiedGames.addSourceModel(consolePortals.get());
  }
  const auto applySourcePreferences = [&] {
    unifiedGames.setSourceEnabled(QStringLiteral("Steam"), preferences.steamEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Lutris"), preferences.lutrisEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Heroic"), preferences.heroicEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("GOG"), preferences.gogEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Faugus"), preferences.faugusEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RetroArch"), preferences.retroArchEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("PCSX2"), preferences.pcsx2Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RPCS3"), preferences.rpcs3Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("PPSSPP"), preferences.ppssppEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Ryujinx"), preferences.ryujinxEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("shadPS4"), preferences.shadps4Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Cemu"), preferences.cemuEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("melonDS"), preferences.melondsEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RomM"), preferences.rommEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Xenia"), preferences.xeniaEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Kyty"), preferences.kytyEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Ports"), preferences.nativePortsEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Dolphin"), preferences.dolphinEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Battle.net"), preferences.battleNetEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Xodus Nexus"), preferences.xodusEnabled());
  };
  applySourcePreferences();
  QObject::connect(&preferences, &AppSettings::sourcesChanged, &unifiedGames,
                   applySourcePreferences);
  if (!playKey.isEmpty()) {
    // No window is running, so launch without showing one. A Sunshine request can arrive before
    // a fresh process has finished rebuilding a missing or stale library cache, so retry after the
    // requested source's asynchronous refresh.
    SaveBackups headlessBackups;
    headlessBackups.setEnabled(preferences.protectRetroArchSaves());
    GameLauncher headlessLauncher;
    // This process exits once the game is out, and nothing would keep a log small then.
    headlessLauncher.setKeepLaunchLogs(false);
    headlessLauncher.setSaveBackups(&headlessBackups);
    headlessLauncher.setSetupDatabase(libraryDatabasePath);
    headlessLauncher.setRommLibraryRoot(preferences.rommLibraryRoot());
    unifiedGames.setLaunchSetups(headlessLauncher.setupOverrides());
    headlessLauncher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
    const LaunchKey key = LaunchKey::parse(playKey);
    QString error;
    if (key.source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0 &&
        preferences.pcsx2AutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("PCSX2"), true);
    } else if (key.source.compare(QStringLiteral("RPCS3"), Qt::CaseInsensitive) == 0 &&
               preferences.rpcs3AutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("RPCS3"), true);
    } else if (key.source.compare(QStringLiteral("PPSSPP"), Qt::CaseInsensitive) == 0 &&
               preferences.ppssppAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("PPSSPP"), true);
    } else if (key.source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0 &&
               preferences.ryujinxAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Ryujinx"), true);
    } else if (key.source.compare(QStringLiteral("shadPS4"), Qt::CaseInsensitive) == 0 &&
               preferences.shadps4AutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("shadPS4"), true);
    } else if (key.source.compare(QStringLiteral("Cemu"), Qt::CaseInsensitive) == 0 &&
               preferences.cemuAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Cemu"), true);
    } else if (key.source.compare(QStringLiteral("melonDS"), Qt::CaseInsensitive) == 0 &&
               preferences.melondsAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("melonDS"), true);
    } else if (key.source.compare(QStringLiteral("Xenia"), Qt::CaseInsensitive) == 0 &&
               preferences.xeniaAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Xenia"), true);
    } else if (key.source.compare(QStringLiteral("Dolphin"), Qt::CaseInsensitive) == 0 &&
               preferences.dolphinAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Dolphin"), true);
    } else if (key.source.compare(QStringLiteral("Kyty"), Qt::CaseInsensitive) == 0 &&
               preferences.kytyAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Kyty"), true);
    } else if (key.source.compare(QStringLiteral("Ports"), Qt::CaseInsensitive) == 0 &&
               preferences.nativePortsAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Ports"), true);
    }
    if (PlayRequest::findInstallation(unifiedGames, key, nullptr).isEmpty() && key.isValid()) {
      bool refreshStarted = false;
      if (key.source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0 &&
          steamLibrary != nullptr && preferences.steamEnabled()) {
        steamLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Lutris"), Qt::CaseInsensitive) == 0 &&
                 lutrisLibrary != nullptr && preferences.lutrisEnabled()) {
        lutrisLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Heroic"), Qt::CaseInsensitive) == 0 &&
                 heroicLibrary != nullptr && preferences.heroicEnabled()) {
        heroicLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("GOG"), Qt::CaseInsensitive) == 0 &&
                 heroicLibrary != nullptr && preferences.gogEnabled()) {
        heroicLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) == 0 &&
                 faugusLibrary != nullptr && preferences.faugusEnabled()) {
        faugusLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("RetroArch"), Qt::CaseInsensitive) == 0 &&
                 retroArchLibrary != nullptr && preferences.retroArchEnabled()) {
        retroArchLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0 &&
                 pcsx2Library != nullptr &&
                 (preferences.pcsx2Enabled() || preferences.pcsx2AutoEnabled())) {
        pcsx2Library->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("RPCS3"), Qt::CaseInsensitive) == 0 &&
                 rpcs3Library != nullptr &&
                 (preferences.rpcs3Enabled() || preferences.rpcs3AutoEnabled())) {
        rpcs3Library->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("PPSSPP"), Qt::CaseInsensitive) == 0 &&
                 ppssppLibrary != nullptr &&
                 (preferences.ppssppEnabled() || preferences.ppssppAutoEnabled())) {
        ppssppLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0 &&
                 ryujinxLibrary != nullptr &&
                 (preferences.ryujinxEnabled() || preferences.ryujinxAutoEnabled())) {
        ryujinxLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("shadPS4"), Qt::CaseInsensitive) == 0 &&
                 shadps4Library != nullptr &&
                 (preferences.shadps4Enabled() || preferences.shadps4AutoEnabled())) {
        shadps4Library->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Cemu"), Qt::CaseInsensitive) == 0 &&
                 cemuLibrary != nullptr &&
                 (preferences.cemuEnabled() || preferences.cemuAutoEnabled())) {
        cemuLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("melonDS"), Qt::CaseInsensitive) == 0 &&
                 melondsLibrary != nullptr &&
                 (preferences.melondsEnabled() || preferences.melondsAutoEnabled())) {
        melondsLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Xenia"), Qt::CaseInsensitive) == 0 &&
                 xeniaLibrary != nullptr &&
                 (preferences.xeniaEnabled() || preferences.xeniaAutoEnabled())) {
        xeniaLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Dolphin"), Qt::CaseInsensitive) == 0 &&
                 dolphinLibrary != nullptr &&
                 (preferences.dolphinEnabled() || preferences.dolphinAutoEnabled())) {
        dolphinLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Kyty"), Qt::CaseInsensitive) == 0 &&
                 kytyLibrary != nullptr &&
                 (preferences.kytyEnabled() || preferences.kytyAutoEnabled())) {
        kytyLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Ports"), Qt::CaseInsensitive) == 0 &&
                 nativePortLibrary != nullptr &&
                 (preferences.nativePortsEnabled() || preferences.nativePortsAutoEnabled())) {
        nativePortLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("RomM"), Qt::CaseInsensitive) == 0 && rommGames && preferences.rommEnabled()) {
        rommGames->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0 &&
                 battleNetLibrary != nullptr && preferences.battleNetEnabled()) {
        battleNetLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Xodus Nexus"), Qt::CaseInsensitive) == 0 &&
                 xodusLibrary != nullptr && preferences.xodusEnabled()) {
        xodusLibrary->refresh();
        refreshStarted = true;
      }
      if (refreshStarted) {
        PlayRequest::waitForInstallation(unifiedGames, key, 15000);
      }
    }
    if (!PlayRequest::perform(unifiedGames, headlessLauncher, key, &error)) {
      qCritical().noquote() << error;
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
  LibraryFilterModel library;
  library.setSourceModel(&unifiedGames);
  // Migrate legacy master-off to Games without disabling explicit system overrides.
  if (!preferences.consolePortalsEnabled()) {
    preferences.setExpandConsoles(true);
    preferences.setConsolePortalsEnabled(true);
  }
  library.setConsolePortalsEnabled(true);
  library.setCardSystems(preferences.cardSystems());
  library.setFixedCardSystems(preferences.fixedCardSystems());
  library.setConsoleExpandLimit(preferences.consoleExpandLimit());
  library.setExpandConsoles(preferences.expandConsoles());
  library.setSortMode(static_cast<LibraryFilterModel::SortMode>(preferences.librarySortMode()));
  QObject::connect(&preferences, &AppSettings::consoleLayoutsChanged, &library,
                   [&] {
                     library.setFixedCardSystems(preferences.fixedCardSystems());
                     library.setCardSystems(preferences.cardSystems());
                   });
  QObject::connect(&preferences, &AppSettings::consoleExpandLimitChanged, &library,
                   [&] { library.setConsoleExpandLimit(preferences.consoleExpandLimit()); });
  QObject::connect(&preferences, &AppSettings::expandConsolesChanged, &library,
                   [&] { library.setExpandConsoles(preferences.expandConsoles()); });
  QObject::connect(&library, &LibraryFilterModel::consoleNavigationChanged, &preferences,
                   [&] { preferences.setExpandConsoles(library.expandConsoles()); });
  QObject::connect(&preferences, &AppSettings::consolePortalsEnabledChanged, &library, [&] {
    library.setConsolePortalsEnabled(preferences.consolePortalsEnabled());
  });
  QObject::connect(&library, &LibraryFilterModel::sortModeChanged, &preferences, [&]() {
    preferences.setLibrarySortMode(static_cast<int>(library.sortMode()));
  });
  app.library = &library;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::LibraryFiltered, app)) return *exit;
  }
  QString achievementDatabasePath =
      steamLibrary == nullptr ? QStringLiteral(":memory:") : steamLibrary->databasePath();
  app.achievementDatabasePath = &achievementDatabasePath;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::Achievements, app)) return *exit;
  }
  AchievementModel achievements(achievementDatabasePath, &preferences);
  app.achievements = &achievements;
  ProtonDbService protonDb(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                          + QStringLiteral("/protondb/summaries.json"));
  protonDb.setEnabled(!syntheticLibrary && preferences.protonDbEnabled());
  QObject::connect(&preferences, &AppSettings::protonDbEnabledChanged, &protonDb,
                   [&] { protonDb.setEnabled(!syntheticLibrary && preferences.protonDbEnabled()); });
  std::unique_ptr<SteamAccountService> steamAccount;
  std::unique_ptr<GameInsightsService> gameInsights;
  std::unique_ptr<GameMetadata> gameMetadata;
  app.gameMetadata = &gameMetadata;
  std::unique_ptr<QTemporaryDir> demoMetadataDir;
  std::unique_ptr<RetroAchievementsService> retroAchievements;
  std::unique_ptr<XodusAchievementsService> xboxAchievements;
  if (steamLibrary != nullptr) {
    steamAccount =
        std::make_unique<SteamAccountService>(steamLibrary->databasePath(), &preferences);
    QObject::connect(steamAccount.get(), &SteamAccountService::achievementsUpdated, &achievements,
                     [&achievements](const QString& appId) { achievements.load(appId); });
    QObject::connect(steamAccount.get(), &SteamAccountService::achievementsUpdated, steamLibrary,
                     &SteamGameModel::reloadAchievementSummary);
    QObject::connect(steamAccount.get(), &SteamAccountService::ownedGamesUpdated, steamLibrary,
                     &SteamGameModel::reloadOwnedGames);
    gameInsights =
        std::make_unique<GameInsightsService>(steamLibrary->databasePath(), &preferences);
    gameMetadata = std::make_unique<GameMetadata>(steamLibrary->databasePath(), gameInsights.get());
    gameMetadata->setLibrary(&unifiedGames);
    // The filtered view decides what gets identified first, so opening a console fills that
    // console in rather than waiting for the rest of the library.
    gameMetadata->setVisibleLibrary(&library);
    gameMetadata->setCacheLimitMb(preferences.artworkCacheLimitMb());
    QObject::connect(&preferences, &AppSettings::artworkCacheLimitMbChanged, gameMetadata.get(), [&preferences, metadata = gameMetadata.get()] { metadata->setCacheLimitMb(preferences.artworkCacheLimitMb()); });
    gameMetadata->setAutomaticPass(preferences.metadataAutoIdentify());
    QObject::connect(&preferences, &AppSettings::metadataAutoIdentifyChanged, gameMetadata.get(), [&preferences, metadata = gameMetadata.get()] { metadata->setAutomaticPass(preferences.metadataAutoIdentify()); });
    unifiedGames.setMetadata(gameMetadata.get());
  } else if (syntheticLibrary) {
    // The rating and cover art section only exists when there is a metadata service behind it.
    // Without one it is invisible in every automated mode, so nothing could reach it and the
    // controller chain was free to route around it unnoticed, which is exactly what happened.
    // A throwaway database gives it something to bind to. With no IGDB or SteamGridDB
    // credentials the service stays inert and never reaches the network.
    demoMetadataDir = std::make_unique<QTemporaryDir>();
    if (demoMetadataDir->isValid()) {
      gameMetadata = std::make_unique<GameMetadata>(
          modes.metadataInLibraryDatabase
              ? libraryDatabasePath
              : demoMetadataDir->filePath(QStringLiteral("metadata.sqlite3")),
          nullptr);
      gameMetadata->setLibrary(&unifiedGames);
      unifiedGames.setMetadata(gameMetadata.get());
    }
  }
  if (retroArchLibrary != nullptr && steamLibrary != nullptr) {
    retroAchievements = std::make_unique<RetroAchievementsService>(steamLibrary->databasePath(),
                                                                    &preferences);
    QObject::connect(retroAchievements.get(), &RetroAchievementsService::achievementsUpdated,
                     &achievements,
                     [&achievements](const QString& gameId) { achievements.load(gameId); });
    QObject::connect(retroAchievements.get(), &RetroAchievementsService::achievementsUpdated,
                     retroArchLibrary, &RetroArchGameModel::reloadAchievementSummary);
    QObject::connect(retroAchievements.get(), &RetroAchievementsService::achievementsCleared,
                     retroArchLibrary, &RetroArchGameModel::clearAchievementSummaries);
    QObject::connect(retroAchievements.get(), &RetroAchievementsService::achievementsCleared,
                     &achievements,
                     [&achievements] { achievements.load(achievements.appId()); });
  }
  if (xodusLibrary != nullptr && steamLibrary != nullptr && !demoMode) {
    xboxAchievements = std::make_unique<XodusAchievementsService>(steamLibrary->databasePath());
    QObject::connect(xboxAchievements.get(), &XodusAchievementsService::achievementsUpdated,
                     &achievements, [&achievements](const QString& key) {
                       if (achievements.appId() == key) achievements.load(key);
                     });
    QObject::connect(xboxAchievements.get(), &XodusAchievementsService::achievementsUpdated,
                     xodusLibrary, &XodusGameModel::reloadAchievementSummary);
  }
  std::unique_ptr<SaveBackups> saveBackupsOwner;
  app.saveBackupsOwner = &saveBackupsOwner;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::SaveBackups, app)) return *exit;
  }
  if (saveBackupsOwner == nullptr) saveBackupsOwner = std::make_unique<SaveBackups>();
  SaveBackups& saveBackups = *saveBackupsOwner;
  app.saveBackups = &saveBackups;
  saveBackups.setEnabled(preferences.protectRetroArchSaves());
  QObject::connect(&preferences, &AppSettings::protectRetroArchSavesChanged, &saveBackups, [&] {
    saveBackups.setEnabled(preferences.protectRetroArchSaves());
  });
  GameLauncher launcher;
  app.launcher = &launcher;
  // MangoHud settings that games from an earlier run left behind once they closed, and logs
  // that something a game left running kept writing to after Omakade stopped following it.
  if (!isolatedTest) QThreadPool::globalInstance()->start([] {
    GameLauncher::sweepStaleMangoConfigs();
    GameLauncher::trimLaunchLogs();
  });
  launcher.setRommLibraryRoot(preferences.rommLibraryRoot());
  unifiedGames.setReviewPlanContext(preferences.rommLibraryRoot(),
                                   preferences.preferStandaloneEmulators());
  QObject::connect(&preferences, &AppSettings::rommConfigurationChanged, &launcher, [&] {
    launcher.setRommLibraryRoot(preferences.rommLibraryRoot());
    unifiedGames.setReviewPlanContext(preferences.rommLibraryRoot(),
                                      preferences.preferStandaloneEmulators());
  });
  launcher.setSetupDatabase(libraryDatabasePath.isEmpty() ? settingsPath + ".launch.sqlite3"
                                                          : libraryDatabasePath);
  unifiedGames.setLaunchInspector([&launcher](const QVariantMap& game) { return launcher.inspect(game); });
  unifiedGames.setLaunchSetupResolver([&launcher](const QVariantMap& installation) {
    return launcher.setupOverride(installation);
  });
  unifiedGames.setLaunchSetups(launcher.setupOverrides());
  QObject::connect(&launcher,&GameLauncher::setupChanged,&unifiedGames,[&] {unifiedGames.setLaunchSetups(launcher.setupOverrides());});
  SaveProtection saveProtection(&unifiedGames,&launcher,&saveBackups);
  LibraryRepair libraryRepair(&unifiedGames,gameMetadata.get(),settingsPath + ".review.ini");
  app.saveProtection = &saveProtection;
  app.libraryRepair = &libraryRepair;
  libraryRepair.setLauncher(&launcher);
  libraryRepair.setSaveBackups(&saveBackups);
  // Stopping a running game (issue #53). The rows come from the unified model, not the
  // filtered view, so a filter cannot hide a running game from the global action.
  GameStopService gameStop;
  app.gameStop = &gameStop;
  gameStop.setRowsProvider([&unifiedGames] {
    QVariantList rows;
    const int count = unifiedGames.rowCount();
    rows.reserve(count);
    for (int row = 0; row < count; ++row) {
      const QModelIndex index = unifiedGames.index(row, 0);
      QVariantMap game;
      game.insert(QStringLiteral("title"), unifiedGames.data(index, GameRoles::Title));
      game.insert(QStringLiteral("source"), unifiedGames.data(index, GameRoles::Source));
      game.insert(QStringLiteral("appId"), unifiedGames.data(index, GameRoles::AppId));
      game.insert(QStringLiteral("installPath"), unifiedGames.data(index, GameRoles::InstallPath));
      game.insert(QStringLiteral("runner"), unifiedGames.data(index, GameRoles::Runner));
      game.insert(QStringLiteral("flatpak"), unifiedGames.data(index, GameRoles::Flatpak));
      game.insert(QStringLiteral("launchTarget"), unifiedGames.data(index, GameRoles::LaunchTarget));
      rows.append(game);
    }
    return rows;
  });
  {
    QString profileError;
    gameStop.setProfiles(ProcessMatcher::load(ProcessMatcher::profilesPath(), &profileError));
    if (!profileError.isEmpty()) {
      qWarning() << "omakade: stopping a game could not read the session profiles:" << profileError;
    }
    // The processes that must never be signalled, on top of the built-in names.
    GameStop::Guards stopGuards = GameStop::defaultGuards();
    stopGuards.protectedPids.append(QCoreApplication::applicationPid());
    if (!libraryDatabasePath.isEmpty()) {
      QLockFile recorderLock(libraryDatabasePath + QStringLiteral(".sessiond.lock"));
      qint64 recorderPid = 0;
      QString recorderHost;
      QString recorderApplication;
      if (recorderLock.getLockInfo(&recorderPid, &recorderHost, &recorderApplication) &&
          recorderPid > 0) {
        stopGuards.protectedPids.append(recorderPid);
      }
    }
    gameStop.setGuards(stopGuards);
  }
  // A game the user stops did not close on its own, however soon after starting.
  QObject::connect(&gameStop, &GameStopService::stopping, &launcher, [&launcher](const QVariantList& games) {
    for (const QVariant& game : games)
      launcher.forgetEarlyExit(game.toMap().value(QStringLiteral("source")).toString(),
                               game.toMap().value(QStringLiteral("appId")).toString());
  });
  // A game that closed right after starting was not played, so it does not lead Home.
  QObject::connect(&launcher, &GameLauncher::gameClosedEarly, &unifiedGames,
                   [&unifiedGames](const QVariantMap& game) {
                     unifiedGames.forgetLaunch(game.value(QStringLiteral("source")).toString(),
                                               game.value(QStringLiteral("appId")).toString());
                   });
  if (!syntheticLibrary) launcher.setSaveBackups(&saveBackups);
  launcher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
  unifiedGames.setReviewPlanContext(preferences.rommLibraryRoot(),
                                    preferences.preferStandaloneEmulators());
  QObject::connect(&preferences, &AppSettings::preferStandaloneEmulatorsChanged, &launcher, [&] {
    launcher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
    unifiedGames.setReviewPlanContext(preferences.rommLibraryRoot(),
                                      preferences.preferStandaloneEmulators());
    unifiedGames.setLaunchSetups(launcher.setupOverrides());
  });
  if (retroArchLibrary != nullptr) {
    QObject::connect(&preferences, &AppSettings::romFoldersChanged, retroArchLibrary, [&] {
      retroArchLibrary->setConfiguredRomFolders(preferences.romFolders());
      if (preferences.retroArchEnabled()) {
        retroArchLibrary->refresh();
      }
    });
  }
  if (melondsLibrary != nullptr) {
    QObject::connect(&preferences, &AppSettings::romFoldersChanged, melondsLibrary, [&] {
      melondsLibrary->setConfiguredRomFolders(preferences.romFolders());
      if (preferences.melondsEnabled() || preferences.melondsAutoEnabled()) {
        melondsLibrary->refresh();
      }
    });
  }
  if (rpcs3Library != nullptr) {
    QObject::connect(&preferences, &AppSettings::romFoldersChanged, rpcs3Library, [&] {
      rpcs3Library->setConfiguredRomFolders(preferences.romFolders());
      if (preferences.rpcs3Enabled() || preferences.rpcs3AutoEnabled()) {
        rpcs3Library->refresh();
      }
    });
  }
  if (ppssppLibrary != nullptr) {
    QObject::connect(&preferences, &AppSettings::romFoldersChanged, ppssppLibrary, [&] {
      ppssppLibrary->setConfiguredRomFolders(preferences.romFolders());
      if (preferences.ppssppEnabled() || preferences.ppssppAutoEnabled()) {
        ppssppLibrary->refresh();
      }
    });
  }
  std::unique_ptr<SunshineIntegration> sunshine;
  if (steamLibrary != nullptr) {
    sunshine = std::make_unique<SunshineIntegration>(&unifiedGames, &preferences);
    sunshine->setIconSource(
        QStringLiteral(":/icons/resources/icons/io.github.tsouth89.Omakade.svg"));
  }

  auto managerPaths = backupPaths;
  app.managerPaths = &managerPaths;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::Backups, app)) return *exit;
  }
  BackupManager backups(managerPaths, &preferences, steamLibrary != nullptr || modes.backupFixture);
  HomeModel home(&unifiedGames, libraryDatabasePath);
  // The figures the Stats screen shows. It reads the same model the library does, so its
  // playtime can never disagree with what a game's card says, and it computes nothing until
  // the screen is open.
  PlayStats stats(&unifiedGames, libraryDatabasePath);
  app.backups = &backups;
  app.home = &home;
  app.stats = &stats;
  stats.setPeriod(preferences.statsPeriod());
  QObject::connect(&preferences, &AppSettings::statsPeriodChanged, &stats, [&] {
    stats.setPeriod(preferences.statsPeriod());
  });
  QObject::connect(&stats, &PlayStats::changed, &preferences, [&] {
    preferences.setStatsPeriod(stats.period());
  });
  // The card is written from C++ so the path is one place rather than composed in QML. The exit
  // hook is only wired for a one-shot export: saving a card from the screen must never end the
  // session, which is what an unconditional connection here would do to the first SAVE IMAGE press.
  CardExport cardExport;
  app.cardExport = &cardExport;
  if (!modes.cardExport) {
    qInfo() << "Card export: interactive, the app keeps running after a save";
  }
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::CardExport, app)) return *exit;
  }
  // Declared before the engine so the interface never outlives it. Test and render runs
  // get a Game Mode that cannot reach the desktop or the user's files.
  HyprlandGameModeCompositor gameModeCompositor;
  PactlGameModeAudio gameModeAudio;
  OmarchyGameModeNotifications gameModeNotifications;
  QTemporaryDir gameModeFixture;
  GameModeSession gameMode(
      gameModeTest   ? modes.gameModeCompositor
      : isolatedTest ? nullptr
                     : &gameModeCompositor,
      isolatedTest ? nullptr : &gameModeAudio, isolatedTest ? nullptr : &gameModeNotifications,
      isolatedTest ? gameModeFixture.filePath(QStringLiteral("game-mode.json"))
                   : configRoot + QStringLiteral("/omakade/game-mode.json"),
      isolatedTest ? gameModeFixture.filePath(QStringLiteral("game-mode-state.json"))
                   : gameModeStatePath);
  app.gameMode = &gameMode;
  // Test and render runs show the setting so navigation covers it, but never start Steam.
  // Without Steam the setting is hidden.
  if (isolatedTest) {
    gameMode.setSteamStarter([] { return GameModeSession::SteamStart::Running; });
  } else if (SteamLauncher::installed()) {
    gameMode.setSteamStarter([] {
      if (SteamLauncher::clientRunning()) return GameModeSession::SteamStart::Running;
      return SteamLauncher::startInBackground() ? GameModeSession::SteamStart::Started
                                                : GameModeSession::SteamStart::Failed;
    });
  }
  // Test runs get no hold: their compositors cannot keep windows.
  LaunchHold launchHold(gameModeTest   ? modes.gameModeCompositor
                        : isolatedTest ? nullptr
                                       : &gameModeCompositor,
                        GameModeController::workspace());
  app.launchHold = &launchHold;
  LaunchWatch launchWatch(gameModeTest || isolatedTest ? nullptr : &gameModeCompositor);
  // Omarchy keeps personal key bindings in this file and provides the `o.bind` helper the
  // Game Mode binding is written with.
  const QString omarchyPath = qEnvironmentVariable("OMARCHY_PATH");
  const bool onOmarchy =
      QFileInfo(omarchyPath.isEmpty() ? QStringLiteral("/usr/share/omarchy") : omarchyPath)
          .isDir() ||
      QFileInfo(QDir::homePath() + QStringLiteral("/.local/share/omarchy")).isDir();
  GameModeShortcut gameModeShortcut(
      isolatedTest ? QString{} : configRoot + QStringLiteral("/hypr/bindings.lua"), onOmarchy);
  GameModeShortcut clipShortcut(
      isolatedTest ? QString{} : configRoot + QStringLiteral("/hypr/bindings.lua"), onOmarchy, nullptr,
      GameModeShortcut::clip());
  GameModeGuideButton gameModeGuideButton(!isolatedTest);
  GameModeOverlay gameModeOverlay;
  GuideClient inGameGuide(!isolatedTest && onOmarchy);
  // While Home opens the guide, RetroArch launches leave that button to Omakade. Read at
  // launch: the Home button service is on when systemd has it linked into a target.
  launcher.setRetroArchHomeOwner([&preferences, onOmarchy, isolatedTest, configRoot] {
    if (isolatedTest || !onOmarchy || !preferences.homeButtonOpensGuide()) return false;
    const QString unit = QString::fromLatin1(GameModeGuideButton::kUnit);
    for (const QString& root : {configRoot + QStringLiteral("/systemd/user"), QStringLiteral("/etc/systemd/user")})
      for (const QString& wants : QDir(root).entryList({QStringLiteral("*.wants")}, QDir::Dirs))
        if (QFileInfo(root + QLatin1Char('/') + wants + QLatin1Char('/') + unit).isSymLink()) return true;
    return false;
  });
  // Publish launcher-owned games even when session recording is disabled. The resident
  // service retains exact process identities and resolves compositor data asynchronously.
  QTimer guideSnapshotTimer;
  guideSnapshotTimer.setInterval(1000);
  const auto publishGuideGames = [&inGameGuide, &launcher, &unifiedGames, &gameMode, &preferences] {
    auto sessions = launcher.trackedGames();
    for (auto& value : sessions) {
      auto session = value.toMap();
      QVariantMap metadata;
      // A hash lookup: this runs every second, so it must not walk the library or inspect
      // every installation.
      const int row = unifiedGames.rowForGame(session.value("source").toString(), session.value("appId").toString());
      if (row >= 0) {
        const auto index = unifiedGames.index(row);
        for (const int role : {GameRoles::Title, GameRoles::CoverPath, GameRoles::HeroPath, GameRoles::LogoPath, GameRoles::Tags})
          metadata.insert(QString::fromUtf8(GameRoles::names().value(role)), index.data(role));
        metadata.insert("appId", session.value("appId"));
      }
      session.insert("metadata", metadata); value = session;
    }
    bool couch = preferences.couchModeEnabled();
    for (auto* window : QGuiApplication::topLevelWindows())
      if (window->property("couchMode").isValid()) { couch = window->property("couchMode").toBool(); break; }
    inGameGuide.publish(sessions, gameMode.guideState(couch));
  };
  QObject::connect(&gameMode, &GameModeSession::stateChanged, &inGameGuide, publishGuideGames);
  QObject::connect(&guideSnapshotTimer, &QTimer::timeout, &inGameGuide, publishGuideGames);
  if (!isolatedTest && onOmarchy) {
    guideSnapshotTimer.start();
    GuideClient::ensureResident(&inGameGuide);
    QObject::connect(&launcher, &GameLauncher::gameRunningChanged, &inGameGuide, publishGuideGames);
  } else if (!isolatedTest) {
    // Off Omarchy the package no longer starts the recorder at login, so set it to start
    // there for this user once. It keeps playtime and Highlights recording after updates.
    GuideClient::enableResidentOnce(&inGameGuide);
  }
  QQmlApplicationEngine engine;
  app.engine = &engine;
  engine.rootContext()->setContextProperty("Home", &home);
  engine.rootContext()->setContextProperty("Stats", &stats);
  engine.rootContext()->setContextProperty("CardExport", &cardExport);
  const bool scrollTrace = qEnvironmentVariableIsSet("OMAKADE_SCROLL_TRACE");
  engine.rootContext()->setContextProperty("ScrollTraceEnabled", scrollTrace);
  if (scrollTrace) {
    auto* heartbeat = new QTimer(&application);
    heartbeat->setInterval(16);
    heartbeat->setTimerType(Qt::PreciseTimer);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    QObject::connect(heartbeat, &QTimer::timeout, &home, [elapsed, &home] {
      const auto gap = elapsed->restart();
      if (home.active() && gap > 50)
        qInfo() << "scroll-trace gui-gap-ms" << gap;
    });
    heartbeat->start();
  }
  // Cover art is decoded once and kept, so scrolling away and back, or changing a filter, does
  // not send every card to disk again. The engine takes ownership.
  engine.addImageProvider(QStringLiteral("covers"), new CoverImageProvider());
  engine.rootContext()->setContextProperty("Backups", &backups);
  engine.rootContext()->setContextProperty("SaveBackups", &saveBackups);
  engine.rootContext()->setContextProperty("GogSettingsFixture", false);
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
    for (const QQmlError& warning : warnings) {
      qWarning().noquote() << warning.toString();
    }
  });
  engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
  engine.rootContext()->setContextProperty(QStringLiteral("Library"), &library);
  engine.rootContext()->setContextProperty(QStringLiteral("ManualLibrary"), &manualGames);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamLibrary"), steamLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("LutrisLibrary"), lutrisLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("HeroicLibrary"), heroicLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("FaugusLibrary"), faugusLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RetroArchLibrary"), retroArchLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Pcsx2Library"), pcsx2Library);
  engine.rootContext()->setContextProperty(QStringLiteral("Rpcs3Library"), rpcs3Library);
  engine.rootContext()->setContextProperty(QStringLiteral("PpssppLibrary"), ppssppLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RyujinxLibrary"), ryujinxLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Shadps4Library"), shadps4Library);
  engine.rootContext()->setContextProperty(QStringLiteral("CemuLibrary"), cemuLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("MelondsLibrary"), melondsLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RommLibrary"), rommGames.get());
  engine.rootContext()->setContextProperty(QStringLiteral("XeniaLibrary"), xeniaLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("KytyLibrary"), kytyLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("NativePortLibrary"), nativePortLibrary);
  NativePortAdvisor nativePortAdvisor(PortCatalog::bundled(), nativePortLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("NativePorts"), &nativePortAdvisor);
  // After the library changes, a port found beside its dump joins it as one game.
  QTimer portLinks;
  portLinks.setSingleShot(true);
  portLinks.setInterval(1500);
  QObject::connect(&portLinks, &QTimer::timeout, &unifiedGames,
                   [&nativePortAdvisor, &unifiedGames] { nativePortAdvisor.linkInstalledPorts(&unifiedGames); });
  if (nativePortLibrary != nullptr) {
    QObject::connect(&unifiedGames, &QAbstractItemModel::modelReset, &portLinks,
                     qOverload<>(&QTimer::start));
    QObject::connect(&unifiedGames, &QAbstractItemModel::rowsInserted, &portLinks,
                     [&portLinks] { portLinks.start(); });
    portLinks.start();
    QObject::connect(nativePortLibrary, &LauncherGameModel::statusChanged, &portLinks,
                     qOverload<>(&QTimer::start));
  }
  engine.rootContext()->setContextProperty(QStringLiteral("DolphinLibrary"), dolphinLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("BattleNetLibrary"), battleNetLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("XodusLibrary"), xodusLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &launcher);
  engine.rootContext()->setContextProperty(QStringLiteral("SaveProtection"), &saveProtection);
  engine.rootContext()->setContextProperty(QStringLiteral("LibraryRepair"), &libraryRepair);
  engine.rootContext()->setContextProperty(QStringLiteral("GameStop"), &gameStop);
  engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::Sessions, app)) return *exit;
  }
  engine.rootContext()->setContextProperty(QStringLiteral("SessionRecorderStatus"), playSessionStore.get());
  // Highlight clips omakade-sessiond saved, read from the same library database.
  HighlightClips highlightClips(libraryDatabasePath);
  QObject::connect(&singleInstance, &SingleInstance::clipsChanged, &highlightClips, &HighlightClips::refresh);
  engine.rootContext()->setContextProperty(QStringLiteral("HighlightClips"), &highlightClips);
  app.highlightClips = &highlightClips;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::Clips, app)) return *exit;
  }
  engine.rootContext()->setContextProperty(QStringLiteral("Controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("Achievements"), &achievements);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamAccount"), steamAccount.get());
  engine.rootContext()->setContextProperty(QStringLiteral("RetroAchievements"),
                                           retroAchievements.get());
  engine.rootContext()->setContextProperty(QStringLiteral("XboxAchievements"),
                                           xboxAchievements.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Insights"), gameInsights.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Metadata"), gameMetadata.get());
  engine.rootContext()->setContextProperty(QStringLiteral("ProtonDB"), &protonDb);
  engine.rootContext()->setContextProperty(QStringLiteral("Sunshine"), sunshine.get());
  engine.rootContext()->setContextProperty(QStringLiteral("GameMode"), &gameMode);
  engine.rootContext()->setContextProperty(QStringLiteral("LaunchHold"), &launchHold);
  engine.rootContext()->setContextProperty(QStringLiteral("LaunchWatch"), &launchWatch);
  engine.rootContext()->setContextProperty(QStringLiteral("LaunchingPreview"), QVariantMap{});
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeOverlay"), &gameModeOverlay);
  engine.rootContext()->setContextProperty(QStringLiteral("InGameGuide"), &inGameGuide);
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeShortcut"), &gameModeShortcut);
  engine.rootContext()->setContextProperty(QStringLiteral("ClipShortcut"), &clipShortcut);
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeGuideButton"),
                                           &gameModeGuideButton);
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"), demoMode);
  engine.rootContext()->setContextProperty(QStringLiteral("StatsFixtureView"), 0);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());
  engine.rootContext()->setContextProperty(QStringLiteral("OwnedGameCountOverride"), QVariant(0));
  engine.rootContext()->setContextProperty(QStringLiteral("CouchModeRequested"),
                                           startInCouchMode);
  engine.rootContext()->setContextProperty(QStringLiteral("ColdGameModeRequested"),
                                           gameModeRequest);
  // Fullscreen starts an in-place Game Mode session. Test runs other than the Game Mode
  // fixture have no desktop to manage and keep the plain fullscreen layout.
  engine.rootContext()->setContextProperty(QStringLiteral("FullscreenStartsGameMode"),
                                           !isolatedTest || gameModeTest);
  engine.rootContext()->setContextProperty(QStringLiteral("CouchLibraryViewOverride"), QString{});

  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::BeforeLoad, app)) return *exit;
  }
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Omakade"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty()) {
    qCritical() << "Omakade failed to create its QML root object";
    return EXIT_FAILURE;
  }
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::Loaded, app)) return *exit;
  }

  auto* rootWindow = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
  app.rootWindow = &rootWindow;
  if (rootWindow != nullptr) {
    new ControllerFocusGuard(&controller, rootWindow);
    // Hold the compositor's idle timer while a launched game runs, since controller input alone
    // never resets it and most emulators do not inhibit for themselves.
    auto* idleInhibitor = new IdleInhibitor(rootWindow, rootWindow);
    // A configured Game Mode session holds it throughout: browsing with a controller must
    // not let the television blank either. An in-place one holds it only while a game
    // runs, so a handheld browsing its library can still sleep.
    const auto syncIdleInhibitor = [&launcher, &gameMode, idleInhibitor] {
      idleInhibitor->setInhibited(gameMode.keepsScreenAwake(launcher.gameRunning()));
    };
    QObject::connect(&launcher, &GameLauncher::gameRunningChanged, idleInhibitor,
                     syncIdleInhibitor);
    QObject::connect(&gameMode, &GameModeSession::stateChanged, idleInhibitor,
                     syncIdleInhibitor);
    auto* couchCursor = new CouchCursorManager(rootWindow, 1600, rootWindow);
    couchCursor->setObjectName(QStringLiteral("couchCursorManager"));
    QObject::connect(rootWindow, SIGNAL(couchModeChanged()), couchCursor,
                     SLOT(syncCouchMode()));
    for (auto signal : {&ControllerInput::acceptRequested, &ControllerInput::backRequested,
                        &ControllerInput::favoriteRequested, &ControllerInput::toolbarRequested,
                        &ControllerInput::startRequested}) {
      QObject::connect(&controller, signal, couchCursor, &CouchCursorManager::navigationActivity);
    }
    QObject::connect(&controller, &ControllerInput::focusDirectionRequested, couchCursor,
                     &CouchCursorManager::navigationActivity);
  }
  if (rootWindow != nullptr && startInCouchMode && !gameModeRequest && !renderMode && (!navigationTest || startupNavigationTest) && !smokeTest) {
    // Couch mode fills the chosen display. Sunshine selects its configured output first.
    const QList<QScreen*> screens = QGuiApplication::screens();
    QStringList screenNames;
    screenNames.reserve(screens.size());
    for (const QScreen* screen : screens) {
      screenNames.append(screen->name());
    }
    if (qEnvironmentVariableIsSet("SUNSHINE_APP_ID")) {
      const int screenIndex = SunshineIntegration::outputScreenIndex(
          SunshineIntegration::configuredOutputName(), screenNames);
      if (screenIndex >= 0) {
        rootWindow->setScreen(screens.at(screenIndex));
      }
    } else if (!isolatedTest) {
      // Fullscreen goes to the window's screen, which Qt sets to the primary one. Game
      // Mode starts in place, so open on the display that has focus instead.
      for (const auto& output : gameModeCompositor.outputs()) {
        const int screenIndex = output.focused ? screenNames.indexOf(output.name) : -1;
        if (screenIndex >= 0) rootWindow->setScreen(screens.at(screenIndex));
      }
    }
    rootWindow->showFullScreen();
  }
  if (rootWindow != nullptr && !gameModeRequest && !renderMode && (!navigationTest || startupNavigationTest)) {
    const auto activateWindow = [rootWindow, &gameMode] {
      if (gameMode.hasSession() || gameMode.busy()) return;
      rootWindow->requestActivate();
      QMetaObject::invokeMethod(rootWindow, "focusCurrentSurface");
    };
    QTimer::singleShot(0, rootWindow, activateWindow);
    QTimer::singleShot(160, rootWindow, activateWindow);
  }
  if (auto* quickWindow = qobject_cast<QQuickWindow*>(rootWindow)) {
    app.quickWindow = quickWindow;
    if (harness) {
      if (auto exit = harness->windowReady(app)) return *exit;
    }
    QObject::connect(
        quickWindow, &QQuickWindow::frameSwapped, &application,
        [&controller, &startupTimer, isolatedTest, startupNavigationTest] {
          qInfo() << "First frame in" << startupTimer.elapsed() << "ms";
          if (!isolatedTest || startupNavigationTest) {
            controller.start();
          }
        },
        Qt::SingleShotConnection);
  }
  QObject::connect(&singleInstance, &SingleInstance::activationRequested, &application,
                   [rootWindow, &gameMode](bool fullscreen) {
                     if (rootWindow == nullptr) {
                       return;
                     }
                     if (gameMode.parked()) {
                       gameMode.enter();
                       return;
                     }
                     if (fullscreen) {
                       if (!QMetaObject::invokeMethod(rootWindow, "activateCouchMode")) {
                         rootWindow->setProperty("couchMode", true);
                         rootWindow->showFullScreen();
                       }
                     } else {
                       rootWindow->show();
                     }
                     rootWindow->requestActivate();
                   });
  QObject::connect(&singleInstance, &SingleInstance::gameModeRequested, &gameMode,
                   [&gameMode](bool enter) {
                     if (enter) {
                       gameMode.enter();
                     } else {
                       gameMode.exit();
                     }
                   });
  QObject::connect(&singleInstance, &SingleInstance::gameModeDesktopRequested, &gameMode,
                   &GameModeSession::park);
  // Home after the guide's Desktop: resume. The guide service brings the game in front of
  // the library and reopens itself once it sees Game Mode active again.
  QObject::connect(&singleInstance, &SingleInstance::gameModeReturnRequested, &gameMode,
                   [&gameMode] { if (gameMode.parked()) gameMode.enter(); });
  QObject::connect(&singleInstance, &SingleInstance::gameModeToggleRequested, &gameMode,
                   [&gameMode, &inGameGuide, rootWindow](const QString& node) {
                     if (node != "game-mode-fallback") { inGameGuide.toggle(node, true); return; }
                     // The shortcut parks or resumes the complete library session.
                     if (rootWindow == nullptr ||
                         !QMetaObject::invokeMethod(rootWindow, "toggleGameMode")) {
                       gameMode.toggle();
                     }
                   });
  // Home on a controller with no game running: what Start does in this window. A parked
  // session comes back first, as any other activation brings it back. A press while
  // Game Mode starts, parks or resumes waits for that to finish, so it is not read as a
  // leave halfway through.
  const auto homeWaiting = std::make_shared<bool>(false);
  const auto homePress = [&gameMode, rootWindow, homeWaiting] {
    if (gameMode.busy()) {
      *homeWaiting = true;
      return;
    }
    if (gameMode.parked()) {
      gameMode.enter();
      return;
    }
    rootWindow->requestActivate();
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
  };
  QObject::connect(&singleInstance, &SingleInstance::fullscreenToggleRequested, &gameMode,
                   [rootWindow, homePress] {
                     if (rootWindow != nullptr) homePress();
                   });
  QObject::connect(&gameMode, &GameModeSession::stateChanged, &gameMode,
                   [&gameMode, homeWaiting, homePress] {
                     if (!*homeWaiting || gameMode.busy()) return;
                     *homeWaiting = false;
                     // After the finished change's own handlers have run.
                     QTimer::singleShot(0, &gameMode, homePress);
                   });
  // The shell was unreachable when the guide was requested: do what the shortcut did before the guide.
  QObject::connect(&inGameGuide, &GuideClient::summonFailed, &gameMode, [&gameMode, rootWindow] {
    if (rootWindow == nullptr || !QMetaObject::invokeMethod(rootWindow, "toggleGameMode")) gameMode.toggle();
  });
  QObject::connect(&singleInstance, &SingleInstance::guideToggleRequested, &inGameGuide,
                   [&inGameGuide](const QString& node) { inGameGuide.toggle(node, true); });
  gameMode.setTemporaryWindow(gameModeRequest);
  if (rootWindow != nullptr) {
    const auto windowStateBeforePreparation =
        std::make_shared<Qt::WindowState>(rootWindow->windowState());
    const auto openOutput = std::make_shared<QString>();
    QObject::connect(&gameMode, &GameModeSession::openOutputRequested, rootWindow,
                     [openOutput](const QString& output) { *openOutput = output; });
    QObject::connect(&gameMode, &GameModeSession::windowVisibilityRequested, rootWindow,
                     [rootWindow, openOutput](bool visible) {
                       // The controller snapshots desktop focus before asking to map
                       // a cold root. Request its native mode while it is still hidden.
                       QScreen* screen = nullptr;
                       if (visible && !rootWindow->isVisible() && !openOutput->isEmpty()) {
                         for (auto* candidate : QGuiApplication::screens())
                           if (candidate->name() == *openOutput) screen = candidate;
                         // The compositor's open rule matches this initial title.
                         rootWindow->setTitle(HyprlandGameModeCompositor::openTitle());
                         if (screen) rootWindow->setScreen(screen);
                       }
                       openOutput->clear();
                       if (visible) rootWindow->setWindowState(Qt::WindowFullScreen);
                       rootWindow->setVisible(visible);
                       // A hidden window does not keep a new size: showing it reapplies the
                       // last one, here the old tiled half. Set the display's size now, before
                       // the compositor's first configure, so the first frame is full size.
                       if (screen) rootWindow->setGeometry(screen->geometry());
                     });
    // Once mapped, the root keeps its ordinary title for the rest of the session.
    const auto restoreTitle = [rootWindow, title = rootWindow->title()] { rootWindow->setTitle(title); };
    QObject::connect(&gameMode, &GameModeSession::entered, rootWindow, restoreTitle);
    QObject::connect(&gameMode, &GameModeSession::resumed, rootWindow, restoreTitle);
    QObject::connect(&gameMode, &GameModeSession::failed, rootWindow, restoreTitle);
    QObject::connect(&gameMode, &GameModeSession::preparationCancelled, rootWindow, restoreTitle);
    QObject::connect(&gameMode, &GameModeSession::preparing, rootWindow,
                     [rootWindow, windowStateBeforePreparation](bool retainNavigation) {
                       *windowStateBeforePreparation = rootWindow->windowState();
                       QMetaObject::invokeMethod(rootWindow, "prepareGameModeLayout",
                                                 Q_ARG(QVariant, QVariant(retainNavigation)));
                     });
    QObject::connect(&gameMode, &GameModeSession::preparationCancelled, rootWindow,
                     [&gameMode, rootWindow, windowStateBeforePreparation] {
      QMetaObject::invokeMethod(rootWindow, "cancelGameModeLayout");
      // A failed cold entry is shown as an ordinary library by the failure
      // handler. A parked resume failure keeps its hidden fullscreen session.
      if (!gameMode.hasSession()) rootWindow->setWindowState(*windowStateBeforePreparation);
    });
    QObject::connect(&gameMode, &GameModeSession::resumed, rootWindow, [rootWindow] {
      QMetaObject::invokeMethod(rootWindow, "resumeGameMode");
    });
    QObject::connect(&gameMode, &GameModeSession::parking, rootWindow, [rootWindow] {
      QMetaObject::invokeMethod(rootWindow, "captureGameModeNavigation");
    });
    QObject::connect(&gameMode, &GameModeSession::parkedOnDesktop, rootWindow, [rootWindow] {
      QMetaObject::invokeMethod(rootWindow, "parkGameModeNavigation");
    });
    QObject::connect(&gameMode, &GameModeSession::exited, rootWindow,
                     [rootWindow] { QMetaObject::invokeMethod(rootWindow, "endGameMode"); });
    QObject::connect(&gameMode, &GameModeSession::entering, rootWindow, [rootWindow] {
      QMetaObject::invokeMethod(rootWindow, "captureGameModeDesktopMode");
    });
    QObject::connect(&gameMode, &GameModeSession::entered, rootWindow, [rootWindow] {
      QMetaObject::invokeMethod(rootWindow, "enterGameMode");
      rootWindow->setVisible(true);
      rootWindow->requestActivate();
    });
    QObject::connect(&gameMode, &GameModeSession::leaving, rootWindow, [rootWindow](bool retainNavigation) {
      QMetaObject::invokeMethod(rootWindow, "leaveGameMode",
                                Q_ARG(QVariant, QVariant(retainNavigation)));
    });
    QObject::connect(&gameMode, &GameModeSession::placeholderRequested, rootWindow,
                     [rootWindow](bool visible) {
                       rootWindow->setProperty("gameModePlaceholderVisible", visible);
                     });
    const auto toast = [rootWindow](const QString& message) {
      QMetaObject::invokeMethod(rootWindow, "showToast", Q_ARG(QVariant, message));
    };
    QObject::connect(&gameMode, &GameModeSession::failed, rootWindow, toast);
    QObject::connect(&gameMode, &GameModeSession::failed, rootWindow,
                     [&gameMode, rootWindow](const QString& message) {
                       if (!rootWindow->isActive()) {
                         auto notification = QDBusMessage::createMethodCall(
                             QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("Notify"));
                         notification.setArguments({QStringLiteral("Omakade"), uint(0),
                             QStringLiteral("io.github.tsouth89.Omakade"),
                             QStringLiteral("Game Mode"), message, QStringList{},
                             QVariantMap{}, 8000});
                         (void)QDBusConnection::sessionBus().asyncCall(notification, 2500);
                       }
                       if (!gameMode.hasSession()) rootWindow->setVisible(true);
                     });
    QObject::connect(&gameMode, &GameModeSession::notice, rootWindow, toast);
  }
  // Leaving puts the desktop back even when Omakade is closed from inside Game Mode.
  QObject::connect(&application, &QCoreApplication::aboutToQuit, &gameMode,
                   &GameModeSession::shutdown);
  // A new instance started only for Game Mode is temporary. An existing instance
  // receives the command above and keeps its window when the session ends.
  if (gameModeRequest) {
    // A parked session keeps IPC and recovery ownership while its window is hidden.
    application.setQuitOnLastWindowClosed(false);
    if (rootWindow != nullptr) {
      QObject::connect(rootWindow, &QWindow::visibleChanged, &application,
                       [&application, &gameMode](bool visible) {
                         if (!visible && !gameMode.hasSession() && !gameMode.busy())
                           application.quit();
                       });
    }
    QObject::connect(&gameMode, &GameModeSession::exited, &application,
                     &QCoreApplication::quit, Qt::QueuedConnection);
  }
  if (!isolatedTest) {
    // The first refresh also undoes a session that an earlier run left behind.
    QTimer::singleShot(0, &gameMode, &GameModeSession::refresh);
    if (gameModeRequest) {
      QTimer::singleShot(0, &gameMode, &GameModeSession::enter);
    } else if (startInCouchMode && rootWindow != nullptr) {
      // --couch, a Sunshine stream and a remembered fullscreen start are Game Mode on
      // the display the window opened on, never the configured one.
      QTimer::singleShot(0, &gameMode, &GameModeSession::enterInPlace);
    }
  }
  if (playSessionStore != nullptr) {
    QObject::connect(&preferences, &AppSettings::trackPlaySessionsChanged, playSessionStore.get(),
                     [&preferences, store = playSessionStore.get()] {
                       store->setEnabled(preferences.trackPlaySessions());
                     });
  }
  QObject* rootObject = engine.rootObjects().constFirst();
  QObject::connect(
      &singleInstance, &SingleInstance::trackingStorageFailed, &application, [rootObject] {
        QMetaObject::invokeMethod(
            rootObject, "showToast",
            Q_ARG(QVariant,
                  QStringLiteral("Playtime could not be saved. Check available storage.")));
      });
  QObject::connect(&singleInstance, &SingleInstance::journalProtectionDegraded, &application,
                   [rootObject] {
                     QMetaObject::invokeMethod(
                         rootObject, "showToast",
                         Q_ARG(QVariant,
                               QStringLiteral("Session recovery protection is degraded. Open "
                                              "Settings for the recorder status.")));
                   });
  QObject::connect(&singleInstance, &SingleInstance::playRequested, &application,
                   [&unifiedGames, &launcher, rootObject](const QString& key) {
                     QString error;
                     const bool okay = PlayRequest::perform(unifiedGames, launcher,
                                                            LaunchKey::parse(key), &error);
                     QMetaObject::invokeMethod(
                         rootObject, "showToast",
                         Q_ARG(QVariant, okay ? QStringLiteral("Launching from Sunshine") : error));
                   });
  QObject::connect(&singleInstance, &SingleInstance::rescanRequested, &application,
                   [&retroArchLibrary, &pcsx2Library, &rpcs3Library, &ppssppLibrary, &ryujinxLibrary,
                    &dolphinLibrary,
                    &melondsLibrary,
                    &preferences](const QString& source) {
                     // omakade-sessiond reports an emulator exit; some emulators only
                     // write their own playtime and last-played records on exit, so the
                     // owning source re-imports right away.
                     if (source == QStringLiteral("Ryujinx") && ryujinxLibrary != nullptr &&
                         preferences.ryujinxEnabled()) {
                       ryujinxLibrary->refresh();
                     } else if (source == QStringLiteral("PCSX2") && pcsx2Library != nullptr &&
                                preferences.pcsx2Enabled()) {
                       pcsx2Library->refresh();
                     } else if (source == QStringLiteral("RPCS3") && rpcs3Library != nullptr &&
                                preferences.rpcs3Enabled()) {
                       rpcs3Library->refresh();
                     } else if (source == QStringLiteral("PPSSPP") && ppssppLibrary != nullptr &&
                                preferences.ppssppEnabled()) {
                       ppssppLibrary->refresh();
                     } else if (source == QStringLiteral("RetroArch") &&
                                retroArchLibrary != nullptr && preferences.retroArchEnabled()) {
                       retroArchLibrary->refresh();
                     } else if (source == QStringLiteral("Dolphin") && dolphinLibrary != nullptr &&
                                preferences.dolphinEnabled()) {
                       dolphinLibrary->refresh();
                     } else if (source == QStringLiteral("melonDS") &&
                                melondsLibrary != nullptr && preferences.melondsEnabled()) {
                       melondsLibrary->refresh();
                     }
                   });
  // Quitting closes every window before aboutToQuit, and a session cannot put back a window
  // that is gone. End it first, as a Moonlight stream's `omakade --quit` expects.
  QObject::connect(&singleInstance, &SingleInstance::quitRequested, &application,
                   [&application, &gameMode] {
                     gameMode.shutdown();
                     application.quit();
                   });

  // Keep the empty library's loading state continuous across staggered source
  // starts. A gap between scans must not briefly claim that no games exist.
  const auto scheduleStartupScan = [rootWindow](int delay, auto* source, auto refresh) {
    if (rootWindow)
      rootWindow->setProperty("pendingStartupScans",
                             rootWindow->property("pendingStartupScans").toInt() + 1);
    QTimer::singleShot(delay, source, [rootWindow, source, refresh] {
      (source->*refresh)();
      if (rootWindow)
        rootWindow->setProperty("pendingStartupScans",
                               rootWindow->property("pendingStartupScans").toInt() - 1);
    });
  };
  if (steamLibrary != nullptr && preferences.steamEnabled()) {
    scheduleStartupScan(0, steamLibrary, &SteamGameModel::refresh);
  }
  if (lutrisLibrary != nullptr && preferences.lutrisEnabled()) {
    scheduleStartupScan(150, lutrisLibrary, &LutrisGameModel::refresh);
  }
  if (heroicLibrary != nullptr && (preferences.heroicEnabled() || preferences.gogEnabled())) {
    scheduleStartupScan(300, heroicLibrary, &HeroicGameModel::refresh);
  }
  if (faugusLibrary != nullptr && preferences.faugusEnabled()) {
    scheduleStartupScan(450, faugusLibrary, &FaugusGameModel::refresh);
  }
  // A demo library's RetroArch games are a harness fixture, scanned when it made them.
  if (retroArchLibrary != nullptr && preferences.retroArchEnabled() && !syntheticLibrary) {
    scheduleStartupScan(600, retroArchLibrary, &RetroArchGameModel::refresh);
  }
  // Sources start disabled and switch on once their emulator is detected, unless
  // the user wrote an explicit pcsx2_enabled/ryujinx_enabled key. Scans only run
  // while the source is enabled or still eligible for automatic detection.
  if (pcsx2Library != nullptr &&
      (preferences.pcsx2Enabled() || preferences.pcsx2AutoEnabled())) {
    scheduleStartupScan(650, pcsx2Library, &Pcsx2GameModel::refresh);
    QObject::connect(pcsx2Library, &Pcsx2GameModel::statusChanged, pcsx2Library,
                     [&preferences, pcsx2Library] {
                       if (pcsx2Library->pcsx2Detected() && preferences.pcsx2AutoEnabled()) {
                         preferences.setPcsx2AutoEnabled(false);
                         preferences.setPcsx2Enabled(true);
                       }
                     });
  }
  if (ryujinxLibrary != nullptr &&
      (preferences.ryujinxEnabled() || preferences.ryujinxAutoEnabled())) {
    scheduleStartupScan(700, ryujinxLibrary, &RyujinxGameModel::refresh);
    QObject::connect(ryujinxLibrary, &RyujinxGameModel::statusChanged, ryujinxLibrary,
                     [&preferences, ryujinxLibrary] {
                       if (ryujinxLibrary->ryujinxDetected() && preferences.ryujinxAutoEnabled()) {
                         preferences.setRyujinxAutoEnabled(false);
                         preferences.setRyujinxEnabled(true);
                       }
                     });
  }
  if (shadps4Library != nullptr &&
      (preferences.shadps4Enabled() || preferences.shadps4AutoEnabled())) {
    scheduleStartupScan(720, shadps4Library, &Shadps4GameModel::refresh);
    QObject::connect(shadps4Library, &Shadps4GameModel::statusChanged, shadps4Library,
                     [&preferences, shadps4Library] {
                       if (shadps4Library->shadps4Detected() && preferences.shadps4AutoEnabled()) {
                         preferences.setShadps4AutoEnabled(false);
                         preferences.setShadps4Enabled(true);
                       }
                     });
  }
  if (dolphinLibrary != nullptr &&
      (preferences.dolphinEnabled() || preferences.dolphinAutoEnabled())) {
    scheduleStartupScan(760, dolphinLibrary, &DolphinGameModel::refresh);
    QObject::connect(dolphinLibrary, &DolphinGameModel::statusChanged, dolphinLibrary,
                     [&preferences, dolphinLibrary] {
                       if (dolphinLibrary->dolphinDetected() && preferences.dolphinAutoEnabled()) {
                         preferences.setDolphinAutoEnabled(false);
                         preferences.setDolphinEnabled(true);
                       }
                     });
  }
  if (cemuLibrary != nullptr && (preferences.cemuEnabled() || preferences.cemuAutoEnabled())) {
    scheduleStartupScan(740, cemuLibrary, &CemuGameModel::refresh);
    QObject::connect(cemuLibrary, &CemuGameModel::statusChanged, cemuLibrary,
                     [&preferences, cemuLibrary] {
                       if (cemuLibrary->cemuDetected() && preferences.cemuAutoEnabled()) {
                         preferences.setCemuAutoEnabled(false);
                         preferences.setCemuEnabled(true);
                       }
    });
  }
  if (rpcs3Library != nullptr &&
      (preferences.rpcs3Enabled() || preferences.rpcs3AutoEnabled())) {
    scheduleStartupScan(675, rpcs3Library, &Rpcs3GameModel::refresh);
    QObject::connect(rpcs3Library, &Rpcs3GameModel::statusChanged, rpcs3Library,
                     [&preferences, rpcs3Library] {
                       if (rpcs3Library->rpcs3Detected() && preferences.rpcs3AutoEnabled()) {
                         preferences.setRpcs3AutoEnabled(false);
                         preferences.setRpcs3Enabled(true);
                       }
    });
  }
  if (ppssppLibrary != nullptr &&
      (preferences.ppssppEnabled() || preferences.ppssppAutoEnabled())) {
    scheduleStartupScan(690, ppssppLibrary, &PpssppGameModel::refresh);
    QObject::connect(ppssppLibrary, &PpssppGameModel::statusChanged, ppssppLibrary,
                     [&preferences, ppssppLibrary] {
                       if (ppssppLibrary->ppssppDetected() && preferences.ppssppAutoEnabled()) {
                         preferences.setPpssppAutoEnabled(false);
                         preferences.setPpssppEnabled(true);
                       }
                     });
  }
  if (melondsLibrary != nullptr &&
      (preferences.melondsEnabled() || preferences.melondsAutoEnabled())) {
    scheduleStartupScan(750, melondsLibrary, &MelondsGameModel::refresh);
    QObject::connect(melondsLibrary, &MelondsGameModel::statusChanged, melondsLibrary,
                     [&preferences, melondsLibrary] {
                       if (melondsLibrary->melondsDetected() &&
                           preferences.melondsAutoEnabled()) {
                         preferences.setMelondsAutoEnabled(false);
                         preferences.setMelondsEnabled(true);
                       }
                     });
  }
  if (xeniaLibrary != nullptr && (preferences.xeniaEnabled() || preferences.xeniaAutoEnabled())) {
    scheduleStartupScan(745, xeniaLibrary, &XeniaGameModel::refresh);
    QObject::connect(xeniaLibrary, &XeniaGameModel::statusChanged, xeniaLibrary,
                     [&preferences, xeniaLibrary] {
                       if (xeniaLibrary->xeniaDetected() && preferences.xeniaAutoEnabled()) {
                         preferences.setXeniaAutoEnabled(false);
                         preferences.setXeniaEnabled(true);
                       }
                     });
  }
  if (battleNetLibrary != nullptr && preferences.battleNetEnabled()) {
    scheduleStartupScan(750, battleNetLibrary, &BattleNetGameModel::refresh);
  }
  if (xodusLibrary != nullptr && preferences.xodusEnabled()) {
    scheduleStartupScan(755, xodusLibrary, &XodusGameModel::refresh);
  }
  if (kytyLibrary != nullptr && (preferences.kytyEnabled() || preferences.kytyAutoEnabled())) {
    scheduleStartupScan(760, kytyLibrary, &LauncherGameModel::refresh);
    QObject::connect(kytyLibrary, &LauncherGameModel::statusChanged, kytyLibrary,
                     [&preferences, kytyLibrary] {
                       if (kytyLibrary->detected() && preferences.kytyAutoEnabled()) {
                         preferences.setKytyAutoEnabled(false);
                         preferences.setKytyEnabled(true);
                       }
                     });
  }
  if (nativePortLibrary != nullptr &&
      (preferences.nativePortsEnabled() || preferences.nativePortsAutoEnabled())) {
    scheduleStartupScan(770, nativePortLibrary, &LauncherGameModel::refresh);
    QObject::connect(nativePortLibrary, &LauncherGameModel::statusChanged, nativePortLibrary,
                     [&preferences, nativePortLibrary] {
                       if (nativePortLibrary->detected() && preferences.nativePortsAutoEnabled()) {
                         preferences.setNativePortsAutoEnabled(false);
                         preferences.setNativePortsEnabled(true);
                       }
                     });
  }

  if (harness && harness->drive(app)) {
    // The harness's self-test ends the run.
  } else if (smokeTest && !renderMode) {
    QTimer::singleShot(600, &application, &QCoreApplication::quit);
  }

  const int result = application.exec();
  if (harness) return harness->finish(app, result);
  return result;
}

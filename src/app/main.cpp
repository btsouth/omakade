#include "achievements/AchievementModel.h"
#include "achievements/SteamAccountService.h"
#include "app/AppSettings.h"
#include "app/SingleInstance.h"
#include "input/ControllerInput.h"
#include "launch/GameLauncher.h"
#include "library/BattleNetGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/HeroicGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LutrisGameModel.h"
#include "library/MockGameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/SteamGameModel.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameInsightsService.h"
#include "theme/OmarchyTheme.h"

#include <QAbstractItemModel>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>

#include <memory>

namespace {
QString optionValue(const QStringList& arguments, const QString& name) {
  const QString prefix = name + QLatin1Char('=');
  for (qsizetype index = 0; index < arguments.size(); ++index) {
    const QString& argument = arguments.at(index);
    if (argument.startsWith(prefix)) {
      return argument.mid(prefix.size());
    }
    if (argument == name && index + 1 < arguments.size() &&
        !arguments.at(index + 1).startsWith(QStringLiteral("--"))) {
      return arguments.at(index + 1);
    }
  }
  return {};
}
} // namespace

int main(int argc, char* argv[]) {
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
  const QString screenshotPath =
      optionValue(application.arguments(), QStringLiteral("--render-screenshot"));
  const QString renderSize = optionValue(application.arguments(), QStringLiteral("--render-size"));
  const bool renderMode = !screenshotPath.isEmpty();
  const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
  const bool navigationTest =
      application.arguments().contains(QStringLiteral("--controller-navigation-test"));
  const bool ownedLayoutTest =
      application.arguments().contains(QStringLiteral("--owned-layout-test"));
  const bool uninstalledLayoutTest =
      application.arguments().contains(QStringLiteral("--uninstalled-layout-test"));
  const bool demoMode = smokeTest || renderMode ||
                        application.arguments().contains(QStringLiteral("--demo"));
  const bool benchmarkMode = application.arguments().contains(QStringLiteral("--benchmark"));
  const bool stressMode = application.arguments().contains(QStringLiteral("--stress-test"));
  if (benchmarkMode) {
    qInfo() << "Theme ready in" << startupTimer.elapsed() << "ms";
  }
  SingleInstance singleInstance;
  if (!smokeTest && !renderMode && !navigationTest && !singleInstance.claimOrNotify()) {
    return EXIT_SUCCESS;
  }
  const QString settingsPath =
      navigationTest || renderMode || smokeTest
          ? QDir::tempPath() +
                QStringLiteral("/omakade-test-%1.toml").arg(QCoreApplication::applicationPid())
          : QString{};
  AppSettings preferences(settingsPath);
  ControllerInput controller;
  std::unique_ptr<QAbstractItemModel> games;
  std::unique_ptr<LutrisGameModel> lutrisGames;
  std::unique_ptr<HeroicGameModel> heroicGames;
  std::unique_ptr<FaugusGameModel> faugusGames;
  std::unique_ptr<RetroArchGameModel> retroArchGames;
  std::unique_ptr<BattleNetGameModel> battleNetGames;
  SteamGameModel* steamLibrary = nullptr;
  LutrisGameModel* lutrisLibrary = nullptr;
  HeroicGameModel* heroicLibrary = nullptr;
  FaugusGameModel* faugusLibrary = nullptr;
  RetroArchGameModel* retroArchLibrary = nullptr;
  BattleNetGameModel* battleNetLibrary = nullptr;
  QString libraryDatabasePath;
  if (demoMode || stressMode || navigationTest) {
    games =
        std::make_unique<MockGameModel>(nullptr, stressMode ? 1000 : 100, uninstalledLayoutTest);
  } else {
    auto steam = std::make_unique<SteamGameModel>(QString{}, &preferences);
    steamLibrary = steam.get();
    libraryDatabasePath = steamLibrary->databasePath();
    games = std::move(steam);
    lutrisGames = std::make_unique<LutrisGameModel>(steamLibrary->databasePath());
    lutrisLibrary = lutrisGames.get();
    heroicGames = std::make_unique<HeroicGameModel>(steamLibrary->databasePath());
    heroicLibrary = heroicGames.get();
    faugusGames = std::make_unique<FaugusGameModel>(steamLibrary->databasePath());
    faugusLibrary = faugusGames.get();
    retroArchGames = std::make_unique<RetroArchGameModel>(steamLibrary->databasePath());
    retroArchLibrary = retroArchGames.get();
    battleNetGames =
        std::make_unique<BattleNetGameModel>(steamLibrary->databasePath(), &preferences);
    battleNetLibrary = battleNetGames.get();
  }
  UnifiedGameModel unifiedGames(libraryDatabasePath);
  unifiedGames.addSourceModel(games.get());
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
  if (battleNetGames != nullptr) {
    unifiedGames.addSourceModel(battleNetGames.get());
  }
  const auto applySourcePreferences = [&] {
    unifiedGames.setSourceEnabled(QStringLiteral("Steam"), preferences.steamEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Lutris"), preferences.lutrisEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Heroic"), preferences.heroicEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Faugus"), preferences.faugusEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RetroArch"), preferences.retroArchEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Battle.net"), preferences.battleNetEnabled());
  };
  applySourcePreferences();
  QObject::connect(&preferences, &AppSettings::sourcesChanged, &unifiedGames,
                   applySourcePreferences);
  LibraryFilterModel library;
  library.setSourceModel(&unifiedGames);
  if (uninstalledLayoutTest) {
    library.setAvailability(LibraryFilterModel::Availability::AllGames);
  }
  std::unique_ptr<QTemporaryDir> navigationData;
  QString achievementDatabasePath =
      steamLibrary == nullptr ? QStringLiteral(":memory:") : steamLibrary->databasePath();
  if (navigationTest) {
    navigationData = std::make_unique<QTemporaryDir>();
    achievementDatabasePath = navigationData->filePath(QStringLiteral("achievements.sqlite3"));
    const QString connectionName = QStringLiteral("omakade-navigation-fixture");
    {
      QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
      database.setDatabaseName(achievementDatabasePath);
      if (!database.open()) {
        qFatal("Could not open controller navigation fixture database: %s",
               qPrintable(database.lastError().text()));
      }
      QSqlQuery query(database);
      const auto execute = [&query](const QString& statement) {
        if (!query.exec(statement)) {
          qFatal("Could not prepare controller navigation fixture: %s",
                 qPrintable(query.lastError().text()));
        }
      };
      execute(QStringLiteral("CREATE TABLE achievement_summary (app_id TEXT PRIMARY KEY, "
                             "unlocked INTEGER, total INTEGER, source TEXT)"));
      execute(QStringLiteral(
          "CREATE TABLE achievements (app_id TEXT, api_name TEXT, title TEXT, description TEXT, "
          "icon_url TEXT, icon_path TEXT, unlocked INTEGER, unlock_time INTEGER, rarity REAL, "
          "hidden INTEGER, current_progress REAL, maximum_progress REAL)"));
      if (!query.exec(QStringLiteral(
              "INSERT INTO achievement_summary VALUES ('demo-0', 6, 12, 'steam-local')"))) {
        qFatal("Could not add controller achievement summary: %s",
               qPrintable(query.lastError().text()));
      }
      query.prepare(QStringLiteral(
          "INSERT INTO achievements VALUES ('demo-0', ?, ?, 'Controller navigation fixture', '', "
          "'', "
          "?, ?, ?, 0, 0, 0)"));
      for (int index = 0; index < 12; ++index) {
        query.bindValue(0, QStringLiteral("fixture-%1").arg(index));
        query.bindValue(1, QStringLiteral("Achievement %1").arg(index + 1));
        query.bindValue(2, index < 6 ? 1 : 0);
        query.bindValue(3, index < 6 ? 1700000000 + index : 0);
        query.bindValue(4, 10.0 + index);
        if (!query.exec()) {
          qFatal("Could not add controller achievement: %s",
                 qPrintable(query.lastError().text()));
        }
      }
      database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
  }
  AchievementModel achievements(achievementDatabasePath, &preferences);
  if (navigationTest) {
    achievements.load(QStringLiteral("demo-0"));
  }
  std::unique_ptr<SteamAccountService> steamAccount;
  std::unique_ptr<GameInsightsService> gameInsights;
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
  }
  GameLauncher launcher;

  QObject::connect(&controller, &ControllerInput::keyRequested, &application,
                   [&application](int key, int modifiers) {
                     QWindow* window = application.focusWindow();
                     if (window == nullptr) {
                       return;
                     }
                     const auto keyboardModifiers = static_cast<Qt::KeyboardModifiers>(modifiers);
                     QCoreApplication::postEvent(
                         window, new QKeyEvent(QEvent::KeyPress, key, keyboardModifiers));
                     QCoreApplication::postEvent(
                         window, new QKeyEvent(QEvent::KeyRelease, key, keyboardModifiers));
                   });

  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
    for (const QQmlError& warning : warnings) {
      qWarning().noquote() << warning.toString();
    }
  });
  engine.rootContext()->setContextProperty(QStringLiteral("Theme"), &theme);
  engine.rootContext()->setContextProperty(QStringLiteral("Library"), &library);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamLibrary"), steamLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("LutrisLibrary"), lutrisLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("HeroicLibrary"), heroicLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("FaugusLibrary"), faugusLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RetroArchLibrary"), retroArchLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("BattleNetLibrary"), battleNetLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &launcher);
  engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
  engine.rootContext()->setContextProperty(QStringLiteral("Controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("Achievements"), &achievements);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamAccount"), steamAccount.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Insights"), gameInsights.get());
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"),
                                           (demoMode || stressMode) && !ownedLayoutTest);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());
  engine.rootContext()->setContextProperty(QStringLiteral("OwnedGameCountOverride"),
                                           ownedLayoutTest ? 250 : 0);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Omakade"), QStringLiteral("Main"));
  if (benchmarkMode) {
    qInfo() << "QML loaded in" << startupTimer.elapsed() << "ms";
  }
  if (engine.rootObjects().isEmpty()) {
    qCritical() << "Omakade failed to create its QML root object";
    return EXIT_FAILURE;
  }
  if (uninstalledLayoutTest && !navigationTest) {
    QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "openGame",
                              Q_ARG(QVariant, QVariant(0)));
  }

  auto* rootWindow = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
  if (auto* quickWindow = qobject_cast<QQuickWindow*>(rootWindow)) {
    const QStringList dimensions = renderSize.split(QLatin1Char('x'));
    if ((renderMode || navigationTest) && dimensions.size() == 2) {
      quickWindow->resize(dimensions.at(0).toInt(), dimensions.at(1).toInt());
    }
    if (renderMode) {
      QTimer::singleShot(900, quickWindow, [quickWindow, screenshotPath, &application] {
        const QImage screenshot = quickWindow->grabWindow();
        if (screenshot.isNull() || !screenshot.save(screenshotPath)) {
          qCritical() << "Could not save screenshot to" << screenshotPath;
          application.exit(EXIT_FAILURE);
          return;
        }
        application.quit();
      });
    }
    QObject::connect(
        quickWindow, &QQuickWindow::frameSwapped, &application,
        [&application, &controller, &startupTimer, benchmarkMode] {
          qInfo() << "First frame in" << startupTimer.elapsed() << "ms";
          controller.start();
          if (benchmarkMode) {
            application.quit();
          }
        },
        Qt::SingleShotConnection);

    if (navigationTest) {
      QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller, ownedLayoutTest] {
        auto fail = [&application](const QString& message) {
          qCritical().noquote() << message;
          application.exit(EXIT_FAILURE);
        };
        auto* grid = quickWindow->findChild<QQuickItem*>(QStringLiteral("libraryGrid"));
        auto* search = quickWindow->findChild<QQuickItem*>(QStringLiteral("searchField"));
        if (grid == nullptr || search == nullptr) {
          fail(QStringLiteral("Controller navigation test could not find the library controls"));
          return;
        }
        grid->setProperty("currentIndex", 0);
        grid->forceActiveFocus();
        controller.keyRequested(Qt::Key_Up, Qt::NoModifier);
        QTimer::singleShot(
            50, quickWindow,
            [quickWindow, &application, &controller, grid, search, fail, ownedLayoutTest] {
              if (grid->hasActiveFocus() || search->hasActiveFocus()) {
                fail(
                    QStringLiteral("Controller Up did not move from the top row into the filters"));
                return;
              }
              // Down walks row by row through the controls and then into the grid.
              for (int step = 0; step < 6 && !grid->hasActiveFocus(); ++step) {
                controller.focusDirectionRequested(Qt::Key_Down);
              }
              if (!grid->hasActiveFocus() || grid->property("currentIndex").toInt() != 0) {
                fail(QStringLiteral("Controller Down did not return to the first library row"));
                return;
              }
              auto* sort = quickWindow->findChild<QQuickItem*>(QStringLiteral("sortButton"));
              auto* rescan = quickWindow->findChild<QQuickItem*>(QStringLiteral("rescanButton"));
              auto* settings =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsButton"));
              const bool narrow = quickWindow->width() < 1040;
              auto* allMode = quickWindow->findChild<QQuickItem*>(
                  narrow ? QStringLiteral("narrowAllModeButton") : QStringLiteral("allModeButton"));
              auto* hiddenMode = quickWindow->findChild<QQuickItem*>(
                  narrow ? QStringLiteral("narrowHiddenModeButton")
                         : QStringLiteral("hiddenModeButton"));
              auto* allSources =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("allSourcesButton"));
              auto* retroArchSource =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("retroArchSourceButton"));
              auto* statusFilter =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("statusFilterButton"));
              auto* installedAvailability = quickWindow->findChild<QQuickItem*>(
                  QStringLiteral("installedAvailabilityButton"));
              auto* readyAvailability =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("readyAvailabilityButton"));
              auto* tagFilter =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("tagFilterButton"));
              auto* settingsScroll =
                  quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
              if (sort == nullptr || rescan == nullptr || settings == nullptr ||
                  allMode == nullptr || hiddenMode == nullptr || allSources == nullptr ||
                  retroArchSource == nullptr || statusFilter == nullptr || tagFilter == nullptr ||
                  installedAvailability == nullptr || readyAvailability == nullptr ||
                  settingsScroll == nullptr) {
                fail(QStringLiteral("Controller navigation test could not find toolbar controls"));
                return;
              }
              controller.toolbarRequested();
              if (!sort->hasActiveFocus()) {
                fail(QStringLiteral("Controller Controls did not enter the library toolbar"));
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Left);
              if (narrow) {
                if (!retroArchSource->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Left did not reach source filters when tiled"));
                  return;
                }
                for (int step = 0; step < 6; ++step) {
                  controller.focusDirectionRequested(Qt::Key_Left);
                }
                controller.focusDirectionRequested(Qt::Key_Up);
                if (!allMode->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Up did not reach tiled library mode filters"));
                  return;
                }
                for (int step = 0; step < 3; ++step) {
                  controller.focusDirectionRequested(Qt::Key_Right);
                }
                if (!hiddenMode->hasActiveFocus()) {
                  fail(QStringLiteral("Controller could not traverse tiled library mode filters"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Down);
              } else {
                if (!hiddenMode->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Left did not reach library mode filters"));
                  return;
                }
                for (int step = 0; step < 3; ++step) {
                  controller.focusDirectionRequested(Qt::Key_Left);
                }
                if (!allMode->hasActiveFocus()) {
                  fail(QStringLiteral("Controller could not traverse library mode filters"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Down);
              }
              if (!retroArchSource->hasActiveFocus()) {
                fail(QStringLiteral("Controller Down did not reach source filters"));
                return;
              }
              for (int step = 0; step < 6; ++step) {
                controller.focusDirectionRequested(Qt::Key_Left);
              }
              if (!allSources->hasActiveFocus()) {
                fail(QStringLiteral("Controller could not traverse all source filters"));
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Down);
              if (ownedLayoutTest) {
                if (!installedAvailability->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Down did not reach availability filters"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Right);
                controller.focusDirectionRequested(Qt::Key_Right);
                if (!readyAvailability->hasActiveFocus()) {
                  fail(QStringLiteral("Controller could not traverse availability filters"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Down);
              }
              if (!statusFilter->hasActiveFocus()) {
                fail(QStringLiteral("Controller Down did not reach organization filters"));
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Right);
              controller.focusDirectionRequested(Qt::Key_Right);
              if (!tagFilter->hasActiveFocus()) {
                fail(QStringLiteral("Controller could not traverse organization filters"));
                return;
              }
              controller.toolbarRequested();
              controller.toolbarRequested();
              controller.focusDirectionRequested(Qt::Key_Right);
              if (!rescan->hasActiveFocus()) {
                fail(QStringLiteral("Controller Right did not reach Rescan"));
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Up);
              if (!settings->hasActiveFocus()) {
                controller.focusDirectionRequested(Qt::Key_Right);
              }
              if (!settings->hasActiveFocus()) {
                fail(QStringLiteral("Controller could not reach Settings from Rescan"));
                return;
              }
              controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
              QTimer::singleShot(
                  100, quickWindow,
                  [quickWindow, &application, &controller, grid, settingsScroll, fail] {
                    if (!quickWindow->property("diagnosticsOpen").toBool() ||
                        quickWindow->activeFocusItem() == nullptr) {
                      fail(QStringLiteral("Controller Open did not enter Settings"));
                      return;
                    }
                    QQuickItem* settingsStart = quickWindow->activeFocusItem();
                    for (int step = 0; step < 30; ++step) {
                      controller.focusDirectionRequested(Qt::Key_Down);
                    }
                    if (quickWindow->activeFocusItem() == settingsStart ||
                        settingsScroll->property("navigationContentY").toReal() <= 0) {
                      fail(QStringLiteral("Controller Down did not traverse and scroll Settings"));
                      return;
                    }
                    controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
                    QTimer::singleShot(50, quickWindow, [quickWindow, &application, &controller, grid, fail] {
                      if (quickWindow->property("diagnosticsOpen").toBool()) {
                        fail(QStringLiteral("Controller Back did not close Settings"));
                        return;
                      }
                      controller.toolbarRequested();
                      if (!grid->hasActiveFocus()) {
                        fail(QStringLiteral("Controller Controls did not return to the game grid"));
                        return;
                      }
                      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
                      QTimer::singleShot(100, quickWindow, [quickWindow, &application, &controller, fail] {
                        auto* play =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("playButton"));
                        if (!quickWindow->property("detailOpen").toBool() || play == nullptr ||
                            !play->hasActiveFocus()) {
                          fail(QStringLiteral("Game details did not focus Play"));
                          return;
                        }
                        controller.keyRequested(Qt::Key_Up, Qt::NoModifier);
                        QTimer::singleShot(
                            50, quickWindow,
                            [quickWindow, &application, &controller, play, fail] {
                              QQuickItem* movedUp = quickWindow->activeFocusItem();
                              if (movedUp == nullptr) {
                                fail(QStringLiteral("Keyboard Up cleared detail focus"));
                                return;
                              }
                              if (movedUp == play) {
                                fail(QStringLiteral(
                                    "Keyboard Up did not move focus on game details"));
                                return;
                              }
                              controller.keyRequested(Qt::Key_Down, Qt::NoModifier);
                              QTimer::singleShot(
                                  50, quickWindow,
                                  [quickWindow, &application, &controller, movedUp, fail] {
                                    QQuickItem* movedDown = quickWindow->activeFocusItem();
                                    if (movedDown == nullptr) {
                                      fail(QStringLiteral("Keyboard Down cleared detail focus"));
                                      return;
                                    }
                                    const QPointF down = movedDown->mapToScene(
                                        QPointF(movedDown->width() / 2, movedDown->height() / 2));
                                    if (movedDown == movedUp) {
                                      fail(QStringLiteral(
                                          "Keyboard Down did not move focus on game details"));
                                      return;
                                    }
                                    controller.keyRequested(Qt::Key_Right, Qt::NoModifier);
                                    QTimer::singleShot(
                                        50, quickWindow,
                                        [quickWindow, &application, &controller, down, fail] {
                                          QQuickItem* movedRight = quickWindow->activeFocusItem();
                                          if (movedRight == nullptr) {
                                            fail(QStringLiteral(
                                                "Keyboard Right cleared detail focus"));
                                            return;
                                          }
                                          const QPointF right = movedRight->mapToScene(QPointF(
                                              movedRight->width() / 2, movedRight->height() / 2));
                                          if (right.x() <= down.x() + 3) {
                                            fail(QStringLiteral("Keyboard Right did not move "
                                                                "right on game details"));
                                            return;
                                          }
                                          controller.keyRequested(Qt::Key_Left, Qt::NoModifier);
                                          QTimer::singleShot(
                                              50, quickWindow,
                                              [quickWindow, &application, &controller, right,
                                               fail] {
                                                QQuickItem* movedLeft =
                                                    quickWindow->activeFocusItem();
                                                if (movedLeft == nullptr) {
                                                  fail(QStringLiteral(
                                                      "Keyboard Left cleared detail focus"));
                                                  return;
                                                }
                                                const QPointF left = movedLeft->mapToScene(
                                                    QPointF(movedLeft->width() / 2,
                                                            movedLeft->height() / 2));
                                                if (left.x() >= right.x() - 3) {
                                                  fail(QStringLiteral(
                                                      "Keyboard Left did not move left on game "
                                                      "details"));
                                                  return;
                                                }
                                                auto* newCollection =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("newCollectionButton"));
                                                auto* insightsSection =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("insightsSection"));
                                                auto* insightRefresh =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("insightRefreshButton"));
                                                auto* achievementSection =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("achievementListSection"));
                                                auto* achievementSort =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("achievementSortButton"));
                                                auto* achievementRefresh =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("achievementRefreshButton"));
                                                auto* detailsScroll =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("detailsScroll"));
                                                if (newCollection == nullptr ||
                                                    insightsSection == nullptr ||
                                                    insightRefresh == nullptr ||
                                                    achievementSection == nullptr ||
                                                    achievementSort == nullptr ||
                                                    achievementRefresh == nullptr ||
                                                    detailsScroll == nullptr) {
                                                  fail(QStringLiteral(
                                                      "Controller navigation test could not find "
                                                      "detail sections"));
                                                  return;
                                                }
                                                insightsSection->setVisible(true);
                                                insightRefresh->setVisible(true);
                                                insightRefresh->setEnabled(true);
                                                achievementSection->setVisible(true);
                                                achievementSort->setVisible(true);
                                                achievementSort->setEnabled(true);
                                                achievementRefresh->setVisible(true);
                                                achievementRefresh->setEnabled(true);
                                                newCollection->forceActiveFocus();
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                if (!insightRefresh->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Down left the detail content "
                                                      "flow after collections"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                if (!achievementSort->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Down did not reach achievement "
                                                      "sorting"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                if (!achievementRefresh->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Down did not reach Steam "
                                                      "achievement refresh"));
                                                  return;
                                                }
                                                const qreal initialContentY =
                                                    detailsScroll->property("navigationContentY")
                                                        .toReal();
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                QQuickItem* firstAchievement =
                                                    quickWindow->activeFocusItem();
                                                if (firstAchievement == nullptr ||
                                                    firstAchievement->objectName() !=
                                                        QStringLiteral("achievementCard0")) {
                                                  fail(QStringLiteral(
                                                      "Controller Down did not enter the "
                                                      "achievement list"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                QQuickItem* nextAchievement =
                                                    quickWindow->activeFocusItem();
                                                if (nextAchievement == nullptr ||
                                                    nextAchievement == firstAchievement ||
                                                    !nextAchievement->objectName().startsWith(
                                                        QStringLiteral("achievementCard"))) {
                                                  fail(QStringLiteral(
                                                      "Controller Down did not traverse "
                                                      "achievement cards"));
                                                  return;
                                                }
                                                for (int step = 0; step < 4; ++step) {
                                                  controller.focusDirectionRequested(Qt::Key_Down);
                                                }
                                                if (detailsScroll->property("navigationContentY")
                                                        .toReal() <= initialContentY) {
                                                  fail(QStringLiteral(
                                                      "Controller achievement navigation did not "
                                                      "scroll details"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Up);
                                                if (quickWindow->activeFocusItem() == nullptr ||
                                                    !quickWindow->activeFocusItem()
                                                         ->objectName()
                                                         .startsWith(
                                                             QStringLiteral("achievementCard"))) {
                                                  fail(QStringLiteral(
                                                      "Controller Up did not reverse achievement "
                                                      "navigation"));
                                                  return;
                                                }
                                                controller.keyRequested(Qt::Key_Escape,
                                                                        Qt::NoModifier);
                                                QTimer::singleShot(
                                                    50, quickWindow,
                                                    [quickWindow, &application, &controller, fail] {
                                                      if (quickWindow->property("detailOpen")
                                                              .toBool()) {
                                                        fail(QStringLiteral(
                                                            "Controller Back did not close "
                                                            "game details"));
                                                        return;
                                                      }
                                                      controller.keyRequested(Qt::Key_F,
                                                                              Qt::ControlModifier);
                                                      QTimer::singleShot(
                                                          50, quickWindow,
                                                          [quickWindow, &application, &controller,
                                                           fail] {
                                                            auto* search =
                                                                quickWindow->findChild<QQuickItem*>(
                                                                    QStringLiteral("searchField"));
                                                            if (search == nullptr ||
                                                                !search->hasActiveFocus()) {
                                                              fail(QStringLiteral(
                                                                  "Keyboard Search did not "
                                                                  "focus the search field"));
                                                              return;
                                                            }
                                                            controller.keyRequested(Qt::Key_Escape,
                                                                                    Qt::NoModifier);
                                                            QTimer::singleShot(
                                                                50, quickWindow,
                                                                [quickWindow, &application,
                                                                 &controller, fail] {
                                                                  auto* grid =
                                                                      quickWindow
                                                                          ->findChild<QQuickItem*>(
                                                                              QStringLiteral(
                                                                                  "libraryGrid"));
                                                                  if (grid == nullptr ||
                                                                      !grid->hasActiveFocus()) {
                                                                    fail(QStringLiteral(
                                                                        "Keyboard Escape did "
                                                                        "not return to the "
                                                                        "library grid"));
                                                                    return;
                                                                  }
                                                                  controller.keyRequested(
                                                                      Qt::Key_F6, Qt::NoModifier);
                                                                  QTimer::singleShot(
                                                                      50, quickWindow,
                                                                      [quickWindow, &application,
                                                                       &controller, grid, fail] {
                                                                        auto* sort =
                                                                            quickWindow->findChild<
                                                                                QQuickItem*>(
                                                                                QStringLiteral(
                                                                                    "sortButton"));
                                                                        if (sort == nullptr ||
                                                                            !sort->hasActiveFocus()) {
                                                                          fail(QStringLiteral(
                                                                              "Keyboard F6 did "
                                                                              "not enter library "
                                                                              "controls"));
                                                                          return;
                                                                        }
                                                                        controller.keyRequested(
                                                                            Qt::Key_F6,
                                                                            Qt::NoModifier);
                                                                        QTimer::singleShot(
                                                                            50, quickWindow,
                                                                            [grid, &application,
                                                                             fail] {
                                                                              if (!grid->hasActiveFocus()) {
                                                                                fail(QStringLiteral(
                                                                                    "Keyboard F6 "
                                                                                    "did "
                                                                                    "not return to "
                                                                                    "the "
                                                                                    "library "
                                                                                    "grid"));
                                                                                return;
                                                                              }
                                                                              application.quit();
                                                                            });
                                                                      });
                                                                });
                                                          });
                                                    });
                                              });
                                        });
                                  });
                            });
                      });
                    });
                  });
            });
      });
    }
  }
  QObject::connect(&singleInstance, &SingleInstance::activationRequested, &application,
                   [rootWindow] {
                     if (rootWindow != nullptr) {
                       rootWindow->show();
                       rootWindow->requestActivate();
                     }
                   });

  if (steamLibrary != nullptr && preferences.steamEnabled()) {
    QTimer::singleShot(0, steamLibrary, &SteamGameModel::refresh);
  }
  if (lutrisLibrary != nullptr && preferences.lutrisEnabled()) {
    QTimer::singleShot(150, lutrisLibrary, &LutrisGameModel::refresh);
  }
  if (heroicLibrary != nullptr && preferences.heroicEnabled()) {
    QTimer::singleShot(300, heroicLibrary, &HeroicGameModel::refresh);
  }
  if (faugusLibrary != nullptr && preferences.faugusEnabled()) {
    QTimer::singleShot(450, faugusLibrary, &FaugusGameModel::refresh);
  }
  if (retroArchLibrary != nullptr && preferences.retroArchEnabled()) {
    QTimer::singleShot(600, retroArchLibrary, &RetroArchGameModel::refresh);
  }
  if (battleNetLibrary != nullptr && preferences.battleNetEnabled()) {
    QTimer::singleShot(750, battleNetLibrary, &BattleNetGameModel::refresh);
  }

  if (smokeTest && !renderMode) {
    QTimer::singleShot(600, &application, &QCoreApplication::quit);
  }

  return application.exec();
}

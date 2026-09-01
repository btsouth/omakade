#include "achievements/AchievementModel.h"
#include "achievements/SteamAccountService.h"
#include "app/AppSettings.h"
#include "app/SingleInstance.h"
#include "input/ControllerInput.h"
#include "launch/GameLauncher.h"
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
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include <memory>

namespace {
QString optionValue(const QStringList& arguments, const QString& name) {
  const QString prefix = name + QLatin1Char('=');
  for (const QString& argument : arguments) {
    if (argument.startsWith(prefix)) {
      return argument.mid(prefix.size());
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
  AppSettings preferences;
  ControllerInput controller;
  std::unique_ptr<QAbstractItemModel> games;
  std::unique_ptr<LutrisGameModel> lutrisGames;
  std::unique_ptr<HeroicGameModel> heroicGames;
  std::unique_ptr<FaugusGameModel> faugusGames;
  std::unique_ptr<RetroArchGameModel> retroArchGames;
  SteamGameModel* steamLibrary = nullptr;
  LutrisGameModel* lutrisLibrary = nullptr;
  HeroicGameModel* heroicLibrary = nullptr;
  FaugusGameModel* faugusLibrary = nullptr;
  RetroArchGameModel* retroArchLibrary = nullptr;
  QString libraryDatabasePath;
  if (demoMode || stressMode || navigationTest) {
    games = std::make_unique<MockGameModel>(nullptr, stressMode ? 1000 : 100);
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
  const auto applySourcePreferences = [&] {
    unifiedGames.setSourceEnabled(QStringLiteral("Steam"), preferences.steamEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Lutris"), preferences.lutrisEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Heroic"), preferences.heroicEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Faugus"), preferences.faugusEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RetroArch"), preferences.retroArchEnabled());
  };
  applySourcePreferences();
  QObject::connect(&preferences, &AppSettings::sourcesChanged, &unifiedGames,
                   applySourcePreferences);
  LibraryFilterModel library;
  library.setSourceModel(&unifiedGames);
  AchievementModel achievements(steamLibrary == nullptr ? QStringLiteral(":memory:")
                                                        : steamLibrary->databasePath(),
                                &preferences);
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
  engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &launcher);
  engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
  engine.rootContext()->setContextProperty(QStringLiteral("Controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("Achievements"), &achievements);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamAccount"), steamAccount.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Insights"), gameInsights.get());
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"), demoMode || stressMode);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());

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

  auto* rootWindow = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
  if (auto* quickWindow = qobject_cast<QQuickWindow*>(rootWindow)) {
    if (renderMode) {
      const QStringList dimensions = renderSize.split(QLatin1Char('x'));
      if (dimensions.size() == 2) {
        quickWindow->resize(dimensions.at(0).toInt(), dimensions.at(1).toInt());
      }
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
      QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
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
        QTimer::singleShot(50, quickWindow,
                           [quickWindow, &application, &controller, grid, search, fail] {
          if (!grid->hasActiveFocus() || search->hasActiveFocus()
              || grid->property("currentIndex").toInt() != 0) {
            fail(QStringLiteral("Controller Up left the first library row"));
            return;
          }
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
          QTimer::singleShot(100, quickWindow, [quickWindow, &application, &controller, fail] {
            auto* play = quickWindow->findChild<QQuickItem*>(QStringLiteral("playButton"));
            if (!quickWindow->property("detailOpen").toBool() || play == nullptr
                || !play->hasActiveFocus()) {
              fail(QStringLiteral("Game details did not focus Play"));
              return;
            }
            const QPointF initial = play->mapToScene(QPointF(play->width() / 2, play->height() / 2));
            controller.focusDirectionRequested(Qt::Key_Up);
            QTimer::singleShot(50, quickWindow,
                               [quickWindow, &application, &controller, initial, fail] {
              QQuickItem* movedUp = quickWindow->activeFocusItem();
              if (movedUp == nullptr) {
                fail(QStringLiteral("Controller Up cleared detail focus"));
                return;
              }
              const QPointF up = movedUp->mapToScene(
                  QPointF(movedUp->width() / 2, movedUp->height() / 2));
              if (up.y() >= initial.y() - 3) {
                fail(QStringLiteral("Controller Up did not move up on game details"));
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Down);
              QTimer::singleShot(50, quickWindow,
                                 [quickWindow, &application, &controller, up, fail] {
                QQuickItem* movedDown = quickWindow->activeFocusItem();
                if (movedDown == nullptr) {
                  fail(QStringLiteral("Controller Down cleared detail focus"));
                  return;
                }
                const QPointF down = movedDown->mapToScene(
                    QPointF(movedDown->width() / 2, movedDown->height() / 2));
                if (down.y() <= up.y() + 3) {
                  fail(QStringLiteral("Controller Down did not move down on game details"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Right);
                QTimer::singleShot(50, quickWindow,
                                   [quickWindow, &application, &controller, down, fail] {
                  QQuickItem* movedRight = quickWindow->activeFocusItem();
                  if (movedRight == nullptr) {
                    fail(QStringLiteral("Controller Right cleared detail focus"));
                    return;
                  }
                  const QPointF right = movedRight->mapToScene(
                      QPointF(movedRight->width() / 2, movedRight->height() / 2));
                  if (right.x() <= down.x() + 3) {
                    fail(QStringLiteral("Controller Right did not move right on game details"));
                    return;
                  }
                  controller.focusDirectionRequested(Qt::Key_Left);
                  QTimer::singleShot(
                      50, quickWindow, [quickWindow, &application, &controller, right, fail] {
                    QQuickItem* movedLeft = quickWindow->activeFocusItem();
                    if (movedLeft == nullptr) {
                      fail(QStringLiteral("Controller Left cleared detail focus"));
                      return;
                    }
                    const QPointF left = movedLeft->mapToScene(
                        QPointF(movedLeft->width() / 2, movedLeft->height() / 2));
                    if (left.x() >= right.x() - 3) {
                      fail(QStringLiteral("Controller Left did not move left on game details"));
                      return;
                    }
                    controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
                    QTimer::singleShot(50, quickWindow, [quickWindow, &application, fail] {
                      if (quickWindow->property("detailOpen").toBool()) {
                        fail(QStringLiteral("Controller Back did not close game details"));
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

  if (smokeTest && !renderMode) {
    QTimer::singleShot(600, &application, &QCoreApplication::quit);
  }

  return application.exec();
}

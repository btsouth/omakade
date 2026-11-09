#include "achievements/AchievementModel.h"
#include "achievements/XodusAchievementsService.h"
#include "achievements/RetroAchievementsService.h"
#include "achievements/SteamAccountService.h"
#include "app/AppHarness.h"
#include "app/CommandLine.h"
#include "app/OmakadeApp.h"
#include "app/RestoreStartup.h"
#include "app/SelfTestSupport.h"
#include "app/AppSettings.h"
#include "app/CardExport.h"
#include "app/CouchNavigationContract.h"
#include "app/KeyboardTestInput.h"
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
#include <QColor>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QPainter>
#include <QDir>
#include <QSet>
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
#include <QImage>
#include <QKeyEvent>
#include <QLockFile>
#include <QMouseEvent>
#include <functional>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSize>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QWheelEvent>

#include <algorithm>
#include <atomic>
#include <memory>

namespace {

void runEmptyFilterFocusTest(QQuickWindow* window, QGuiApplication* application) {
  const auto fail = [application](const QString& message) {
    qCritical().noquote() << message;
    application->exit(EXIT_FAILURE);
  };
  auto* library = qobject_cast<QAbstractItemModel*>(
      qmlContext(window)->contextProperty(QStringLiteral("Library")).value<QObject*>());
  if (library == nullptr) {
    fail(QStringLiteral("Navigation test could not find the library model"));
    return;
  }
  bool statusChanged = false;
  const bool invoked = QMetaObject::invokeMethod(
      library, "setCompletionStatus", Q_RETURN_ARG(bool, statusChanged), Q_ARG(int, 0),
      Q_ARG(QString, QStringLiteral("backlog")));
  library->setProperty("completionFilter", QStringLiteral("backlog"));
  if (!invoked || !statusChanged || library->rowCount() != 1) {
    fail(QStringLiteral("Navigation test could not prepare one filtered game"));
    return;
  }
  QMetaObject::invokeMethod(window, "openGame", Q_ARG(QVariant, QVariant(0)));
  QTimer::singleShot(80, window, [window, library, application, fail] {
    auto* details = window->findChild<QQuickItem*>(QStringLiteral("gameDetails"));
    if (details == nullptr || !window->property("detailOpen").toBool()) {
      fail(QStringLiteral("Navigation test could not open the filtered game"));
      return;
    }
    QMetaObject::invokeMethod(details, "completionStatusRequested",
                              Q_ARG(QString, QStringLiteral("completed")));
    QTimer::singleShot(100, window, [window, library, application, fail] {
      auto* emptyClear = window->findChild<QQuickItem*>(QStringLiteral("emptyClearButton"));
      if (library->rowCount() != 0 || emptyClear == nullptr || !emptyClear->isVisible() ||
          !emptyClear->hasActiveFocus()) {
        fail(QStringLiteral("Removing the last filtered game did not focus Clear Filters"));
        return;
      }
      // With a search on top of the status filter, the empty state names both, not only the search.
      auto* libraryView = window->findChild<QQuickItem*>(QStringLiteral("libraryView"));
      library->setProperty("searchText", QStringLiteral("zz-no-match"));
      const QString title = libraryView ? libraryView->property("emptyTitle").toString() : QString{};
      const QString message = libraryView ? libraryView->property("emptyMessage").toString() : QString{};
      if (title != QStringLiteral("No games match these filters") ||
          !message.contains(QStringLiteral("Search: zz-no-match")) ||
          !message.contains(QStringLiteral("Status: backlog"))) {
        fail(QStringLiteral("Combined filter empty state did not name every filter: %1 / %2")
                 .arg(title, message));
        return;
      }
      // Sources widen each other, so they are named as one filter, and the full emulator
      // set by its button.
      const auto messageFor = [library, libraryView](const QStringList& sources) {
        library->setProperty("sourceFilters", sources);
        return libraryView->property("emptyMessage").toString();
      };
      const QString twoSources = messageFor({QStringLiteral("Steam"), QStringLiteral("GOG")});
      const QString emulated = messageFor(library->property("emulatorSources").toStringList());
      library->setProperty("sourceFilters", QStringList{});
      library->setProperty("searchText", QString{});
      if (!twoSources.contains(QStringLiteral("Source: Steam or GOG")) ||
          twoSources.contains(QStringLiteral("Source: GOG")) ||
          !emulated.contains(QStringLiteral("Source: Emulated")) ||
          emulated.contains(QStringLiteral("RetroArch"))) {
        fail(QStringLiteral("Source filters were not named as one filter: %1 / %2")
                 .arg(twoSources, emulated));
        return;
      }
      library->setProperty("completionFilter", QString{});
      // A library that has games never asks to set up sources, even when this view is empty.
      if (window->property("librarySetupAvailable").toBool()) {
        fail(QStringLiteral("A library with games offered to set up sources"));
        return;
      }
      // Set Up Sources opens Settings on Sources, not on a source page left open before.
      QMetaObject::invokeMethod(window, "loadSettingsPanel");
      auto* settings = window->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
      if (settings == nullptr) {
        fail(QStringLiteral("Navigation test could not load Settings"));
        return;
      }
      settings->setProperty("section", 2);
      settings->setProperty("sourceDetail", QStringLiteral("Steam"));
      QMetaObject::invokeMethod(window, "openSourceSettings");
      if (!window->property("diagnosticsOpen").toBool() ||
          settings->property("section").toInt() != 0 ||
          !settings->property("sourceDetail").toString().isEmpty()) {
        fail(QStringLiteral("Set Up Sources did not open Settings on Sources"));
        return;
      }
      window->setProperty("diagnosticsOpen", false);
      application->quit();
    });
  });
}
// Opens the Super Nintendo portal in the desktop grid and checks that every card the
// user can see belongs to the console, that no card from the previous view is left
// behind, and that leaving the console removes the cartridges again. The proxy is
// covered by core tests; this guards the QML view, which recycles delegates.
void runConsolePortalTest(QQuickWindow* window, QGuiApplication* application) {
  const auto fail = [application](const QString& message) {
    qCritical().noquote() << message;
    application->exit(EXIT_FAILURE);
  };
  auto* library = qobject_cast<QAbstractItemModel*>(
      qmlContext(window)->contextProperty(QStringLiteral("Library")).value<QObject*>());
  auto* grid = window->findChild<QQuickItem*>(QStringLiteral("libraryGrid"));
  if (library == nullptr || grid == nullptr) {
    fail(QStringLiteral("Console portal test could not find the library grid"));
    return;
  }
  struct VisibleCard {
    QString title;
    QString source;
    QPointF position;
  };
  const auto visibleCards = [grid] {
    QVector<VisibleCard> cards;
    auto* content = grid->property("contentItem").value<QQuickItem*>();
    if (content == nullptr) {
      return cards;
    }
    const qreal top = grid->property("contentY").toReal();
    const qreal bottom = top + grid->height();
    for (QQuickItem* child : content->childItems()) {
      if (!child->property("appId").isValid() || !child->isVisible() || child->opacity() <= 0 ||
          child->y() + child->height() <= top || child->y() >= bottom) {
        continue;
      }
      cards.append({child->property("title").toString(), child->property("source").toString(),
                    child->position()});
    }
    return cards;
  };
  const auto portalRow = [library] {
    int row = -1;
    QMetaObject::invokeMethod(library, "indexOf", Q_RETURN_ARG(int, row),
                              Q_ARG(QString, QStringLiteral("RetroArch")), Q_ARG(QString, QString{}),
                              Q_ARG(QString, QStringLiteral("portal:snes")));
    return row;
  };
  const auto checkConsoleView = [visibleCards, library, grid, fail](const QString& phase) {
    const QVector<VisibleCard> cards = visibleCards();
    if (cards.isEmpty()) {
      qWarning() << "Grid diagnostics" << grid->property("count") << grid->property("contentY")
                 << grid->property("originY") << grid->property("contentHeight") << grid->height();
      auto* content = grid->property("contentItem").value<QQuickItem*>();
      if (content) for (auto* child : content->childItems())
        if (child->property("appId").isValid()) qWarning() << child->property("title") << child->y() << child->isVisible();
      fail(QStringLiteral("%1: no cards are visible inside the console").arg(phase));
      return false;
    }
    QSet<QString> positions;
    for (const VisibleCard& card : cards) {
      if (card.source != QStringLiteral("RetroArch") ||
          !card.title.startsWith(QStringLiteral("SNES Cart"))) {
        fail(QStringLiteral("%1: '%2' from %3 is visible under Super Nintendo")
                 .arg(phase, card.title, card.source));
        return false;
      }
      const QString key = QStringLiteral("%1,%2").arg(card.position.x()).arg(card.position.y());
      if (positions.contains(key)) {
        fail(QStringLiteral("%1: two cards overlap at %2 (stale delegate)").arg(phase, key));
        return false;
      }
      positions.insert(key);
    }
    if (library->property("consoleFilter").toString() != QStringLiteral("snes")) {
      fail(QStringLiteral("%1: the console filter is not set").arg(phase));
      return false;
    }
    return true;
  };
  const int firstPortal = portalRow();
  if (firstPortal < 0) {
    fail(QStringLiteral("Console portal test could not find the Super Nintendo portal"));
    return;
  }
  QElapsedTimer openTimer;
  openTimer.start();
  QMetaObject::invokeMethod(window, "openGame", Q_ARG(QVariant, QVariant(firstPortal)));
  const qint64 openMs = openTimer.elapsed();
  qInfo().noquote() << QStringLiteral("Opening the console took %1 ms").arg(openMs);
  QTimer::singleShot(400, window, [=] {
    if (openMs > 1000) {
      fail(QStringLiteral("Opening the console blocked the interface for %1 ms").arg(openMs));
      return;
    }
    if (!checkConsoleView(QStringLiteral("first open"))) {
      return;
    }
    QMetaObject::invokeMethod(window, "leaveConsole");
    QTimer::singleShot(400, window, [=] {
      for (const VisibleCard& card : visibleCards()) {
        if (card.title.startsWith(QStringLiteral("SNES Cart"))) {
          fail(QStringLiteral("after leaving: cartridge '%1' is still visible").arg(card.title));
          return;
        }
      }
      const int secondPortal = portalRow();
      if (secondPortal < 0) {
        fail(QStringLiteral("after leaving: the Super Nintendo portal is gone"));
        return;
      }
      QMetaObject::invokeMethod(window, "openGame", Q_ARG(QVariant, QVariant(secondPortal)));
      QTimer::singleShot(400, window, [=] {
        if (!checkConsoleView(QStringLiteral("second open"))) return;
        QMetaObject::invokeMethod(window, "leaveConsole");
        if (application->arguments().contains(QStringLiteral("--expand-scroll-test")))
          library->setProperty("expandConsoles", true);
        QTimer::singleShot(150, window, [=] {
          // Removing the earlier non-emulated rows moves GridView's content origin.
          QMetaObject::invokeMethod(grid, "positionViewAtEnd");
          library->setProperty("sourceFilters", LibraryFilterModel::emulatorSources());
          grid->setProperty("currentIndex", 0);
          // Layout changes cancel a wheel scroll, and under parallel test load the filtered
          // grid can still be laying out after a fixed delay. Wait until it holds still.
          auto stablePolls = std::make_shared<int>(0);
          auto settleAttempts = std::make_shared<int>(0);
          auto lastShape = std::make_shared<QList<qreal>>();
          auto settle = std::make_shared<std::function<void()>>();
          *settle = [=] {
            QMetaObject::invokeMethod(grid, "positionViewAtBeginning");
            const QList<qreal> shape = {grid->property("count").toReal(),
                                        grid->property("originY").toReal(),
                                        grid->property("contentY").toReal(),
                                        grid->property("contentHeight").toReal()};
            *stablePolls = shape == *lastShape ? *stablePolls + 1 : 0;
            *lastShape = shape;
            if (*stablePolls < 2 && ++*settleAttempts < 60) {
              QTimer::singleShot(50, window, *settle);
              return;
            }
            const qreal origin = grid->property("originY").toReal();
            qInfo() << "Filtered grid origin:" << origin;
            if ((qFuzzyIsNull(origin) && !library->property("expandConsoles").toBool()) || visibleCards().isEmpty()) {
              fail(QStringLiteral("Wheel regression fixture did not create a shifted visible grid"));
              return;
            }
            const QPointF point = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
            QWheelEvent wheel(point, window->mapToGlobal(point), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheel);
            // Poll for the scroll rather than guessing at a delay. The animated wheel can
            // take longer than a fixed wait when the suite runs tests in parallel.
            auto attempts = std::make_shared<int>(0);
            auto check = std::make_shared<std::function<void()>>();
            *check = [=] {
              const qreal y = grid->property("contentY").toReal();
              const qreal first = grid->property("originY").toReal();
              const qreal last = first + qMax(0.0, grid->property("contentHeight").toReal() - grid->height());
              if (visibleCards().isEmpty() || y < first - 1 || y > last + 1) {
                fail(QStringLiteral("Wheel after source filtering hid the cards: y=%1, bounds=%2..%3")
                         .arg(y).arg(first).arg(last));
                return;
              }
              // Wait for the wheel animation to land too, or it would overwrite the scrollbar
              // jump below.
              const qreal target = grid->property("wheelTargetY").toReal();
              if (last > first + 1 && (y <= first + 1 || qAbs(y - target) > 1)) {
                if (++*attempts < 60) {
                  QTimer::singleShot(50, window, *check);
                  return;
                }
                fail(QStringLiteral("Wheel did not scroll the expanded collection: y=%1, target=%2")
                         .arg(y).arg(target));
                return;
              }
              auto* track = window->findChild<QQuickItem*>(QStringLiteral("libraryScrollTrack"));
              if (last > first + 1 && track) {
                QMetaObject::invokeMethod(track, "scrollTo", Q_ARG(QVariant, track->height()));
                QTimer::singleShot(100, window, [=] {
                  if (visibleCards().isEmpty() || qAbs(grid->property("contentY").toReal() - last) > 1) {
                    fail(QStringLiteral("Scrollbar after filtering did not reach the visible last row"));
                    return;
                  }
                  application->quit();
                });
              } else {
                application->quit();
              }
            };
            QTimer::singleShot(250, window, *check);
          };
          QTimer::singleShot(150, window, *settle);
        });
      });
    });
  });
}

// The Details action grid's columns: Stop Game holds every cell to its scaled minimum.
int expectedDetailActionColumns(qreal width, qreal scale, bool running) {
  if (!running) return width < 300 ? 1 : width < 620 ? 2 : 4;
  const auto fits = [width, scale](int count) {
    return count * 220 * scale + (count - 1) * 10 <= width;
  };
  return fits(5) ? 5 : fits(3) ? 3 : fits(2) ? 2 : 1;
}

// Names the first visible action that reaches past the details column, or returns empty.
QString clippedDetailAction(QQuickItem* content, QQuickItem* actions) {
  const qreal right = content->mapToScene(QPointF(content->width(), 0)).x();
  for (QQuickItem* action : actions->childItems()) {
    if (!action->isVisible()) continue;
    if (action->mapToScene(QPointF(action->width(), 0)).x() > right + 0.5)
      return action->objectName();
  }
  return {};
}
} // namespace

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
  const bool detailsDirectionTest =
      application.arguments().contains(QStringLiteral("--details-direction-test"));
  const bool smokeTest = modes.selfTest || application.arguments().contains(QStringLiteral("--smoke-test"));
  const bool couchNavigationTest =
      application.arguments().contains(QStringLiteral("--couch-navigation-test"));
  const bool couchNavigationContract = application.arguments().contains(QStringLiteral("--couch-navigation-contract"));
  const bool couchNavigationSweep = application.arguments().contains(QStringLiteral("--couch-navigation-sweep"));
  const bool startupNavigationTest = application.arguments().contains(QStringLiteral("--startup-navigation-test"));
  const bool navigationTest = couchNavigationTest || couchNavigationContract || couchNavigationSweep || startupNavigationTest ||
                              application.arguments().contains(
                                  QStringLiteral("--controller-navigation-test"));
  const bool ownedLayoutTest =
      application.arguments().contains(QStringLiteral("--owned-layout-test"));
  const bool uninstalledLayoutTest =
      application.arguments().contains(QStringLiteral("--uninstalled-layout-test"));
  const bool consolePortalTest =
      application.arguments().contains(QStringLiteral("--console-portal-test"));
  const bool demoMode = smokeTest || renderMode || consolePortalTest ||
                        application.arguments().contains(QStringLiteral("--demo"));
  const bool benchmarkMode = application.arguments().contains(QStringLiteral("--benchmark"));
  const QString benchmarkMaxOption = QStringLiteral("--benchmark-max-ms");
  const bool benchmarkLimitSupplied = optionSupplied(application.arguments(), benchmarkMaxOption);
  bool benchmarkLimitValid = false;
  const int benchmarkMaxMs =
      optionValue(application.arguments(), benchmarkMaxOption).toInt(&benchmarkLimitValid);
  if (benchmarkLimitSupplied && (!benchmarkLimitValid || benchmarkMaxMs <= 0)) {
    qCritical() << "--benchmark-max-ms requires a positive integer";
    return EXIT_FAILURE;
  }
  const QString stressCountOption = QStringLiteral("--stress-count");
  const bool stressCountSupplied = optionSupplied(application.arguments(), stressCountOption);
  bool stressCountValid = false;
  const int requestedStressCount =
      optionValue(application.arguments(), stressCountOption).toInt(&stressCountValid);
  if (stressCountSupplied &&
      (!stressCountValid || requestedStressCount < 1 || requestedStressCount > 100000)) {
    qCritical() << "--stress-count requires an integer between 1 and 100000";
    return EXIT_FAILURE;
  }
  const bool stressMode = application.arguments().contains(QStringLiteral("--stress-test"));
  const int stressGameCount = stressCountSupplied ? requestedStressCount : 1000;
  const bool isolatedTest = smokeTest || renderMode || navigationTest || detailsDirectionTest ||
                            consolePortalTest || benchmarkMode || stressMode;
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
  if (benchmarkMode) {
    qInfo() << "Theme ready in" << startupTimer.elapsed() << "ms";
  }
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
  if (!demoMode && !stressMode && !navigationTest && !detailsDirectionTest &&
      !runRestoreStartup(application, theme, backupPaths, couchRequest)) return EXIT_FAILURE;
  AppSettings preferences(settingsPath);
  app.settingsPath = &settingsPath;
  app.preferences = &preferences;
  if (reducedMotionRequest) {
    preferences.setReducedMotion(true);
  } else if (!isolatedTest) {
    preferences.detectSystemReducedMotion();
  }
  if (!isolatedTest) {
    preferences.detectSystemTextSize();
  } else if (optionSupplied(application.arguments(), QStringLiteral("--text-size"))) {
    // Renders and UI tests check layouts at a chosen Settings > Appearance text size.
    const int textSize = optionValue(application.arguments(), QStringLiteral("--text-size")).toInt();
    if (!AppSettings::textSizes().contains(textSize)) {
      qCritical() << "--text-size requires one of" << AppSettings::textSizes();
      return EXIT_FAILURE;
    }
    preferences.setTextSizePreference(textSize);
  }
  if (startupNavigationTest && application.arguments().contains("--startup-grid")) {
    preferences.setCouchLibraryView(QStringLiteral("grid"));
  }
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
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::BeforeSources, app)) return *exit;
  }
  QString libraryDatabasePath;
  std::unique_ptr<QTemporaryDir> consoleFixture;
  if (demoMode || stressMode || navigationTest || detailsDirectionTest) {
    games =
        std::make_unique<MockGameModel>(nullptr, stressMode ? stressGameCount : 100,
                                        uninstalledLayoutTest, modes.demoStatsFixture || couchNavigationContract || couchNavigationSweep);
    if (consolePortalTest) {
      // A few hundred cartridges behind one portal, next to the demo library.
      consoleFixture = std::make_unique<QTemporaryDir>();
      const QString root = consoleFixture->filePath(QStringLiteral("retroarch"));
      QDir().mkpath(root + QStringLiteral("/playlists"));
      QDir().mkpath(consoleFixture->filePath(QStringLiteral("roms")));
      const auto writeText = [](const QString& path, const QString& text) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
          qFatal("Could not write console fixture %s", qPrintable(path));
        }
        file.write(text.toUtf8());
      };
      writeText(root + QStringLiteral("/retroarch.cfg"),
                QStringLiteral("playlist_directory = \"%1/playlists\"\n").arg(root));
      QStringList items;
      // A few sidecar covers in different shapes exercise the cover renderer:
      // wide box art, tall box art, a square icon, and an exact 2:3 portrait.
      const QList<QSize> coverShapes = {QSize(300, 200), QSize(120, 320), QSize(200, 200), QSize(200, 300)};
      for (int index = 0; index < 400; ++index) {
        const QString rom =
            consoleFixture->filePath(QStringLiteral("roms/SNES Cart %1.sfc").arg(index, 3, 10, QLatin1Char('0')));
        writeText(rom, QStringLiteral("sfc"));
        if (index < coverShapes.size()) {
          QImage cover(coverShapes.at(index), QImage::Format_RGB32);
          cover.fill(QColor::fromHsl((index * 90) % 360, 160, 120));
          QPainter painter(&cover);
          painter.setPen(Qt::white);
          painter.drawRect(2, 2, cover.width() - 5, cover.height() - 5);
          painter.drawText(cover.rect(), Qt::AlignCenter, QStringLiteral("%1x%2").arg(cover.width()).arg(cover.height()));
          painter.end();
          cover.save(consoleFixture->filePath(QStringLiteral("roms/SNES Cart %1.png").arg(index, 3, 10, QLatin1Char('0'))));
        }
        items.append(QStringLiteral("{\"path\":\"%1\",\"label\":\"SNES Cart %2\",\"db_name\":\"Nintendo - SNES.lpl\"}")
                         .arg(rom)
                         .arg(index, 3, 10, QLatin1Char('0')));
      }
      writeText(root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
                QStringLiteral("{\"version\":\"1.5\",\"items\":[%1]}").arg(items.join(QLatin1Char(','))));
      retroArchGames = std::make_unique<RetroArchGameModel>(
          consoleFixture->filePath(QStringLiteral("omakade.sqlite3")), &preferences);
      retroArchLibrary = retroArchGames.get();
      // Folder mode, like an EmuDeck layout: sidecar covers next to the dumps count.
      retroArchLibrary->refreshFromSources({root}, {consoleFixture->filePath(QStringLiteral("roms")) + QStringLiteral("|snes")});
      consolePortals = std::make_unique<ConsolePortalModel>();
      consolePortals->addRomModel(retroArchGames.get());
    }
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
  if (navigationTest) {
    libraryDatabasePath = QStringLiteral(":memory:");
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
  if (uninstalledLayoutTest) {
    library.setAvailability(LibraryFilterModel::Availability::AllGames);
  }
  app.library = &library;
  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::LibraryFiltered, app)) return *exit;
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
  app.achievements = &achievements;
  if (navigationTest) {
    achievements.load(QStringLiteral("demo-0"));
  }
  ProtonDbService protonDb(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                          + QStringLiteral("/protondb/summaries.json"));
  protonDb.setEnabled(!demoMode && !stressMode && !navigationTest && !detailsDirectionTest
                      && preferences.protonDbEnabled());
  QObject::connect(&preferences, &AppSettings::protonDbEnabledChanged, &protonDb,
                   [&] { protonDb.setEnabled(!demoMode && !stressMode && !navigationTest
                                            && !detailsDirectionTest && preferences.protonDbEnabled()); });
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
  } else if (demoMode || stressMode || navigationTest || detailsDirectionTest) {
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
  if (xodusLibrary != nullptr && steamLibrary != nullptr && !demoMode && !stressMode) {
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
  if (!demoMode && !stressMode && !navigationTest && !detailsDirectionTest) launcher.setSaveBackups(&saveBackups);
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
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"),
                                           (demoMode || stressMode) && !ownedLayoutTest);
  engine.rootContext()->setContextProperty(QStringLiteral("StatsFixtureView"), 0);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());
  engine.rootContext()->setContextProperty(QStringLiteral("OwnedGameCountOverride"),
                                           ownedLayoutTest ? 250 : 0);
  engine.rootContext()->setContextProperty(QStringLiteral("CouchModeRequested"),
                                           startInCouchMode);
  engine.rootContext()->setContextProperty(QStringLiteral("ColdGameModeRequested"),
                                           gameModeRequest);
  // Fullscreen starts an in-place Game Mode session. Test runs other than the Game Mode
  // fixture have no desktop to manage and keep the plain fullscreen layout.
  engine.rootContext()->setContextProperty(QStringLiteral("FullscreenStartsGameMode"),
                                           !isolatedTest || gameModeTest);
  engine.rootContext()->setContextProperty(QStringLiteral("CouchLibraryViewOverride"), QString{});

  if (startupNavigationTest && (application.arguments().contains("--startup-empty") ||
                                application.arguments().contains("--startup-delayed"))) {
    library.setSearchText(QStringLiteral("no-matching-startup-game"));
  }

  if (harness) {
    if (auto exit = harness->prepare(AppHarness::Stage::BeforeLoad, app)) return *exit;
  }
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
  if (consolePortalTest) {
    if (auto* testWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
      QTimer::singleShot(700, testWindow, [testWindow, &application] {
        runConsolePortalTest(testWindow, &application);
      });
    }
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
        [&application, &controller, &startupTimer, benchmarkMode, benchmarkLimitSupplied,
         benchmarkMaxMs, isolatedTest, startupNavigationTest] {
          const qint64 firstFrameMs = startupTimer.elapsed();
          qInfo() << "First frame in" << firstFrameMs << "ms";
          if (!isolatedTest || startupNavigationTest) {
            controller.start();
          }
          if (benchmarkMode) {
            if (benchmarkLimitSupplied && firstFrameMs > benchmarkMaxMs) {
              qCritical() << "First frame exceeded benchmark limit of" << benchmarkMaxMs << "ms";
              application.exit(EXIT_FAILURE);
            } else {
              application.quit();
            }
          }
        },
        Qt::SingleShotConnection);

    if (couchNavigationContract) {
      QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
        application.exit(runCouchNavigationContract(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
      });
    }

    if (couchNavigationSweep) {
      QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
        application.exit(runCouchNavigationSweep(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
      });
    }

    if (startupNavigationTest) {
      QTimer::singleShot(500, quickWindow, [quickWindow, &application, &controller] {
        application.exit(runStartupNavigationContract(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
      });
    }

    if (couchNavigationTest) {
      QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
        const auto fail = [&application](const QString& message) {
          qCritical().noquote() << message;
          application.exit(EXIT_FAILURE);
        };
        auto* couch = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchLibrary"));
        auto* couchCursor =
            quickWindow->findChild<QObject*>(QStringLiteral("couchCursorManager"));
        auto* strip = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchGameStrip"));
        auto* grid = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchGameGrid"));
        auto* view = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchViewButton"));
        auto* show = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchShowButton"));
        auto* sourceFilter =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchSourceButton"));
        auto* sortOrder = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchSortButton"));
        auto* layout = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchLayoutButton"));
        auto* favorite =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchFavoriteButton"));
        auto* settings =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchSettingsButton"));
        auto* search = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchSearchButton"));
        auto* browsePanel =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchBrowsePanel"));
        auto* browseCategories =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchBrowseCategories"));
        auto* browseOptions =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchBrowseOptions"));
        auto* keyboard = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchKeyboard"));
        auto* keyboardGrid =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchKeyboardGrid"));
        auto* textEntryKeyboard =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchTextEntryKeyboard"));
        auto* textEntryGrid =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("couchTextEntryGrid"));
        auto* desktopSearch =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("searchField"));
        QObject* preferences =
            qmlContext(quickWindow)->contextProperty(QStringLiteral("Preferences")).value<QObject*>();
        if (!quickWindow->property("couchMode").toBool() || couch == nullptr ||
            couchCursor == nullptr ||
            !couch->isVisible() || strip == nullptr || grid == nullptr || view == nullptr ||
            show == nullptr || sourceFilter == nullptr || sortOrder == nullptr || layout == nullptr ||
            favorite == nullptr || settings == nullptr ||
            search == nullptr || preferences == nullptr ||
            browsePanel == nullptr || browseCategories == nullptr || browseOptions == nullptr ||
            keyboard == nullptr || keyboardGrid == nullptr || textEntryKeyboard == nullptr ||
            textEntryGrid == nullptr || desktopSearch == nullptr) {
          fail(QStringLiteral("Couch navigation test could not find the couch controls"));
          return;
        }
        auto* homeScreen = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeScreen"));
        auto* homeAppHeader = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeAppHeader"));
        auto* couchHomeLibrary = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeCouchHomeButton"));
        if (!homeScreen || !homeAppHeader || !couchHomeLibrary) {
          fail(QStringLiteral("Couch Home header fixtures were not available"));
          return;
        }
        quickWindow->setProperty("homeOpen", true);
        QCoreApplication::processEvents();
        if (!homeScreen->isVisible() || homeAppHeader->isVisible() || !couchHomeLibrary->isVisible()) {
          fail(QStringLiteral("Couch Home did not retain its couch header"));
          return;
        }
        QMetaObject::invokeMethod(homeScreen, "focusHome");
        if (quickWindow->activeFocusItem() != couchHomeLibrary) {
          fail(QStringLiteral("Couch Home focus did not reach its header"));
          return;
        }
        quickWindow->setProperty("homeOpen", false);
        QCoreApplication::processEvents();
        preferences->setProperty("couchLibraryView", QStringLiteral("detail"));
        QCoreApplication::processEvents();
        strip->setProperty("currentIndex", 0);
        strip->forceActiveFocus();
        controller.focusDirectionRequested(Qt::Key_Right);
        QTimer::singleShot(50, quickWindow,
                           [quickWindow, &application, &controller, couch, couchCursor, strip, grid,
                            view, show, sourceFilter, sortOrder, layout, favorite, browsePanel,
                            browseCategories, browseOptions, search, settings,
                            keyboard, keyboardGrid, textEntryKeyboard, textEntryGrid, desktopSearch,
                            preferences, fail] {
          if (!strip->hasActiveFocus() || strip->property("currentIndex").toInt() != 1) {
            fail(QStringLiteral("Controller Right did not advance the couch game strip"));
            return;
          }
          if (!couchCursor->property("cursorHidden").toBool()) {
            fail(QStringLiteral("Controller navigation did not hide the couch cursor"));
            return;
          }
          const auto sendKey = [&controller](int key) {
            requestControllerCommand(controller, key);
            QEventLoop eventLoop;
            QTimer::singleShot(30, &eventLoop, &QEventLoop::quit);
            eventLoop.exec();
          };
          auto* emptyState = couch->findChild<QQuickItem*>(QStringLiteral("couchEmptyState"));
          QObject* regressionLibrary =
              qmlContext(quickWindow)->contextProperty(QStringLiteral("Library")).value<QObject*>();
          if (emptyState == nullptr || regressionLibrary == nullptr) {
            fail(QStringLiteral("Couch regression fixtures were not available"));
            return;
          }
          strip->setProperty("currentIndex", 7);
          for (const QString& layoutName : {QStringLiteral("grid"), QStringLiteral("detail")}) {
            QMetaObject::invokeMethod(couch, "toggleLibraryView");
            QCoreApplication::processEvents();
            QQuickItem* activeView = layoutName == QStringLiteral("grid") ? grid : strip;
            if (activeView->property("currentIndex").toInt() != 7 ||
                couch->property("currentIndex").toInt() != 7 || !activeView->hasActiveFocus()) {
              fail(QStringLiteral("Couch layout switch lost selection or focus in %1").arg(layoutName));
              return;
            }
          }
          // The empty state follows the view's count, and a view only announces a new
          // count when it lays out on its next polish. The model has already changed
          // by then, so the check waits for that layout instead of reading the state
          // before it has had a chance to follow.
          const auto waitForEmptyState = [emptyState](bool visible) {
            QElapsedTimer timer;
            timer.start();
            while (emptyState->isVisible() != visible && timer.elapsed() < 1000) {
              QEventLoop events;
              QTimer::singleShot(10, &events, &QEventLoop::quit);
              events.exec();
            }
            return emptyState->isVisible() == visible;
          };
          strip->forceActiveFocus();
          regressionLibrary->setProperty("searchText", QStringLiteral("omakade-no-matching-game-regression"));
          if (!waitForEmptyState(true)) {
            fail(QStringLiteral("Empty couch library did not show its empty state"));
            return;
          }
          regressionLibrary->setProperty("searchText", QString{});
          if (!waitForEmptyState(false)) {
            fail(QStringLiteral("Populated couch library retained its empty state"));
            return;
          }
          strip->setProperty("currentIndex", 1);
          strip->forceActiveFocus();
          const auto focusDescription = [quickWindow] {
            QQuickItem* focused = quickWindow->activeFocusItem();
            QStringList chain;
            while (focused != nullptr && chain.size() < 5) {
              chain.append(QStringLiteral("%1[%2]")
                               .arg(QString::fromLatin1(focused->metaObject()->className()),
                                    focused->objectName()));
              focused = focused->parentItem();
            }
            return chain.isEmpty() ? QStringLiteral("none") : chain.join(QStringLiteral(" <- "));
          };
          sendKey(Qt::Key_Up);
          if (!view->hasActiveFocus()) {
            fail(QStringLiteral("Controller Up did not reach the couch game action"));
            return;
          }
          sendKey(Qt::Key_Right);
          if (!favorite->hasActiveFocus()) {
            fail(QStringLiteral("Controller Right did not reach the couch favorite action"));
            return;
          }
          controller.toolbarRequested();
          QCoreApplication::processEvents();
          if (!strip->hasActiveFocus()) {
            fail(QStringLiteral("Controller Controls did not return to the couch game strip"));
            return;
          }
          controller.toolbarRequested();
          QCoreApplication::processEvents();
          if (!view->hasActiveFocus()) {
            fail(QStringLiteral("Controller Controls did not return to couch actions"));
            return;
          }
          sendKey(Qt::Key_Up);
          auto* consoleView = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchConsoleViewButton"));
          if (!consoleView) {
            fail(QStringLiteral("Couch console view toggle is missing"));
            return;
          }
          const QList<QQuickItem*> detailToolbarPath = {show, sourceFilter, sortOrder, consoleView, layout};
          for (int step = 0; step < detailToolbarPath.size(); ++step) {
            if (!detailToolbarPath.at(step)->hasActiveFocus()) {
              fail(QStringLiteral("Controller detail toolbar step %1 failed; focus=%2")
                       .arg(step)
                       .arg(focusDescription()));
              return;
            }
            if (step + 1 < detailToolbarPath.size()) {
              sendKey(Qt::Key_Right);
            }
          }
          sendKey(Qt::Key_Return);
          if (!grid->isVisible() || !layout->hasActiveFocus() ||
              preferences->property("couchLibraryView").toString() != QStringLiteral("grid")) {
            fail(QStringLiteral("Couch layout control did not activate the persistent grid"));
            return;
          }
          if (!layout->hasActiveFocus()) {
            fail(QStringLiteral("Controller Controls did not reach the Grid layout action"));
            return;
          }
          controller.toolbarRequested();
          QCoreApplication::processEvents();
          if (!grid->hasActiveFocus()) {
            fail(QStringLiteral("Controller Controls did not return to the game grid"));
            return;
          }
          const int gridColumns = grid->property("columnCount").toInt();
          grid->setProperty("currentIndex", gridColumns + 1);
          sendKey(Qt::Key_Up);
          if (!grid->hasActiveFocus() || grid->property("currentIndex").toInt() != 1) {
            fail(QStringLiteral("Controller Grid Up did not move to the previous game row"));
            return;
          }
          sendKey(Qt::Key_Up);
          if (!layout->hasActiveFocus()) {
            fail(QStringLiteral("Controller Grid Up did not restore the control used to enter games"));
            return;
          }
          show->forceActiveFocus();
          const QList<QQuickItem*> toolbarPath = {show, sourceFilter, sortOrder, consoleView, layout};
          for (int step = 0; step < toolbarPath.size(); ++step) {
            if (!toolbarPath.at(step)->hasActiveFocus()) {
              fail(QStringLiteral("Controller grid toolbar step %1 failed; focus=%2")
                       .arg(step)
                       .arg(focusDescription()));
              return;
            }
            if (toolbarPath.at(step) == consoleView) {
              const bool expanded = regressionLibrary->property("expandConsoles").toBool();
              sendKey(Qt::Key_Return);
              if (regressionLibrary->property("expandConsoles").toBool() == expanded || !consoleView->hasActiveFocus()) {
                fail(QStringLiteral("Couch console toggle did not switch view and retain controller focus"));
                return;
              }
              sendKey(Qt::Key_Return);
              if (regressionLibrary->property("expandConsoles").toBool() != expanded) {
                fail(QStringLiteral("Couch console toggle did not restore the previous view"));
                return;
              }
            }
            if (step + 1 < toolbarPath.size()) {
              sendKey(Qt::Key_Right);
            }
          }
          for (QQuickItem* control : {show, sourceFilter, sortOrder}) {
            control->forceActiveFocus();
            for (int cycle = 0; cycle < 18; ++cycle) {
              const QString previousSource = regressionLibrary->property("sourceFilter").toString();
              sendKey(Qt::Key_Return);
              if (control == sourceFilter && regressionLibrary->property("sourceFilter").toString() == previousSource) {
                fail(QStringLiteral("Couch Source did not advance to another filter"));
                return;
              }
              if (!control->hasActiveFocus() || couch->property("browseOpen").toBool()) {
                fail(QStringLiteral("Couch filter cycling stole focus or opened a panel"));
                return;
              }
            }
          }
          regressionLibrary->setProperty("mode", 0);
          regressionLibrary->setProperty("sourceFilter", QString{});
          auto* filters = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchFiltersButton"));
          if (!filters) { fail(QStringLiteral("Missing couch Filters control")); return; }
          filters->forceActiveFocus();
          sendKey(Qt::Key_Return);
          if (!couch->property("browseOpen").toBool() || !browsePanel->isVisible() ||
              !browseCategories->hasActiveFocus()) {
            fail(QStringLiteral("Couch Browse did not open with category focus"));
            return;
          }
          // The header row is reached from the first category and walked with the pad alone.
          auto* savedFiltersButton =
              browsePanel->findChild<QQuickItem*>(QStringLiteral("couchSavedFiltersButton"));
          auto* pickGameButton =
              browsePanel->findChild<QQuickItem*>(QStringLiteral("couchRandomGameButton"));
          auto* organizeButton =
              browsePanel->findChild<QQuickItem*>(QStringLiteral("couchOrganizeButton"));
          auto* clearFiltersButton =
              browsePanel->findChild<QQuickItem*>(QStringLiteral("couchClearFiltersButton"));
          sendKey(Qt::Key_Up);
          sendKey(Qt::Key_Left);
          if (!pickGameButton || !pickGameButton->hasActiveFocus()) {
            fail(QStringLiteral("Controller Left did not move along the couch Browse header"));
            return;
          }
          sendKey(Qt::Key_Left);
          sendKey(Qt::Key_Left);
          if (!organizeButton || !organizeButton->hasActiveFocus()) {
            fail(QStringLiteral("Controller Left did not reach Organize in couch Browse"));
            return;
          }
          for (int step = 0; step < 5; ++step) sendKey(Qt::Key_Right);
          if (!clearFiltersButton || !clearFiltersButton->hasActiveFocus()) {
            fail(QStringLiteral("Controller Right did not reach Clear Filters in couch Browse"));
            return;
          }
          for (int step = 0; step < 4; ++step) sendKey(Qt::Key_Left);
          if (!savedFiltersButton || !savedFiltersButton->hasActiveFocus()) {
            fail(QStringLiteral("Controller Left did not return along the couch Browse header"));
            return;
          }
          sendKey(Qt::Key_Down);
          if (!browseCategories->hasActiveFocus()) {
            fail(QStringLiteral("Controller Down did not leave the couch Browse header"));
            return;
          }
          sendKey(Qt::Key_Right);
          if (!browseOptions->hasActiveFocus()) {
            fail(QStringLiteral("Controller Right did not reach couch Browse options"));
            return;
          }
          QObject* library =
              qmlContext(quickWindow)->contextProperty(QStringLiteral("Library")).value<QObject*>();
          sendKey(Qt::Key_Down);
          sendKey(Qt::Key_Return);
          if (!library || library->property("sourceFilter").toString() != QStringLiteral("Emulated") ||
              library->property("sourceFilters").toStringList() != library->property("emulatorSources").toStringList()) {
            fail(QStringLiteral("Couch Browse did not select all emulated sources"));
            return;
          }
          sendKey(Qt::Key_Escape);
          sendKey(Qt::Key_Return);
          sendKey(Qt::Key_Right);
          if (!browseOptions->hasActiveFocus() || browseOptions->property("currentIndex").toInt() != 1) {
            fail(QStringLiteral("Reopening Couch Browse did not highlight Emulated"));
            return;
          }
          sendKey(Qt::Key_Up);
          sendKey(Qt::Key_Return);
          if (!library->property("sourceFilters").toStringList().isEmpty()) {
            fail(QStringLiteral("Couch All Sources did not clear the Emulated filter"));
            return;
          }
          // Reach the new categories through the actual scrolling category list.
          sendKey(Qt::Key_Left);
          for (int i = 0; i < 9; ++i)
            sendKey(Qt::Key_Down);
          sendKey(Qt::Key_Right);
          sendKey(Qt::Key_Down);
          sendKey(Qt::Key_Return);
          if (library->property("decadeFilter").toString().isEmpty()) {
            fail(QStringLiteral("Couch Browse did not apply a release decade"));
            return;
          }
          library->setProperty("decadeFilter", QString());
          sendKey(Qt::Key_Left);
          for (int i = 0; i < 9; ++i)
            sendKey(Qt::Key_Up);
          sendKey(Qt::Key_Right);
          sendKey(Qt::Key_Left);
          sendKey(Qt::Key_Down);
          sendKey(Qt::Key_Right);
          sendKey(Qt::Key_Down);
          sendKey(Qt::Key_Return);
          if (library->property("mode").toInt() != 1) {
            fail(QStringLiteral("Couch Browse did not apply the selected library view"));
            return;
          }
          sendKey(Qt::Key_Escape);
          // Closing the filter panel restores its toolbar control.
          if (couch->property("browseOpen").toBool() || !filters->hasActiveFocus()) {
            fail(QStringLiteral("Controller Back did not close couch Browse"));
            return;
          }
          sendKey(Qt::Key_Left);
          if (!search->hasActiveFocus()) {
            fail(QStringLiteral("Controller could not reach couch Search"));
            return;
          }
          sendKey(Qt::Key_Return);
          if (!couch->property("searchOpen").toBool() || !keyboard->isVisible() ||
              !keyboardGrid->hasActiveFocus()) {
            fail(QStringLiteral("Couch Search did not open the on-screen keyboard"));
            return;
          }
          sendKey(Qt::Key_Return);
          if (keyboard->property("value").toString() != QStringLiteral("A")) {
            fail(QStringLiteral("Controller confirm did not type with the on-screen keyboard"));
            return;
          }
          // Enter after typing on a physical keyboard is done, not the highlighted key.
          // With no match, focus stays on Search so the text can be changed.
          deliverKeyboardTestText(quickWindow, QStringLiteral("zq"));
          deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
          QCoreApplication::processEvents();
          auto* searchModel = couch->property("libraryModel").value<QObject*>();
          if (couch->property("searchOpen").toBool() || searchModel == nullptr ||
              searchModel->property("searchText").toString() != QStringLiteral("Azq")) {
            fail(QStringLiteral("Enter after typing did not finish the couch search"));
            return;
          }
          {
            QEventLoop settle;
            QTimer::singleShot(30, &settle, &QEventLoop::quit);
            settle.exec();
          }
          if (!search->hasActiveFocus()) {
            fail(QStringLiteral("A search with no match did not return focus to Search"));
            return;
          }
          searchModel->setProperty("searchText", QString());
          sendKey(Qt::Key_Return);
          if (!couch->property("searchOpen").toBool() || !keyboardGrid->hasActiveFocus()) {
            fail(QStringLiteral("Couch Search did not reopen after a finished search"));
            return;
          }
          deliverKeyboardTestKey(quickWindow, Qt::Key_F11);
          if (quickWindow->property("couchMode").toBool() ||
              couch->property("searchOpen").toBool() || keyboard->isVisible() ||
              preferences->property("couchModeEnabled").toBool()) {
            fail(QStringLiteral("Leaving Couch Mode did not cancel Search cleanly"));
            return;
          }
          const bool activated = QMetaObject::invokeMethod(quickWindow, "activateCouchMode");
          QCoreApplication::processEvents();
          if (!activated || !quickWindow->property("couchMode").toBool() ||
              preferences->property("couchModeEnabled").toBool()) {
            fail(QStringLiteral("Session Couch activation changed the startup preference"));
            return;
          }
          const bool openedTextEntry = QMetaObject::invokeMethod(
              quickWindow, "openCouchTextEntry",
              Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(desktopSearch))),
              Q_ARG(QVariant, QStringLiteral("TEST TEXT ENTRY")), Q_ARG(QVariant, true),
              Q_ARG(QVariant, QStringLiteral("Enter a value")));
          QCoreApplication::processEvents();
          if (!openedTextEntry || !quickWindow->property("couchTextEntryOpen").toBool() ||
              !textEntryKeyboard->isVisible() || !textEntryGrid->hasActiveFocus() ||
              !textEntryKeyboard->property("passwordMode").toBool()) {
            fail(QStringLiteral("Couch text entry did not open with masked keyboard focus"));
            return;
          }
          sendKey(Qt::Key_Return);
          if (textEntryKeyboard->property("value").toString() != QStringLiteral("A")) {
            fail(QStringLiteral("Controller confirm did not type in couch text entry"));
            return;
          }
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 43));
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 20));
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 44));
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 10));
          if (textEntryKeyboard->property("value").toString() != QStringLiteral("Aa!")) {
            fail(QStringLiteral("Couch text entry did not switch letter and symbol layouts"));
            return;
          }
          textEntryKeyboard->setProperty("maximumLength", 3);
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 41));
          if (textEntryKeyboard->property("value").toString() != QStringLiteral("Aa!")) {
            fail(QStringLiteral("Couch text entry exceeded its maximum length with Space"));
            return;
          }
          QMetaObject::invokeMethod(textEntryKeyboard, "activateKey", Q_ARG(QVariant, 45));
          QCoreApplication::processEvents();
          if (quickWindow->property("couchTextEntryOpen").toBool() ||
              desktopSearch->property("text").toString() != QStringLiteral("Aa!")) {
            fail(QStringLiteral("Couch text entry did not apply its value"));
            return;
          }
          desktopSearch->setProperty("text", QString());
          QEventLoop focusRestoreLoop;
          QTimer::singleShot(30, &focusRestoreLoop, &QEventLoop::quit);
          focusRestoreLoop.exec();
          search->forceActiveFocus();
          sendKey(Qt::Key_Right);
          if (!filters->hasActiveFocus()) { fail(QStringLiteral("Search did not reach Filters")); return; }
          // Header navigation follows the visible rows, rather than wrapping Right
          // into a different row. Reach Settings vertically, then along the header.
          sendKey(Qt::Key_Up);
          const QStringList header{QStringLiteral("couchHomeButton"), QStringLiteral("couchLibraryButton"),
                                   QStringLiteral("couchStatsButton"), QStringLiteral("couchSettingsButton"),
                                   QStringLiteral("couchDesktopButton")};
          for (int step = 0; step < header.size() && !settings->hasActiveFocus(); ++step) {
            auto* focused = quickWindow->activeFocusItem();
            const int at = focused ? header.indexOf(focused->objectName()) : -1;
            if (at < 0) break;
            sendKey(at < header.indexOf(QStringLiteral("couchSettingsButton")) ? Qt::Key_Right : Qt::Key_Left);
          }
          if (!settings->hasActiveFocus()) {
            fail(QStringLiteral("Controller could not reach couch Settings"));
            return;
          }
          sendKey(Qt::Key_Return);
          QTimer::singleShot(80, quickWindow,
                             [quickWindow, &application, &controller, couch,
                              fail] {
            if (!quickWindow->property("diagnosticsOpen").toBool()) {
              fail(QStringLiteral("Couch Settings did not open"));
              return;
            }
            auto* settingsPage = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
            auto* settingsScroll = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
            if (settingsPage == nullptr || settingsScroll == nullptr) {
              fail(QStringLiteral("Settings opened without its controls"));
              return;
            }
                    settingsPage->setProperty("section", 1);
                    QEventLoop settingsLayout;
                    QTimer::singleShot(50, &settingsLayout, &QEventLoop::quit);
                    settingsLayout.exec();
                    QQuickItem* settingsStart = quickWindow->activeFocusItem();
            for (int step = 0; step < 30; ++step) {
              controller.focusDirectionRequested(Qt::Key_Down);
            }
            if (quickWindow->activeFocusItem() == settingsStart ||
                settingsScroll->property("navigationContentY").toReal() <= 0) {
              qWarning() << "Settings focus" << (settingsStart ? settingsStart->property("text") : QVariant{}) << (quickWindow->activeFocusItem() ? quickWindow->activeFocusItem()->property("text") : QVariant{}) << "scroll" << settingsScroll->property("navigationContentY");
              fail(QStringLiteral("Controller did not traverse and scroll couch Settings"));
              return;
            }
            controller.backRequested();
            QTimer::singleShot(
                50, quickWindow,
                [quickWindow, &application, &controller, couch, fail] {
              if (quickWindow->property("diagnosticsOpen").toBool()) {
                fail(QStringLiteral("Controller Back did not close couch Settings"));
                return;
              }
              couch->setProperty("currentIndex", 0);
              QMetaObject::invokeMethod(couch, "refreshCurrentGame");
              QMetaObject::invokeMethod(couch, "focusGrid");
              QCoreApplication::processEvents();
              controller.acceptRequested();
              QTimer::singleShot(80, quickWindow,
                                 [quickWindow, &application, &controller, fail] {
                auto* play =
                    quickWindow->findChild<QQuickItem*>(QStringLiteral("playButton"));
                auto* favorite =
                    quickWindow->findChild<QQuickItem*>(QStringLiteral("favoriteButton"));
                if (!quickWindow->property("detailOpen").toBool() || play == nullptr ||
                    favorite == nullptr || !play->hasActiveFocus()) {
                  fail(QStringLiteral("Couch game details did not open with Play focused"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Right);
                if (!favorite->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Right did not reach the couch favorite action"));
                  return;
                }
                controller.focusDirectionRequested(Qt::Key_Left);
                if (!play->hasActiveFocus()) {
                  fail(QStringLiteral("Controller Left did not return to the couch Play action"));
                  return;
                }
                auto* newCollection =
                    quickWindow->findChild<QQuickItem*>(QStringLiteral("newCollectionButton"));
                const bool demoMode =
                    qmlContext(quickWindow)->contextProperty(QStringLiteral("DemoMode")).toBool();
                if (!demoMode && newCollection != nullptr && newCollection->isVisible()) {
                  const auto sendDetailKey = [&controller](int key) {
                    requestControllerCommand(controller, key);
                    QEventLoop eventLoop;
                    QTimer::singleShot(30, &eventLoop, &QEventLoop::quit);
                    eventLoop.exec();
                  };
                  auto* details =
                      quickWindow->findChild<QQuickItem*>(QStringLiteral("gameDetails"));
                  auto* textEntry = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("couchTextEntryKeyboard"));
                  auto* insights =
                      quickWindow->findChild<QQuickItem*>(QStringLiteral("insightsSection"));
                  auto* insightRefresh = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("insightRefreshButton"));
                  auto* achievementSection = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("achievementListSection"));
                  auto* achievementSort = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("achievementSortButton"));
                  auto* achievementRefresh = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("achievementRefreshButton"));
                  auto* detailsScroll =
                      quickWindow->findChild<QQuickItem*>(QStringLiteral("detailsScroll"));
                  if (details == nullptr || textEntry == nullptr || insights == nullptr ||
                      insightRefresh == nullptr || achievementSection == nullptr ||
                      achievementSort == nullptr || achievementRefresh == nullptr ||
                      detailsScroll == nullptr) {
                    fail(QStringLiteral("Couch focus sweep could not find detail controls"));
                    return;
                  }
                  newCollection->forceActiveFocus();
                  sendDetailKey(Qt::Key_Return);
                  if (!quickWindow->property("couchTextEntryOpen").toBool() ||
                      !textEntry->isVisible()) {
                    fail(QStringLiteral("New Collection did not open couch text entry"));
                    return;
                  }
                  sendDetailKey(Qt::Key_Escape);
                  sendDetailKey(Qt::Key_Escape);
                  details =
                      quickWindow->findChild<QQuickItem*>(QStringLiteral("gameDetails"));
                  newCollection = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("newCollectionButton"));
                  if (quickWindow->property("couchTextEntryOpen").toBool()) {
                    fail(QStringLiteral("Controller Back did not close couch text entry"));
                    return;
                  }
                  if (details == nullptr || newCollection == nullptr) {
                    fail(QStringLiteral("Controller Back unexpectedly closed game details"));
                    return;
                  }
                  if (details->property("collectionEditorOpen").toBool()) {
                    fail(QStringLiteral("Controller Back did not close the collection editor"));
                    return;
                  }
                  if (!newCollection->hasActiveFocus()) {
                    fail(QStringLiteral("Controller Back did not restore New Collection focus"));
                    return;
                  }

                  if (insights->isVisible() && achievementSection->isVisible()) {
                    if (!insightRefresh->isVisible() || !insightRefresh->isEnabled() ||
                        !achievementSort->isVisible() || !achievementSort->isEnabled() ||
                        !achievementRefresh->isVisible() || !achievementRefresh->isEnabled()) {
                      fail(QStringLiteral("Couch detail fixture is missing focusable controls"));
                      return;
                    }
                    controller.focusDirectionRequested(Qt::Key_Down);
                    if (!insightRefresh->hasActiveFocus()) {
                      fail(QStringLiteral("Controller did not reach couch game insights"));
                      return;
                    }
                    controller.focusDirectionRequested(Qt::Key_Down);
                    if (!achievementSort->hasActiveFocus()) {
                      fail(QStringLiteral("Controller did not reach couch achievement sorting"));
                      return;
                    }
                    controller.focusDirectionRequested(Qt::Key_Right);
                    if (!achievementRefresh->hasActiveFocus()) {
                      fail(QStringLiteral("Controller did not reach couch achievement refresh"));
                      return;
                    }
                    controller.focusDirectionRequested(Qt::Key_Left);
                    const qreal initialContentY =
                        detailsScroll->property("navigationContentY").toReal();
                    controller.focusDirectionRequested(Qt::Key_Down);
                    QQuickItem* firstAchievement = quickWindow->activeFocusItem();
                    if (firstAchievement == nullptr ||
                        !firstAchievement->objectName().startsWith(
                            QStringLiteral("achievementCard"))) {
                      fail(QStringLiteral("Controller did not enter couch achievement cards"));
                      return;
                    }
                    for (int step = 0; step < 5; ++step) {
                      controller.focusDirectionRequested(Qt::Key_Down);
                    }
                    if (quickWindow->activeFocusItem() == firstAchievement ||
                        detailsScroll->property("navigationContentY").toReal() <= initialContentY) {
                      fail(QStringLiteral("Couch achievement navigation did not move and scroll"));
                      return;
                    }
                  }
                }
                controller.backRequested();
                QTimer::singleShot(50, quickWindow,
                                   [quickWindow, &application, &controller, fail] {
                  auto* currentStrip = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("couchGameStrip"));
                  auto* currentGrid = quickWindow->findChild<QQuickItem*>(
                      QStringLiteral("couchGameGrid"));
                  const bool libraryFocused =
                      (currentStrip != nullptr && currentStrip->isVisible() &&
                       currentStrip->hasActiveFocus()) ||
                      (currentGrid != nullptr && currentGrid->isVisible() &&
                       currentGrid->hasActiveFocus());
                  if (quickWindow->property("detailOpen").toBool() ||
                      !libraryFocused) {
                    fail(QStringLiteral("Controller Back did not restore the couch library"));
                    return;
                  }
                  controller.startRequested();
                  QTimer::singleShot(50, quickWindow, [quickWindow, &application, fail] {
                    if (quickWindow->property("couchMode").toBool()) {
                      fail(QStringLiteral("Controller Start did not return to desktop mode"));
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
    } else if (detailsDirectionTest) {
      // Every direction out of the rating and cover art section, checked one control at a time.
      // Left off "NOT THIS GAME" used to land on the cover sidebar four hundred pixels up, and
      // up off "SEARCH IGDB" on the BACK button at the top of the page, because the spatial
      // search took any candidate in the half plane over no candidate at all. Staying put is
      // the right answer when nothing is really in that direction.
      QTimer::singleShot(300, &application, [&application, rootWindow, &controller] {
        QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
        // Wait for the section to be laid out rather than guessing at a delay. Fixed timers
        // pass alone and fail when the suite runs everything at once.
        auto attempts = std::make_shared<int>(0);
        auto step = std::make_shared<std::function<void()>>();
        *step = [&application, rootWindow, &controller, attempts, step] {
          auto* window = qobject_cast<QQuickWindow*>(rootWindow);
          auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("metadataEditor"));
          auto* identifyPanel = rootWindow->findChild<QObject*>("identifyGamePanel");
          if (identifyPanel && !identifyPanel->property("opened").toBool())
            QMetaObject::invokeMethod(identifyPanel, "open");
          if (editor != nullptr) {
            editor->setProperty("matchControlsOpen", true);
            editor->setProperty("coverControlsOpen", true);
            editor->setProperty("editing", true);
          }
          auto* opener =
              rootWindow->findChild<QQuickItem*>(QStringLiteral("metadataArtworkButton"));
          auto* lastRow =
              rootWindow->findChild<QQuickItem*>(QStringLiteral("metadataClearCoverButton"));
          auto* second =
              rootWindow->findChild<QQuickItem*>(QStringLiteral("metadataIdentifyButton"));
          // Sizes alone are not enough: mid layout the rows briefly sit on top of one another,
          // and left off the first control then finds the second beside it. Wait until the rows
          // are actually stacked, which is the arrangement every expectation here is about.
          const auto rect = [](QQuickItem* item) {
            return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
          };
          const bool present = window != nullptr && editor != nullptr && editor->isVisible() &&
                               opener != nullptr && opener->isVisible() && opener->width() > 0 &&
                               second != nullptr && second->isVisible() && second->width() > 0 &&
                               lastRow != nullptr && lastRow->isVisible() && lastRow->width() > 0;
          const bool ready = present && rect(second).top() >= rect(opener).bottom() - 1 &&
                             rect(lastRow).top() >= rect(second).bottom() - 1;
          if (!ready) {
            if (++(*attempts) > 80) {
              qCritical("Direction test could not find the cover art section");
              application.exit(EXIT_FAILURE);
              return;
            }
            QTimer::singleShot(50, &application, [step] { (*step)(); });
            return;
          }
          {
            const auto item = [rootWindow](const char* name) {
              return rootWindow->findChild<QQuickItem*>(QString::fromLatin1(name));
            };
            // Several of these are gated on credentials the demo library has none of. The
            // question here is where focus goes, not whether they would do anything.
            const char* controls[] = {"metadataArtworkButton", "metadataIdentifyButton",
                                      "metadataRejectButton", "metadataChoosePortraitButton",
                                      "metadataClearCoverButton"};
            for (const char* name : controls) {
              if (auto* control = item(name))
                control->setProperty("enabled", true);
            }
            struct Move {
              const char* from;
              Qt::Key key;
              const char* to; // nullptr means focus must not move
            };
            const Move moves[] = {
                {"metadataArtworkButton", Qt::Key_Down, "metadataIdentifyButton"},
                {"metadataArtworkButton", Qt::Key_Left, nullptr},
                {"metadataArtworkButton", Qt::Key_Right, nullptr},
                {"metadataIdentifyButton", Qt::Key_Up, "metadataArtworkButton"},
                {"metadataIdentifyButton", Qt::Key_Down, "metadataRejectButton"},
                // The field beside it is a navigation target once the controller is what is
                // being used, which this test is by definition. On a desktop being driven by a
                // mouse and keyboard the field stays out of the arrow order and focus would not
                // move here at all.
                {"metadataIdentifyButton", Qt::Key_Left, "metadataTitleField"},
                {"metadataIdentifyButton", Qt::Key_Right, nullptr},
                {"metadataRejectButton", Qt::Key_Up, "metadataIdentifyButton"},
                {"metadataRejectButton", Qt::Key_Left, nullptr},
                {"metadataRejectButton", Qt::Key_Right, "metadataChoosePortraitButton"},
                {"metadataChoosePortraitButton", Qt::Key_Up, "metadataIdentifyButton"},
                {"metadataChoosePortraitButton", Qt::Key_Left, "metadataRejectButton"},
                {"metadataChoosePortraitButton", Qt::Key_Right, "metadataClearCoverButton"},
                {"metadataClearCoverButton", Qt::Key_Up, "metadataIdentifyButton"},
                {"metadataClearCoverButton", Qt::Key_Left, "metadataChoosePortraitButton"},
                {"metadataClearCoverButton", Qt::Key_Right, nullptr},
            };
            const auto describe = [](QQuickItem* focused) {
              if (focused == nullptr)
                return QStringLiteral("nothing");
              return focused->objectName().isEmpty()
                         ? QString::fromLatin1(focused->metaObject()->className())
                         : focused->objectName();
            };
            for (const Move& move : moves) {
              auto* from = item(move.from);
              if (from == nullptr || !from->isVisible()) {
                qCritical().noquote() << QStringLiteral("Direction test could not reach %1")
                                             .arg(QString::fromLatin1(move.from));
                application.exit(EXIT_FAILURE);
                return;
              }
              from->forceActiveFocus();
              controller.focusDirectionRequested(move.key);
              auto* landed = window->activeFocusItem();
              auto* wanted = move.to == nullptr ? from : item(move.to);
              if (landed != wanted) {
                qCritical().noquote()
                    << QStringLiteral("%1 %2 went to %3, expected %4")
                           .arg(QString::fromLatin1(move.from),
                                move.key == Qt::Key_Up     ? QStringLiteral("up")
                                : move.key == Qt::Key_Down ? QStringLiteral("down")
                                : move.key == Qt::Key_Left ? QStringLiteral("left")
                                                           : QStringLiteral("right"),
                                describe(landed),
                                move.to == nullptr ? QStringLiteral("no movement")
                                                   : QString::fromLatin1(move.to));
                application.exit(EXIT_FAILURE);
                return;
              }
            }
            // The clear button sits inside its field's own rectangle, so no amount of geometry
            // finds it: right never enters the field it is already inside, and left prefers the
            // field itself. The field points at it, and from there right carries on.
            auto* titleField = item("metadataTitleField");
            auto* titleClear = item("metadataTitleFieldClearButton");
            if (titleField == nullptr || titleClear == nullptr) {
              qCritical("Metadata title field or clear button is missing");
              application.exit(EXIT_FAILURE);
              return;
            }
            titleField->setProperty("text", "Clear control test");
            {
              titleField->forceActiveFocus();
              controller.focusDirectionRequested(Qt::Key_Right);
              if (window->activeFocusItem() != titleClear) {
                qCritical().noquote()
                    << QStringLiteral("Right from a text field did not reach its clear button, "
                                      "it went to %1")
                           .arg(describe(window->activeFocusItem()));
                application.exit(EXIT_FAILURE);
                return;
              }
              controller.focusDirectionRequested(Qt::Key_Left);
              if (window->activeFocusItem() != titleField) {
                qCritical("Left from a clear button did not return to its field");
                application.exit(EXIT_FAILURE);
                return;
              }
              // And it has to actually empty the field, then hand focus back rather than
              // leaving it on a button that has just disappeared.
              titleClear->forceActiveFocus();
              QMetaObject::invokeMethod(titleClear, "clearField");
              if (titleField->property("length").toInt() != 0) {
                qCritical("The clear button did not empty the field");
                application.exit(EXIT_FAILURE);
                return;
              }
              if (titleClear->isVisible()) {
                qCritical("The clear button stayed visible over an empty field");
                application.exit(EXIT_FAILURE);
                return;
              }
            }
            // Pressing the controller's primary button on a field has to offer a keyboard, on
            // the desktop as well as the couch. Every field used to test couch mode itself, so
            // changing the rule in the shared function reached two fields and missed seven.
            if (titleField != nullptr) {
              rootWindow->setProperty("couchTextEntryOpen", false);
              titleField->forceActiveFocus();
              controller.acceptRequested();
              if (!rootWindow->property("couchTextEntryOpen").toBool()) {
                qCritical("The controller reached a text field and no keyboard opened");
                application.exit(EXIT_FAILURE);
                return;
              }
              rootWindow->setProperty("couchTextEntryOpen", false);
            }
            controller.backRequested();
            auto* coverInvoker = item("coverEditButton");
            auto* manageInvoker = coverInvoker && coverInvoker->isVisible()
                                      ? coverInvoker : item("detailManageButton");
            if (identifyPanel->property("opened").toBool() || !manageInvoker || !manageInvoker->hasActiveFocus()) {
              qCritical() << "Closing artwork did not restore the visible Manage control focus";
              application.exit(EXIT_FAILURE); return;
            }
            // Closing the on-screen keyboard and popup schedules layout polish. Check the
            // underlying page after that polish, not its intermediate row positions.
            QTimer::singleShot(50, &application, [rootWindow, &application, item] {
            auto* statusLayout = item("statusLayout");
            if (statusLayout && statusLayout->property("columns").toInt() == 5) {
              qreal rowY = -1;
              for (auto* child : statusLayout->childItems()) {
                if (!child->property("modelData").isValid()) continue;
                if (rowY < 0) rowY = child->y();
                if (qAbs(child->y() - rowY) > 1) {
                  qCritical("Status buttons do not share one row at wide widths");
                  application.exit(EXIT_FAILURE);
                  return;
                }
              }
            }
            auto* collectionsScroll = item("collectionsScroll");
            auto* newCollection = item("newCollectionButton");
            if (!collectionsScroll || !newCollection ||
                newCollection->height() > collectionsScroll->height()) {
              qCritical("Collection controls are clipped vertically");
              application.exit(EXIT_FAILURE);
              return;
            }
            auto* scroll = item("detailsScroll");
            auto* wiki = item("detailsBackButton");
            auto* details = item("gameDetails");
            auto* flickable = scroll ? scroll->property("navigationFlickable").value<QObject*>() : nullptr;
            if (!flickable || !wiki || !wiki->isVisible() || !details) {
              qCritical("Details title visibility fixture is missing");
              application.exit(EXIT_FAILURE);
              return;
            }
            flickable->setProperty("contentY", flickable->property("originY").toReal() + 100);
            wiki->forceActiveFocus();
            QMetaObject::invokeMethod(rootWindow, "revealNavigationItem",
                                     Q_ARG(QVariant, QVariant::fromValue(details)),
                                     Q_ARG(QVariant, QVariant::fromValue(wiki)));
            if (qAbs(flickable->property("contentY").toReal() - flickable->property("originY").toReal()) > 1) {
              qCritical("Returning to the top details control left the title scrolled away");
              application.exit(EXIT_FAILURE);
              return;
            }
            QObject* preferences =
                qmlContext(rootWindow)->contextProperty(QStringLiteral("Preferences")).value<QObject*>();
            auto* gameStop = qobject_cast<GameStopService*>(
                qmlContext(rootWindow)->contextProperty(QStringLiteral("GameStop")).value<QObject*>());
            if (!preferences || !gameStop) {
              qCritical("Details direction test could not inspect playtime or stop services");
              application.exit(EXIT_FAILURE);
              return;
            }
            auto stopProcesses = std::make_shared<QVector<ProcessSnapshot>>();
            gameStop->setSnapshotProvider([stopProcesses] { return *stopProcesses; });
            preferences->setProperty("trackPlaySessions", true);
            QVariantMap installation = details->property("selectedInstallation").toMap();
            const QString emulatorPath = QStringLiteral("/fixtures/details-direction.sfc");
            installation.insert(QStringLiteral("source"), QStringLiteral("RetroArch"));
            installation.insert(QStringLiteral("system"), QStringLiteral("snes"));
            installation.insert(QStringLiteral("installPath"), emulatorPath);
            details->setProperty("selectedInstallation", installation);
            details->setProperty("runningSessionsOverride", QVariantList{});
            QCoreApplication::processEvents();
            auto* actionGrid = item("gameActions");
            auto* stop = item("stopGameButton");
            auto* content = item("detailsContent");
            if (!actionGrid || !stop || !content || stop->isVisible() ||
                actionGrid->property("actionCount").toInt() != 4) {
              qCritical() << "An emulator game without an active session showed Stop Game"
                          << "button" << (stop ? stop->isVisible() : false)
                          << "available" << details->property("stopGameAvailable")
                          << "override" << details->property("runningSessionsOverride")
                          << "sessions" << details->property("runningSessions")
                          << "action count" << (actionGrid ? actionGrid->property("actionCount") : QVariant{});
              application.exit(EXIT_FAILURE);
              return;
            }
            const auto expectedColumns = [content, details](bool running) {
              return expectedDetailActionColumns(content->width(),
                                                 details->property("uiScale").toReal(), running);
            };
            if (actionGrid->property("columns").toInt() != expectedColumns(false)) {
              qCritical() << "The hidden Stop Game state used the wrong action columns"
                          << actionGrid->property("columns") << content->width();
              application.exit(EXIT_FAILURE);
              return;
            }
            details->setProperty(
                "runningSessionsOverride",
                QVariantList{QVariantMap{{QStringLiteral("source"), QStringLiteral("RetroArch")},
                                        {QStringLiteral("path"), emulatorPath},
                                        {QStringLiteral("stoppable"), true}}});
            QCoreApplication::processEvents();
            if (!stop->isVisible() || actionGrid->property("actionCount").toInt() != 5 ||
                actionGrid->property("columns").toInt() != expectedColumns(true)) {
              qCritical() << "An active emulator session did not expose the five-button action layout"
                          << stop->isVisible() << actionGrid->property("actionCount")
                          << actionGrid->property("columns") << content->width();
              application.exit(EXIT_FAILURE);
              return;
            }
            {
              QEventLoop actionLayout;
              QTimer::singleShot(50, &actionLayout, &QEventLoop::quit);
              actionLayout.exec();
            }
            if (const QString clipped = clippedDetailAction(content, actionGrid); !clipped.isEmpty()) {
              qCritical() << "The five-button action layout clipped" << clipped
                          << "at width" << content->width() << "scale" << details->property("uiScale")
                          << "columns" << actionGrid->property("columns");
              application.exit(EXIT_FAILURE);
              return;
            }
            details->setProperty(
                "runningSessionsOverride",
                QVariantList{QVariantMap{{QStringLiteral("source"), QStringLiteral("RetroArch")},
                                        {QStringLiteral("path"), emulatorPath},
                                        {QStringLiteral("stoppable"), false}}});
            QCoreApplication::processEvents();
            if (stop->isVisible()) {
              qCritical("A running session without a safe stop identity showed Stop Game");
              application.exit(EXIT_FAILURE);
              return;
            }
            details->setProperty("runningSessionsOverride", QVariantList{});
            installation.insert(QStringLiteral("source"), QStringLiteral("Steam"));
            installation.insert(QStringLiteral("installPath"), QStringLiteral("/fixtures/steam-game"));
            details->setProperty("selectedInstallation", installation);
            QCoreApplication::processEvents();
            if (stop->isVisible() || actionGrid->property("actionCount").toInt() != 4 ||
                actionGrid->property("columns").toInt() != expectedColumns(false)) {
              qCritical("An idle Steam game showed Stop Game without a recorder session");
              application.exit(EXIT_FAILURE);
              return;
            }
            ProcessSnapshot runningSteam;
            runningSteam.pid = 4242;
            runningSteam.procStart = 424200;
            runningSteam.comm = QStringLiteral("game");
            runningSteam.exePath = QStringLiteral("/fixtures/steam-game/bin/game");
            runningSteam.arguments = {runningSteam.exePath};
            stopProcesses->append(runningSteam);
            QMetaObject::invokeMethod(details, "refreshStopTarget");
            QCoreApplication::processEvents();
            if (!stop->isVisible() || actionGrid->property("actionCount").toInt() != 5 ||
                actionGrid->property("columns").toInt() != expectedColumns(true)) {
              qCritical("A detected Steam game did not show Stop Game");
              application.exit(EXIT_FAILURE);
              return;
            }
            stop->forceActiveFocus();
            preferences->setProperty("trackPlaySessions", false);
            QCoreApplication::processEvents();
            if (!stop->isVisible()) {
              qCritical("Disabling recording hid Stop Game for a detected Steam game");
              application.exit(EXIT_FAILURE);
              return;
            }
            stopProcesses->clear();
            QMetaObject::invokeMethod(details, "refreshStopTarget");
            QCoreApplication::processEvents();
            auto* manage = item("detailManageButton");
            if (stop->isVisible() || actionGrid->property("actionCount").toInt() != 4 ||
                !manage || !manage->hasActiveFocus()) {
              qCritical("Stop Game stayed visible or focus was lost after the Steam process exited");
              application.exit(EXIT_FAILURE);
              return;
            }
            installation.insert(QStringLiteral("source"), QStringLiteral("RetroArch"));
            installation.insert(QStringLiteral("installPath"), emulatorPath);
            details->setProperty("selectedInstallation", installation);
            QCoreApplication::processEvents();
            if (stop->isVisible()) {
              qCritical("An idle emulator showed Stop Game with recording disabled");
              application.exit(EXIT_FAILURE);
              return;
            }
            preferences->setProperty("trackPlaySessions", true);
            QCoreApplication::processEvents();
            if (stop->isVisible() || actionGrid->property("actionCount").toInt() != 4) {
              qCritical("Stop Game remained visible after restoring recording with no session");
              application.exit(EXIT_FAILURE);
              return;
            }
            application.exit(EXIT_SUCCESS);
            });
          }
        };
        (*step)();
      });
    } else if (navigationTest && !couchNavigationContract && !couchNavigationSweep && !startupNavigationTest) {
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
        // The app ignores the controller until its window is active; wait for that rather
        // than racing startup.
        quickWindow->requestActivate();
        QElapsedTimer activation;
        activation.start();
        // The library also restores its selection once its games load; start from a settled grid.
        const auto ready = [&] {
          return quickWindow->isActive() && controller.inputEnabled() &&
                 grid->property("count").toInt() > 0;
        };
        while (!ready() && activation.elapsed() < 3000)
          QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        while (activation.elapsed() < 3000) {
          grid->setProperty("currentIndex", 0);
          grid->forceActiveFocus();
          QElapsedTimer quiet;
          quiet.start();
          while (quiet.elapsed() < 150) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
          if (grid->hasActiveFocus() && grid->property("currentIndex").toInt() == 0) break;
        }
        if (!ready() || !grid->hasActiveFocus() || grid->property("currentIndex").toInt() != 0) {
          fail(QStringLiteral("Controller navigation test could not settle on the first game"));
          return;
        }
        controller.focusDirectionRequested(Qt::Key_Up);
        QTimer::singleShot(
            50, quickWindow,
            [quickWindow, &application, &controller, grid, search, fail, ownedLayoutTest] {
              if (grid->hasActiveFocus() || search->hasActiveFocus()) {
                fail(QStringLiteral("Controller Up did not move from the top row into the filters "
                                    "(focus %1, grid row %2, columns %3)")
                         .arg(quickWindow->activeFocusItem() ? quickWindow->activeFocusItem()->objectName() : QString())
                         .arg(grid->property("currentIndex").toInt())
                         .arg(grid->property("columns").toInt()));
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
              const auto item = [quickWindow](const char* name) {
                return quickWindow->findChild<QQuickItem*>(name);
              };
              const auto settle = [] {
                QEventLoop loop;
                QTimer::singleShot(50, &loop, &QEventLoop::quit);
                loop.exec();
              };
              const auto activate = [&controller, &settle](QQuickItem* control) {
                if (!control || !control->isVisible() || !control->isEnabled()) return false;
                control->forceActiveFocus();
                controller.acceptRequested();
                settle();
                return true;
              };
              const auto withinWindow = [quickWindow](QQuickItem* control) {
                if (!control || control->width() <= 0 || control->height() <= 0) return false;
                const auto p = control->mapToScene(QPointF(0, 0));
                return p.x() >= 0 && p.y() >= 0 && p.x() + control->width() <= quickWindow->width() + 1
                    && p.y() + control->height() <= quickWindow->height() + 1;
              };
              const auto opened = [quickWindow](const char* name) {
                auto* menu = quickWindow->findChild<QObject*>(name);
                return menu && menu->property("opened").toBool();
              };
              auto* sort = item("sortButton");
              auto* sources = item("sourcesMenuButton");
              auto* filters = item("filtersMenuButton");
              auto* view = item("viewMenuButton");
              auto* more = item("libraryMoreButton");
              auto* settings = item("settingsButton");
              for (auto* control : {sort, sources, filters, view, more, settings, search}) {
                if (!withinWindow(control)) { fail("Library toolbar extends outside the window"); return; }
              }
              auto* homeDestination = item("openHomeButton");
              auto* libraryDestination = item("libraryDestinationButton");
              auto* statsDestination = item("statsDestinationButton");
              auto* couchDestination = item("couchModeButton");
              if (!homeDestination || !libraryDestination || !statsDestination || !settings ||
                  !couchDestination) {
                fail("Library shared header destinations are incomplete"); return;
              }
              const auto moveHeader = [&controller, quickWindow](int direction, QQuickItem* expected) {
                controller.focusDirectionRequested(direction);
                return quickWindow->activeFocusItem() == expected;
              };
              libraryDestination->forceActiveFocus();
              if (!moveHeader(Qt::Key_Left, homeDestination) ||
                  !moveHeader(Qt::Key_Right, libraryDestination) ||
                  !moveHeader(Qt::Key_Right, statsDestination) ||
                  !moveHeader(Qt::Key_Right, settings) ||
                  !moveHeader(Qt::Key_Right, couchDestination) ||
                  !moveHeader(Qt::Key_Left, settings) ||
                  !moveHeader(Qt::Key_Left, statsDestination) ||
                  !moveHeader(Qt::Key_Left, libraryDestination)) {
                fail("Library shared header controller destinations are out of order"); return;
              }
              auto* recent = item(quickWindow->width() >= 1040
                                      ? "recentModeButton" : "narrowRecentModeButton");
              if (!recent || !recent->isVisible()) {
                fail("Library Recent control is missing"); return;
              }
              const int towardSearch = quickWindow->width() < 720 ? Qt::Key_Down : Qt::Key_Right;
              recent->forceActiveFocus();
              deliverKeyboardTestKey(quickWindow, towardSearch, Qt::NoModifier);
              settle();
              if (!search->hasActiveFocus()) {
                fail("Keyboard could not move from Recent to Search"); return;
              }
              recent->forceActiveFocus();
              controller.focusDirectionRequested(towardSearch);
              if (!search->hasActiveFocus()) {
                fail("Controller could not move from Recent to Search"); return;
              }
              auto* allMode = item(quickWindow->width() >= 1040
                                       ? "allModeButton" : "narrowAllModeButton");
              auto* favoritesMode = item(quickWindow->width() >= 1040
                                             ? "favoritesModeButton" : "narrowFavoritesModeButton");
              if (!allMode || !favoritesMode || !allMode->isVisible() || !favoritesMode->isVisible()) {
                fail("Library mode controls are incomplete"); return;
              }
              const auto move = [&controller, &settle, quickWindow](QQuickItem* from, int direction,
                                                                    QQuickItem* expected, bool keyboard) {
                from->forceActiveFocus();
                if (keyboard) deliverKeyboardTestKey(quickWindow, direction, Qt::NoModifier);
                else controller.focusDirectionRequested(direction);
                settle();
                return quickWindow->activeFocusItem() == expected;
              };
              for (const bool keyboard : {true, false}) {
                if (!move(allMode, Qt::Key_Right, favoritesMode, keyboard) ||
                    !move(favoritesMode, Qt::Key_Right, recent, keyboard) ||
                    !move(recent, towardSearch, search, keyboard)) {
                  fail(QStringLiteral("Library modes cannot reach Search using %1")
                           .arg(keyboard ? QStringLiteral("keyboard") : QStringLiteral("controller")));
                  return;
                }
                for (const auto& route : {
                         std::pair{allMode, quickWindow->width() < 720 ? search : sources},
                         std::pair{favoritesMode, quickWindow->width() < 720 ? search : filters},
                         std::pair{recent, quickWindow->width() < 720 ? search : sort}}) {
                  if (!move(route.first, Qt::Key_Down, route.second, keyboard)) {
                    fail(QStringLiteral("Library mode Down skipped its next control using %1")
                             .arg(keyboard ? QStringLiteral("keyboard") : QStringLiteral("controller")));
                    return;
                  }
                }
                for (auto* header : {homeDestination, libraryDestination, statsDestination,
                                     settings, couchDestination}) {
                  if (!move(header, Qt::Key_Down, search, keyboard)) {
                    fail(QStringLiteral("Library header Down skipped Search using %1")
                             .arg(keyboard ? QStringLiteral("keyboard") : QStringLiteral("controller")));
                    return;
                  }
                }
                for (const auto& route : {
                         std::pair{sources, filters}, std::pair{filters, sort},
                         std::pair{sort, view}, std::pair{view, more}}) {
                  if (!move(route.first, Qt::Key_Right, route.second, keyboard) ||
                      !move(route.second, Qt::Key_Left, route.first, keyboard)) {
                    fail(QStringLiteral("Library toolbar horizontal path skipped a control using %1")
                             .arg(keyboard ? QStringLiteral("keyboard") : QStringLiteral("controller")));
                    return;
                  }
                }
              }
              const QString fieldError = verifyEditorTextFields(quickWindow, search, controller);
              if (!fieldError.isEmpty()) { fail(fieldError); return; }
              auto* queryBar = item("libraryQueryBar");
              if (!queryBar) { fail("Library query bar is missing"); return; }
              const auto inQueryBar = [queryBar](QQuickItem* target) {
                for (auto* parent = target; parent; parent = parent->parentItem()) {
                  if (parent == queryBar) return true;
                }
                return false;
              };
              // Check every toolbar entry in both input paths. In particular,
              // View and More sit below Search and must not jump to the header.
              for (auto* control : {sources, filters, sort, view, more}) {
                for (const bool keyboard : {true, false}) {
                  control->forceActiveFocus();
                  if (keyboard) deliverKeyboardTestKey(quickWindow, Qt::Key_Up, Qt::NoModifier);
                  else controller.focusDirectionRequested(Qt::Key_Up);
                  settle();
                  auto* target = quickWindow->activeFocusItem();
                  if (!inQueryBar(target) ||
                      ((control == view || control == more) && target != search)) {
                    fail(QStringLiteral("Library toolbar Up skipped the query bar from %1 using %2")
                             .arg(control->objectName(), keyboard ? QStringLiteral("keyboard")
                                                                    : QStringLiteral("controller")));
                    return;
                  }
                }
              }
              grid->forceActiveFocus();
              controller.toolbarRequested();
              if (!sort->hasActiveFocus()) { fail("Controls did not enter the toolbar"); return; }
              controller.focusDirectionRequested(Qt::Key_Left);
              if (!filters->hasActiveFocus()) { fail("Toolbar left skipped Filters"); return; }
              if (!activate(sources) || !opened("librarySources")) { fail("Sources menu did not open"); return; }
              auto* allSources = item("allSourcesButton");
              auto* retro = item("retroArchSourceButton");
              auto* steam = item("steamSourceButton");
              auto* model = qmlContext(quickWindow)->contextProperty("Library").value<QObject*>();
              if (!model || !allSources || !retro || !steam || !allSources->hasActiveFocus()) {
                fail("Sources menu did not focus All sources"); return;
              }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Down, Qt::NoModifier); settle();
              if (allSources->hasActiveFocus()) { fail("Keyboard Down did not navigate Sources popup"); return; }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Up, Qt::NoModifier); settle();
              if (!allSources->hasActiveFocus()) { fail("Keyboard Up did not return to All sources"); return; }
              if (!activate(retro) || model->property("sourceFilters").toStringList() != QStringList{"RetroArch"}) {
                fail("Source selection changed behavior"); return;
              }
              steam->forceActiveFocus();
              deliverKeyboardTestKey(quickWindow, Qt::Key_Return, Qt::ShiftModifier);
              settle();
              if (model->property("sourceFilters").toStringList().size() != 2) {
                fail("Sources menu lost additive selection"); return;
              }
              controller.favoriteRequested();
              if (model->property("sourceFilters").toStringList() != QStringList{"RetroArch"}) {
                fail("Controller favorite did not remove a source"); return;
              }
              // Emulated filters by every emulator but reads as one chip.
              if (!activate(item("emulatedSourcesButton")) ||
                  model->property("sourceFilters").toStringList().size() < 2) {
                fail("Emulated did not select the emulator sources"); return;
              }
              QStringList sourceChips;
              for (const QVariant& chip : quickWindow->property("visibleLibraryFilters").toList()) {
                if (chip.toMap().value("key").toString() == "sourceFilters")
                  sourceChips.append(chip.toMap().value("label").toString());
              }
              if (sourceChips != QStringList{"Source: Emulated"}) {
                fail(QStringLiteral("Emulated showed source chips %1").arg(sourceChips.join(", "))); return;
              }
              activate(allSources);
              controller.backRequested(); settle();
              if (opened("librarySources") || !sources->hasActiveFocus()) { fail("Sources did not restore focus"); return; }
              // The Emulated chip counts once, and removing it clears every emulator.
              model->setProperty("sourceFilters", LibraryFilterModel::emulatorSources()); settle();
              if (sources->property("text").toString() != QStringLiteral("SOURCES (1)")) {
                fail(QStringLiteral("Emulated counted as %1").arg(sources->property("text").toString())); return;
              }
              // Repeater chips are visual children only, so search the item tree.
              auto* emulatedChip = findVisualItem(quickWindow->contentItem(), QStringLiteral("sourceFilterChip"));
              if (!activate(emulatedChip) || !model->property("sourceFilters").toStringList().isEmpty()) {
                fail(QStringLiteral("Removing the Emulated chip left filters %1 (chip %2)")
                         .arg(model->property("sourceFilters").toStringList().join(", "),
                              emulatedChip ? QStringLiteral("found") : QStringLiteral("missing"))); return;
              }
              if (!activate(filters) || !opened("libraryFilters")) { fail("Filters menu did not open"); return; }
              auto* repairInFilters = item("libraryRepairButton");
              if (repairInFilters && repairInFilters->isVisible()) {
                fail("Repair Library remained in the Filters popup"); return;
              }
              auto* filterStart = quickWindow->activeFocusItem();
              deliverKeyboardTestKey(quickWindow, Qt::Key_Down, Qt::NoModifier); settle();
              if (quickWindow->activeFocusItem() == filterStart) { fail("Keyboard Down did not navigate Filters popup"); return; }
              auto* hidden = item("hiddenModeButton");
              if (hidden && hidden->isVisible()) {
                activate(hidden);
                if (model->property("mode").toInt() != 3) { fail("Hidden games filter was not applied"); return; }
                activate(hidden);
                if (model->property("mode").toInt() != 0) { fail("Hidden games filter did not clear"); return; }
              }
              if (!activate(item("statusFilterButton")) || !quickWindow->property("filterPickerOpen").toBool()) {
                fail("Filters did not open the status picker"); return;
              }
              controller.backRequested(); settle();
              if (!opened("libraryFilters")) { fail("Value picker did not return to Filters"); return; }
              controller.backRequested(); settle();
              if (!filters->hasActiveFocus()) { fail("Filters did not restore its invoker"); return; }
              if (!activate(sort) || !opened("librarySort")) { fail("Sort menu did not open"); return; }
              controller.focusDirectionRequested(Qt::Key_Down); settle();
              controller.acceptRequested(); settle();
              if (model->property("sortMode").toInt() != 1 || !sort->hasActiveFocus()) { fail("Sort choice was not applied"); return; }
              model->setProperty("sortMode", 0);
              if (!activate(view) || !opened("libraryViewMenu")) { fail("View menu did not open"); return; }
              if (!activate(item("coverSizeButton"))) { fail("Cover size is unreachable"); return; }
              auto* slider = item("coverSizeSlider");
              if (!slider || !slider->hasActiveFocus()) { fail("Cover size did not focus its slider"); return; }
              const double size = slider->property("value").toDouble();
              deliverKeyboardTestKey(quickWindow, Qt::Key_Left, Qt::NoModifier);
              if (slider->property("value").toDouble() >= size) { fail("Cover size keyboard input failed"); return; }
              controller.focusDirectionRequested(Qt::Key_Right);
              controller.backRequested(); settle();
              if (!opened("libraryViewMenu")) { fail("Cover size did not return to View"); return; }
              controller.backRequested(); settle();
              if (!view->hasActiveFocus()) { fail("View did not restore focus"); return; }
              if (!activate(more) || !opened("libraryActions")) { fail("More did not open"); return; }
              auto* first = item("randomGameButton");
              if (!first || !first->hasActiveFocus()) { fail("More initial focus is unstable"); return; }
              for (const char* shortcutName : {"navigationTabForward", "navigationTabBackward"}) {
                first->forceActiveFocus();
                QSet<QString> visited;
                bool returned = false;
                for (int step = 0; step < 20; ++step) {
                  deliverKeyboardTestKey(quickWindow, QString(shortcutName) == "navigationTabForward" ? Qt::Key_Tab : Qt::Key_Backtab, Qt::NoModifier);
                  settle();
                  auto* focused = quickWindow->activeFocusItem();
                  if (!focused || !focused->isVisible() || !withinWindow(focused)) { fail("Menu Tab lost usable focus"); return; }
                  visited.insert(focused->objectName());
                  if (focused == first) { returned = true; break; }
                }
                if (!returned || !visited.contains("libraryRepairButton") ||
                    !visited.contains("bulkOrganizationButton") || !visited.contains("savedFiltersButton")
                    || !visited.contains("rescanButton") || !visited.contains("actionMenuCloseButton")) {
                  fail("Menu Tab skipped a command"); return;
                }
              }
              controller.backRequested(); settle();
              for (const char* command : {"bulkOrganizationButton", "savedFiltersButton"}) {
                activate(more);
                if (!activate(item(command))) { fail("Editor menu command unavailable"); return; }
                const char* state = QString(command) == "bulkOrganizationButton" ? "bulkOrganizationOpen" : "savedFiltersOpen";
                if (!quickWindow->property(state).toBool()) { fail("Menu did not open its editor"); return; }
                controller.backRequested(); settle();
                if (quickWindow->property(state).toBool() || !more->hasActiveFocus()) { fail("Editor did not return to More"); return; }
              }
              activate(more);
              QMetaObject::invokeMethod(quickWindow, "updateCouchMode", Q_ARG(QVariant, true), Q_ARG(QVariant, false));
              settle();
              if (opened("libraryActions")) { fail("Desktop menu remained open in Couch Mode"); return; }
              QMetaObject::invokeMethod(quickWindow, "updateCouchMode", Q_ARG(QVariant, false), Q_ARG(QVariant, false));
              settle();
              grid->setProperty("currentIndex", 0);
              if (!activate(settings)) { fail("Settings is unreachable"); return; }
              QTimer::singleShot(
                  100, quickWindow,
                  [quickWindow, &application, &controller, grid, fail] {
                    if (!quickWindow->property("diagnosticsOpen").toBool() ||
                        quickWindow->activeFocusItem() == nullptr) {
                      fail(QStringLiteral("Controller Open did not enter Settings"));
                      return;
                    }
                    auto* settingsPage = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
                    auto* settingsScroll = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
                    if (settingsPage == nullptr || settingsScroll == nullptr) {
                      fail(QStringLiteral("Settings opened without its controls"));
                      return;
                    }
                    // Choosing a section, as its button does, starts on that section's first control.
                    QMetaObject::invokeMethod(settingsPage, "chooseSection", Q_ARG(QVariant, QVariant(1)));
                    QEventLoop settingsLayout;
                    QTimer::singleShot(50, &settingsLayout, &QEventLoop::quit);
                    settingsLayout.exec();
                    QQuickItem* firstControl = settingsScroll->nextItemInFocusChain(true);
                    for (int hops = 0; firstControl && hops < 300 &&
                                       !(settingsScroll->isAncestorOf(firstControl) && firstControl->isVisible() &&
                                         firstControl->isEnabled() && firstControl->activeFocusOnTab());
                         ++hops) {
                      firstControl = firstControl->nextItemInFocusChain(true);
                    }
                    QQuickItem* settingsStart = quickWindow->activeFocusItem();
                    if (firstControl == nullptr || settingsStart != firstControl) {
                      fail(QStringLiteral("Choosing a Settings section did not focus its first control (%1)")
                               .arg(settingsStart ? settingsStart->objectName() : QStringLiteral("nothing")));
                      return;
                    }
                    for (int step = 0; step < 30; ++step) {
                      controller.focusDirectionRequested(Qt::Key_Down);
                    }
                    if (quickWindow->activeFocusItem() == settingsStart ||
                        settingsScroll->property("navigationContentY").toReal() <= 0) {
                      fail(QStringLiteral("Controller Down did not traverse and scroll Settings"));
                      return;
                    }
                    controller.backRequested();
                    QTimer::singleShot(50, quickWindow, [quickWindow, &application, &controller, grid, fail, settingsPage] {
                      if (quickWindow->property("diagnosticsOpen").toBool()) {
                        fail(QStringLiteral("Controller Back did not close Settings"));
                        return;
                      }
                      // Connect in game details picks Connections and then opens Settings.
                      // Opening must not step past the section's first control.
                      settingsPage->setProperty("section", 2);
                      quickWindow->setProperty("diagnosticsOpen", true);
                      QEventLoop connectionsLayout;
                      QTimer::singleShot(50, &connectionsLayout, &QEventLoop::quit);
                      connectionsLayout.exec();
                      QQuickItem* connectionsStart = quickWindow->activeFocusItem();
                      if (connectionsStart == nullptr ||
                          connectionsStart->objectName() != QStringLiteral("rommConnectionToggle")) {
                        fail(QStringLiteral("Opening Settings on Connections did not focus its first control (%1)")
                                 .arg(connectionsStart ? connectionsStart->objectName() : QStringLiteral("nothing")));
                        return;
                      }
                      quickWindow->setProperty("diagnosticsOpen", false);
                      QEventLoop connectionsClosed;
                      QTimer::singleShot(50, &connectionsClosed, &QEventLoop::quit);
                      connectionsClosed.exec();
                      controller.toolbarRequested();
                      if (!grid->hasActiveFocus()) {
                        fail(QStringLiteral("Controller Controls did not return to the game grid"));
                        return;
                      }
                      controller.acceptRequested();
                      QTimer::singleShot(100, quickWindow, [quickWindow, &application, &controller, fail] {
                        auto* play =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("playButton"));
                        auto* favorite =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("favoriteButton"));
                        auto* queue =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("addToQueueButton"));
                        auto* stop =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("stopGameButton"));
                        auto* manage =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("detailManageButton"));
                        auto* gameActions =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("gameActions"));
                        if (!quickWindow->property("detailOpen").toBool() || play == nullptr ||
                            favorite == nullptr || queue == nullptr || stop == nullptr ||
                            manage == nullptr || gameActions == nullptr || !play->hasActiveFocus()) {
                          fail(QStringLiteral("Game details did not focus Play"));
                          return;
                        }
                        const auto sendKey = [quickWindow](int key) {
                          QCoreApplication::postEvent(
                              quickWindow,
                              new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
                          QCoreApplication::postEvent(
                              quickWindow,
                              new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
                          QEventLoop eventLoop;
                          QTimer::singleShot(30, &eventLoop, &QEventLoop::quit);
                          eventLoop.exec();
                        };
                        sendKey(Qt::Key_Right);
                        if (!favorite->hasActiveFocus()) {
                          QQuickItem* focused = quickWindow->activeFocusItem();
                          fail(QStringLiteral("Keyboard Right did not move from Play to Favorite; "
                                              "focused %1")
                                   .arg(focused ? focused->objectName()
                                                : QStringLiteral("nothing")));
                          return;
                        }
                        sendKey(Qt::Key_Left);
                        if (!play->hasActiveFocus()) {
                          QQuickItem* focused = quickWindow->activeFocusItem();
                          fail(QStringLiteral("Keyboard Left did not return from Favorite to Play; "
                                              "focused %1")
                                   .arg(focused ? focused->objectName()
                                                : QStringLiteral("nothing")));
                          return;
                        }
                        auto* details = quickWindow->findChild<QQuickItem*>("gameDetails");
                        auto* detailsContent = quickWindow->findChild<QQuickItem*>("detailsContent");
                        if (!details || !detailsContent) {
                          fail("Game details is missing its action content"); return;
                        }
                        // Favorite is a toggle: checked, FAVORITED and a filled accent heart only
                        // while the game is a favorite. Toggle it both ways and back.
                        QObject* theme = qmlContext(quickWindow)
                                             ->contextProperty(QStringLiteral("Theme"))
                                             .value<QObject*>();
                        auto* favoriteHeart = favorite->findChild<QQuickItem*>("favoriteHeart");
                        if (!theme || !favoriteHeart) {
                          fail("Favorite action has no drawn heart"); return;
                        }
                        const auto favoriteShows = [favorite, favoriteHeart, theme](bool on) {
                          const QColor heartColor = favoriteHeart->property("color").value<QColor>();
                          const QColor accent = theme->property("accent").value<QColor>();
                          return favorite->property("checkable").toBool() &&
                                 favorite->property("checked").toBool() == on &&
                                 favorite->property("text").toString() ==
                                     (on ? QStringLiteral("FAVORITED")
                                         : QStringLiteral("ADD TO FAVORITES")) &&
                                 favoriteHeart->property("filled").toBool() == on &&
                                 (heartColor == accent) == on;
                        };
                        const bool wasFavorite = favorite->property("checked").toBool();
                        for (const bool on : {wasFavorite, !wasFavorite, wasFavorite}) {
                          if (on != favorite->property("checked").toBool()) {
                            QMetaObject::invokeMethod(favorite, "clicked");
                            QCoreApplication::processEvents();
                            if (quickWindow->findChild<QQuickItem*>("favoriteButton") != favorite) {
                              fail("Toggling favorite closed game details"); return;
                            }
                          }
                          if (!favoriteShows(on)) {
                            fail(on ? QStringLiteral("Favorite action does not show a favorite")
                                    : QStringLiteral("Favorite action shows a favorite that is not one"));
                            return;
                          }
                        }
                        QObject* preferences = qmlContext(quickWindow)
                                                  ->contextProperty(QStringLiteral("Preferences"))
                                                  .value<QObject*>();
                        if (!preferences) { fail("Game details test has no preferences"); return; }
                        preferences->setProperty("trackPlaySessions", true);
                        QVariantMap emulatorInstallation =
                            details->property("selectedInstallation").toMap();
                        emulatorInstallation.insert(QStringLiteral("source"), QStringLiteral("RetroArch"));
                        emulatorInstallation.insert(QStringLiteral("system"), QStringLiteral("snes"));
                        emulatorInstallation.insert(QStringLiteral("installPath"),
                                                     QStringLiteral("/fixtures/controller-navigation.sfc"));
                        details->setProperty("selectedInstallation", emulatorInstallation);
                        details->setProperty("runningSessionsOverride", QVariantList{});
                        QCoreApplication::processEvents();
                        if (stop->isVisible() ||
                            gameActions->property("actionCount").toInt() != 4) {
                          fail("A game without an active session showed Stop Game"); return;
                        }
                        const auto expectedColumns = [detailsContent, details](bool running) {
                          return expectedDetailActionColumns(
                              detailsContent->width(), details->property("uiScale").toReal(), running);
                        };
                        const auto moveControllerFocus =
                            [quickWindow, &controller, fail](int direction, QQuickItem* expected,
                                                             const QString& message) {
                              controller.focusDirectionRequested(direction);
                              QCoreApplication::processEvents();
                              if (!expected->hasActiveFocus()) {
                                QQuickItem* focused = quickWindow->activeFocusItem();
                                fail(message + QStringLiteral("; focused %1")
                                                    .arg(focused ? focused->objectName()
                                                                 : QStringLiteral("nothing")));
                                return false;
                              }
                              return true;
                            };
                        const int hiddenColumns = gameActions->property("columns").toInt();
                        if (hiddenColumns != expectedColumns(false)) {
                          fail(QStringLiteral("Hidden Stop Game used %1 columns instead of %2")
                                   .arg(hiddenColumns).arg(expectedColumns(false)));
                          return;
                        }
                        QQuickItem* hiddenChain[] = {play, favorite, queue, manage};
                        for (int index = 1; index < 4; ++index) {
                          if (!moveControllerFocus(Qt::Key_Right, hiddenChain[index],
                                                   QStringLiteral("Hidden action chain skipped %1")
                                                       .arg(hiddenChain[index]->objectName()))) return;
                        }
                        for (int index = 2; index >= 0; --index) {
                          if (!moveControllerFocus(Qt::Key_Left, hiddenChain[index],
                                                   QStringLiteral("Hidden action chain missed %1")
                                                       .arg(hiddenChain[index]->objectName()))) return;
                        }
                        if (hiddenColumns == 1) {
                          for (int index = 1; index < 4; ++index) {
                            if (!moveControllerFocus(Qt::Key_Down, hiddenChain[index],
                                                     QStringLiteral("Hidden single-column chain skipped %1")
                                                         .arg(hiddenChain[index]->objectName()))) return;
                          }
                          for (int index = 2; index >= 0; --index) {
                            if (!moveControllerFocus(Qt::Key_Up, hiddenChain[index],
                                                     QStringLiteral("Hidden single-column chain missed %1")
                                                         .arg(hiddenChain[index]->objectName()))) return;
                          }
                        } else if (hiddenColumns == 2) {
                          if (!moveControllerFocus(Qt::Key_Down, queue,
                                                   QStringLiteral("Hidden two-column chain skipped Up Next")) ||
                              !moveControllerFocus(Qt::Key_Right, manage,
                                                   QStringLiteral("Hidden two-column chain skipped Manage")) ||
                              !moveControllerFocus(Qt::Key_Up, favorite,
                                                   QStringLiteral("Hidden two-column chain missed Favorite")) ||
                              !moveControllerFocus(Qt::Key_Left, play,
                                                   QStringLiteral("Hidden two-column chain missed Play"))) return;
                        }
                        QVariantMap installation = details->property("selectedInstallation").toMap();
                        const QVariantMap game = details->property("game").toMap();
                        const QString source = installation.value(QStringLiteral("source"),
                                                                   game.value(QStringLiteral("source"))).toString();
                        QString path = installation.value(QStringLiteral("installPath")).toString();
                        if (path.isEmpty()) path = game.value(QStringLiteral("installPath")).toString();
                        if (path.isEmpty()) path = QStringLiteral("/fixtures/controller-navigation.rom");
                        installation.insert(QStringLiteral("source"), source);
                        installation.insert(QStringLiteral("installPath"), path);
                        details->setProperty("selectedInstallation", installation);
                        details->setProperty(
                            "runningSessionsOverride",
                            QVariantList{QVariantMap{{QStringLiteral("source"), source},
                                                    {QStringLiteral("path"), path},
                                                    {QStringLiteral("stoppable"), true}}});
                        QEventLoop actionLayout;
                        QTimer::singleShot(50, &actionLayout, &QEventLoop::quit);
                        actionLayout.exec();
                        const int columns = gameActions->property("columns").toInt();
                        if (!stop->isVisible() || gameActions->property("actionCount").toInt() != 5 ||
                            columns != expectedColumns(true)) {
                          fail(QStringLiteral("An active session did not expose the five-button layout"));
                          return;
                        }
                        if (const QString clipped = clippedDetailAction(detailsContent, gameActions);
                            !clipped.isEmpty()) {
                          fail(QStringLiteral("The five-button layout clipped %1 at width %2 in %3 columns")
                                   .arg(clipped).arg(detailsContent->width()).arg(columns));
                          return;
                        }
                        QQuickItem* actionChain[] = {play, favorite, queue, stop, manage};
                        for (int index = 1; index < 5; ++index) {
                          if (!moveControllerFocus(Qt::Key_Right, actionChain[index],
                                                   QStringLiteral("Running action chain skipped %1")
                                                       .arg(actionChain[index]->objectName()))) return;
                        }
                        for (int index = 3; index >= 0; --index) {
                          if (!moveControllerFocus(Qt::Key_Left, actionChain[index],
                                                   QStringLiteral("Running action chain missed %1")
                                                       .arg(actionChain[index]->objectName()))) return;
                        }
                        if (columns == 1) {
                          for (int index = 1; index < 5; ++index) {
                            if (!moveControllerFocus(Qt::Key_Down, actionChain[index],
                                                     QStringLiteral("Running single-column chain skipped %1")
                                                         .arg(actionChain[index]->objectName()))) return;
                          }
                          for (int index = 3; index >= 0; --index) {
                            if (!moveControllerFocus(Qt::Key_Up, actionChain[index],
                                                     QStringLiteral("Running single-column chain missed %1")
                                                         .arg(actionChain[index]->objectName()))) return;
                          }
                        } else if (columns == 2) {
                          if (!moveControllerFocus(Qt::Key_Down, queue,
                                                   QStringLiteral("Running two-column chain skipped Up Next")) ||
                              !moveControllerFocus(Qt::Key_Right, stop,
                                                   QStringLiteral("Running two-column chain skipped Stop")) ||
                              !moveControllerFocus(Qt::Key_Down, manage,
                                                   QStringLiteral("Running two-column chain skipped Manage")) ||
                              !moveControllerFocus(Qt::Key_Up, queue,
                                                   QStringLiteral("Running two-column chain missed Up Next")) ||
                              !moveControllerFocus(Qt::Key_Up, play,
                                                   QStringLiteral("Running two-column chain missed Play")) ||
                              !moveControllerFocus(Qt::Key_Right, favorite,
                                                   QStringLiteral("Running two-column chain missed Favorite")) ||
                              !moveControllerFocus(Qt::Key_Down, stop,
                                                   QStringLiteral("Running two-column chain skipped Stop")) ||
                              !moveControllerFocus(Qt::Key_Up, favorite,
                                                   QStringLiteral("Running two-column chain missed Favorite")) ||
                              !moveControllerFocus(Qt::Key_Left, play,
                                                   QStringLiteral("Running two-column chain missed Play"))) return;
                        } else if (columns == 4) {
                          if (!moveControllerFocus(Qt::Key_Down, manage,
                                                   QStringLiteral("Running four-column chain missed Manage")) ||
                              !moveControllerFocus(Qt::Key_Up, play,
                                                   QStringLiteral("Running four-column chain missed Play"))) return;
                        }
                        if (!play->hasActiveFocus()) {
                          fail(QStringLiteral("Controller could not reverse through game actions"));
                          return;
                        }
                        for (const char* name : {"favoriteButton", "addToQueueButton",
                                                 "stopGameButton", "detailManageButton"}) {
                          auto* action = quickWindow->findChild<QQuickItem*>(name);
                          if (!action || qAbs(action->width() - play->width()) > 1) {
                            qCritical() << "Game action widths" << name << (action ? action->width() : -1)
                                        << play->width() << "columns" << columns
                                        << "visible" << (action ? action->isVisible() : false);
                            fail("Game action buttons have unequal widths"); return;
                          }
                        }
                        details->setProperty("runningSessionsOverride", QVariantList{});
                        QCoreApplication::processEvents();
                        auto* manageMenuButton = quickWindow->findChild<QQuickItem*>("detailManageButton");
                        manageMenuButton->forceActiveFocus();
                        controller.acceptRequested();
                        QCoreApplication::processEvents();
                        auto* managePopup = quickWindow->findChild<QObject*>("detailManageMenu");
                        if (!managePopup || !managePopup->property("opened").toBool()) { fail("Game Manage menu did not open"); return; }
                        auto* firstAction = quickWindow->activeFocusItem();
                        for (int step = 0; step < 12; ++step) controller.focusDirectionRequested(Qt::Key_Down);
                        if (!firstAction || quickWindow->activeFocusItem() == firstAction) { fail("Game Manage menu did not traverse"); return; }
                        controller.backRequested();
                        QCoreApplication::processEvents();
                        if (!manageMenuButton->hasActiveFocus() || !quickWindow->property("detailOpen").toBool()) { fail("Game Manage Back lost detail context"); return; }
                        play->forceActiveFocus();
                        deliverKeyboardTestKey(quickWindow, Qt::Key_Up, Qt::NoModifier);
                        QTimer::singleShot(
                            50, quickWindow, [quickWindow, &application, &controller, play, fail] {
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
                              deliverKeyboardTestKey(quickWindow, Qt::Key_Down, Qt::NoModifier);
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
                                    deliverKeyboardTestKey(quickWindow, Qt::Key_Right, Qt::NoModifier);
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
                                          deliverKeyboardTestKey(quickWindow, Qt::Key_Left, Qt::NoModifier);
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
                                                auto* metadataArtwork =
                                                    quickWindow->findChild<QQuickItem*>(
                                                        QStringLiteral("metadataArtworkButton"));
                                                newCollection->forceActiveFocus();
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                // The rating and cover art section sits between
                                                // the collections row and the insights row. It
                                                // was missing from the chain, so down off
                                                // collections jumped clean over it and nothing
                                                // in it could be reached with a controller.
                                                if (metadataArtwork != nullptr &&
                                                    metadataArtwork->isVisible()) {
                                                  if (!metadataArtwork->hasActiveFocus()) {
                                                    fail(QStringLiteral(
                                                        "Controller Down skipped the rating and "
                                                        "cover art section"));
                                                    return;
                                                  }
                                                  controller.focusDirectionRequested(Qt::Key_Up);
                                                  if (!newCollection->hasActiveFocus()) {
                                                    fail(QStringLiteral(
                                                        "Controller Up did not leave the cover "
                                                        "art section for collections"));
                                                    return;
                                                  }
                                                  controller.focusDirectionRequested(Qt::Key_Down);
                                                  controller.focusDirectionRequested(Qt::Key_Down);
                                                }
                                                if (!insightRefresh->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Down left the detail content "
                                                      "flow after collections"));
                                                  return;
                                                }
                                                // And back up into the section rather than over
                                                // it, so it is not a one-way trip.
                                                if (metadataArtwork != nullptr &&
                                                    metadataArtwork->isVisible()) {
                                                  controller.focusDirectionRequested(Qt::Key_Up);
                                                  if (!metadataArtwork->hasActiveFocus()) {
                                                    fail(QStringLiteral(
                                                        "Controller Up skipped the rating and "
                                                        "cover art section"));
                                                    return;
                                                  }
                                                  controller.focusDirectionRequested(Qt::Key_Down);
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                if (!achievementSort->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Down did not reach achievement "
                                                      "sorting"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Right);
                                                if (!achievementRefresh->hasActiveFocus()) {
                                                  fail(QStringLiteral(
                                                      "Controller Right did not reach Steam "
                                                      "achievement refresh"));
                                                  return;
                                                }
                                                controller.focusDirectionRequested(Qt::Key_Left);
                                                if (!achievementSort->hasActiveFocus()) {
                                                  fail(QStringLiteral("Controller Left did not "
                                                                      "return to achievement "
                                                                      "sorting"));
                                                  return;
                                                }
                                                const qreal initialContentY =
                                                    detailsScroll->property("navigationContentY")
                                                        .toReal();
                                                controller.focusDirectionRequested(Qt::Key_Down);
                                                QQuickItem* firstAchievement =
                                                    quickWindow->activeFocusItem();
                                                if (firstAchievement == nullptr ||
                                                    !firstAchievement->objectName().startsWith(
                                                        QStringLiteral("achievementCard"))) {
                                                  fail(QStringLiteral(
                                                           "Controller Down did not enter the "
                                                           "achievement list; focused %1")
                                                           .arg(firstAchievement
                                                                    ? firstAchievement->objectName()
                                                                    : QStringLiteral("nothing")));
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
                                                controller.backRequested();
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
                                                      deliverKeyboardTestKey(QGuiApplication::focusWindow(), Qt::Key_F, Qt::ControlModifier);
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
                                                            deliverKeyboardTestKey(quickWindow, Qt::Key_Escape,
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
                                                                  deliverKeyboardTestKey(QGuiApplication::focusWindow(), Qt::Key_F6, Qt::NoModifier);
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
                                                                        deliverKeyboardTestKey(QGuiApplication::focusWindow(), Qt::Key_F6, Qt::NoModifier);
                                                                        QTimer::singleShot(
                                                                            50, quickWindow,
                                                                            [grid, quickWindow, &application, &controller, fail] {
  if (!grid->hasActiveFocus()) {
    fail(QStringLiteral("Keyboard F6 did not return to the library grid"));
    return;
  }
  // The organize filters open a picker list. Return opens it on the current value and
  // Escape closes it and hands focus back to the button.
  auto* statusFilter =
      quickWindow->findChild<QQuickItem*>(QStringLiteral("statusFilterButton"));
  auto* picker =
      quickWindow->findChild<QQuickItem*>(QStringLiteral("filterPickerOverlay"));
  if (statusFilter == nullptr || picker == nullptr) {
    fail(QStringLiteral("Navigation test could not find the filter picker"));
    return;
  }
  auto* filtersMenu = quickWindow->findChild<QQuickItem*>("filtersMenuButton");
  if (!filtersMenu || !QMetaObject::invokeMethod(filtersMenu, "clicked")) { fail("Filters menu missing"); return; }
  statusFilter->forceActiveFocus();
  controller.acceptRequested();
  QTimer::singleShot(
      80, quickWindow, [quickWindow, statusFilter, picker, &application, &controller, fail] {
        bool focusInsidePicker = false;
        for (QQuickItem* item = quickWindow->activeFocusItem(); item != nullptr;
             item = item->parentItem()) {
          focusInsidePicker = focusInsidePicker || item == picker;
        }
        if (!quickWindow->property("filterPickerOpen").toBool() || !picker->isVisible() ||
            !focusInsidePicker) {
          fail(QStringLiteral("Return did not open the status filter picker with focus"));
          return;
        }
        controller.backRequested();
        QTimer::singleShot(80, quickWindow, [quickWindow, statusFilter, &application, fail] {
          auto* filters = quickWindow->findChild<QObject*>("libraryFilters");
          if (quickWindow->property("filterPickerOpen").toBool() || !filters || !filters->property("opened").toBool()) {
            fail(QStringLiteral("Escape did not return to the Filters menu")); return;
          }
          QMetaObject::invokeMethod(filters, "close");
          runEmptyFilterFocusTest(quickWindow, &application);
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
            });
      });
    }
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
  if (retroArchLibrary != nullptr && preferences.retroArchEnabled() && !consolePortalTest) {
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

#include "selftest/SelfTest.h"

#include "achievements/AchievementModel.h"
#include "app/AppSettings.h"
#include "app/CommandLine.h"
#include "app/SelfTestSupport.h"
#include "backup/BackupManager.h"
#include "input/ControllerInput.h"
#include "launch/GameLauncher.h"
#include "library/GameRoles.h"
#include "library/GameStopService.h"
#include "library/HomeModel.h"
#include "library/LauncherGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LibraryRepair.h"
#include "library/ManualGameModel.h"
#include "library/UnifiedGameModel.h"
#include "library/UserDateFormat.h"
#include "metadata/GameMetadata.h"
#include "selftest/KeyboardTestInput.h"
#include "sources/ports/PortCatalog.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPainter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSize>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include <atomic>
#include <functional>
#include <memory>

namespace SelfTest {

std::optional<int> renderWindow(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  AppSettings& preferences = *app.preferences;
  ControllerInput& controller = *app.controller;
  LauncherGameModel*& kytyLibrary = *app.kytyLibrary;
  LauncherGameModel*& nativePortLibrary = *app.nativePortLibrary;
  QString& libraryDatabasePath = *app.libraryDatabasePath;
  ManualGameModel& manualGames = *app.manualGames;
  UnifiedGameModel& unifiedGames = *app.unifiedGames;
  LibraryFilterModel& library = *app.library;
  std::unique_ptr<GameMetadata>& gameMetadata = *app.gameMetadata;
  GameLauncher& launcher = *app.launcher;
  LibraryRepair& libraryRepair = *app.libraryRepair;
  GameStopService& gameStop = *app.gameStop;
  BackupManager& backups = *app.backups;
  HomeModel& home = *app.home;
  QQuickWindow*& quickWindow = app.quickWindow;
  const QString& screenshotPath = harness.options.screenshotPath;
  const QString& renderOverlay = harness.options.renderOverlay;
  const QString& cardExportPath = harness.options.cardExportPath;
  const bool& statsFixture = harness.options.statsFixture;
  const bool& repairNavigationFixture = harness.options.repairNavigationFixture;
  const bool& heroicOwnedFixture = harness.options.heroicOwnedFixture;
  const bool& sourceRescanFixture = harness.options.sourceRescanFixture;
  const QString& heroicOwnedDispatchLog = harness.fixtures.heroicOwnedDispatchLog;
  const QString& saveFixtureGame = harness.fixtures.saveFixtureGame;
  QString& backupFixturePath = harness.fixtures.backupFixturePath;
  qint64& nowPlayingFixturePid = harness.fixtures.nowPlayingFixturePid;
  bool& heroicOwnedDispatchComplete = harness.fixtures.heroicOwnedDispatchComplete;
  std::shared_ptr<std::atomic<int>>& kytyRefreshes = harness.fixtures.kytyRefreshes;
  std::shared_ptr<std::atomic<int>>& portRefreshes = harness.fixtures.portRefreshes;
  std::unique_ptr<QTemporaryDir>& relocationFixtureDirectory = harness.fixtures.relocationFixtureDirectory;
  std::unique_ptr<QStandardItemModel>& relocationFixture = harness.fixtures.relocationFixture;
    if (sourceRescanFixture) {
      // Exercise the real QML action with synthetic scanners, including disabled sources.
      auto* check = new QTimer(quickWindow);
      check->setInterval(20);
      auto phase = std::make_shared<int>(0);
      auto ticks = std::make_shared<int>(0);
      QObject::connect(check, &QTimer::timeout, quickWindow,
                       [&, quickWindow, check, phase, ticks, kytyRefreshes, portRefreshes] {
        if (++*ticks > 150) {
          qCritical() << "Source rescan fixture timed out";
          application.exit(EXIT_FAILURE);
          check->stop();
          return;
        }
        if (kytyLibrary->scanning() || nativePortLibrary->scanning()) return;
        const int expectedKyty = *phase >= 1 ? 1 : 0;
        const int expectedPorts = *phase >= 2 ? 1 : 0;
        if (kytyRefreshes->load() != expectedKyty || portRefreshes->load() != expectedPorts) {
          qCritical() << "Global rescan ignored source enablement" << *phase
                      << kytyRefreshes->load() << portRefreshes->load();
          application.exit(EXIT_FAILURE);
          check->stop();
          return;
        }
        if (*phase == 3) {
          quickWindow->setProperty("sourceRescanComplete", true);
          check->stop();
          return;
        }
        preferences.setKytyEnabled(*phase == 0);
        preferences.setNativePortsEnabled(*phase == 1);
        ++*phase;
        if (!QMetaObject::invokeMethod(quickWindow, "rescanLibraries")) {
          application.exit(EXIT_FAILURE);
          check->stop();
        }
      });
      check->start();
    }
    if (renderOverlay.startsWith(QStringLiteral("stop-"))) {
      // A fixture, so the confirmation renders the same list every run and the
      // confirm action signals nothing: the sink here answers Done and touches
      // no process, and liveness lets each target go on the post-check.
      static NoSignalSink noSignalSink;
      const QString fixtureInstall = QStringLiteral("/fixtures/stopped-game");
      const QString fixtureRom = QStringLiteral("/fixtures/Zelda.wua");
      gameStop.setSignalSink(&noSignalSink);
      gameStop.setGracePeriodMs(0);
      // A static counter: the liveness callback outlives this block, so a local
      // it captured by reference would dangle the moment the block exits.
      static int stopFixtureReads = 0;
      stopFixtureReads = 0;
      gameStop.setLiveness([](qint64, qint64) { return ++stopFixtureReads % 3 != 0; });
      gameStop.setSnapshotProvider([fixtureInstall, fixtureRom] {
        QVector<ProcessSnapshot> processes;
        ProcessSnapshot running;
        running.pid = 4242;
        running.procStart = 424200;
        running.comm = QStringLiteral("fixture-game");
        running.arguments = {QStringLiteral("fixture-game")};
        running.exePath = fixtureInstall + QStringLiteral("/fixture-game");
        processes.append(running);
        ProcessSnapshot emulator;
        emulator.pid = 4243;
        emulator.procStart = 424300;
        emulator.comm = QStringLiteral("cemu");
        emulator.arguments = {QStringLiteral("/usr/bin/cemu"), QStringLiteral("-g"), fixtureRom};
        processes.append(emulator);
        return processes;
      });
      gameStop.setRowsProvider([fixtureInstall, fixtureRom] {
        QVariantList rows;
        rows.append(QVariantMap{{QStringLiteral("title"), QStringLiteral("Stopped Game")},
                                {QStringLiteral("source"), QStringLiteral("Manual")},
                                {QStringLiteral("appId"), QStringLiteral("fixture")},
                                {QStringLiteral("installPath"), fixtureInstall}});
        rows.append(QVariantMap{{QStringLiteral("title"), QStringLiteral("Zelda")},
                                {QStringLiteral("source"), QStringLiteral("Cemu")},
                                {QStringLiteral("appId"), QStringLiteral("fixture-cemu")},
                                {QStringLiteral("installPath"), fixtureRom}});
        return rows;
      });
    }
    if (renderOverlay.startsWith(QStringLiteral("stop-game"))) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(160, quickWindow, [quickWindow, renderOverlay, &application, &gameStop] {
        auto* details = quickWindow->findChild<QObject*>(QStringLiteral("gameDetails"));
        auto* button = quickWindow->findChild<QQuickItem*>(QStringLiteral("stopGameButton"));
        if (!details || !button) {
          qCritical() << "The stop control is missing from the details screen";
          application.exit(EXIT_FAILURE);
          return;
        }
        details->setProperty("selectedInstallation",
                             QVariantMap{{QStringLiteral("source"), QStringLiteral("Manual")},
                                         {QStringLiteral("appId"), QStringLiteral("fixture")},
                                         {QStringLiteral("installPath"),
                                          QStringLiteral("/fixtures/stopped-game")}});
        details->setProperty("runningSessionsOverride",
                             QVariantList{QVariantMap{{QStringLiteral("source"), QStringLiteral("Manual")},
                                                     {QStringLiteral("path"), QStringLiteral("/fixtures/stopped-game")},
                                                     {QStringLiteral("stoppable"), true}}});
        if (!button->isVisible()) {
          qCritical() << "Fixture running session did not expose Stop Game";
          application.exit(EXIT_FAILURE);
          return;
        }
        QMetaObject::invokeMethod(button, "clicked");
        QTimer::singleShot(140, quickWindow, [quickWindow, renderOverlay, &application, &gameStop] {
          auto* panel = quickWindow->findChild<QObject*>(QStringLiteral("detailgameStopPanel"));
          auto* cancel =
              quickWindow->findChild<QQuickItem*>(QStringLiteral("detailcancelStop"));
          auto* confirm =
              quickWindow->findChild<QQuickItem*>(QStringLiteral("detailconfirmStop"));
          if (!panel || !cancel || !confirm) {
            qCritical() << "The stop confirmation is missing its actions";
            application.exit(EXIT_FAILURE);
            return;
          }
          if (!panel->property("opened").toBool() || !cancel->hasActiveFocus() ||
              panel->property("targets").toList().isEmpty()) {
            qCritical() << "The stop confirmation did not open with a safe cancel and a listed "
                           "target";
            application.exit(EXIT_FAILURE);
            return;
          }
          if (renderOverlay.endsWith(QStringLiteral("refused"))) {
            gameStop.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
            QMetaObject::invokeMethod(confirm, "clicked");
            if (panel->property("pending").toBool() || panel->property("resultMessage").toString().isEmpty()) {
              qCritical() << "A refused stop left the dialog pending";
              application.exit(EXIT_FAILURE);
            }
            return;
          }
          if (!renderOverlay.endsWith(QStringLiteral("apply"))) {
            return;
          }
          QMetaObject::invokeMethod(confirm, "clicked");
          QTimer::singleShot(220, quickWindow, [quickWindow, &application] {
            auto* panel = quickWindow->findChild<QObject*>(QStringLiteral("detailgameStopPanel"));
            if (!panel || panel->property("resultMessage").toString().isEmpty()) {
              qCritical() << "Stopping a game reported no result";
              application.exit(EXIT_FAILURE);
              return;
            }
            // The post-check reads the process table again, so the result has to
            // name the fate of the target it signalled.
            bool closed = false;
            for (const QVariant& line : panel->property("resultLines").toList()) {
              if (line.toString().contains(QStringLiteral("closed"))) {
                closed = true;
              }
            }
            if (!closed) {
              qCritical() << "Stopping a game did not report what happened to its target"
                          << panel->property("resultLines").toList();
              application.exit(EXIT_FAILURE);
            }
          });
        });
      });
    }
    if (renderOverlay.startsWith(QStringLiteral("stop-all"))) {
      QTimer::singleShot(160, quickWindow, [quickWindow, &application] {
        // The entry lives in the library actions popup, which is created when it
        // is first opened, so the entry is checked after opening it.
        auto* more = quickWindow->findChild<QQuickItem*>(QStringLiteral("libraryMoreButton"));
        if (!more) {
          qCritical() << "The library actions button is missing";
          application.exit(EXIT_FAILURE);
          return;
        }
        QMetaObject::invokeMethod(more, "clicked");
        QTimer::singleShot(140, quickWindow, [quickWindow, &application] {
          auto* menu = quickWindow->findChild<QObject*>(QStringLiteral("libraryActions"));
          auto* entry = quickWindow->findChild<QObject*>(QStringLiteral("stopAllGamesButton"));
          if (!menu || !entry || !menu->property("opened").toBool() ||
              !entry->property("visible").toBool()) {
            qCritical() << "STOP ALL GAMES is not offered in the library actions";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(entry, "clicked");
          QTimer::singleShot(160, quickWindow, [quickWindow, &application] {
            auto* panel = quickWindow->findChild<QObject*>(QStringLiteral("allgameStopPanel"));
            auto* cancel = quickWindow->findChild<QQuickItem*>(QStringLiteral("allcancelStop"));
            if (!panel || !cancel || !panel->property("opened").toBool() ||
                !cancel->hasActiveFocus()) {
              qCritical() << "The stop-all confirmation did not open with a safe cancel";
              application.exit(EXIT_FAILURE);
              return;
            }
            if (panel->property("games").toList().size() < 2) {
              qCritical() << "The stop-all confirmation did not list every running game"
                          << panel->property("games").toList().size();
              application.exit(EXIT_FAILURE);
            }
          });
        });
      });
    }
    if (renderOverlay.startsWith(QStringLiteral("library-reflow"))) {
      auto* timer = new QTimer(quickWindow);
      timer->setInterval(140);
      auto step = std::make_shared<int>(0);
      QObject::connect(timer, &QTimer::timeout, quickWindow,
                       [quickWindow, timer, step, renderOverlay, &library, &preferences, &application] {
        auto* grid = quickWindow->findChild<QQuickItem*>("libraryGrid");
        auto* content = grid ? grid->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!grid || !content) { application.exit(EXIT_FAILURE); return; }
        // A delegate that has not been positioned by the grid yet sits at the
        // origin, where it would read as an overlap with whatever is already at
        // (0,0). That is a layout that has not happened, not a layout that is
        // wrong, so the check waits for every delegate to be placed first. Without
        // this the very first tick fails intermittently, since whether the grid has
        // laid out by then depends on machine load.
        QList<QPair<QQuickItem*, QRectF>> placed;
        for (auto* item : content->childItems()) {
          if (!item->property("appId").isValid() || !item->isVisible()) continue;
          const auto bounds = item->mapRectToItem(grid, item->boundingRect());
          if (!bounds.intersects(grid->boundingRect())) continue;
          if (bounds.topLeft() == bounds.bottomRight()) continue;
          placed.append({item, bounds});
        }
        // A grid lays out in order, so any delegate past the first still sitting at
        // the origin has not been placed yet. The first delegate legitimately lives
        // there, which is why the index is part of the test. Layout that has not
        // happened must not be reported as layout that is wrong.
        int unplaced = 0;
        for (int index = 0; index < placed.size(); ++index) {
          if (index > 0 && placed.at(index).second.topLeft() == QPointF(0, 0)) ++unplaced;
        }
        if (unplaced > 0) {
          return;
        }
        for (int index = 0; index < placed.size(); ++index) {
          for (int other = index + 1; other < placed.size(); ++other) {
            const auto overlap = placed.at(index).second.intersected(placed.at(other).second);
            if (overlap.width() > 2 && overlap.height() > 2) {
              qCritical() << "Library delegates overlap after resize/filter" << *step
                          << placed.at(other).first->property("index") << placed.at(other).second
                          << placed.at(index).second;
              application.exit(EXIT_FAILURE); timer->stop(); return;
            }
          }
        }
        if (*step == 48) {
          quickWindow->setProperty("libraryReflowComplete", true);
          timer->stop(); return;
        }
        const int widths[] = {1255, 2024, 927, 1600, 600, 2039};
        const int n = (*step)++;
        if (n % 8 == 0 || n % 8 == 4) {
          const QSize size(widths[(n / 8) % 6], n % 8 == 4 ? 1104 : 1000);
          if (!renderOverlay.endsWith("return")) {
            quickWindow->setProperty("testRenderSize", size);
            quickWindow->resize(size);
            preferences.setCoverSize(n % 8 == 0 ? 100 : 140);
          }
        } else if (n % 8 == 1 || n % 8 == 7) {
          library.setMode(LibraryFilterModel::Mode::Recent);
          library.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
        } else if (n % 8 == 2) {
          grid->setProperty("contentY", grid->property("originY").toReal() +
                            qMax(0.0, grid->property("contentHeight").toReal() - grid->height()));
        } else if (n % 8 == 3) {
          QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
        } else if (n % 8 == 6) {
          QMetaObject::invokeMethod(quickWindow, "closeDetails");
        } else {
          if (renderOverlay.endsWith("return")) {
            // A completed launch updates Recent while Details hides the grid.
            const int row = library.rowCount() - 1;
            const auto game = library.get(row);
            if (!library.recordLaunch(row, game.value("source").toString(),
                                      game.value("runner").toString(), game.value("appId").toString())) {
              qCritical() << "Could not record isolated launch";
              application.exit(EXIT_FAILURE); timer->stop(); return;
            }
          } else {
            library.setMode(LibraryFilterModel::Mode::All);
            library.setSortMode(LibraryFilterModel::SortMode::Title);
          }
          grid->setProperty("contentY", grid->property("originY"));
        }
      });
      timer->start();
    }
    if (renderOverlay == QStringLiteral("couch-grid-small")) preferences.setCouchCoverSize(60);
    if (renderOverlay == QStringLiteral("couch-grid-large")) preferences.setCouchCoverSize(160);
    if (renderOverlay.startsWith("library-repair")) {
      if (renderOverlay == "library-repair-manual" ||
          renderOverlay == "library-repair-manual-runtime") {
        const bool runtimeProblem = renderOverlay == "library-repair-manual-runtime";
        relocationFixtureDirectory = std::make_unique<QTemporaryDir>();
        const QString executable = relocationFixtureDirectory->filePath("manual/Manual Game");
        const QString workdir = runtimeProblem
                                    ? relocationFixtureDirectory->filePath("missing-workdir")
                                    : QFileInfo(executable).absolutePath();
        QDir().mkpath(QFileInfo(executable).absolutePath());
        if (runtimeProblem)
          QDir().mkpath(workdir);
        QFile file(executable);
        const QByteArray script = QByteArrayLiteral("#!/bin/sh\nexit 0\n");
        if (!relocationFixtureDirectory->isValid() || !file.open(QIODevice::WriteOnly) ||
            file.write(script) != script.size() ||
            !QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner |
                                                      QFile::ExeOwner)) {
          qCritical() << "Could not create the manual repair fixture";
          application.exit(EXIT_FAILURE);
          return EXIT_FAILURE;
        }
        const QString id = manualGames.saveEntry(
            {{QStringLiteral("title"), QStringLiteral("Manual Repair Fixture")},
             {QStringLiteral("executable"), executable},
             {QStringLiteral("directory"), workdir},
             {QStringLiteral("arguments"), QStringList{}}});
        if (id.isEmpty() || (runtimeProblem ? !QDir().rmdir(workdir)
                                             : !QFile::remove(executable))) {
          qCritical() << "Could not prepare the missing manual game fixture";
          application.exit(EXIT_FAILURE);
          return EXIT_FAILURE;
        }
        libraryRepair.setSource(QStringLiteral("Manual"));
        libraryRepair.setReason(runtimeProblem ? QStringLiteral("runtime")
                                               : QStringLiteral("missing-file"));
        QTimer::singleShot(700, quickWindow, [quickWindow, id, runtimeProblem, &libraryRepair, &application] {
          if (libraryRepair.current().value(QStringLiteral("appId")).toString() != id ||
              !libraryRepair.current().value(QStringLiteral("reasons")).toStringList().contains(
                  runtimeProblem ? QStringLiteral("runtime") : QStringLiteral("missing-file"))) {
            qCritical() << "The manual repair fixture did not reach its review state";
            application.exit(EXIT_FAILURE);
            return;
          }
          auto* edit = findVisualItem(quickWindow->contentItem(),
                                      QStringLiteral("libraryRepairEditManualButton"));
          auto* launchSetup = findVisualItem(quickWindow->contentItem(),
                                             QStringLiteral("libraryRepairLaunchSetupButton"));
          if (!edit || !edit->isVisible() || (runtimeProblem && launchSetup && launchSetup->isVisible())) {
            qCritical() << "Manual repair did not offer the correct edit action";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(edit, "clicked");
          QTimer::singleShot(100, quickWindow, [quickWindow, id, &application] {
            auto* editor = quickWindow->findChild<QObject*>(QStringLiteral("manualGameEditor"));
            if (!editor || !editor->property("visible").toBool() ||
                editor->property("entryId").toString() != id) {
              qCritical() << "EDIT GAME did not open the existing manual entry";
              application.exit(EXIT_FAILURE);
            }
          });
        });
      } else if (renderOverlay == "library-repair-relocate") {
        relocationFixtureDirectory = std::make_unique<QTemporaryDir>();
        const QString oldPath = relocationFixtureDirectory->filePath("missing/Relocation.sfc");
        const QString newPath = relocationFixtureDirectory->filePath("found/Relocation.sfc");
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        QFile relocatedContent(newPath);
        if (!relocationFixtureDirectory->isValid() ||
            !relocatedContent.open(QIODevice::WriteOnly) ||
            relocatedContent.write("fixture rom") < 0) {
          qCritical() << "Could not create the relocation render fixture";
          application.exit(EXIT_FAILURE);
          return EXIT_FAILURE;
        }
        relocationFixture = std::make_unique<QStandardItemModel>(1, 1);
        auto* relocationGame = new QStandardItem(QStringLiteral("Relocation Fixture"));
        relocationGame->setData(QStringLiteral("RetroArch"), GameRoles::Source);
        relocationGame->setData(QStringLiteral("relocation-fixture"), GameRoles::AppId);
        relocationGame->setData(QStringLiteral("snes"), GameRoles::System);
        relocationGame->setData(oldPath, GameRoles::InstallPath);
        relocationGame->setData(QStringLiteral("fixture://cover"), GameRoles::CoverPath);
        relocationGame->setData(QColor(QStringLiteral("#4b6a86")), GameRoles::AccentStart);
        relocationGame->setData(QColor(QStringLiteral("#64835c")), GameRoles::AccentEnd);
        relocationGame->setData(true, GameRoles::Installed);
        relocationFixture->setItem(0, 0, relocationGame);
        unifiedGames.addSourceModel(relocationFixture.get());
        libraryRepair.setSource(QString{});
        libraryRepair.setReason(QStringLiteral("missing-file"));
        QString key;
        for (int row = 0; row < unifiedGames.rowCount(); ++row)
          if (unifiedGames.data(unifiedGames.index(row), GameRoles::AppId).toString() ==
              QStringLiteral("relocation-fixture")) {
            key = unifiedGames.reviewGame(row).value(QStringLiteral("metadataKey")).toString();
            break;
          }
        QTimer::singleShot(700, quickWindow, [quickWindow, key, newPath, &libraryRepair,
                                              &application, &controller] {
          if (key.isEmpty() ||
              libraryRepair.current().value(QStringLiteral("metadataKey")).toString() != key ||
              !libraryRepair.current().value(QStringLiteral("reasons")).toStringList().contains(
                  QStringLiteral("missing-file"))) {
            qCritical() << "The relocation fixture did not reach the missing-file review state";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(quickWindow, "openRepairRelocation",
                                    Q_ARG(QVariant, key), Q_ARG(QVariant, newPath));
          QTimer::singleShot(100, quickWindow, [quickWindow, &application, &controller] {
          QCoreApplication::processEvents();
          const auto item = [quickWindow](const QString& name) {
            return findVisualItem(quickWindow->contentItem(), name);
          };
          auto* pathField = item(QStringLiteral("libraryRepairRelocationPath"));
          auto* browse = item(QStringLiteral("libraryRepairRelocationBrowseButton"));
          auto* enterPath = item(QStringLiteral("libraryRepairRelocationTextEntryButton"));
          auto* cancel = item(QStringLiteral("libraryRepairRelocationCancelButton"));
          auto* confirm = item(QStringLiteral("libraryRepairRelocationConfirmButton"));
          QQuickItem* firstPathAction = pathField
                                            ? pathField->property("controllerRightTarget")
                                                  .value<QQuickItem*>()
                                            : nullptr;
          if (!pathField || (!browse && !enterPath) || !firstPathAction ||
              (firstPathAction != browse && firstPathAction != enterPath) ||
              !firstPathAction->isVisible() || !cancel || !confirm || !confirm->isEnabled()) {
            qCritical() << "The relocation preview did not expose its path and confirmation controls";
            application.exit(EXIT_FAILURE);
            return;
          }
          const auto focusDirection = [&quickWindow, &controller](int direction,
                                                                  QQuickItem* expected) {
            QQuickItem* start = quickWindow->activeFocusItem();
            controller.focusDirectionRequested(direction);
            QCoreApplication::processEvents();
            if (!expected && start) {
              const QString property = direction == Qt::Key_Right ? QStringLiteral("controllerRightTarget")
                                     : direction == Qt::Key_Left ? QStringLiteral("controllerLeftTarget")
                                     : direction == Qt::Key_Up ? QStringLiteral("controllerUpTarget")
                                     : QStringLiteral("controllerDownTarget");
              expected = start->property(property.toUtf8().constData()).value<QQuickItem*>();
            }
            const bool reached = expected &&
                                 (quickWindow->activeFocusItem() == expected ||
                                  expected->hasActiveFocus());
            if (!reached) {
              QQuickItem* focused = quickWindow->activeFocusItem();
              qCritical() << "Relocation preview controller move missed" << direction
                          << (expected ? expected->objectName() : QStringLiteral("none"))
                          << (focused ? focused->objectName() : QStringLiteral("nothing"));
            }
            return reached;
          };
          pathField->forceActiveFocus();
          bool reachable = focusDirection(Qt::Key_Right, firstPathAction);
          if (reachable && !quickWindow->property("couchMode").toBool())
            reachable = focusDirection(Qt::Key_Right, enterPath) &&
                        focusDirection(Qt::Key_Left, browse);
          if (!reachable || !focusDirection(Qt::Key_Down, cancel) ||
              !focusDirection(Qt::Key_Right, confirm)) {
            qCritical() << "The relocation preview skipped a controller action";
            application.exit(EXIT_FAILURE);
            return;
          }
          auto* overlay = item(QStringLiteral("libraryRepairRelocationPreview"));
          quickWindow->requestActivate();
          QEventLoop activation;
          QTimer::singleShot(40, &activation, &QEventLoop::quit);
          activation.exec();
          for (bool forward : {true, false}) {
            confirm->forceActiveFocus();
            for (int step = 0; step < 8; ++step) {
              const int key = forward ? Qt::Key_Tab : Qt::Key_Backtab;
              const auto modifiers = forward ? Qt::NoModifier : Qt::ShiftModifier;
              deliverKeyboardTestKey(quickWindow, key, modifiers);
              QCoreApplication::processEvents();
              auto* ancestor = quickWindow->activeFocusItem();
              while (ancestor && ancestor != overlay) ancestor = ancestor->parentItem();
              if (!overlay || ancestor != overlay) {
                qCritical() << "Relocation dialog Tab navigation escaped to a hidden action"
                            << forward << step << quickWindow->activeFocusItem()
                            << "active" << quickWindow->isActive()
                            << "repair" << quickWindow->property("repairOpen");
                application.exit(EXIT_FAILURE);
                return;
              }
            }
          }
          });
        });
      }
      libraryRepair.refresh();
      quickWindow->setProperty("repairOpen", true);
      if (renderOverlay == "library-repair-controls") {
        const QString key = libraryRepair.current().value("metadataKey").toString();
        QTimer::singleShot(100, quickWindow,
                           [quickWindow, key, &libraryRepair, metadata = gameMetadata.get(),
                            &application, &controller, repairNavigationFixture] {
          if (repairNavigationFixture) {
            const QStringList initialReasons =
                libraryRepair.current().value("reasons").toStringList();
            if (!initialReasons.contains(QStringLiteral("identification")) ||
                !initialReasons.contains(QStringLiteral("artwork")) ||
                !libraryRepair.checkpoint("identity") || !libraryRepair.checkpoint("artwork")) {
              qCritical() << "Repair panel could not set up its independent undo controls";
              application.exit(EXIT_FAILURE);
              return;
            }
            QCoreApplication::processEvents();
            const auto item = [quickWindow](const QString& name) {
              return findVisualItem(quickWindow->contentItem(), name);
            };
            auto* close = item(QStringLiteral("libraryRepairCloseButton"));
            auto* sources = item(QStringLiteral("libraryRepairSourceFilter"));
            auto* reasons = item(QStringLiteral("libraryRepairReasonFilter"));
            auto* correct = item(QStringLiteral("libraryRepairCorrectIdentityButton"));
            auto* undoIdentity = item(QStringLiteral("libraryRepairUndoIdentityButton"));
            auto* artwork = item(QStringLiteral("libraryRepairChooseArtworkButton"));
            auto* undoArtwork = item(QStringLiteral("libraryRepairUndoArtworkButton"));
            auto* previous = item(QStringLiteral("libraryRepairPreviousButton"));
            auto* next = item(QStringLiteral("libraryRepairNextButton"));
            auto* retry = item(QStringLiteral("libraryRepairRetryThisGameButton"));
            auto* select = item(QStringLiteral("libraryRepairSelectForRetryButton"));
            auto* retrySelected = item(QStringLiteral("libraryRepairRetrySelectedButton"));
            auto* stopRetry = item(QStringLiteral("libraryRepairStopRetryButton"));
            auto* summary = item(QStringLiteral("libraryRepairReasonSummary"));
            auto* title = item(QStringLiteral("libraryRepairTitle"));
            if (!close || !sources || !reasons || !correct || !undoIdentity || !artwork ||
                !undoArtwork || !previous || !next || !retry || !select || !retrySelected ||
                !stopRetry || !summary || !title ||
                !correct->isVisible() || !artwork->isVisible() || !undoIdentity->isVisible() ||
                !undoArtwork->isVisible()) {
              qCritical() << "Repair panel did not expose its keyboard actions";
              application.exit(EXIT_FAILURE);
              return;
            }
            const auto withinWidth = [quickWindow](QQuickItem* control) {
              const qreal left = control->mapToScene(QPointF()).x();
              return left >= -1 && left + control->width() <= quickWindow->width() + 1;
            };
            if (!withinWidth(title) || !withinWidth(close) || !withinWidth(summary) ||
                !withinWidth(retry) ||
                !withinWidth(select) || !withinWidth(retrySelected) ||
                !withinWidth(stopRetry)) {
              qCritical() << "Repair panel has a control beyond the window width";
              application.exit(EXIT_FAILURE);
              return;
            }
            const auto moveFocus = [&quickWindow, &controller](int direction,
                                                               QQuickItem* expected) {
              controller.focusDirectionRequested(direction);
              QCoreApplication::processEvents();
              if (quickWindow->activeFocusItem() == expected || expected->hasActiveFocus())
                return true;
              QQuickItem* focused = quickWindow->activeFocusItem();
              qCritical() << "Repair panel focus move missed" << direction
                          << expected->objectName()
                          << (focused ? focused->objectName() : QStringLiteral("nothing"));
              return false;
            };
            close->forceActiveFocus();
            if (!moveFocus(Qt::Key_Down, sources) || !moveFocus(Qt::Key_Right, reasons) ||
                !moveFocus(Qt::Key_Down, correct) || !moveFocus(Qt::Key_Right, undoIdentity) ||
                !moveFocus(Qt::Key_Left, correct) || !moveFocus(Qt::Key_Down, artwork) ||
                !moveFocus(Qt::Key_Right, undoArtwork) || !moveFocus(Qt::Key_Up, undoIdentity) ||
                !moveFocus(Qt::Key_Down, artwork) || !moveFocus(Qt::Key_Down, previous) ||
                !moveFocus(Qt::Key_Right, next) || !moveFocus(Qt::Key_Down, retry) ||
                !moveFocus(Qt::Key_Right, select)) {
              qCritical() << "Repair panel controller navigation skipped a control";
              application.exit(EXIT_FAILURE);
              return;
            }
            libraryRepair.toggleSelected();
            QCoreApplication::processEvents();
            if (!retrySelected->isEnabled() || !moveFocus(Qt::Key_Right, retrySelected)) {
              qCritical() << "Repair panel retry selection is not controller reachable";
              application.exit(EXIT_FAILURE);
              return;
            }
          }
          QMetaObject::invokeMethod(quickWindow, "openRepairGame",
                                    Q_ARG(QVariant, QStringLiteral("identity")));
          QTimer::singleShot(100, quickWindow, [quickWindow, key, &libraryRepair, metadata,
                                                &application] {
            auto* panel = quickWindow->findChild<QObject*>(QStringLiteral("identifyGamePanel"));
            if (!panel || !panel->property("opened").toBool() ||
                !quickWindow->property("repairSession").toBool()) {
              qCritical() << "Repair workflow did not open the identity editor";
              application.exit(EXIT_FAILURE);
              return;
            }
            metadata->rejectMatch();
            if (!metadata->entry(key).value("rejected").toBool()) {
              application.exit(EXIT_FAILURE);
              return;
            }
            QMetaObject::invokeMethod(panel, "close");
            QMetaObject::invokeMethod(quickWindow, "closeDetails");
            QTimer::singleShot(100, quickWindow, [quickWindow, key, &libraryRepair, metadata,
                                                  &application] {
              if (!quickWindow->property("repairOpen").toBool() ||
                  libraryRepair.current().value("metadataKey").toString() != key ||
                  !libraryRepair.undo("identity") ||
                  metadata->entry(key).value("rejected").toBool()) {
                qCritical() << "Repair workflow lost its position or undo";
                application.exit(EXIT_FAILURE);
              }
            });
          });
        });
      }
    }
    if (renderOverlay.startsWith("launch-setup")) {
      QMetaObject::invokeMethod(quickWindow,"openGame",Q_ARG(QVariant,0));
      QTimer::singleShot(120,quickWindow,[quickWindow,renderOverlay,&application] {
        auto* details=quickWindow->findChild<QObject*>("gameDetails");
        auto* setup=quickWindow->findChild<QObject*>("launchSetupPanel");
        auto* launchSetupEntry=quickWindow->findChild<QQuickItem*>("launchSetupMenuButton");
        if(!details || !setup || !launchSetupEntry ||
           launchSetupEntry->property("text").toString() != QStringLiteral("LAUNCH SETUP")) {
          application.exit(EXIT_FAILURE);return;
        }
        details->setProperty("selectedInstallation",QVariantMap{{"source","RetroArch"},{"appId","fixture"},{"system","snes"},{"installPath","/missing/Game.sfc"}});
        auto* save = quickWindow->findChild<QQuickItem*>("saveLaunchSetup");
        auto* firstControl = setup->property("firstControl").value<QQuickItem*>();
        auto* manageButton = quickWindow->findChild<QQuickItem*>("detailManageButton");
        auto* manageMenu = quickWindow->findChild<QObject*>("detailManageMenu");
        if (!save || !firstControl || !manageButton || !manageMenu ||
            !QMetaObject::invokeMethod(manageButton, "clicked")) {
          application.exit(EXIT_FAILURE); return;
        }
        QCoreApplication::processEvents();
        if (!manageMenu->property("opened").toBool() || !launchSetupEntry->isVisible() ||
            !QMetaObject::invokeMethod(launchSetupEntry, "clicked")) {
          qCritical() << "Manage did not expose its Launch Setup entry";
          application.exit(EXIT_FAILURE); return;
        }
        QTimer::singleShot(80, quickWindow, [quickWindow, details, setup, firstControl, save, renderOverlay, &application] {
            if (!setup->property("expanded").toBool() || !firstControl->hasActiveFocus()) {
              qCritical() << "Manage did not expand Launch Setup and focus its first control";
              application.exit(EXIT_FAILURE); return;
            }
            if (!renderOverlay.endsWith("-entry")) {
              auto* locateMissing = findVisualItem(
                  quickWindow->contentItem(), QStringLiteral("launchSetupLocateMissingContentButton"));
              if (!locateMissing || !locateMissing->isVisible()) {
                qCritical() << "Launch Setup did not offer relocation for missing content";
                application.exit(EXIT_FAILURE); return;
              }
            }
            save->forceActiveFocus();
            QMetaObject::invokeMethod(details,"revealFocusedItem",Q_ARG(QVariant,QVariant::fromValue(save)));
            if(renderOverlay=="launch-setup-entry") {
              auto* field=quickWindow->findChild<QQuickItem*>("launchCorePath");
              if(!field) {application.exit(EXIT_FAILURE);return;}
              field->forceActiveFocus();
              deliverKeyboardTestKey(quickWindow, Qt::Key_Enter);
              QTimer::singleShot(80,quickWindow,[quickWindow,field,&application] {
                if(!quickWindow->property("couchTextEntryOpen").toBool()) {
                  qCritical()<<"Launch setup field did not open controller text entry";application.exit(EXIT_FAILURE);return;
                }
                QMetaObject::invokeMethod(quickWindow,"closeCouchTextEntry",Q_ARG(QVariant,false));
                QTimer::singleShot(60,quickWindow,[quickWindow,field,&application] {
                  if(quickWindow->property("couchTextEntryOpen").toBool() || !field->hasActiveFocus() || !field->property("text").toString().isEmpty()) {
                    qCritical()<<"Canceling launch setup text entry lost focus or changed the core";application.exit(EXIT_FAILURE);
                  }
                });
              });
            }
          });
      });
    }
    // `--render-overlay=settings|picker` opens an overlay so visual checks can cover it.
    if (heroicOwnedFixture) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QObject::connect(&launcher, &GameLauncher::gameRunningChanged, quickWindow, [quickWindow] {
        quickWindow->setProperty("heroicOwnedTracked", true);
      });
      QTimer::singleShot(120, quickWindow, [quickWindow, &application, &launcher, &preferences,
                                           libraryDatabasePath, heroicOwnedDispatchLog,
                                           &heroicOwnedDispatchComplete] {
        auto* play = quickWindow->findChild<QQuickItem*>("playButton");
        auto* feedback = quickWindow->findChild<QObject*>("launchFeedback");
        const auto choice = quickWindow->property("selectedInstallation").toMap();
        if (!play || !feedback || !play->isVisible() || play->property("text") != "OPEN IN HEROIC" ||
            choice.value("source") != "Heroic" || choice.value("appId") != "owned-fixture" ||
            choice.value("installed").toBool() || !preferences.closeAfterLaunch()) {
          qCritical() << "Owned Heroic game did not offer Open in Heroic"
                      << "button" << (play ? play->property("text") : QVariant{})
                      << "visible" << (play && play->isVisible())
                      << "choice" << choice << "closeAfterLaunch" << preferences.closeAfterLaunch();
          application.exit(EXIT_FAILURE);
          return;
        }
        if (!QMetaObject::invokeMethod(play, "clicked")) { application.exit(EXIT_FAILURE); return; }
        // Wait for the stub and a subsequent event-loop turn, so Qt.callLater(Qt.quit)
        // would terminate the run before it can report successful verification.
        auto* probe = new QTimer(quickWindow);
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        QObject::connect(probe, &QTimer::timeout, quickWindow,
            [probe, elapsed, quickWindow, feedback, &application, &launcher,
             libraryDatabasePath, heroicOwnedDispatchLog, &heroicOwnedDispatchComplete] {
          QFile log(heroicOwnedDispatchLog);
          if (!log.open(QIODevice::ReadOnly)) {
            if (elapsed->elapsed() < 3000) return;
            qCritical() << "Owned Heroic action did not dispatch to the stub";
            application.exit(EXIT_FAILURE); return;
          }
          if (elapsed->elapsed() < 250) return;
          probe->stop();
          const QString connection = "omakade-heroic-owned-dispatch";
          bool noRecordedLaunch = false;
          {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(libraryDatabasePath);
            if (db.open()) {
              QSqlQuery query(db);
              noRecordedLaunch = query.exec(
                  "SELECT COUNT(*) FROM launch_activity WHERE source='Heroic' "
                  "AND runner='legendary' AND app_id='owned-fixture'") &&
                  query.next() && query.value(0).toInt() == 0;
            }
          }
          QSqlDatabase::removeDatabase(connection);
          if (log.readAll() != "0\n" || feedback->property("failed").toBool() ||
              !feedback->property("message").toString().contains(" in Heroic") ||
              launcher.gameRunning() || quickWindow->property("heroicOwnedTracked").toBool() ||
              !noRecordedLaunch) {
            qCritical() << "Owned Heroic action launched, tracked, or recorded play instead of management";
            application.exit(EXIT_FAILURE); return;
          }
          heroicOwnedDispatchComplete = true;
        });
        probe->start(25);
      });
    }
    if (renderOverlay == QStringLiteral("launch-feedback")) {
      QTimer::singleShot(120, quickWindow, [quickWindow, &application] {
        QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
        auto* play = quickWindow->findChild<QQuickItem*>("playButton");
        auto* feedback = quickWindow->findChild<QObject*>("launchFeedback");
        if (!play || !feedback) { application.exit(EXIT_FAILURE); return; }
        play->forceActiveFocus();
        QMetaObject::invokeMethod(quickWindow, "playSelected");
        QMetaObject::invokeMethod(quickWindow, "playSelected");
        if (!feedback->property("pending").toBool() || play->property("text") != "OPENING...") {
          qCritical() << "Launch feedback was not immediate";
          application.exit(EXIT_FAILURE); return;
        }
        QTimer::singleShot(150, quickWindow, [quickWindow, feedback, play, &application] {
          auto* status = quickWindow->findChild<QQuickItem*>("launchStatusText");
          if (feedback->property("pending").toBool() || !feedback->property("failed").toBool()
              || !status || !status->isVisible() || !status->property("text").toString().contains("Demo games cannot be launched")
              || quickWindow->activeFocusItem() != play || !play->isEnabled()) {
            qCritical() << "Launch failure lost feedback or retry focus";
            application.exit(EXIT_FAILURE); return;
          }
        });
      });
    }
    if (renderOverlay == QStringLiteral("launch-closed")) {
      // A game Omakade started closed at once: the notice names it from the launch, offers
      // its log first, and gives focus back to Play when it closes.
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      // After the details page has put focus on Play itself.
      QTimer::singleShot(200, quickWindow, [quickWindow, &application, &launcher] {
        auto* play = quickWindow->findChild<QQuickItem*>("playButton");
        if (!play || quickWindow->activeFocusItem() != play) {
          qCritical() << "Details did not focus Play before the closed game notice";
          application.exit(EXIT_FAILURE); return;
        }
        const QString appId = QStringLiteral("fixture-closed");
        const QString title = quickWindow->property("selectedGame").toMap().value("title").toString();
        quickWindow->setProperty("launchTitles",
                                 QVariantMap{{QStringLiteral("Manual\n") + appId, title}});
        const QVariantMap game{{QStringLiteral("source"), QStringLiteral("Manual")},
                               {QStringLiteral("appId"), appId},
                               {QStringLiteral("logPath"), GameLauncher::launchLogPath(QStringLiteral("Manual"), appId)}};
        emit launcher.gameClosedEarly(game);
        QTimer::singleShot(200, quickWindow, [quickWindow, play, title, &application] {
          auto* notice = quickWindow->findChild<QObject*>("launchClosedNotice");
          auto* text = quickWindow->findChild<QQuickItem*>("launchClosedText");
          auto* openLog = quickWindow->findChild<QQuickItem*>("launchClosedOpenLog");
          if (title.isEmpty() || !notice || !notice->property("opened").toBool() || !text ||
              text->property("text").toString() != title + QStringLiteral(" closed right after starting.") ||
              !openLog || !openLog->isVisible() || quickWindow->activeFocusItem() != openLog) {
            qCritical() << "Closed game notice did not name the game or focus Open log"
                        << (notice ? notice->property("opened") : QVariant())
                        << (text ? text->property("text") : QVariant())
                        << (openLog ? openLog->isVisible() : false) << quickWindow->activeFocusItem();
            application.exit(EXIT_FAILURE); return;
          }
          QMetaObject::invokeMethod(notice, "close");
          QTimer::singleShot(200, quickWindow, [quickWindow, notice, play, &application] {
            if (quickWindow->activeFocusItem() != play) {
              qCritical() << "Closing the closed game notice did not return focus to Play";
              application.exit(EXIT_FAILURE); return;
            }
            QMetaObject::invokeMethod(notice, "open");
          });
        });
      });
    }
    if (renderOverlay == QStringLiteral("session-history-pages")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(120, quickWindow, [quickWindow, &application] {
        auto* details = quickWindow->findChild<QObject*>("gameDetails");
        if (!details) { application.exit(EXIT_FAILURE); return; }
        details->setProperty("selectedInstallation", QVariantMap{{"source", "RetroArch"},
                             {"installPath", "/games/demo-0.nes"}, {"appId", "demo-0"}});
        QTimer::singleShot(100, quickWindow, [quickWindow, details, &application] {
          auto* button = findVisualItem(quickWindow->contentItem(), "playHistoryButton");
          if (!button || !button->isVisible()) { application.exit(EXIT_FAILURE); return; }
          QMetaObject::invokeMethod(button, "clicked");
          QTimer::singleShot(100, quickWindow, [quickWindow, details, &application] {
            auto* older = findVisualItem(quickWindow->contentItem(), "nextHistoryPage");
            if (!older || !older->isVisible()) { qCritical() << "Older history is unreachable"; application.exit(EXIT_FAILURE); return; }
            QMetaObject::invokeMethod(older, "clicked");
            QTimer::singleShot(100, quickWindow, [quickWindow, details, &application] {
              auto* newer = findVisualItem(quickWindow->contentItem(), "previousHistoryPage");
              auto* first = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_0");
              auto* done = quickWindow->findChild<QQuickItem*>("playHistoryDoneButton");
              if (details->property("historyPage").toInt() != 1 || !newer || !newer->isVisible() ||
                  !first || !first->property("text").toString().contains("1m") || !done || !done->hasActiveFocus()) {
                qCritical() << "Older history page or safe focus failed"; application.exit(EXIT_FAILURE); return;
              }
              auto refreshed = details->property("selectedInstallation").toMap();
              refreshed.insert("title", "Updated metadata for the same game");
              details->setProperty("selectedInstallation", refreshed);
              if (details->property("historyPage").toInt() != 1) {
                qCritical() << "A metadata refresh reset the history page"; application.exit(EXIT_FAILURE); return;
              }
              QMetaObject::invokeMethod(newer, "clicked");
              if (details->property("historyPage").toInt() != 0) application.exit(EXIT_FAILURE);
            });
          });
        });
      });
    }
    if (renderOverlay == QStringLiteral("game-clips")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      // Main.qml may replace the installation with the model's own choice, so the fixture's
      // is put back before each check, as the session history fixture does.
      auto useFixtureInstallation = [quickWindow] {
        auto* details = quickWindow->findChild<QObject*>("gameDetails");
        if (details == nullptr) return false;
        details->setProperty("selectedInstallation",
                             QVariantMap{{"source", "RetroArch"}, {"installPath", "/games/demo-0.nes"},
                                         {"launchTarget", "snes9x_libretro.so"}, {"appId", "demo-0"}});
        return true;
      };
      QTimer::singleShot(120, quickWindow, [quickWindow, &application, useFixtureInstallation] {
        if (!useFixtureInstallation()) { application.exit(EXIT_FAILURE); return; }
        QTimer::singleShot(150, quickWindow, [quickWindow, &application, useFixtureInstallation] {
          useFixtureInstallation();
          auto* details = quickWindow->findChild<QObject*>("gameDetails");
          auto* section = findVisualItem(quickWindow->contentItem(), "clipSection");
          auto* count = findVisualItem(quickWindow->contentItem(), "clipCountText");
          auto* trash = findVisualItem(quickWindow->contentItem(), "trashClip0");
          auto* last = findVisualItem(quickWindow->contentItem(), "clipCard2");
          if (!details || !section || !section->isVisible() || !count || count->property("text").toString() != "3" ||
              !trash || !last) {
            qCritical() << "The game's clips were not listed";
            application.exit(EXIT_FAILURE);
            return;
          }
          // A clip card keeps a thumbnail size rather than stretching across the page.
          auto* first = findVisualItem(quickWindow->contentItem(), "clipCard0");
          const qreal widest = 300 * details->property("uiScale").toReal() + 1;
          if (first == nullptr || first->width() > widest) {
            qCritical() << "A clip card stretched to" << (first ? first->width() : 0) << "px";
            application.exit(EXIT_FAILURE);
            return;
          }
          // Trash asks for a second press, so one stray press cannot lose a clip.
          QMetaObject::invokeMethod(trash, "clicked");
          if (trash->property("text").toString() != QStringLiteral("CONFIRM TRASH") ||
              details->property("trashArmedClip").toInt() < 0) {
            qCritical() << "Trashing a clip did not ask to confirm";
            application.exit(EXIT_FAILURE);
            return;
          }
          details->setProperty("trashArmedClip", -1);
          // Bring the clips into view for the screenshot.
          if (auto* scroll = findVisualItem(quickWindow->contentItem(), "detailsScroll")) {
            if (auto* flickable = scroll->property("navigationFlickable").value<QQuickItem*>()) {
              const qreal target = section->mapToItem(flickable->property("contentItem").value<QQuickItem*>(), QPointF(0, 0)).y();
              flickable->setProperty("contentY", qMax<qreal>(0, target - 24));
            }
          }
        });
      });
    }
    if (renderOverlay == QStringLiteral("session-history")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      // Totals changes make Main.qml refresh the selected game, which replaces the
      // installation with the model's own choice. The fixture path has to be put back
      // after every change, or the history view loses the game it is showing.
      auto useFixtureInstallation = [quickWindow] {
        auto* details = quickWindow->findChild<QObject*>("gameDetails");
        if (details == nullptr) return false;
        details->setProperty(
            "selectedInstallation",
            QVariantMap{{"source", "RetroArch"},
                        {"installPath", "/games/demo-0.nes"},
                        {"launchTarget", "snes9x_libretro.so"},
                        {"appId", "demo-0"}});
        return details->property("sessionHistoryPaths").toList().size() == 1;
      };
      QTimer::singleShot(120, quickWindow, [quickWindow, &application, useFixtureInstallation] {
        auto* details = quickWindow->findChild<QObject*>("gameDetails");
        if (!details || !useFixtureInstallation()) {
          application.exit(EXIT_FAILURE);
          return;
        }
        QTimer::singleShot(80, quickWindow, [quickWindow, &application, useFixtureInstallation] {
          auto* button = findVisualItem(quickWindow->contentItem(), "playHistoryButton");
          if (!button || !button->isVisible()) {
            qCritical() << "Play history was not available for the selected game";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(button, "clicked");
          QTimer::singleShot(80, quickWindow, [quickWindow, &application, useFixtureInstallation] {
            auto* menu = quickWindow->findChild<QObject*>("playHistoryMenu");
            auto* done = quickWindow->findChild<QQuickItem*>("playHistoryDoneButton");
            auto* first = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_0");
            auto* second = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_1");
            if (!menu || !menu->property("opened").toBool() || !done ||
                !done->hasActiveFocus() || !first || !second) {
              qCritical() << "Play history did not open with safe focus and recorded sessions";
              application.exit(EXIT_FAILURE);
              return;
            }
            // Deleting a session must ask first, with the way out focused, and
            // only then remove exactly the session that was chosen. The entries
            // are newest first, and their own text is what the later checks key
            // off, so a deletion that removed the wrong row fails here rather
            // than passing on a count alone.
            if (!first->property("text").toString().contains(QStringLiteral("20m")) ||
                !second->property("text").toString().contains(QStringLiteral("30m"))) {
              qCritical() << "Play history entries were not the expected fixture sessions"
                          << first->property("text").toString()
                          << second->property("text").toString();
              application.exit(EXIT_FAILURE);
              return;
            }
            QMetaObject::invokeMethod(first, "clicked");
            QTimer::singleShot(80, quickWindow, [quickWindow, &application, useFixtureInstallation] {
              auto* menu = quickWindow->findChild<QObject*>("playHistoryMenu");
              auto* cancel = quickWindow->findChild<QQuickItem*>("cancelHistoryDelete");
              auto* confirm = quickWindow->findChild<QObject*>("confirmHistoryDelete");
              auto* entry = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_0");
              if (!menu || cancel == nullptr || !cancel->isVisible() || !cancel->hasActiveFocus() ||
                  !confirm || !menu->property("confirming").toBool() || entry == nullptr ||
                  entry->isVisible()) {
                qCritical() << "Deleting a recorded session did not require a focused confirmation";
                application.exit(EXIT_FAILURE);
                return;
              }
              QMetaObject::invokeMethod(confirm, "clicked");
              QTimer::singleShot(120, quickWindow, [quickWindow, &application, useFixtureInstallation] {
                auto* menu = quickWindow->findChild<QObject*>("playHistoryMenu");
                if (!useFixtureInstallation() || menu == nullptr ||
                    menu->property("message").toString() != QStringLiteral("Session removed.")) {
                  qCritical() << "Deleting a recorded session did not report the removal"
                              << "message:" << (menu != nullptr
                                                    ? menu->property("message").toString()
                                                    : QStringLiteral("<none>"));
                  application.exit(EXIT_FAILURE);
                  return;
                }
                // The session that was chosen is gone and the older one is still
                // listed, so a deletion never clears the whole game's history.
                auto* stillListed = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_0");
                auto* removed = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_1");
                if (removed != nullptr || stillListed == nullptr ||
                    !stillListed->property("text").toString().contains(QStringLiteral("30m"))) {
                  qCritical() << "Deleting a recorded session did not remove it from the history"
                              << "entry0:" << (stillListed != nullptr)
                              << "entry1:" << (removed != nullptr)
                              << "entry0Text:" << (stillListed != nullptr
                                                       ? stillListed->property("text").toString()
                                                       : QStringLiteral("<none>"));
                  application.exit(EXIT_FAILURE);
                }
              });
            });
          });
        });
      });
    }
    if (statsFixture) {
      if (renderOverlay.startsWith(QStringLiteral("year-in-review")) &&
          cardExportPath.isEmpty())
        quickWindow->setProperty("pendingCardPreview", true);
      quickWindow->setProperty("statsOpen", true);
    }
    if (!cardExportPath.isEmpty()) {
      // One shot: the export writes the card and reports the outcome, which ends the run.
      QMetaObject::invokeMethod(quickWindow, "exportYearInReviewCard",
                                Q_ARG(QVariant, cardExportPath));
    }
    if (renderOverlay == QStringLiteral("now-playing")) {
      // Home has to be the open view: the panel lives on the Home screen.
      quickWindow->setProperty("homeOpen", true);
      auto* poll = new QTimer(quickWindow);
      auto* attempts = new int(0);
      poll->setInterval(100);
      QObject::connect(poll, &QTimer::timeout, quickWindow,
                       [quickWindow, poll, attempts, nowPlayingFixturePid, &controller,
                        &application] {
        const QString stopName =
            QStringLiteral("nowPlayingStop_%1").arg(nowPlayingFixturePid);
        auto* section = findVisualItem(quickWindow->contentItem(), "homeNowPlayingSection");
        auto* stop = findVisualItem(quickWindow->contentItem(), stopName);
        if (section == nullptr || !section->isVisible() || stop == nullptr || !stop->isVisible() ||
            !stop->isEnabled()) {
          if (++*attempts < 8) return;
          poll->stop();
          qCritical() << "Now Playing did not show a live session with a usable stop control"
                      << "section:" << (section != nullptr) << "visible:"
                      << (section != nullptr && section->isVisible()) << "stop:" << (stop != nullptr);
          application.exit(EXIT_FAILURE);
          return;
        }
        if (stop->property("text").toString() != QStringLiteral("STOP")) {
          poll->stop();
          qCritical() << "Now Playing stop control had an unexpected label"
                      << stop->property("text").toString();
          application.exit(EXIT_FAILURE);
          return;
        }
        poll->stop();
        // On desktop the panel is informational: the couch treatment belongs to Couch
        // Mode, and the arrow order the keyboard had must not change under it.
        if (!quickWindow->property("couchMode").toBool()) {
          auto* window = qobject_cast<QQuickWindow*>(quickWindow);
          auto* home = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeScreen"));
          auto* hint = findVisualItem(quickWindow->contentItem(), "nowPlayingHint");
          if (window == nullptr || home == nullptr) {
            qCritical() << "Desktop Now Playing could not find the Home screen";
            application.exit(EXIT_FAILURE);
            return;
          }
          if (hint != nullptr && hint->isVisible()) {
            qCritical() << "Desktop Now Playing showed the couch controller hint";
            application.exit(EXIT_FAILURE);
            return;
          }
          // Down from the toolbar, with a game running, still goes straight to the
          // games on desktop: the panel is couch navigation, not a new stop.
          auto* library = findVisualItem(quickWindow->contentItem(), "homeOpenHomeButton");
          if (library == nullptr) {
            qCritical() << "Desktop Now Playing could not find the Home toolbar";
            application.exit(EXIT_FAILURE);
            return;
          }
          library->forceActiveFocus();
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(library)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Down)));
          if (window->activeFocusItem() != nullptr &&
              window->activeFocusItem()->objectName() == stopName) {
            qCritical() << "Desktop Down moved into the couch panel instead of the games";
            application.exit(EXIT_FAILURE);
            return;
          }
        }
        // In Couch Mode the controller has to be able to reach the stop control
        // and the panel has to be scaled for a TV, not drawn at desktop size.
        if (quickWindow->property("couchMode").toBool()) {
          auto* window = qobject_cast<QQuickWindow*>(quickWindow);
          auto* home = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeScreen"));
          auto* hint = findVisualItem(quickWindow->contentItem(), "nowPlayingHint");
          if (window == nullptr || home == nullptr || hint == nullptr || !hint->isVisible()) {
            qCritical() << "Couch Now Playing had no controller hint"
                        << "home:" << (home != nullptr) << "hint:" << (hint != nullptr);
            application.exit(EXIT_FAILURE);
            return;
          }
          if (hint->property("text").toString().isEmpty()) {
            qCritical() << "Couch Now Playing hint was empty";
            application.exit(EXIT_FAILURE);
            return;
          }
          // Couch scale means a genuinely larger target than desktop's, not the
          // same control with different colours.
          const qreal couchHeight = stop->height();
          const qreal couchTitle = stop->property("displayScale").toReal();
          if (couchHeight <= 0 || couchTitle < 1.25) {
            qCritical() << "Couch Now Playing stop control was not couch scaled"
                        << "height:" << couchHeight << "displayScale:" << couchTitle;
            application.exit(EXIT_FAILURE);
            return;
          }
          // The panel's own text has to scale for a TV too. The row is reached from
          // the stop control inside it, since a Repeater exposes its delegates as
          // siblings rather than as children of the Repeater.
          QQuickItem* rowItem = stop->parentItem();
          // The row's Text items are nested a ColumnLayout deep, so the whole subtree
          // is walked with the stop control itself excluded.
          QList<QQuickItem*> texts;
          const std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
            for (auto* child : item->childItems()) {
              if (child != stop && child->property("text").isValid() &&
                  child->property("font").isValid())
                texts.append(child);
              collect(child);
            }
          };
          if (rowItem != nullptr) collect(rowItem);
          if (rowItem == nullptr) {
            qCritical() << "Couch Now Playing could not read the running game row";
            application.exit(EXIT_FAILURE);
            return;
          }
          if (texts.size() < 2) {
            qCritical() << "Couch Now Playing row had" << texts.size() << "text items";
            application.exit(EXIT_FAILURE);
            return;
          }
          // The name and the sub-line are set in points-like pixel sizes on the
          // Text, so a couch panel is readable from a sofa and a desktop one is not.
          const int nameSize = texts.at(0)->property("font").value<QFont>().pixelSize();
          const int sublineSize = texts.at(1)->property("font").value<QFont>().pixelSize();
          if (nameSize < 20 || sublineSize < 15) {
            qCritical() << "Couch Now Playing text was not couch scaled"
                        << "name:" << nameSize << "subline:" << sublineSize;
            application.exit(EXIT_FAILURE);
            return;
          }
          // The hint tells the player which button to press, and Couch Mode draws
          // that button as the pad's own glyph everywhere else in the app. Going
          // through the same property is what keeps it right on a pad whose button
          // is not called A.
          const QString hintText = hint->property("text").toString();
          const QString glyph = controller.primaryGlyph();
          if (hintText.isEmpty() || glyph.isEmpty() || !hintText.contains(glyph)) {
            qCritical() << "Couch Now Playing hint did not use the controller glyph"
                        << hintText << "glyph:" << glyph;
            application.exit(EXIT_FAILURE);
            return;
          }
          // The controller must be able to move down into the panel and back up
          // out of it, which is what makes the stop reachable on a pad.
          auto* library = findVisualItem(quickWindow->contentItem(), "homeCouchHomeButton");
          if (library == nullptr) {
            qCritical() << "Couch Now Playing could not find the Home toolbar";
            application.exit(EXIT_FAILURE);
            return;
          }
          library->forceActiveFocus();
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(library)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Down)));
          if (window->activeFocusItem() != stop) {
            qCritical() << "Controller Down did not reach the Couch Now Playing stop control";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(stop)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Up)));
          if (window->activeFocusItem() != library) {
            qCritical() << "Controller Up did not leave the Couch Now Playing panel";
            application.exit(EXIT_FAILURE);
            return;
          }
          // Down from the only running game has nothing below it inside the panel,
          // so it has to continue into the content rather than stop dead.
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(library)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Down)));
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(stop)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Down)));
          if (window->activeFocusItem() == nullptr ||
              window->activeFocusItem()->objectName() == stopName ||
              window->activeFocusItem() == library) {
            qCritical() << "Controller Down out of the Couch Now Playing panel was a dead end"
                        << "focused:"
                        << (window->activeFocusItem() ? window->activeFocusItem()->objectName()
                                                      : QString{"none"});
            application.exit(EXIT_FAILURE);
            return;
          }
          // A running session's elapsed time advances every second, which replaces
          // the panel's model and recreates every delegate. Focus has to survive
          // that, or a couch player loses their place about once a second.
          library->forceActiveFocus();
          QMetaObject::invokeMethod(home, "navigate",
                                    Q_ARG(QVariant, QVariant::fromValue(library)),
                                    Q_ARG(QVariant, static_cast<int>(Qt::Key_Down)));
          if (window->activeFocusItem() != stop) {
            qCritical() << "Controller Down did not reach the Couch Now Playing stop control";
            application.exit(EXIT_FAILURE);
            return;
          }
          if (!stop->property("visible").toBool()) return;
          // The store replaces the panel's rows whenever a running session's elapsed
          // time advances, which destroys and recreates these delegates. Replacing
          // the Repeater's model forces exactly that rebuild, so focus surviving it
          // is what proves a couch player does not lose their place mid-session.
          auto* list = findVisualItem(quickWindow->contentItem(), "homeNowPlayingList");
          if (list == nullptr) {
            qCritical() << "Couch Now Playing could not find the running game list";
            application.exit(EXIT_FAILURE);
            return;
          }
          const QVariant rows = list->property("model");
          list->setProperty("model", QVariantList{});
          QCoreApplication::processEvents();
          list->setProperty("model", rows);
          // The panel restores focus from a deferred call on the model change, which takes one
          // event turn on an idle machine and can take more when the machine is busy. Waiting for
          // the restore rather than assuming how many turns it needs keeps this check about focus
          // surviving the rebuild: a restore that never arrives still fails, and the message names
          // what the focus ended up on.
          const auto focusName = [&window] {
            const auto* current = window->activeFocusItem();
            return current != nullptr ? current->objectName() : QString{};
          };
          QElapsedTimer settleTimer;
          settleTimer.start();
          while (focusName() != stopName && settleTimer.elapsed() < 2000) {
            QEventLoop turn;
            QTimer::singleShot(5, &turn, &QEventLoop::quit);
            turn.exec(QEventLoop::AllEvents);
          }
          const QString restoredName = focusName();
          if (restoredName != stopName) {
            qCritical() << "Couch Now Playing lost controller focus on a panel refresh"
                        << "focused:"
                        << (restoredName.isEmpty() ? QString{"none"} : restoredName);
            application.exit(EXIT_FAILURE);
            return;
          }
          // The rebuild destroyed the old delegate, so the rest of the check has to
          // work from the recreated one, found the same way the app does.
          stop = findVisualItem(quickWindow->contentItem(), stopName);
          if (stop == nullptr) {
            qCritical() << "Couch Now Playing lost its stop control on a panel refresh";
            application.exit(EXIT_FAILURE);
            return;
          }
          stop->forceActiveFocus();
        }
        QMetaObject::invokeMethod(stop, "clicked");
        QTimer::singleShot(150, quickWindow, [quickWindow, stopName, &application] {
          auto* stopping = findVisualItem(quickWindow->contentItem(), stopName);
          if (stopping == nullptr ||
              !stopping->property("text").toString().startsWith(QStringLiteral("STOPPING")) ||
              stopping->isEnabled()) {
            qCritical() << "Now Playing did not report the stop request";
            application.exit(EXIT_FAILURE);
          }
        });
      });
      poll->start();
    }
    if (renderOverlay.startsWith("save-backups")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(120, quickWindow, [quickWindow, renderOverlay, saveFixtureGame, &application] {
        auto* details = quickWindow->findChild<QObject*>("gameDetails");
        if (!details) { application.exit(EXIT_FAILURE); return; }
        details->setProperty("selectedInstallation", QVariantMap{{"source", "RetroArch"},
            {"installPath", saveFixtureGame}, {"launchTarget", "snes9x_libretro.so"}, {"appId", "fixture"}});
        QMetaObject::invokeMethod(details, "showSaveBackups");
        QTimer::singleShot(120, quickWindow, [quickWindow, renderOverlay, saveFixtureGame, &application] {
          auto* menu = quickWindow->findChild<QObject*>("saveBackupsMenu");
          auto* version = findVisualItem(quickWindow->contentItem(), "saveBackupVersion_0");
          if (!menu || !menu->property("opened").toBool() || !version) { qCritical() << "Save backup list did not open with a selectable version"; application.exit(EXIT_FAILURE); return; }
          if (renderOverlay.endsWith("-delete")) {
            QMetaObject::invokeMethod(version, "clicked");
            QTimer::singleShot(80, quickWindow, [quickWindow, menu, &application] {
              auto* cancel = quickWindow->findChild<QQuickItem*>("cancelSaveRestore");
              auto* remove = quickWindow->findChild<QObject*>("deleteSaveBackup");
              if (!cancel || !cancel->hasActiveFocus() || !remove ||
                  menu->property("pendingVersion").toString().isEmpty()) {
                qCritical() << "Save deletion did not begin from the safe restore choice";
                application.exit(EXIT_FAILURE); return;
              }
              QMetaObject::invokeMethod(remove, "clicked");
              QTimer::singleShot(80, quickWindow, [quickWindow, menu, &application] {
                auto* cancel = quickWindow->findChild<QQuickItem*>("cancelSaveRestore");
                auto* confirm = quickWindow->findChild<QObject*>("confirmSaveRestore");
                if (!cancel || !cancel->hasActiveFocus() || !confirm ||
                    !menu->property("pendingDelete").toBool()) {
                  qCritical() << "Save deletion did not require a focused confirmation";
                  application.exit(EXIT_FAILURE);
                }
              });
            });
          } else if (renderOverlay.endsWith("-confirm")) {
            QMetaObject::invokeMethod(version, "clicked");
            QTimer::singleShot(80, quickWindow, [quickWindow, menu, saveFixtureGame, renderOverlay, &application] {
              auto* cancel = quickWindow->findChild<QQuickItem*>("cancelSaveRestore");
              auto* confirm = quickWindow->findChild<QObject*>("confirmSaveRestore");
              if (!cancel || !cancel->hasActiveFocus() || !confirm || menu->property("pendingVersion").toString().isEmpty() || menu->property("pendingShared").toBool()!=renderOverlay.contains("shared")) {
                qCritical() << "Save restore did not focus its safe cancel action";
                application.exit(EXIT_FAILURE); return;
              }
              QMetaObject::invokeMethod(confirm, "clicked");
              QFile save(QFileInfo(saveFixtureGame).dir().absolutePath() + "/../saves/Snes9x/Test Game.srm");
              if (!save.open(QIODevice::ReadOnly) || save.readAll() != "newer progress") {
                qCritical() << "Save restore UI did not restore the selected fixture version";
                application.exit(EXIT_FAILURE); return;
              }
              QTimer::singleShot(80, quickWindow, [quickWindow] {
                if (auto* latest = findVisualItem(quickWindow->contentItem(), "saveBackupVersion_0"))
                  QMetaObject::invokeMethod(latest, "clicked");
              });
            });
          }
        });
      });
    }
    if (renderOverlay == QStringLiteral("library-empty-review")) {
      unifiedGames.setSourceEnabled("Demo", false);
      quickWindow->setProperty("homeOpen", false);
      QTimer::singleShot(120, quickWindow, [quickWindow, &library, &application] {
        auto* view = quickWindow->findChild<QObject*>("libraryView");
        if (!view) { application.exit(EXIT_FAILURE); return; }
        library.setSourceFilters({"RetroArch", "Dolphin"});
        if (!quickWindow->property("emptySourceFilter").toString().isEmpty()) {
          qCritical() << "Multiple sources were presented as one missing source";
          application.exit(EXIT_FAILURE); return;
        }
        library.setSourceFilters({"RetroArch"});
        if (quickWindow->property("emptySourceFilter").toString() != "RetroArch") {
          application.exit(EXIT_FAILURE); return;
        }
        library.setSourceFilters({});
        library.setMode(LibraryFilterModel::Mode::Favorites);
        if (view->property("emptyTitle").toString() != "No favorites in this view") {
          application.exit(EXIT_FAILURE); return;
        }
        library.setMode(LibraryFilterModel::Mode::Recent);
        if (view->property("emptyTitle").toString() != "No recently played games in this view") {
          application.exit(EXIT_FAILURE); return;
        }
        library.setMode(LibraryFilterModel::Mode::All);
        library.setReviewFilter("artwork");
        if (view->property("emptyTitle").toString() != "No games are missing artwork in this view") {
          application.exit(EXIT_FAILURE); return;
        }
      });
    }
    if (renderOverlay == QStringLiteral("home-empty")) {
      unifiedGames.setSourceEnabled("Demo", false);
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
    }
    if (renderOverlay == QStringLiteral("home-wheel-stream")) {
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
      for (int tick = 0; tick < 6; ++tick) {
        QTimer::singleShot(200 + tick * 60, quickWindow, [quickWindow, &application] {
          auto* scroll = quickWindow->findChild<QQuickItem*>("homeList");
          if (!scroll) { application.exit(EXIT_FAILURE); return; }
          const auto point = scroll->mapToScene(QPointF(8, 8));
          const double before = scroll->property("contentY").toDouble();
          QWheelEvent event(point, quickWindow->mapToGlobal(point), QPoint(), QPoint(0, -120),
                            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
          QCoreApplication::sendEvent(quickWindow, &event);
          if (qAbs(scroll->property("contentY").toDouble() - before) > 1) {
            qCritical() << "A wheel tick jumped the rendered position";
            application.exit(EXIT_FAILURE);
          }
        });
      }
      QTimer::singleShot(1150, quickWindow, [quickWindow, &application] {
        auto* scroll = quickWindow->findChild<QQuickItem*>("homeList");
        const double position = scroll->property("contentY").toDouble();
        const double maximum = scroll->property("maximumScrollY").toDouble();
        if (position < 200 || position > maximum + 1) {
          qCritical() << "Continuous wheel input lost movement" << position << maximum;
          application.exit(EXIT_FAILURE);
        }
      });
    }
    if (renderOverlay == QStringLiteral("home-wheel")) {
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
      QTimer::singleShot(200, quickWindow, [quickWindow, &application, &preferences] {
        auto* scroll = quickWindow->findChild<QQuickItem*>("homeList");
        auto* screen = quickWindow->findChild<QQuickItem*>("homeScreen");
        if (!scroll || !screen) { application.exit(EXIT_FAILURE); return; }
        const auto wheel = [quickWindow, scroll](int angle, int pixel = 0) {
          const auto point = scroll->mapToScene(QPointF(8, 8));
          QWheelEvent event(point, quickWindow->mapToGlobal(point), QPoint(0, pixel), QPoint(0, angle),
                            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
          QCoreApplication::sendEvent(quickWindow, &event);
        };
        scroll->setProperty("contentY", 0);
        wheel(-120);
        wheel(-120);
        const double target = scroll->property("wheelTargetY").toDouble();
        if (target <= 100 || (!preferences.reducedMotion() && scroll->property("contentY").toDouble() >= target)) {
          qCritical() << "Home wheel did not accumulate smooth movement";
          application.exit(EXIT_FAILURE); return;
        }
        QTimer::singleShot(350, quickWindow, [quickWindow, scroll, screen, wheel, target, &application] {
          if (qAbs(scroll->property("contentY").toDouble() - target) > 1) {
            qCritical() << "Home wheel did not settle at its target" << scroll->property("contentY") << target;
            application.exit(EXIT_FAILURE); return;
          }
          const auto stopWheelScroll = [screen, &application] {
            if (QMetaObject::invokeMethod(screen, "stopWheelScroll")) return true;
            qCritical() << "Home wheel stop method could not be invoked";
            application.exit(EXIT_FAILURE);
            return false;
          };
          wheel(-120);
          const double before = scroll->property("contentY").toDouble();
          wheel(120);
          if (scroll->property("wheelTargetY").toDouble() >= before) {
            qCritical() << "Home wheel reversal retained forward momentum";
            application.exit(EXIT_FAILURE); return;
          }
          if (!stopWheelScroll()) return;
          scroll->setProperty("contentY", 100);
          wheel(0, 25);
          if (qAbs(scroll->property("contentY").toDouble() - 75) > 1) {
            qCritical() << "Home pixel scrolling was delayed";
            application.exit(EXIT_FAILURE); return;
          }
          const double maximum = scroll->property("maximumScrollY").toDouble();
          scroll->setProperty("contentY", maximum);
          wheel(-120);
          if (scroll->property("wheelTargetY").toDouble() > maximum) {
            qCritical() << "Home wheel escaped content bounds";
            application.exit(EXIT_FAILURE); return;
          }
          if (!stopWheelScroll()) return;
          scroll->setProperty("contentY", 0);
          wheel(-120);
          auto* first = quickWindow->findChild<QQuickItem*>("homeFeaturedOpen");
          QMetaObject::invokeMethod(screen, "reveal", Q_ARG(QVariant, QVariant::fromValue(first)));
          const double revealed = scroll->property("contentY").toDouble();
          QTimer::singleShot(350, quickWindow, [scroll, revealed, &application] {
            if (qAbs(scroll->property("contentY").toDouble() - revealed) > 1) {
              qCritical() << "Home wheel fought navigation reveal";
              application.exit(EXIT_FAILURE);
            }
          });
        });
      });
    }
    if (renderOverlay == QStringLiteral("home-delayed")) {
      const QSize originalSize = quickWindow->size();
      QTimer::singleShot(250, quickWindow, [quickWindow] { quickWindow->setProperty("homeOpen", true); });
      QTimer::singleShot(400, quickWindow, [quickWindow, &home] {
        quickWindow->resize(820, 590);
        home.enqueue("Demo", "", "demo-9");
      });
      QTimer::singleShot(550, quickWindow, [quickWindow] { quickWindow->setProperty("homeOpen", false); });
      QTimer::singleShot(650, quickWindow, [quickWindow, originalSize] {
        quickWindow->resize(originalSize);
        quickWindow->setProperty("homeOpen", true);
      });
    }
    if (renderOverlay == QStringLiteral("home-overview")) {
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
    }
    if (renderOverlay == QStringLiteral("home-full-queue")) {
      for (int i = 0; i < 100; ++i) home.enqueue("Demo", "", QString("demo-%1").arg(i));
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
      QObject::connect(quickWindow, &QQuickWindow::frameSwapped, quickWindow, [quickWindow, &home, &application, &controller] {
        auto* screen = quickWindow->findChild<QQuickItem*>("homeScreen");
        if (!screen || home.queue().size() != 100) {
          qCritical() << "Full queue fixture did not load";
          application.exit(EXIT_FAILURE); return;
        }
        quickWindow->requestActivate();
        QMetaObject::invokeMethod(screen, "focusHome");
        auto* first = quickWindow->activeFocusItem();
        QSet<QString> queueTiles;
        bool wrapped = false;
        for (int step = 0; step < 500; ++step) {
          deliverKeyboardTestKey(quickWindow, Qt::Key_Tab);
          auto* focused = quickWindow->activeFocusItem();
          if (!focused || !focused->isVisible()) break;
          if (focused->objectName().startsWith("homeTile-queue:")) queueTiles.insert(focused->objectName());
          if (focused == first) { wrapped = true; break; }
        }
        if (!wrapped || queueTiles.size() != 100) {
          qCritical() << "Full queue traversal failed" << wrapped << queueTiles.size();
          application.exit(EXIT_FAILURE); return;
        }
        const auto last = home.queue().last().toMap();
        const auto lastIdentity = "queue:" + last.value("queueKey").toString();
        auto* shelf = screen->findChild<QQuickItem*>("homeQueueShelf");
        auto* shelfContent = shelf ? shelf->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!shelf || !shelfContent || shelf->property("contentWidth").toReal() <= shelf->width()) {
          qCritical() << "Full queue shelf did not become a horizontally scrollable row";
          application.exit(EXIT_FAILURE); return;
        }
        auto* pageScroll = screen->findChild<QQuickItem*>("homeList");
        if (!pageScroll) {
          qCritical() << "Home shelf scroll test could not find its page scroll";
          application.exit(EXIT_FAILURE); return;
        }
        if (!QMetaObject::invokeMethod(screen, "stopWheelScroll")) {
          qCritical() << "Home shelf scroll test could not stop page scrolling";
          application.exit(EXIT_FAILURE); return;
        }
        shelf->setProperty("contentX", 0);
        const auto shelfPoint = [&shelf] {
          return shelf->mapToScene(QPointF(shelf->width() / 2, shelf->height() / 2));
        };
        const QRectF pageBounds = pageScroll->mapRectToScene(pageScroll->boundingRect());
        const QRectF wheelShelfBounds = shelf->mapRectToScene(shelf->boundingRect());
        if (!pageBounds.intersects(wheelShelfBounds)) {
          qCritical() << "Full queue shelf is outside the page viewport during wheel testing"
                      << pageBounds << wheelShelfBounds;
          application.exit(EXIT_FAILURE); return;
        }
        const qreal pageTargetBefore = pageScroll->property("wheelTargetY").toReal();
        const bool scrollDown = pageScroll->property("contentY").toReal() + 1 <
                                pageScroll->property("maximumScrollY").toReal();
        const int verticalAngle = scrollDown ? -120 : 120;
        QWheelEvent verticalWheel(shelfPoint(), quickWindow->mapToGlobal(shelfPoint()),
                                  QPoint(), QPoint(0, verticalAngle), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(quickWindow, &verticalWheel);
        QCoreApplication::processEvents();
        const qreal pageTargetAfter = pageScroll->property("wheelTargetY").toReal();
        if (shelf->property("contentX").toReal() != 0 ||
            (scrollDown ? pageTargetAfter <= pageTargetBefore : pageTargetAfter >= pageTargetBefore)) {
          qCritical() << "Plain vertical wheel did not pass from the shelf to the page"
                      << shelf->property("contentX") << pageTargetBefore << pageTargetAfter
                      << pageScroll->property("contentHeight") << pageScroll->height()
                      << pageScroll->property("contentY") << verticalWheel.isAccepted();
          application.exit(EXIT_FAILURE); return;
        }
        QMetaObject::invokeMethod(screen, "stopWheelScroll");
        QWheelEvent horizontalWheel(shelfPoint(), quickWindow->mapToGlobal(shelfPoint()),
                                    QPoint(), QPoint(-120, 0), Qt::NoButton, Qt::NoModifier,
                                    Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(quickWindow, &horizontalWheel);
        if (shelf->property("contentX").toReal() <= 0) {
          qCritical() << "Horizontal wheel did not scroll the Home shelf"
                      << "point" << shelfPoint() << "shelf"
                      << shelf->mapRectToScene(shelf->boundingRect()) << "page"
                      << pageScroll->mapRectToScene(pageScroll->boundingRect())
                      << "pageY" << pageScroll->property("contentY")
                      << "accepted" << horizontalWheel.isAccepted();
          application.exit(EXIT_FAILURE); return;
        }
        shelf->setProperty("contentX", 0);
        QWheelEvent shiftedWheel(shelfPoint(), quickWindow->mapToGlobal(shelfPoint()),
                                 QPoint(), QPoint(0, -120), Qt::NoButton, Qt::ShiftModifier,
                                 Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(quickWindow, &shiftedWheel);
        if (shelf->property("contentX").toReal() <= 0) {
          qCritical() << "Shift+vertical wheel did not scroll the Home shelf";
          application.exit(EXIT_FAILURE); return;
        }
        QMetaObject::invokeMethod(screen, "stopWheelScroll");
        shelf->setProperty("contentX", 0);
        if (!(shelf->property("acceptedButtons").toInt() & int(Qt::LeftButton))) {
          qCritical() << "Home shelf does not accept mouse drag input";
          application.exit(EXIT_FAILURE); return;
        }
        QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, lastIdentity));
        QCoreApplication::processEvents();
        QQuickItem* lastTile = nullptr;
        qreal rowY = -1;
        int tileCount = 0;
        for (auto* tile : shelfContent->childItems()) {
          if (!tile->property("game").isValid()) continue;
          if (rowY < 0) rowY = tile->y();
          if (qAbs(tile->y() - rowY) > 1) {
            qCritical() << "Home shelf wrapped onto a second row" << tile->y() << rowY;
            application.exit(EXIT_FAILURE); return;
          }
          if (tile->property("index").toInt() == 99) lastTile = tile;
          ++tileCount;
        }
        auto* lastOpen = lastTile ? lastTile->property("openControl").value<QQuickItem*>() : nullptr;
        const QRectF shelfBounds = shelf->mapRectToScene(shelf->boundingRect());
        const QRectF lastBounds = lastOpen
                                      ? lastOpen->mapRectToScene(lastOpen->boundingRect())
                                      : QRectF();
        if (tileCount != 100 || !lastOpen || !lastOpen->hasActiveFocus() ||
            shelf->property("contentX").toReal() <= 0 ||
            !shelfBounds.contains(lastBounds)) {
          qCritical() << "Home focus did not reveal the final card in the horizontal shelf"
                      << tileCount << shelf->property("contentX") << lastBounds << shelfBounds;
          application.exit(EXIT_FAILURE); return;
        }
        const auto previousIdentity = "queue:" + home.queue()[98].toMap().value("queueKey").toString();
        controller.focusDirectionRequested(Qt::Key_Left);
        QCoreApplication::processEvents();
        if (screen->property("focusedIdentity").toString() != previousIdentity) {
          qCritical() << "Left did not move to the previous horizontally scrolling card";
          application.exit(EXIT_FAILURE); return;
        }
        controller.focusDirectionRequested(Qt::Key_Right);
        QCoreApplication::processEvents();
        if (screen->property("focusedIdentity").toString() != lastIdentity) {
          qCritical() << "Right did not restore focus to the last horizontal card";
          application.exit(EXIT_FAILURE); return;
        }
        QMetaObject::invokeMethod(screen, "queueAction", Q_ARG(QVariant, last), Q_ARG(QVariant, QString("up")));
        QCoreApplication::processEvents();
        if (screen->property("focusedIdentity").toString() != lastIdentity ||
            home.queue()[98].toMap().value("queueKey") != last.value("queueKey")) {
          qCritical() << "Full queue reorder lost focus";
          application.exit(EXIT_FAILURE); return;
        }
        const auto neighborIdentity = "queue:" + home.queue()[99].toMap().value("queueKey").toString();
        QMetaObject::invokeMethod(screen, "queueAction", Q_ARG(QVariant, last), Q_ARG(QVariant, QString("remove")));
        QCoreApplication::processEvents();
        if (home.queue().size() != 99 || screen->property("focusedIdentity").toString() != neighborIdentity) {
          qCritical() << "Full queue removal lost its neighboring game";
          application.exit(EXIT_FAILURE); return;
        }
        // Up to the toolbar from far down the page brings the top of the page back.
        const qreal pageTop = pageScroll->property("originY").toReal();
        const qreal pageBottom = pageScroll->property("maximumScrollY").toReal();
        QMetaObject::invokeMethod(screen, "stopWheelScroll");
        pageScroll->setProperty("contentY", pageBottom);
        QMetaObject::invokeMethod(screen, "focusHome");
        QCoreApplication::processEvents();
        if (pageBottom <= pageTop + 1 || pageScroll->property("contentY").toReal() != pageTop) {
          qCritical() << "Returning to the Home toolbar left the page scrolled down"
                      << pageTop << pageBottom << pageScroll->property("contentY");
          application.exit(EXIT_FAILURE); return;
        }
        quickWindow->setProperty("fullQueueChecked", true);
      }, Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    }
    if (renderOverlay == QStringLiteral("home")) {
      for (const auto* id : {"demo-1", "demo-2", "demo-3"})
        home.enqueue("Demo", "", id);
      library.setSearchText("unmatched-home-original");
      quickWindow->setProperty("homeOpen", true);
      home.refresh();
      QTimer::singleShot(180, quickWindow, [quickWindow, &home, &library, &application, &controller] {
        auto* screen = quickWindow->findChild<QQuickItem*>("homeScreen");
        if (!screen || !screen->isVisible() || home.recent().isEmpty() ||
            home.queue().size() != 3) {
          qCritical() << "Home fixture did not load";
          application.exit(EXIT_FAILURE);
          return;
        }
        quickWindow->requestActivate();
        const auto identity = home.recent().first().toMap().value("identity");
        QMetaObject::invokeMethod(screen, "focusHome");
        auto* firstControl = quickWindow->activeFocusItem();
        if (quickWindow->property("couchMode").toBool()) {
          auto* sharedHeader = quickWindow->findChild<QQuickItem*>("homeAppHeader");
          auto* couchHomeButton = quickWindow->findChild<QQuickItem*>("homeCouchHomeButton");
          if (!sharedHeader || sharedHeader->isVisible() || !couchHomeButton ||
              firstControl != couchHomeButton) {
            qCritical() << "Couch Home did not retain its couch header";
            application.exit(EXIT_FAILURE); return;
          }
        } else {
          const char* headerNames[] = {"homeOpenHomeButton", "homeLibraryDestinationButton",
                                       "homeStatsDestinationButton", "homeSettingsButton",
                                       "homeCouchModeButton"};
          QQuickItem* headerButtons[5]{};
          for (int index = 0; index < 5; ++index) {
            headerButtons[index] = quickWindow->findChild<QQuickItem*>(headerNames[index]);
            if (!headerButtons[index] || !headerButtons[index]->isVisible()) {
              qCritical() << "Home shared header is missing a desktop destination"
                          << headerNames[index];
              application.exit(EXIT_FAILURE);
              return;
            }
          }
          if (firstControl != headerButtons[0]) {
            qCritical() << "Home focus did not start on the Home destination";
            application.exit(EXIT_FAILURE);
            return;
          }
          for (int index = 1; index < 5; ++index) {
            controller.focusDirectionRequested(Qt::Key_Right);
            if (!headerButtons[index]->hasActiveFocus()) {
              qCritical() << "Home header Right skipped" << headerNames[index];
              application.exit(EXIT_FAILURE);
              return;
            }
          }
          for (int index = 3; index >= 0; --index) {
            controller.focusDirectionRequested(Qt::Key_Left);
            if (!headerButtons[index]->hasActiveFocus()) {
              qCritical() << "Home header Left skipped" << headerNames[index];
              application.exit(EXIT_FAILURE);
              return;
            }
          }
        }
        firstControl->forceActiveFocus();
        QSet<QString> tileControls;
        bool returnedToHeader = false;
        for (int step = 0; step < 150; ++step) {
          deliverKeyboardTestKey(quickWindow, Qt::Key_Tab);
          auto* focused = quickWindow->activeFocusItem();
          if (!focused || !focused->isVisible()) { application.exit(EXIT_FAILURE); return; }
          if (focused->objectName().startsWith("homeTile-")) {
            const auto rect = focused->mapRectToScene(QRectF(0, 0, focused->width(), focused->height()));
            if (rect.left() < 0 || rect.right() > quickWindow->width() + 1 ||
                rect.top() < 0 || rect.bottom() > quickWindow->height() + 1) {
              qCritical() << "Home tile focus is outside the viewport";
              application.exit(EXIT_FAILURE); return;
            }
            tileControls.insert(focused->objectName());
          }
          if (focused == firstControl) { returnedToHeader = true; break; }
        }
        if (!returnedToHeader || tileControls.size() < 8) {
          qCritical() << "Home Tab traversal skipped its game tiles";
          application.exit(EXIT_FAILURE); return;
        }
        if (home.recent().size() > 1 && !home.queue().isEmpty()) {
          const QVariantMap card = home.recent().at(1).toMap();
          const QString identity = card.value("identity").toString();
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, identity));
          auto* upNext = findVisualItem(quickWindow->contentItem(),
                                        QStringLiteral("homeTileAction-") + identity);
          if (!upNext || !upNext->isVisible()) {
            qCritical() << "A focused Home card did not show its Up Next action";
            application.exit(EXIT_FAILURE); return;
          }
          // X runs the card's action, so Down stays free to move to the next shelf.
          screen->setProperty("notice", QString());
          controller.favoriteRequested();
          QCoreApplication::processEvents();
          if (screen->property("notice").toString() != QStringLiteral("Added to Up next")) {
            qCritical() << "X on a Home card did not run its Up next action"
                        << screen->property("notice");
            application.exit(EXIT_FAILURE); return;
          }
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, identity));
          controller.focusDirectionRequested(Qt::Key_Down);
          QCoreApplication::processEvents();
          auto* queueShelf = screen->findChild<QQuickItem*>(QStringLiteral("homeQueueShelf"));
          auto* queueContent = queueShelf
                                   ? queueShelf->property("contentItem").value<QQuickItem*>()
                                   : nullptr;
          bool reachedNextShelf = false;
          if (queueContent) {
            for (auto* tile : queueContent->childItems()) {
              auto* open = tile->property("openControl").value<QQuickItem*>();
              if (tile->property("game").isValid() &&
                  open == quickWindow->activeFocusItem()) reachedNextShelf = true;
            }
          }
          if (!reachedNextShelf) {
            qCritical() << "Down from a Home card did not move to the following shelf";
            application.exit(EXIT_FAILURE); return;
          }
        }
        QMetaObject::invokeMethod(screen, "focusHome");
        deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
        if (screen->property("focusedIdentity") != identity) {
          qCritical() << "Home header did not navigate to the first game";
          application.exit(EXIT_FAILURE);
          return;
        }
        deliverKeyboardTestKey(quickWindow, Qt::Key_Up);
        if (quickWindow->activeFocusItem() != firstControl) {
          qCritical() << "Home featured game could not return to the header";
          application.exit(EXIT_FAILURE); return;
        }
        deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
        if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedPlay") {
          qCritical() << "Home did not prioritize Play";
          application.exit(EXIT_FAILURE); return;
        }
        // Down out of Play's row starts the shelves at their first card. A narrow window
        // wraps the buttons, so Down first visits the wrapped row.
        if (home.recent().size() > 1) {
          const QString firstCard = QStringLiteral("homeTile-") +
                                    home.recent().at(1).toMap().value("identity").toString();
          auto* heroActions = quickWindow->activeFocusItem()->parentItem();
          for (int press = 0; press < 3 && quickWindow->activeFocusItem() &&
                              quickWindow->activeFocusItem()->parentItem() == heroActions;
               ++press) {
            deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
          }
          if (!quickWindow->activeFocusItem() || quickWindow->activeFocusItem()->objectName() != firstCard) {
            qCritical() << "Down from Home Play did not reach the first shelf card"
                        << (quickWindow->activeFocusItem() ? quickWindow->activeFocusItem()->objectName() : QString());
            application.exit(EXIT_FAILURE); return;
          }
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, identity));
          if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedPlay") {
            qCritical() << "Home could not return focus to Play";
            application.exit(EXIT_FAILURE); return;
          }
        }
        // Demo mode exercises the normal launch route without starting an emulator.
        deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
        if (!quickWindow->property("detailOpen").toBool()) {
          qCritical() << "Home Play did not select its game";
          application.exit(EXIT_FAILURE); return;
        }
        QMetaObject::invokeMethod(quickWindow, "closeDetails");
        QCoreApplication::processEvents();
        if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedPlay") {
          qCritical() << "Home did not restore Play focus";
          application.exit(EXIT_FAILURE); return;
        }
        deliverKeyboardTestKey(quickWindow, Qt::Key_Right);
        if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedOpen") {
          qCritical() << "Home Details is not beside Play in navigation";
          application.exit(EXIT_FAILURE); return;
        }
        deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
        if (!quickWindow->property("detailOpen").toBool()) {
          qCritical() << "Home could not open game details using keyboard";
          application.exit(EXIT_FAILURE);
          return;
        }
        QMetaObject::invokeMethod(quickWindow, "closeDetails");
        QCoreApplication::processEvents();
        if (!quickWindow->activeFocusItem() || quickWindow->activeFocusItem()->objectName() != "homeFeaturedOpen") {
          qCritical() << "Home did not restore the Details action";
          application.exit(EXIT_FAILURE); return;
        }
        if (library.searchText() != "unmatched-home-original" ||
            !quickWindow->property("homeOpen").toBool()) {
          qCritical() << "Home did not restore library filters";
          application.exit(EXIT_FAILURE);
          return;
        }
        const auto key = home.queue().last().toMap().value("queueKey").toString();
        if (!home.move(key, -1) ||
            home.queue().at(1).toMap().value("queueKey").toString() != key) {
          application.exit(EXIT_FAILURE);
          return;
        }
        QTimer::singleShot(80, quickWindow, [quickWindow, screen, &home, &application] {
          const QVariant firstKey = "queue:" + home.queue().first().toMap().value("queueKey").toString();
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, firstKey));
          QMetaObject::invokeMethod(screen, "focusQueueActions", Q_ARG(QVariant, firstKey));
          deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
          QCoreApplication::processEvents();
          deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
          auto* focused = quickWindow->activeFocusItem();
          if (!focused || focused->property("text").toString() != "REMOVE") {
            qCritical() << "Home queue actions are not reachable using navigation";
            application.exit(EXIT_FAILURE);
            return;
          }
          deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
          QCoreApplication::processEvents();
          if (home.queue().size() != 2) {
            qCritical() << "Home keyboard remove failed";
            application.exit(EXIT_FAILURE);
            return;
          }
          auto* browse = screen->findChild<QQuickItem*>("homeBrowseAll");
          if (!browse) { application.exit(EXIT_FAILURE); return; }
          browse->forceActiveFocus();
          deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
          auto* libraryModel = qmlContext(quickWindow)->contextProperty("Library").value<QObject*>();
          if (quickWindow->property("homeOpen").toBool() || !libraryModel ||
              !libraryModel->property("searchText").toString().isEmpty() ||
              libraryModel->property("mode").toInt() != 0) {
            qCritical() << "Home quick access retained stale filters";
            application.exit(EXIT_FAILURE); return;
          }
          quickWindow->setProperty("homeOpen", true);
          QCoreApplication::processEvents();
          // Leave the queue visible for the narrow-layout screenshot.
          const QVariant remaining = "queue:" + home.queue().first().toMap().value("queueKey").toString();
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, remaining));
        });
      });
    }
    if (renderOverlay == QStringLiteral("metadata-filters") ||
        renderOverlay == QStringLiteral("review-filters")) {
      const bool review = renderOverlay == QStringLiteral("review-filters");
      QTimer::singleShot(120, quickWindow, [quickWindow, &application, review] {
        auto* button = quickWindow->findChild<QQuickItem*>(review ? "reviewFilterButton" : "decadeFilterButton");
        auto* library = qmlContext(quickWindow)->contextProperty("Library").value<QObject*>();
        if (!button || !library) {
          application.exit(EXIT_FAILURE);
          return;
        }
        quickWindow->requestActivate();
        auto* filters = quickWindow->findChild<QQuickItem*>("filtersMenuButton");
        if (filters) QMetaObject::invokeMethod(filters, "clicked");
        button->forceActiveFocus();
        deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
        QTimer::singleShot(120, quickWindow, [quickWindow, library, button, &application, review] {
          deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
          deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
          if (library->property(review ? "reviewFilter" : "decadeFilter").toString().isEmpty() ||
              quickWindow->property("filterPickerOpen").toBool()) {
            qCritical() << "Metadata review/decade picker failed";
            application.exit(EXIT_FAILURE);
            return;
          }
          QMetaObject::invokeMethod(quickWindow, "clearLibraryFilters");
          if (!library->property(review ? "reviewFilter" : "decadeFilter").toString().isEmpty()) {
            application.exit(EXIT_FAILURE);
            return;
          }
          // Let the picker finish returning focus to the filters menu before reopening it.
          QTimer::singleShot(120, quickWindow, [button] {
            QMetaObject::invokeMethod(button, "clicked");
          });
        });
      });
    }
    if (renderOverlay.startsWith(QStringLiteral("game-info"))) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(120, quickWindow, [quickWindow, renderOverlay, screenshotPath,
                                           metadata = gameMetadata.get(), &application] {
        auto* section = quickWindow->findChild<QQuickItem*>("gameInfoSection");
        auto* details = quickWindow->findChild<QQuickItem*>("gameDetails");
        if (!section || !details) {
          application.exit(EXIT_FAILURE);
          return;
        }
        if (renderOverlay == "game-info-overview-long") {
          auto game = quickWindow->property("selectedGame").toMap();
          game["title"] = "The Legend of an Exceptionally Long International Adventure: Complete Anniversary Collection (North America, Revision 1)";
          game["playtimeSeconds"] = 45000;
          game["playtimeText"] = "12h 30m";
          game["lastPlayed"] = 1700000000;
          game["completionStatus"] = "playing";
          game["achievementsTotal"] = 100;
          game["achievementsUnlocked"] = 42;
          quickWindow->setProperty("selectedGame", game);
        }
        QVariantMap entry;
        if (renderOverlay != "game-info-empty" && renderOverlay != "game-info-rom-only"
            && renderOverlay != "game-info-native-port") {
          entry = {
              {"year", 1997},
              {"rating", 92},
              {"ratingCount", 560},
              {"releaseText", UserDateFormat::format(QDate(1997, 7, 28))},
              {"releaseLabel", "North America release"},
              {"romContext", "ROM region: North America · Revision: 1"},
              {"title", "Catalog title"},
              {"titleEvidence",
               QStringList{"Regional title (North American title)", "別の名前 (Japan)"}},
              {"platformText", "Nintendo Switch"},
              {"genres", QStringList{"Adventure", "Role-playing (RPG)"}},
              {"developers", QStringList{"Example Studio"}},
              {"publishers", QStringList{"Example Publisher"}},
              {"summary", QStringLiteral("Explore a quiet mountain town and uncover the stories "
                                         "its residents have left behind. Each journey opens new "
                                         "paths through forests, "
                                         "old observatories and forgotten gardens. ")
                              .repeated(8)}};
        }
        if (renderOverlay == "game-info-rom-only") {
          auto game = quickWindow->property("selectedGame").toMap();
          game["title"] = "Game (USA)";
          game["source"] = "RetroArch";
          game["description"] = QString();
          quickWindow->setProperty("selectedGame", game);
        }
        if (renderOverlay == "game-info-native-port") {
          auto game = quickWindow->property("selectedGame").toMap();
          game["title"] = "Super Mario 64";
          game["source"] = "RetroArch";
          game["system"] = "n64";
          game["description"] = QString();
          quickWindow->setProperty("selectedGame", game);
          quickWindow->setProperty("selectedInstallation", game);
          // No selected metadata or setup prompt: only the port suggestion can
          // make this empty details section visible.
          if (metadata) metadata->inspect({});
        }
        if (renderOverlay == "game-info-rating-only") {
          entry["year"] = 0;
          entry["platformText"] = QString();
          entry["releaseText"] = QString();
          entry["rating"] = 92;
          entry["ratingCount"] = 560;
          auto game = quickWindow->property("selectedGame").toMap();
          game["system"] = QString();
          game["year"] = 0;
          quickWindow->setProperty("selectedGame", game);
        }
        if (renderOverlay.startsWith("game-info-hero-")) {
          QImage image(960, 540, QImage::Format_RGB32);
          image.fill(QColor("#245b75"));
          QPainter painter(&image);
          painter.fillRect(0, 0, 80, 540, QColor("#d79b56"));
          painter.fillRect(880, 0, 80, 540, QColor("#75bf87"));
          painter.setPen(Qt::white);
          painter.drawText(image.rect(), Qt::AlignCenter, "FULL SCENE");
          painter.end();
          const QString imagePath = screenshotPath + ".hero.png";
          if (!image.save(imagePath)) { application.exit(EXIT_FAILURE); return; }
          const QString imageUrl = QUrl::fromLocalFile(imagePath).toString();
          auto game = quickWindow->property("selectedGame").toMap();
          game["coverPath"] = imageUrl;
          game["heroPath"] = renderOverlay.endsWith("custom") ? imageUrl : QString();
          quickWindow->setProperty("selectedGame", game);
          entry["heroUrl"] = imageUrl;
          if (renderOverlay.endsWith("screenshot")) entry["heroKind"] = "screenshot";
        }
        if (renderOverlay == "game-info-real") {
          QFile fixture(optionValue(application.arguments(), "--render-game-info-file"));
          if (!fixture.open(QIODevice::ReadOnly)) { application.exit(EXIT_FAILURE); return; }
          const auto data = QJsonDocument::fromJson(fixture.readAll()).object();
          const auto game = data.value("game").toObject().toVariantMap();
          entry = data.value("metadata").toObject().toVariantMap();
          if (game.isEmpty() || entry.isEmpty()) { application.exit(EXIT_FAILURE); return; }
          quickWindow->setProperty("selectedGame", game);
          quickWindow->setProperty("selectedInstallation", game);
          if (auto* editor = quickWindow->findChild<QQuickItem*>("metadataEditor"))
            QQmlProperty::write(editor, "entry", entry);
        }
        quickWindow->requestActivate();
        details->setProperty("showOrganizationControls", true);
        section->setProperty("entry", entry);
        QTimer::singleShot(
            100, quickWindow, [quickWindow, section, details, renderOverlay, &application] {
              auto* toggle = quickWindow->findChild<QQuickItem*>("descriptionToggle");
              auto* description = quickWindow->findChild<QQuickItem*>("gameDescription");
              if (renderOverlay == "game-info-native-port") {
                auto* portButton = findVisualItem(quickWindow->contentItem(), "nativePortButton");
                // Every catalog port for the title is suggested; none are installed here.
                const auto catalogPorts = PortCatalog::bundled().portsFor(
                    QStringLiteral("n64"), QString(), QStringLiteral("Super Mario 64"));
                if (details->property("metadataStatusShown").toBool() ||
                    !section->property("entry").toMap().isEmpty() ||
                    !section->property("background").toString().isEmpty() ||
                    details->property("displayTitle").toString() != "Super Mario 64" ||
                    catalogPorts.isEmpty() ||
                    details->property("nativePortSuggestions").toList().size() != catalogPorts.size() ||
                    !portButton || !portButton->isVisible()) {
                  qCritical() << "Native port suggestion hidden without catalog metadata"
                              << details->property("metadataStatusShown")
                              << section->property("entry")
                              << details->property("nativePortSuggestions")
                              << (portButton && portButton->isVisible());
                  application.exit(EXIT_FAILURE);
                  return;
                }
                // Different ports can share an interpreter, but never their session identity.
                const QVariant original = details->property("selectedInstallation");
                const QVariant originalInstallations = details->property("installations");
                const QVariantMap first{{"source", "Ports"}, {"appId", "shipwright"},
                                        {"installPath", "/usr/bin/env"}};
                const QVariantMap second{{"source", "Ports"}, {"appId", "starship"},
                                         {"installPath", "/usr/bin/env"}};
                details->setProperty("selectedInstallation", first);
                details->setProperty("installations", QVariantList{first, second});
                const auto paths = details->property("sessionHistoryPaths").toList();
                QVariant matches;
                const QVariant session = QVariantMap{{"source", "Ports"}, {"path", "shipwright"}};
                const bool invoked = QMetaObject::invokeMethod(
                    details, "isSessionForGame", Q_RETURN_ARG(QVariant, matches),
                    Q_ARG(QVariant, session));
                if (paths != QVariantList{QString("shipwright"), QString("starship")} ||
                    !invoked || !matches.toBool()) {
                  qCritical() << "Ports session identity used its shared interpreter";
                  application.exit(EXIT_FAILURE);
                }
                details->setProperty("selectedInstallation", original);
                details->setProperty("installations", originalInstallations);
                return;
              }
              if (renderOverlay == "game-info-rom-only") {
                auto* romToggle = quickWindow->findChild<QQuickItem*>("romDetailsToggle");
                if (details->property("displayTitle").toString() != "Game" ||
                    details->property("metadataStatusShown").toBool() ||
                    !section->isVisible() || !romToggle || !romToggle->isVisible()) {
                  qCritical() << "ROM details hidden without metadata or description";
                  application.exit(EXIT_FAILURE);
                  return;
                }
                QMetaObject::invokeMethod(romToggle, "clicked");
                if (!details->property("romDetailsExpanded").toBool()) {
                  qCritical() << "ROM details did not expand";
                  application.exit(EXIT_FAILURE);
                }
                return;
              }
              if (renderOverlay.startsWith("game-info-hero-")) {
                auto* hero = quickWindow->findChild<QQuickItem*>("detailsHero");
                const bool legacy = renderOverlay.endsWith("legacy");
                if (!hero || (legacy ? !hero->property("source").toUrl().isEmpty()
                                    : hero->property("status").toInt() != 1) ||
                    hero->property("fillMode").toInt() != 1 ||
                    hero->width() > details->width() || hero->height() > details->height()) {
                  qCritical() << "Detail backdrop selection or fit failed";
                  application.exit(EXIT_FAILURE);
                }
                return;
              }
              if (renderOverlay.startsWith("game-info-tooltip")) {
                auto* rating = quickWindow->findChild<QQuickItem*>("gameRating");
                auto* platform = quickWindow->findChild<QQuickItem*>("gamePlatformRelease");
                auto* tooltip = quickWindow->findChild<QObject*>("gameRatingTooltip");
                if (!rating || !platform || !tooltip) {
                  qCritical() << "Rating tooltip fixture missing";
                  application.exit(EXIT_FAILURE); return;
                }
                const bool show = renderOverlay.endsWith("rating");
                const bool missing = renderOverlay.endsWith("missing");
                if (missing) {
                  auto entry = section->property("entry").toMap();
                  entry.remove("rating");
                  section->setProperty("entry", entry);
                }
                auto* target = show ? rating : platform;
                // Exercise both ends of the platform/date text, then the rating separately.
                const QPointF local(renderOverlay.endsWith("date") ? target->width() - 2 : 2,
                                    target->height() / 2);
                const QPointF scene = target->mapToScene(local);
                QMouseEvent move(QEvent::MouseMove, scene, quickWindow->mapToGlobal(scene),
                                 Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(quickWindow, &move);
                QTimer::singleShot(600, quickWindow, [quickWindow, rating, tooltip, show, missing, &application] {
                  if (tooltip->property("visible").toBool() != show || (missing && rating->isVisible())) {
                    qCritical() << "Rating tooltip hover boundary failed" << show << missing;
                    application.exit(EXIT_FAILURE); return;
                  }
                  if (show) {
                    auto* content = tooltip->property("contentItem").value<QQuickItem*>();
                    const auto bounds = content ? content->mapRectToScene(content->boundingRect()) : QRectF{};
                    const auto anchor = rating->mapRectToScene(rating->boundingRect());
                    if (!content || bounds.left() < 0 || bounds.right() > quickWindow->width() ||
                        bounds.top() < 0 || bounds.bottom() > quickWindow->height() ||
                        qAbs(bounds.top() - anchor.bottom()) > 30) {
                      qCritical() << "Rating tooltip is detached or outside window" << bounds << anchor;
                      application.exit(EXIT_FAILURE);
                    }
                  }
                });
                return;
              }
              if (renderOverlay == "game-info-real") {
                auto* hero = quickWindow->findChild<QQuickItem*>("detailsHero");
                if (!description || description->property("text").toString().isEmpty()
                    || !hero || hero->property("status").toInt() != 1) {
                  qCritical() << "Real game fixture is missing description or artwork";
                  application.exit(EXIT_FAILURE);
                }
                return;
              }
              if (renderOverlay == "game-info-empty") {
                if (section->isVisible() && (!description || description->property("text").toString().isEmpty()))
                  application.exit(EXIT_FAILURE);
                return;
              }
              if (renderOverlay == "game-info-rating-only") {
                auto* rating = quickWindow->findChild<QQuickItem*>("gameRating");
                auto* platform = quickWindow->findChild<QQuickItem*>("gamePlatformRelease");
                if (!rating || !platform || platform->isVisible() ||
                    rating->property("text").toString() != QStringLiteral("92/100 · IGDB")) {
                  qCritical() << "Rating-only details included an orphan separator"
                              << (platform ? platform->isVisible() : false)
                              << (platform ? platform->property("text") : QVariant())
                              << quickWindow->property("selectedGame").toMap().value("system")
                              << (rating ? rating->property("text") : QVariant());
                  application.exit(EXIT_FAILURE);
                }
                return;
              }
              if (renderOverlay == "game-info") {
                auto* activity = quickWindow->findChild<QQuickItem*>("gameActivitySummary");
                auto* achievementCount =
                    quickWindow->findChild<QQuickItem*>("achievementCountText");
                auto* refresh =
                    quickWindow->findChild<QQuickItem*>("achievementRefreshButton");
                QObject* achievements =
                    qmlContext(quickWindow)->contextProperty("Achievements").value<QObject*>();
                const int total = details->property("displayedAchievementTotal").toInt();
                const int unlocked =
                    details->property("displayedAchievementsUnlocked").toInt();
                const QString count = QStringLiteral("%1 / %2").arg(unlocked).arg(total);
                if (!activity || !achievementCount ||
                    achievementCount->property("text").toString() != count ||
                    (total > 0 && !activity->property("text").toString().contains(
                                      QStringLiteral("%1/%2 achievements")
                                          .arg(unlocked)
                                          .arg(total)))) {
                  qCritical() << "Achievement summary counts disagree" << unlocked << total;
                  application.exit(EXIT_FAILURE);
                  return;
                }
                if (achievements &&
                    achievements->property("statusText").toString().contains(
                        QStringLiteral("above"), Qt::CaseInsensitive) &&
                    (!refresh || !refresh->isVisible())) {
                  qCritical() << "Achievement cache message points to a hidden refresh control";
                  application.exit(EXIT_FAILURE);
                  return;
                }
              }
              auto* footer = quickWindow->findChild<QQuickItem*>("detailsFooter");
              auto* scroll = quickWindow->findChild<QQuickItem*>("detailsScroll");
              if (footer && footer->isVisible() && scroll &&
                  scroll->mapToScene(QPointF(0, scroll->height())).y() >
                      footer->mapToScene(QPointF(0, 0)).y()) {
                qCritical() << "Controller hints overlap the detail viewport";
                application.exit(EXIT_FAILURE);
                return;
              }
              if (renderOverlay.startsWith("game-info-identify")) {
                if (renderOverlay.endsWith("matched")) {
                  auto* editor = quickWindow->findChild<QQuickItem*>("metadataEditor");
                  QVariantMap entry = section->property("entry").toMap();
                  entry["igdbId"] = 123;
                  entry["title"] = "Identified Adventure";
                  entry["matchStatus"] = "Matched to IGDB";
                  if (!editor) { application.exit(EXIT_FAILURE); return; }
                  QQmlProperty::write(editor, "entry", entry);
                  QTimer::singleShot(100, quickWindow, [quickWindow, editor, &application] {
                    auto* titleField = quickWindow->findChild<QQuickItem*>("metadataTitleField");
                    if (editor->property("entry").toMap().value("igdbId").toInt() != 123 ||
                        !titleField || titleField->isVisible()) {
                      qCritical() << "Identified artwork panel unexpectedly asks for identification";
                      application.exit(EXIT_FAILURE);
                    }
                  });
                }
                auto* panel = quickWindow->findChild<QObject*>("identifyGamePanel");
                if (!panel || !QMetaObject::invokeMethod(panel, "open")) application.exit(EXIT_FAILURE);
                if (renderOverlay.contains("late")) {
                  QTimer::singleShot(180, quickWindow, [quickWindow, panel, &application] {
                    auto* editor = quickWindow->findChild<QQuickItem*>("metadataEditor");
                    QVariantList covers;
                    for (int i = 0; i < 18; ++i) {
                      const auto svg = QString("<svg xmlns='http://www.w3.org/2000/svg' width='600' height='900'><rect width='600' height='900' fill='%1'/><circle cx='300' cy='340' r='190' fill='#122c29'/><text x='300' y='650' text-anchor='middle' font-size='52' fill='white'>ADVENTURE %2</text></svg>")
                          .arg(QColor::fromHsl((i * 37) % 360, 100, 95).name()).arg(i + 1);
                      covers.append(QVariantMap{{"id", i + 1}, {"url", "data:image/svg+xml;base64," + svg.toUtf8().toBase64()}});
                    }
                    QQmlProperty::write(editor, "coverChoices", covers);
                    QTimer::singleShot(120, quickWindow, [quickWindow, panel, &application] {
                      quickWindow->resize(quickWindow->width(), qMin(600, quickWindow->height()));
                      QTimer::singleShot(80, quickWindow, [quickWindow, panel, &application] {
                        auto* done = quickWindow->findChild<QQuickItem*>("metadataArtworkButton");
                        const qreal bottom = panel->property("y").toReal() + panel->property("height").toReal();
                        if (!done || !done->isVisible() || panel->property("y").toReal() < 23 ||
                            bottom > quickWindow->height() - 23) {
                          qCritical() << "Artwork panel escaped resized window after covers arrived";
                          application.exit(EXIT_FAILURE); return;
                        }
                        auto* content = panel->property("contentItem").value<QQuickItem*>();
                        auto* scroll = content ? content->property("navigationScrollView").value<QQuickItem*>() : nullptr;
                        auto* flickable = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
                        if (!scroll || !flickable || done->mapToScene(QPointF(0, done->height())).y() >
                            scroll->mapToScene(QPointF()).y()) {
                          qCritical() << "Done overlaps the artwork scrolling area";
                          application.exit(EXIT_FAILURE); return;
                        }
                        const auto before = done->mapToScene(QPointF());
                        flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - flickable->height());
                        if (done->mapToScene(QPointF()) != before) {
                          qCritical() << "Done moved when artwork scrolled";
                          application.exit(EXIT_FAILURE);
                        }
                        flickable->setProperty("contentY", 0);
                        auto* tile = findVisualItem(content, "metadataCoverTile0");
                        auto* second = findVisualItem(content, "metadataCoverTile1");
                        if (!tile || !second || tile->property("controllerRightTarget").value<QQuickItem*>() != second) {
                          qCritical() << "Cover tile navigation is unavailable" << tile << second << (tile ? tile->property("controllerRightTarget") : QVariant());
                          application.exit(EXIT_FAILURE); return;
                        }
                      });
                    });
                  });
                }
                return;
              }
              if (renderOverlay.startsWith("game-info-overview")) {
                auto* title = quickWindow->findChild<QQuickItem*>("gameDetailsTitle");
                if (!title || title->property("truncated").toBool() ||
                    (renderOverlay.endsWith("long") &&
                     title->property("text").toString().length() < 60)) {
                  qCritical() << "The Details title is incomplete" <<
                      (title ? title->property("text") : QVariant());
                  application.exit(EXIT_FAILURE); return;
                }
                const QRectF titleBounds = title->mapRectToScene(title->boundingRect());
                const QRectF scrollBounds = scroll->mapRectToScene(scroll->boundingRect());
                if (titleBounds.left() < scrollBounds.left() - 1 ||
                    titleBounds.right() > scrollBounds.right() - 1) {
                  qCritical() << "The Details title extends beyond its viewport" <<
                      titleBounds << scrollBounds;
                  application.exit(EXIT_FAILURE); return;
                }
                for (const auto* name : {"gameDetailsTitle", "gameIdentitySummary", "gameActivitySummary", "gameActions"}) {
                  auto* item = quickWindow->findChild<QQuickItem*>(name);
                  const auto bounds = item ? item->mapRectToScene(item->boundingRect()) : QRectF{};
                  if (!item || !item->isVisible() || bounds.top() < scroll->mapToScene(QPointF()).y() - 1 ||
                      bounds.bottom() > scroll->mapToScene(QPointF(0, scroll->height())).y() + 1 ||
                      bounds.right() > scrollBounds.right() - 1) {
                    qCritical() << "Essential detail information below the fold" << name << bounds
                                << "scroll" << scrollBounds << "content"
                                << quickWindow->findChild<QQuickItem*>("detailsContent")->width();
                    application.exit(EXIT_FAILURE);
                  }
                }
                return;
              }
              auto* regional = quickWindow->findChild<QQuickItem*>("regionalIdentityText");
              auto* aliases = quickWindow->findChild<QQuickItem*>("aliasesText");
              auto* aliasesToggle = quickWindow->findChild<QQuickItem*>("aliasesToggle");
              auto* credits = quickWindow->findChild<QQuickItem*>("gameCredits");
              if (!credits || !regional || credits->mapToScene(QPointF(0, credits->height())).y() >
                  regional->mapToScene(QPointF()).y()) {
                qCritical() << "Credits must precede the regional information group";
                application.exit(EXIT_FAILURE); return;
              }
              if (!regional || !regional->isVisible() || !aliases || aliases->isVisible() ||
                  !aliasesToggle || !aliasesToggle->isVisible()) {
                qCritical() << "Regional details or collapsed alias disclosure missing";
                application.exit(EXIT_FAILURE); return;
              }
              QMetaObject::invokeMethod(aliasesToggle, "clicked");
              if (!aliases->isVisible() || !aliases->property("text").toString().contains("Regional title (North American title)")) {
                qCritical() << "Expanded alias evidence missing";
                application.exit(EXIT_FAILURE); return;
              }
              QMetaObject::invokeMethod(aliasesToggle, "clicked");
              if (details->property("releaseYear").toInt() != 1997) {
                qCritical() << "Provider release year did not reach the title";
                application.exit(EXIT_FAILURE);
                return;
              }
              if (!toggle || !description || !toggle->isVisible() ||
                  !description->property("truncated").toBool()) {
                qCritical() << "Long game description did not offer expansion";
                application.exit(EXIT_FAILURE);
                return;
              }
              toggle->forceActiveFocus();
              deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
              if (!section->property("expanded").toBool()) {
                qCritical() << "Game description did not expand with keyboard activation";
                application.exit(EXIT_FAILURE);
                return;
              }
              if (renderOverlay != "game-info-expanded") {
                deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
                if (section->property("expanded").toBool()) {
                  application.exit(EXIT_FAILURE);
                  return;
                }
              }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
              if (!aliasesToggle->hasActiveFocus()) {
                qCritical() << "Read More did not navigate to Other Names";
                application.exit(EXIT_FAILURE); return;
              }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
              auto* backlog = findVisualItem(quickWindow->contentItem(), "completionStatus-backlog");
              if (!backlog || !backlog->hasActiveFocus()) {
                qCritical() << "Description navigation did not reach organization controls"
                            << backlog << quickWindow->activeFocusItem();
                application.exit(EXIT_FAILURE);
                return;
              }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Up);
              if (!aliasesToggle->hasActiveFocus()) { application.exit(EXIT_FAILURE); return; }
              deliverKeyboardTestKey(quickWindow, Qt::Key_Up);
              if (!toggle->hasActiveFocus()) {
                qCritical() << "Organization navigation did not return to description";
                application.exit(EXIT_FAILURE);
                return;
              }
              // A stale explicit link must never escape the active screen.
              QQuickItem outside(quickWindow->contentItem());
              outside.setWidth(20);
              outside.setHeight(20);
              outside.setActiveFocusOnTab(true);
              const QVariant savedTarget = toggle->property("controllerDownTarget");
              toggle->setProperty("controllerDownTarget", QVariant::fromValue(&outside));
              deliverKeyboardTestKey(quickWindow, Qt::Key_Down);
              const bool escaped = outside.hasActiveFocus();
              toggle->setProperty("controllerDownTarget", savedTarget);
              if (escaped) {
                qCritical() << "Explicit navigation escaped the active screen";
                application.exit(EXIT_FAILURE);
                return;
              }
              // Check both keyboard focus cycles through the real organization controls.
              for (const auto modifiers : {Qt::NoModifier, Qt::ShiftModifier}) {
                toggle->forceActiveFocus();
                QSet<QString> visited;
                bool returned = false;
                for (int step = 0; step < 300; ++step) {
                  // Direct window key events bypass Qt's platform shortcut dispatcher.
                  // Activate the registered Tab shortcut to exercise its actual QML route.
                  auto* shortcut = quickWindow->findChild<QObject*>(
                      modifiers == Qt::NoModifier ? "navigationTabForward" : "navigationTabBackward");
                  if (!shortcut || !shortcut->property("enabled").toBool() ||
                      !QMetaObject::invokeMethod(shortcut, "activated")) {
                    qCritical() << "Details Tab shortcut is unavailable";
                    application.exit(EXIT_FAILURE);
                    return;
                  }
                  auto* focused = quickWindow->activeFocusItem();
                  bool contained = false;
                  for (auto* parent = focused; parent; parent = parent->parentItem()) {
                    visited.insert(parent->objectName());
                    if (parent == details) { contained = true; break; }
                  }
                  if (!focused || !focused->isVisible() || !focused->isEnabled() || !contained) {
                    qCritical() << "Tab navigation lost usable focus within details"
                                << step << modifiers << focused << contained;
                    application.exit(EXIT_FAILURE);
                    return;
                  }
                  const QRectF bounds = focused->mapRectToScene(focused->boundingRect());
                  if (bounds.top() < -1 || bounds.bottom() > quickWindow->height() + 1) {
                    qCritical() << "Tab focus is outside the visible window" << focused << bounds;
                    application.exit(EXIT_FAILURE);
                    return;
                  }
                  if (focused == toggle) { returned = true; break; }
                }
                for (const auto* required : {"completionStatus-backlog", "detailsTagsField",
                                             "newCollectionButton", "coverEditButton"}) {
                  auto* control = findVisualItem(details, required);
                  if (control && !control->isVisible()) continue;
                  if (!returned || !visited.contains(QLatin1String(required))) {
                    qCritical() << "Tab cycle missed a detail control" << required << modifiers;
                    application.exit(EXIT_FAILURE);
                    return;
                  }
                }
              }
              toggle->forceActiveFocus();
              QMetaObject::invokeMethod(details, "revealFocusedItem",
                                        Q_ARG(QVariant, QVariant::fromValue(section)));
            });
      });
    } else if (renderOverlay == "gog-folders") {
      quickWindow->setProperty("diagnosticsOpen", true);
      QTimer::singleShot(120, quickWindow, [quickWindow] {
        auto* section = findVisualItem(quickWindow->contentItem(), "gogFoldersSection");
        auto* scroll = quickWindow->findChild<QQuickItem*>("settingsScroll");
        if (section && scroll) QMetaObject::invokeMethod(quickWindow, "revealInScrollView",
            Q_ARG(QVariant, QVariant::fromValue(scroll)), Q_ARG(QVariant, QVariant::fromValue(section)));
      });
    } else if (renderOverlay == "linked-preference" ||
               renderOverlay == "linked-preference-missing") {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(120, quickWindow, [quickWindow] {
        auto* manage = findVisualItem(quickWindow->contentItem(), "detailManageButton");
        if (manage) QMetaObject::invokeMethod(manage, "clicked");
        auto* button = findVisualItem(quickWindow->contentItem(), "preferredInstallationButton");
        auto* details = quickWindow->findChild<QQuickItem*>("gameDetails");
        if (button && details) QMetaObject::invokeMethod(details, "revealFocusedItem", Q_ARG(QVariant, QVariant::fromValue(button)));
      });
    } else if (renderOverlay == QStringLiteral("detail-manage")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QTimer::singleShot(180, quickWindow, [quickWindow] {
        auto* button = quickWindow->findChild<QQuickItem*>("detailManageButton");
        if (button) QMetaObject::invokeMethod(button, "clicked");
      });
    } else if (renderOverlay == QStringLiteral("backup-editor")) {
      QMetaObject::invokeMethod(quickWindow, "openBackupEditor");
      if (auto* field = quickWindow->findChild<QQuickItem*>("backupPathField")) field->setProperty("text", backupFixturePath);
      backups.previewBackup(backupFixturePath);
    } else if (renderOverlay == QStringLiteral("bulk-editor")) {
      QMetaObject::invokeMethod(quickWindow, "openBulkOrganization");
      library.toggleSelection(0);
      library.toggleSelection(2);
    } else if (renderOverlay == QStringLiteral("saved-filters")) {
      library.saveCurrentFilter(QStringLiteral("Weekend favorites"));
      library.saveCurrentFilter(QStringLiteral("Short games for a quiet evening"));
      QMetaObject::invokeMethod(quickWindow, "openSavedFilters");
    } else if (renderOverlay == QStringLiteral("library-actions") || renderOverlay == QStringLiteral("library-sources") || renderOverlay == QStringLiteral("library-filters") || renderOverlay == QStringLiteral("library-view")) {
      QTimer::singleShot(180, quickWindow, [quickWindow, renderOverlay, &application] {
        const char* name = renderOverlay == "library-sources" ? "sourcesMenuButton"
            : renderOverlay == "library-filters" ? "filtersMenuButton"
            : renderOverlay == "library-view" ? "viewMenuButton" : "libraryMoreButton";
        auto* button = quickWindow->findChild<QQuickItem*>(name);
        if (!button || !QMetaObject::invokeMethod(button, "clicked")) {
          qCritical() << "Library actions preview could not open the menu";
          application.exit(EXIT_FAILURE);
        }
      });
    } else if (renderOverlay == QStringLiteral("random-selection")) {
      QMetaObject::invokeMethod(quickWindow, "pickRandomGame");
    } else if (renderOverlay == QStringLiteral("artwork-editor")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      QMetaObject::invokeMethod(quickWindow, "editArtwork");
    } else if (renderOverlay == QStringLiteral("recorder-details")) {
      QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
      auto installation = quickWindow->property("selectedInstallation").toMap();
      installation.insert("playtimeProvenance", "Imported from emulator: 1h 10m · Recorded by Omakade: 15m (not applied while recording is off)");
      quickWindow->setProperty("selectedInstallation", installation);
    } else if (renderOverlay == QStringLiteral("manual-editor")) {
      QMetaObject::invokeMethod(quickWindow, "editManualGame", Q_ARG(QVariant, QString{}));
      if (auto* editor = quickWindow->findChild<QQuickItem*>(QStringLiteral("manualGameEditor"))) {
        QVariantMap draft{{"title", "Signal Hill"}, {"executable", "/home/player/Games/Signal Hill/start.sh"},
                          {"directory", "/home/player/Games/Signal Hill"},
                          {"arguments", QStringList{"--fullscreen", "profile one"}}};
        QMetaObject::invokeMethod(editor, "loadDraft", Q_ARG(QVariant, draft));
      }
    } else if (renderOverlay.startsWith(QStringLiteral("settings")) ||
               renderOverlay == QStringLiteral("couch-settings-top") ||
               renderOverlay == QStringLiteral("couch-settings-bottom")) {
      quickWindow->setProperty("diagnosticsOpen", true);
      if (renderOverlay.startsWith("settings-")) {
        auto* page = quickWindow->findChild<QQuickItem*>("settingsOverlay");
        const QStringList sections{"sources", "library", "connections", "controls", "storage", "appearance", "streaming", "about", "highlights"};
        const int section = sections.indexOf(renderOverlay.mid(9));
        if (page && section >= 0) page->setProperty("section", section);
        if (page && renderOverlay == QStringLiteral("settings-melonds")) {
          page->setProperty("section", 0);
          page->setProperty("sourceSearch", QStringLiteral("MELONDS"));
          page->setProperty("sourceDetail", QStringLiteral("MELONDS"));
        }
        if (page && renderOverlay == QStringLiteral("settings-rpcs3")) {
          page->setProperty("section", 0);
          page->setProperty("sourceSearch", QStringLiteral("RPCS3"));
          page->setProperty("sourceDetail", QStringLiteral("RPCS3"));
        }
        if (page && renderOverlay == QStringLiteral("settings-ppsspp")) {
          page->setProperty("section", 0);
          page->setProperty("sourceSearch", QStringLiteral("PPSSPP"));
          page->setProperty("sourceDetail", QStringLiteral("PPSSPP"));
        }
        if (page && (renderOverlay == "settings-romm" || renderOverlay == "settings-save-overview")) {
          page->setProperty("section",renderOverlay=="settings-romm" ? 2 : 4);
          auto* panel=quickWindow->findChild<QObject*>(renderOverlay=="settings-romm" ? "rommSettingsPanel" : "saveProtectionPanel");
          if(panel) panel->setProperty("expanded",true);
        }
        if (page && renderOverlay.startsWith("settings-recorder-")) page->setProperty("section", 1);
        if (page && renderOverlay == "settings-categories") {
          auto* category = quickWindow->findChild<QQuickItem*>("settingsCategoryButton");
          if (category) QMetaObject::invokeMethod(category, "clicked");
        }
        if (page && renderOverlay.startsWith("settings-connection-")) {
          bool okay = false;
          const int connection = renderOverlay.mid(QStringLiteral("settings-connection-").size()).toInt(&okay);
          if (okay && connection >= 0 && connection < 4) {
            page->setProperty("section", 2);
            page->setProperty("connection", connection);
          }
        }
      }
      if (renderOverlay.startsWith("settings-recorder-") ||
          renderOverlay == QStringLiteral("settings") ||
          renderOverlay == QStringLiteral("couch-settings-bottom")) {
        QTimer::singleShot(400, quickWindow, [quickWindow] {
          // Scroll to the end so the lower sections land in the capture.
          auto* scroll = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
          QObject* flickable =
              scroll == nullptr ? nullptr : scroll->property("contentItem").value<QObject*>();
          if (flickable != nullptr) {
            const qreal originY = flickable->property("originY").toReal();
            const qreal maximumScroll =
                originY + qMax(0.0, flickable->property("contentHeight").toReal() -
                                        scroll->height());
            flickable->setProperty("contentY", maximumScroll);
          }
        });
      }
    } else if (renderOverlay.startsWith(QStringLiteral("cover-size"))) {
      if (renderOverlay.endsWith("small")) preferences.setCoverSize(60);
      if (renderOverlay.endsWith("large")) preferences.setCoverSize(160);
      QTimer::singleShot(100, quickWindow, [quickWindow] {
        auto* popup = quickWindow->findChild<QObject*>(QStringLiteral("coverSizePopup"));
        if (popup) QMetaObject::invokeMethod(popup, "open");
      });
    } else if (renderOverlay == QStringLiteral("picker")) {
      QMetaObject::invokeMethod(
          quickWindow, "openFilterPicker", Q_ARG(QVariant, QStringLiteral("collection")),
          Q_ARG(QVariant, QVariant(QStringList{QStringLiteral("Couch co-op"),
                                               QStringLiteral("Cozy evenings"),
                                               QStringLiteral("Finish this year")})));
    } else if (renderOverlay == QStringLiteral("couch-search")) {
      if (auto* couch = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchLibrary"))) {
        QMetaObject::invokeMethod(couch, "openSearch");
      }
    } else if (renderOverlay == QStringLiteral("couch-browse")) {
      if (auto* couch = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchLibrary"))) {
        QMetaObject::invokeMethod(couch, "openBrowse");
      }
    } else if (renderOverlay == QStringLiteral("couch-entry")) {
      if (auto* target = quickWindow->findChild<QQuickItem*>(QStringLiteral("searchField"))) {
        target->setProperty("text", QStringLiteral("Secret-42!"));
        QMetaObject::invokeMethod(
            quickWindow, "openCouchTextEntry",
            Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(target))),
            Q_ARG(QVariant, QStringLiteral("ENTER API KEY")), Q_ARG(QVariant, true),
            Q_ARG(QVariant, QStringLiteral("Enter a value")));
      }
    }
    const int renderDelay = renderOverlay == "home-full-queue" ? 6000
                            : renderOverlay.startsWith("library-reflow") ? 1000
                            : renderOverlay.startsWith("home-wheel") ? 1300
                            : renderOverlay == "library-repair-relocate" ? 1600
                            : renderOverlay == "library-repair-manual" ? 1200 : 900;
    const auto render = [quickWindow, screenshotPath, renderOverlay, &application, &controller,
                         heroicOwnedFixture, &heroicOwnedDispatchComplete] {
      if (renderOverlay == "source-rescan" &&
          !quickWindow->property("sourceRescanComplete").toBool()) {
        qCritical() << "Source rescan enablement checks did not complete";
        application.exit(EXIT_FAILURE); return;
      }
      if (heroicOwnedFixture && !heroicOwnedDispatchComplete) {
        qCritical() << "Owned Heroic dispatch verification did not complete";
        application.exit(EXIT_FAILURE); return;
      }
      if (renderOverlay.startsWith(QStringLiteral("year-in-review"))) {
        auto* preview = quickWindow->findChild<QQuickItem*>(QStringLiteral("yearInReviewPreviewHost"));
        if (!preview || !preview->isVisible()) {
          qCritical() << "Card preview fixture did not open the card";
          application.exit(EXIT_FAILURE); return;
        }
        // The preview's title and buttons sit on its own opaque panel inside the window margin,
        // not straight over the app header's logo and buttons behind the backdrop.
        auto* panel = findVisualItem(preview, QStringLiteral("cardPreviewPanel"));
        const QRectF panelRect =
            panel ? panel->mapRectToScene(panel->boundingRect()) : QRectF{};
        if (!panel || panel->property("color").value<QColor>().alphaF() < 0.95 ||
            panelRect.top() < 1 || panelRect.left() < 1) {
          qCritical() << "Card preview has no opaque panel of its own" << panelRect;
          application.exit(EXIT_FAILURE); return;
        }
        for (const QString& name : {QStringLiteral("cardPreviewTitle"),
                                    QStringLiteral("cardSaveButton"),
                                    QStringLiteral("cardCloseButton")}) {
          auto* item = findVisualItem(preview, name);
          const QRectF rect = item ? item->mapRectToScene(item->boundingRect()) : QRectF{};
          if (!item || !panelRect.contains(rect)) {
            qCritical() << "Card preview control is outside its panel" << name << rect
                        << panelRect;
            application.exit(EXIT_FAILURE); return;
          }
        }
      }
      if (renderOverlay == QStringLiteral("artwork-editor")) {
        for (const QString& kind : {QStringLiteral("cover"), QStringLiteral("hero")}) {
          for (const QString& prefix : {QStringLiteral("artworkPreview_"),
                                        QStringLiteral("artworkPath_"),
                                        QStringLiteral("artworkApply_"),
                                        QStringLiteral("artworkAutomatic_")}) {
            auto* item = findVisualItem(quickWindow->contentItem(), prefix + kind);
            const QRectF bounds = item ? item->mapRectToScene(item->boundingRect()) : QRectF{};
            if (!item || !item->isVisible() || bounds.left() < -1 ||
                bounds.right() > quickWindow->width() + 1) {
              qCritical() << "Artwork editor control extends outside the window"
                          << prefix << kind << bounds;
              application.exit(EXIT_FAILURE); return;
            }
          }
        }
      }
      if (!quickWindow->property("couchMode").toBool() &&
          (renderOverlay == QStringLiteral("stats") ||
           renderOverlay.startsWith(QStringLiteral("home")))) {
        auto* libraryHeader = quickWindow->findChild<QQuickItem*>(QStringLiteral("libraryAppHeader"));
        auto* currentHeader = quickWindow->findChild<QQuickItem*>(
            renderOverlay == QStringLiteral("stats") ? QStringLiteral("statsAppHeader")
                                                     : QStringLiteral("homeAppHeader"));
        if (!libraryHeader || !currentHeader) {
          qCritical() << "A desktop destination is missing the shared app header" << renderOverlay;
          application.exit(EXIT_FAILURE);
          return;
        }
        const QRectF libraryRect = libraryHeader->mapRectToScene(libraryHeader->boundingRect());
        const QRectF currentRect = currentHeader->mapRectToScene(currentHeader->boundingRect());
        qInfo() << "Shared header strip geometry" << renderOverlay << libraryRect << currentRect;
        if (qAbs(libraryRect.left() - currentRect.left()) > 1 ||
            qAbs(libraryRect.top() - currentRect.top()) > 1 ||
            qAbs(libraryRect.width() - currentRect.width()) > 1 ||
            qAbs(libraryRect.height() - currentRect.height()) > 1) {
          qCritical() << "Desktop header strips do not align" << renderOverlay
                      << libraryRect << currentRect;
          application.exit(EXIT_FAILURE);
          return;
        }
      }
      if (renderOverlay == "stats" || renderOverlay == "stats-patterns" ||
          renderOverlay == "stats-library") {
        auto* statsScreen = quickWindow->findChild<QQuickItem*>(QStringLiteral("statsScreen"));
        if (!statsScreen) {
          qCritical() << "Stats screen is missing from its render fixture";
          application.exit(EXIT_FAILURE);
          return;
        }
        const int expectedStatsView = renderOverlay == "stats-patterns" ? 1
                                      : renderOverlay == "stats-library" ? 2 : 0;
        if (statsScreen->property("currentView").toInt() != expectedStatsView) {
          qCritical() << "Stats fixture opened the wrong view" << renderOverlay;
          application.exit(EXIT_FAILURE);
          return;
        }
        auto* recordedStat = quickWindow->findChild<QQuickItem*>("statsRecordedTime");
        auto* libraryTotal = quickWindow->findChild<QQuickItem*>("statsLibraryTotal");
        if (!recordedStat || !libraryTotal ||
            recordedStat->isVisible() != (expectedStatsView == 0) ||
            libraryTotal->isVisible() != (expectedStatsView == 2)) {
          qCritical() << "Stats mixed dated and lifetime totals in a view" << renderOverlay;
          application.exit(EXIT_FAILURE);
          return;
        }
        if (renderOverlay == "stats-patterns") {
          auto* statsScroller = quickWindow->findChild<QQuickItem*>("statsScroller");
          if (!statsScroller ||
              statsScroller->property("contentHeight").toReal() <= statsScroller->height()) {
            qCritical() << "Play patterns fixture has no scrollable content";
            application.exit(EXIT_FAILURE); return;
          }
          deliverKeyboardTestKey(quickWindow, Qt::Key_PageDown);
          if (statsScroller->property("contentY").toReal() <= 0) {
            qCritical() << "Page Down did not scroll Stats from the period control";
            application.exit(EXIT_FAILURE); return;
          }
          statsScroller->setProperty("contentY", 0);
        }
        QObject* statsModel =
            qmlContext(quickWindow)->contextProperty(QStringLiteral("Stats")).value<QObject*>();
        QSet<QString> systemNames;
        QSet<QString> sourceNames;
        if (statsModel) {
          for (const QVariant& row : statsModel->property("bySystem").toList())
            systemNames.insert(row.toMap().value(QStringLiteral("name")).toString());
          for (const QVariant& row : statsModel->property("bySource").toList())
            sourceNames.insert(row.toMap().value(QStringLiteral("name")).toString());
        }
        if (!statsModel || systemNames.size() < 3 || sourceNames.size() < 2 ||
            systemNames == sourceNames) {
          qCritical() << "Stats fixture did not distinguish systems from sources"
                      << systemNames << sourceNames;
          application.exit(EXIT_FAILURE);
          return;
        }
        if (renderOverlay == "stats-library") {
          for (const auto* scopeName : {"statsSystemScope", "statsSourceScope", "statsGenreScope"}) {
            auto* scope = quickWindow->findChild<QQuickItem*>(scopeName);
            if (!scope || !scope->isVisible() ||
                !scope->property("text").toString().contains("RECORDED PLAY IN 2026")) {
              qCritical() << "Library breakdown lost its recorded period scope" << scopeName;
              application.exit(EXIT_FAILURE); return;
            }
          }
        }
        if (quickWindow->property("couchMode").toBool()) {
          auto* back = quickWindow->findChild<QQuickItem*>(QStringLiteral("statsBackButton"));
          if (!back || !back->isVisible()) {
            qCritical() << "Couch Stats lost its Back action";
            application.exit(EXIT_FAILURE);
            return;
          }
        } else {
          const char* headerNames[] = {"statsOpenHomeButton", "statsLibraryDestinationButton",
                                       "statsStatsDestinationButton", "statsSettingsButton",
                                       "statsCouchModeButton"};
          auto* firstPeriod =
              quickWindow->findChild<QQuickItem*>(QStringLiteral("statsThisYearButton"));
          if (!firstPeriod || quickWindow->activeFocusItem() != firstPeriod) {
            auto* focused = quickWindow->activeFocusItem();
            qCritical() << "Desktop Stats did not focus its first content control"
                        << (focused ? focused->objectName() : QStringLiteral("nothing"))
                        << (firstPeriod ? firstPeriod->hasActiveFocus() : false)
                        << statsScreen->isVisible();
            application.exit(EXIT_FAILURE);
            return;
          }
          QQuickItem* headerButtons[5]{};
          for (int index = 0; index < 5; ++index) {
            headerButtons[index] = quickWindow->findChild<QQuickItem*>(headerNames[index]);
            if (!headerButtons[index] || !headerButtons[index]->isVisible()) {
              qCritical() << "Stats shared header is missing a destination" << headerNames[index];
              application.exit(EXIT_FAILURE);
              return;
            }
          }
          const auto moveHeader = [&controller, quickWindow](int direction,
                                                             QQuickItem* expected) {
            controller.focusDirectionRequested(direction);
            return quickWindow->activeFocusItem() == expected;
          };
          headerButtons[2]->forceActiveFocus();
          if (!moveHeader(Qt::Key_Left, headerButtons[1]) ||
              !moveHeader(Qt::Key_Left, headerButtons[0]) ||
              !moveHeader(Qt::Key_Right, headerButtons[1]) ||
              !moveHeader(Qt::Key_Right, headerButtons[2]) ||
              !moveHeader(Qt::Key_Right, headerButtons[3]) ||
              !moveHeader(Qt::Key_Right, headerButtons[4]) ||
              !moveHeader(Qt::Key_Left, headerButtons[3]) ||
              !moveHeader(Qt::Key_Left, headerButtons[2])) {
            qCritical() << "Stats header controller destinations are out of order";
            application.exit(EXIT_FAILURE);
            return;
          }
          firstPeriod->forceActiveFocus();
          if (!moveHeader(Qt::Key_Up, headerButtons[2]) ||
              !moveHeader(Qt::Key_Down, firstPeriod)) {
            qCritical() << "Stats content did not reach or return from the shared header";
            application.exit(EXIT_FAILURE);
            return;
          }
          firstPeriod->forceActiveFocus();
          auto* statsContent = quickWindow->findChild<QQuickItem*>(
              QStringLiteral("statsPageContent"));
          if (!statsContent || statsContent->width() > 1201) {
            qCritical() << "Desktop Stats content exceeds its 1200 pixel maximum"
                        << (statsContent ? statsContent->width() : -1);
            application.exit(EXIT_FAILURE);
            return;
          }
        }
        if (renderOverlay == "stats-patterns")
        for (const auto& chart : {std::pair{"Hour", 24}, std::pair{"Weekday", 7}}) {
          auto* row = findVisualItem(quickWindow->contentItem(),
                                     QStringLiteral("stats%1Chart").arg(chart.first));
          int count = 0;
          qreal firstWidth = -1;
          bool equalWidths = true;
          if (row) {
            for (auto* bin : row->childItems()) {
              if (bin->objectName() != QStringLiteral("stats%1Bin").arg(chart.first)) continue;
              if (firstWidth < 0) firstWidth = bin->width();
              equalWidths = equalWidths && bin->width() > 0 && qAbs(bin->width() - firstWidth) <= 1;
              ++count;
            }
          }
          if (count != chart.second || !equalWidths) {
            qCritical() << "Stats chart bins must have equal widths:" << chart.first << count;
            application.exit(1);
            return;
          }
        }
      }
      // Recording is off on a new installation, so an empty Stats says so and turns it on
      // from there, reachable by controller. Once it is on, the nothing-recorded-yet text
      // returns.
      if (renderOverlay == "stats-empty") {
        QObject* preferences =
            qmlContext(quickWindow)->contextProperty(QStringLiteral("Preferences")).value<QObject*>();
        auto* recordingOff = quickWindow->findChild<QQuickItem*>("statsRecordingOff");
        auto* noRecordedPlay = quickWindow->findChild<QQuickItem*>("statsNoRecordedPlay");
        auto* turnOn = quickWindow->findChild<QQuickItem*>("statsTurnOnRecordingButton");
        auto* overview = quickWindow->findChild<QQuickItem*>("statsOverviewButton");
        if (!preferences || !recordingOff || !noRecordedPlay || !turnOn || !overview ||
            preferences->property("trackPlaySessions").toBool() || !recordingOff->isVisible() ||
            noRecordedPlay->isVisible() || !turnOn->isVisible()) {
          qCritical() << "Empty Stats did not say that recording is off";
          application.exit(EXIT_FAILURE); return;
        }
        overview->forceActiveFocus();
        controller.focusDirectionRequested(Qt::Key_Down);
        if (quickWindow->activeFocusItem() != turnOn) {
          auto* focused = quickWindow->activeFocusItem();
          qCritical() << "Controller could not reach Turn On Recording from the Stats views"
                      << (focused ? focused->objectName() : QStringLiteral("nothing"));
          application.exit(EXIT_FAILURE); return;
        }
        deliverKeyboardTestKey(quickWindow, Qt::Key_Return);
        QCoreApplication::processEvents();
        if (!preferences->property("trackPlaySessions").toBool() || recordingOff->isVisible() ||
            !noRecordedPlay->isVisible() || quickWindow->activeFocusItem() != overview) {
          qCritical() << "Turn On Recording did not turn recording on and keep focus in Stats";
          application.exit(EXIT_FAILURE); return;
        }
        // The screenshot shows the recording-off state.
        preferences->setProperty("trackPlaySessions", false);
        QCoreApplication::processEvents();
      }
      if (renderOverlay.startsWith("library-reflow") &&
          !quickWindow->property("libraryReflowComplete").toBool()) {
        qCritical() << "Library return fixture did not complete every transition";
        application.exit(EXIT_FAILURE); return;
      }
      if (renderOverlay == "home-full-queue" && !quickWindow->property("fullQueueChecked").toBool()) {
        qCritical() << "Full queue checks did not complete after layout";
        application.exit(EXIT_FAILURE); return;
      }
      if (renderOverlay.startsWith(QStringLiteral("couch-grid"))) {
        auto* grid = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchGameGrid"));
        auto* content = grid ? grid->property("contentItem").value<QQuickItem*>() : nullptr;
        QQuickItem *first = nullptr, *last = nullptr;
        const int columns = grid ? grid->property("columnCount").toInt() : 0;
        if (content) for (auto* card : content->childItems()) {
          if (!card->property("title").isValid()) continue;
          const int index = card->property("index").toInt();
          if (index == 0) first = card;
          if (index == columns - 1) last = card;
        }
        const qreal left = first ? first->mapToItem(grid, QPointF(0, 0)).x() : -1;
        const qreal right = last ? grid->width() - last->mapToItem(grid, QPointF(last->width(), 0)).x() : -1;
        if (!first || !last || left < 0 || right < 0 || qAbs(left - right) > 2) {
          qCritical() << "Couch grid is not centered: left/right margins" << left << right;
          application.exit(EXIT_FAILURE);
          return;
        }
      }
      if (renderOverlay.startsWith(QStringLiteral("settings-")) ||
          renderOverlay == QStringLiteral("settings")) {
        auto* overlay = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
        auto* scroll = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
        bool okay = overlay && scroll;
        const auto check = [&](auto&& self, QQuickItem* item) -> void {
          if (item->isVisible() && item->activeFocusOnTab() && item->width() > 0) {
            const auto rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            auto* ancestor = item;
            while (ancestor && ancestor != scroll) ancestor = ancestor->parentItem();
            const auto bounds = ancestor
                ? scroll->mapRectToScene(QRectF(0, 0, scroll->width(), scroll->height()))
                : QRectF(0, 0, quickWindow->width(), quickWindow->height());
            if (rect.left() < bounds.left() - 1 || rect.right() > bounds.right() + 1) {
              qCritical() << "Settings control extends beyond its viewport" << item->objectName();
              okay = false;
            }
          }
          if (item->isVisible() && item->property("connectionStatusButton").toBool()) {
            auto* text = item->property("contentItem").value<QQuickItem*>();
            if (!text || text->implicitHeight() > text->height() + 1 ||
                text->property("contentWidth").toReal() > text->width() + 1) {
              qCritical() << "Connection status label is clipped" << item->property("text")
                          << (text ? text->size() : QSizeF())
                          << (text ? text->implicitHeight() : -1) << item->height() << item->implicitHeight()
                          << item->property("topPadding") << item->property("bottomPadding");
              okay = false;
            }
          }
          for (auto* child : item->childItems()) self(self, child);
        };
        if (overlay) check(check, overlay);
        // The text size from Settings > Appearance scales the shared type roles and control
        // heights on the desktop. Game Mode keeps them at 100% and scales to the display.
        auto* metrics = qmlEngine(quickWindow)
                            ? qmlEngine(quickWindow)->singletonInstance<QObject*>(
                                  QStringLiteral("Omakade"), QStringLiteral("UiMetrics"))
                            : nullptr;
        auto* textPreferences = qmlContext(quickWindow)
                                    ->contextProperty(QStringLiteral("Preferences"))
                                    .value<QObject*>();
        const qreal textScale =
            quickWindow->property("couchMode").toBool() || !textPreferences
                ? 1.0
                : textPreferences->property("textSize").toInt() / 100.0;
        const auto role = [metrics](const char* name) {
          return metrics ? metrics->property(name).toInt() : -1;
        };
        if (!metrics || qAbs(metrics->property("textScale").toReal() - textScale) > 0.001 ||
            role("body") != qRound(14 * textScale) || role("supporting") != qRound(12 * textScale) ||
            role("label") != qRound(12 * textScale) || role("caption") != qRound(11 * textScale) ||
            role("section") != qRound(18 * textScale) ||
            role("controlHeight") != qRound(42 * textScale) ||
            role("compactControlHeight") != qRound(36 * textScale)) {
          qCritical() << "Type roles do not follow the text size" << textScale
                      << (metrics ? metrics->property("textScale").toReal() : -1) << role("body")
                      << role("label") << role("controlHeight");
          okay = false;
        }
        if (renderOverlay == QStringLiteral("settings-appearance")) {
          const int preference =
              textPreferences ? textPreferences->property("textSizePreference").toInt() : -1;
          auto* chosen = findVisualItem(
              overlay, QStringLiteral("textSize_") +
                           (preference == 0 ? QStringLiteral("system") : QString::number(preference)));
          const qreal buttonScale =
              chosen ? chosen->property("displayScale").toReal() : 0;
          if (!chosen || !chosen->isVisible() || !chosen->property("selected").toBool() ||
              qAbs(chosen->height() - role("compactControlHeight") * buttonScale) > 1) {
            qCritical() << "Text size choice is missing, unselected or not scaled" << preference
                        << (chosen ? chosen->height() : -1) << role("compactControlHeight");
            okay = false;
          }
        }
        // Backup & Storage and Appearance draw their own text, so they are held to the same
        // type roles as the other pages: section headings, body and supporting text, never
        // the default size.
        if (renderOverlay == QStringLiteral("settings-storage") ||
            renderOverlay == QStringLiteral("settings-save-overview") ||
            renderOverlay == QStringLiteral("settings-appearance")) {
          const QString pageName = renderOverlay == QStringLiteral("settings-appearance")
                                       ? QStringLiteral("appearanceSettings")
                                       : QStringLiteral("backupStorageSettings");
          auto* page = quickWindow->findChild<QQuickItem*>(pageName);
          auto* panel = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsPanel"));
          const qreal scale = panel ? panel->property("uiScale").toReal() : 0;
          int checked = 0;
          const auto checkRoles = [&](auto&& self, QQuickItem* item) -> void {
            if (!item->isVisible() || item->inherits("QQuickControl") ||
                item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
              return;
            if (item->inherits("QQuickText")) {
              const int size = item->property("font").value<QFont>().pixelSize();
              bool matches = false;
              for (const int value : {role("supporting"), role("body"), role("section")})
                matches |= qAbs(size - value * scale) <= 1;
              if (!matches) {
                qCritical() << pageName << "text is off the shared type roles"
                            << item->property("text").toString().left(40) << size << scale;
                okay = false;
              }
              ++checked;
            }
            for (auto* child : item->childItems()) self(self, child);
          };
          if (page && page->isVisible() && scale > 0) checkRoles(checkRoles, page);
          if (checked < 6) {
            qCritical() << pageName << "did not render its text" << checked;
            okay = false;
          }
        }
        if (overlay && scroll && scroll->isVisible() &&
            !renderOverlay.startsWith(QStringLiteral("settings-recorder-"))) {
          auto* navigation = quickWindow->findChild<QQuickItem*>(
              QStringLiteral("settingsSectionNavigation"));
          auto* firstNavigationButton = quickWindow->findChild<QQuickItem*>(
              QStringLiteral("settingsSection0"));
          auto* content = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsContent"));
          auto* tabs = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsSourceTabs"));
          auto* rescan = quickWindow->findChild<QQuickItem*>(
              QStringLiteral("rescanEnabledSourcesButton"));
          auto* search = quickWindow->findChild<QQuickItem*>(
              QStringLiteral("settingsSourceSearchField"));
          QQuickItem* firstSection = nullptr;
          if (content) {
            for (auto* child : content->childItems()) {
              if (!child->isVisible() || child->height() <= 0) continue;
              if (!firstSection || child->mapToScene(QPointF()).y() <
                                       firstSection->mapToScene(QPointF()).y()) {
                firstSection = child;
              }
            }
          }
          QQuickItem* firstNavigationItem = firstNavigationButton;
          const auto findFirstNavigationItem = [&](auto&& self, QQuickItem* item) -> void {
            for (auto* child : item->childItems()) {
              if (child->isVisible() && child->width() > 0 && child->height() > 0 &&
                  (firstNavigationItem == nullptr ||
                   child->mapToScene(QPointF()).y() <
                       firstNavigationItem->mapToScene(QPointF()).y())) {
                firstNavigationItem = child;
              }
              self(self, child);
            }
          };
          if (navigation) findFirstNavigationItem(findFirstNavigationItem, navigation);
          const qreal navigationY = firstNavigationItem
                                        ? firstNavigationItem->mapToScene(QPointF()).y()
                                        : navigation ? navigation->mapToScene(QPointF()).y() : -1;
          const qreal sectionY = firstSection ? firstSection->mapToScene(QPointF()).y() : -1;
          const qreal tabsY = tabs ? tabs->mapToScene(QPointF()).y() : -1;
          const qreal scrollY = scroll->mapToScene(QPointF()).y();
          auto* scrollContent = scroll->property("contentItem").value<QQuickItem*>();
          const qreal scrollContentY = scroll->property("navigationContentY").toReal();
          const qreal scrollOriginY = scrollContent
                                          ? scrollContent->property("originY").toReal()
                                          : 0;
          qInfo() << "Settings layout geometry" << renderOverlay
                  << "navigation" << (navigation && navigation->isVisible())
                  << (navigation ? navigation->y() : -1)
                  << "firstButton" << (firstNavigationItem ? firstNavigationItem->objectName() : QString())
                  << navigationY
                  << "section" << (firstSection ? firstSection->objectName() : QString())
                  << (firstSection ? firstSection->height() : -1) << sectionY
                  << "scroll" << scrollY << scroll->height()
                  << "contentY" << scrollContentY << "originY" << scrollOriginY
                  << "tabs" << tabsY
                  << "rescan" << (rescan && rescan->isVisible())
                  << (rescan ? rescan->y() : -1) << (rescan ? rescan->height() : -1)
                  << "search" << (search ? search->mapToScene(QPointF()).y() : -1);
          if (renderOverlay == QStringLiteral("settings") && scrollContent &&
              scrollContentY < scrollOriginY - 1) {
            qCritical() << "Settings render fixture scrolled before content origin"
                        << scrollContentY << scrollOriginY;
            application.exit(EXIT_FAILURE);
            return;
          }
          if (renderOverlay != QStringLiteral("settings") && navigation &&
              navigation->isVisible() && firstNavigationItem && firstSection &&
              qAbs(navigationY - sectionY) > 2) {
            qCritical() << "Settings section content does not align with its navigation"
                        << renderOverlay << navigationY << sectionY;
            application.exit(EXIT_FAILURE);
            return;
          }
          if (renderOverlay == QStringLiteral("settings-sources") && tabs &&
              firstNavigationItem && qAbs(navigationY - tabsY) > 2) {
            qCritical() << "Source tabs do not align with the Sources navigation button"
                        << navigationY << tabsY;
            application.exit(EXIT_FAILURE);
            return;
          }
        }
        if (!okay) { application.exit(EXIT_FAILURE); return; }
      }
      if (renderOverlay == "home-delayed") {
        auto* feature = quickWindow->findChild<QQuickItem*>("homeFeaturedSection");
        auto* shelf = quickWindow->findChild<QQuickItem*>("homeRecentShelf");
        auto* shelfContent = shelf ? shelf->property("contentItem").value<QQuickItem*>() : nullptr;
        const auto rect = [](QQuickItem* item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); };
        if (!feature || !shelf || !shelfContent || feature->height() < 100 || shelf->height() < 150 ||
            rect(shelf).top() < rect(feature).bottom()) {
          qCritical() << "Opening Home after startup collapsed its sections";
          application.exit(EXIT_FAILURE); return;
        }
        int tiles = 0;
        qreal rowY = -1;
        qreal previousRight = -1;
        for (auto* child : shelfContent->childItems()) {
          if (!child->property("game").isValid()) continue;
          const QRectF bounds = child->mapRectToItem(shelfContent,
                                                     QRectF(0, 0, child->width(), child->height()));
          if (bounds.width() < 80 || bounds.height() < 150 || bounds.top() < -1 ||
              (rowY >= 0 && qAbs(bounds.top() - rowY) > 1) ||
              (previousRight >= 0 && bounds.left() < previousRight - 1)) {
            qCritical() << "Home tile wrapped or overlapped after delayed loading" << bounds;
            application.exit(EXIT_FAILURE); return;
          }
          rowY = bounds.top();
          previousRight = bounds.right();
          ++tiles;
        }
        if (tiles != 6) { qCritical() << "Home tiles did not load" << tiles; application.exit(EXIT_FAILURE); return; }
      }
      // A run that was asked for a card and not for a screenshot ends when the card has been
      // written, so there is nothing to grab here and no reason to end the run early.
      if (screenshotPath.isEmpty()) return;
      const QImage screenshot = quickWindow->grabWindow();
      if (screenshot.isNull() || !screenshot.save(screenshotPath)) {
        qCritical() << "Could not save screenshot to" << screenshotPath;
        application.exit(EXIT_FAILURE);
        return;
      }
      application.quit();
    };
    if (renderOverlay.startsWith(QStringLiteral("library-reflow")) || heroicOwnedFixture) {
      // Render once the fixture reports completion, with a bounded wait for
      // dispatch or the longer sequence of layout transitions.
      auto waited = std::make_shared<QElapsedTimer>();
      waited->start();
      auto poll = std::make_shared<std::function<void()>>();
      *poll = [quickWindow, render, waited, poll, heroicOwnedFixture, &heroicOwnedDispatchComplete] {
        const bool complete = heroicOwnedFixture ? heroicOwnedDispatchComplete
            : quickWindow->property("libraryReflowComplete").toBool();
        if (!complete && waited->elapsed() < (heroicOwnedFixture ? 3000 : 30000)) {
          QTimer::singleShot(100, quickWindow, *poll);
          return;
        }
        render();
      };
      QTimer::singleShot(renderDelay, quickWindow, *poll);
    } else {
      QTimer::singleShot(renderDelay, quickWindow, render);
    }
  return std::nullopt;
}

} // namespace SelfTest

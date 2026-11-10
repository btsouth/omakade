#include "selftest/SelfTest.h"

#include "app/AppSettings.h"
#include "selftest/SelfTestSupport.h"
#include "gamemode/GameModeSession.h"
#include "input/ControllerInput.h"
#include "library/GameStopService.h"
#include "library/HomeModel.h"
#include "library/LibraryFilterModel.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include <functional>

namespace SelfTest {

void driveGameMode(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  GameStopService& gameStop = *app.gameStop;
  GameModeSession& gameMode = *app.gameMode;
  QWindow*& rootWindow = *app.rootWindow;
  LibraryGameModeTestCompositor& gameModeTestCompositor = harness.fixtures.gameModeTestCompositor;
  // Game Mode holds Couch Mode for its session. Every way of switching modes opens its
  // controls instead of leaving, and leaving returns the window to the mode it had.
  QTimer::singleShot(200, &application, [&application, rootWindow, &gameMode, &gameStop, &controller,
                                       &gameModeTestCompositor] {
    const auto fail = [&application](const QString& message) {
      qCritical().noquote() << message;
      application.exit(EXIT_FAILURE);
    };
    const auto settled = [](const std::function<bool()>& ready) {
      QElapsedTimer timer;
      timer.start();
      while (!ready() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      }
      QCoreApplication::processEvents();
      return ready();
    };
    const bool couchBefore = rootWindow->property("couchMode").toBool();
    const int pendingScansBefore = rootWindow->property("pendingStartupScans").toInt();
    rootWindow->setProperty("pendingStartupScans", 0);
    const bool scanningBefore = rootWindow->property("libraryScanning").toBool();
    rootWindow->setProperty("pendingStartupScans", 2);
    if (!rootWindow->property("libraryScanning").toBool()) {
      fail(QStringLiteral("Queued startup scans did not retain the library loading state")); return;
    }
    rootWindow->setProperty("pendingStartupScans", 1);
    if (!rootWindow->property("libraryScanning").toBool()) {
      fail(QStringLiteral("A gap between startup scans flashed the empty library")); return;
    }
    rootWindow->setProperty("pendingStartupScans", 0);
    if (rootWindow->property("libraryScanning").toBool() != scanningBefore) {
      fail(QStringLiteral("Startup scan completion did not release the loading state")); return;
    }
    rootWindow->setProperty("pendingStartupScans", pendingScansBefore);
    const auto preparationRestored = [rootWindow, couchBefore](const QVariant& desktopVisibility) {
      return rootWindow->property("couchMode").toBool() == couchBefore &&
             rootWindow->property("desktopVisibility") == desktopVisibility &&
             !rootWindow->property("gameModeNavigationRestoring").toBool();
    };
    const QVariant initialDesktopVisibility = rootWindow->property("desktopVisibility");
    const auto initialWindowState = rootWindow->windowState();
    gameModeTestCompositor.placeFails = true;
    gameMode.enter();
    if (!settled([&gameMode] { return !gameMode.busy(); }) || gameMode.hasSession() ||
        !preparationRestored(initialDesktopVisibility) ||
        rootWindow->windowState() != initialWindowState) {
      fail(QStringLiteral("Failed initial entry did not cancel layout without changing desktop mode"));
      return;
    }
    gameModeTestCompositor.placeFails = false;
    // A cold retained root must prepare layout/native fullscreen before its
    // first visibleChanged, including both remaps. This observes the production
    // signal hookup, not merely the final state after the async handoff.
    const Qt::WindowState originalState = rootWindow->windowState();
    rootWindow->hide();
    gameModeTestCompositor.mapped.store(false);
    gameMode.setTemporaryWindow(true);
    const auto mappingStateCheck = QObject::connect(
        rootWindow, &QWindow::visibleChanged, rootWindow,
        [&gameModeTestCompositor](bool visible) {
          if (visible) gameModeTestCompositor.remapped.store(true);
          gameModeTestCompositor.mapped.store(visible);
        });
    const QVariant coldDesktopVisibility = rootWindow->property("desktopVisibility");
    gameModeTestCompositor.placeFails = true;
    gameMode.enter();
    if (!settled([&gameMode] { return !gameMode.busy(); }) || gameMode.hasSession() ||
        !rootWindow->isVisible() || rootWindow->windowState() != originalState ||
        !preparationRestored(coldDesktopVisibility)) {
      fail(QStringLiteral("Failed cold entry did not restore the fallback library's native mode"));
      return;
    }
    gameModeTestCompositor.placeFails = false;
    rootWindow->hide();
    bool mappedWrongPresentation = false;
    int mapped = 0;
    const auto mappingCheck = QObject::connect(
        rootWindow, &QWindow::visibleChanged, rootWindow, [&](bool visible) {
          if (!visible) return;
          ++mapped;
          mappedWrongPresentation = mappedWrongPresentation ||
              !rootWindow->property("couchMode").toBool() ||
              rootWindow->windowState() != Qt::WindowFullScreen;
        });
    for (int cycle = 0; cycle < 3; ++cycle) {
      gameMode.enter();
      if (!rootWindow->property("couchMode").toBool() || rootWindow->isVisible() ||
          rootWindow->windowState() != (cycle == 0 ? originalState : Qt::WindowFullScreen)) {
        fail(QStringLiteral("Cold layout preparation mapped or changed native mode before snapshot"));
        return;
      }
      if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); })) {
        fail(QStringLiteral("Cold presentation fixture did not enter or resume"));
        return;
      }
      gameMode.park();
      if (!settled([&gameMode, rootWindow] {
            return gameMode.parked() && !gameMode.busy() && !rootWindow->isVisible();
          })) {
        fail(QStringLiteral("Cold presentation fixture did not hide on park"));
        return;
      }
      if (cycle == 0) {
        const QVariant desktopVisibility = rootWindow->property("desktopVisibility");
        gameModeTestCompositor.placeFails = true;
        gameMode.enter();
        if (!settled([&gameMode] { return !gameMode.busy(); }) || !gameMode.parked() ||
            rootWindow->isVisible() || !preparationRestored(desktopVisibility)) {
          qCritical() << "Cancelled resume state" << gameMode.parked() << rootWindow->isVisible()
                      << rootWindow->property("couchMode") << couchBefore
                      << rootWindow->property("desktopVisibility") << desktopVisibility
                      << rootWindow->property("gameModeNavigationRestoring");
          fail(QStringLiteral("Failed resume did not restore parked layout, visibility and navigation guard"));
          return;
        }
        gameModeTestCompositor.placeFails = false;
      }
    }
    QObject::disconnect(mappingCheck);
    if (mappedWrongPresentation || mapped != 4) {
      fail(QStringLiteral("Cold root was exposed before couch layout and native fullscreen"));
      return;
    }
    gameMode.exit();
    if (!settled([&gameMode] { return !gameMode.hasSession() && !gameMode.busy(); })) {
      fail(QStringLiteral("Cold presentation fixture did not end"));
      return;
    }
    gameMode.setTemporaryWindow(false);
    rootWindow->setWindowState(originalState);
    // Shown again, the root maps as a new client on the desktop workspace, as it does
    // in Hyprland. The mapping check models that until it is shown.
    rootWindow->show();
    QObject::disconnect(mappingStateCheck);
    gameModeTestCompositor.mapped.store(true);
    // Smoke fixtures skip the normal --couch startup fullscreen step. Match the
    // real startup state before exercising a transient overlay and its focus.
    if (couchBefore) {
      rootWindow->showFullScreen();
      rootWindow->requestActivate();
      QCoreApplication::processEvents();
    }
    const Qt::WindowState warmState = rootWindow->windowState();
    gameMode.enter();
    if (!rootWindow->property("couchMode").toBool() || !rootWindow->isVisible() ||
        rootWindow->windowState() != warmState) {
      fail(QStringLiteral("Warm preparation changed native mode before the desktop snapshot"));
      return;
    }
    if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); })) {
      fail(QStringLiteral("Game Mode did not start"));
      return;
    }
    if (!rootWindow->property("couchMode").toBool() ||
        !rootWindow->property("gameModeActive").toBool()) {
      fail(QStringLiteral("Game Mode did not open its fullscreen layout"));
      return;
    }
    // Game Mode lands on Home, and the desktop goes back to the page it showed.
    const bool homeBefore = false;
    if (!rootWindow->property("homeOpen").toBool()) {
      fail(QStringLiteral("Game Mode did not land on Home"));
      return;
    }
    // With no game running, leaving ends Game Mode at once, with no controls menu.
    // Both Leave buttons and the Start and F11 toggle do the same.
    for (const char* leaveRoute : {"toggle", "couchDesktopButton", "homeCouchDesktopButton"}) {
      if (QByteArray(leaveRoute) == "toggle") {
        QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
      } else {
        rootWindow->setProperty("homeOpen", QByteArray(leaveRoute) == "homeCouchDesktopButton");
        auto* button = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), leaveRoute);
        if (!button || !settled([button] { return button->isVisible() && button->isEnabled(); }) ||
            button->property("text").toString() != "LEAVE") {
          fail(QStringLiteral("Game Mode's shared header has no LEAVE on %1").arg(leaveRoute));
          return;
        }
        QMetaObject::invokeMethod(button, "clicked");
      }
      if (!settled([&gameMode] { return !gameMode.hasSession() && !gameMode.busy(); }) ||
          rootWindow->property("couchMode").toBool() != couchBefore ||
          rootWindow->property("homeOpen").toBool() != homeBefore) {
        fail(QStringLiteral("Leaving with no game running (%1) did not end Game Mode directly")
                 .arg(leaveRoute));
        return;
      }
      gameMode.enter();
      if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); }) ||
          !rootWindow->property("homeOpen").toBool()) {
        fail(QStringLiteral("Game Mode did not start again on Home"));
        return;
      }
    }
    // A game on the Game Mode workspace: every way of leaving opens the controls instead.
    gameModeTestCompositor.otherWindows = 1;
    rootWindow->setProperty("homeOpen", false);
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
    auto* leave = rootWindow->findChild<QQuickItem*>(QStringLiteral("gameModeLeaveButton"));
    auto* safeBack = rootWindow->findChild<QQuickItem*>(QStringLiteral("gameModeBackButton"));
    if (leave == nullptr || safeBack == nullptr || !settled([safeBack] { return safeBack->hasActiveFocus(); })) {
      fail(QStringLiteral("Switching modes in Game Mode did not focus Back to Library"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "setCouchMode", Q_ARG(QVariant, QVariant(false)));
    if (!settled([rootWindow] { return !rootWindow->property("gameModeLeaveChecking").toBool(); }) ||
        !rootWindow->property("couchMode").toBool() || !gameMode.active()) {
      fail(QStringLiteral("Switching to desktop with a game running left Game Mode"));
      return;
    }
    auto* back = rootWindow->findChild<QQuickItem*>(QStringLiteral("gameModeBackButton"));
    if (back == nullptr) {
      fail(QStringLiteral("Game Mode controls have no Back to Library action"));
      return;
    }
    QMetaObject::invokeMethod(back, "clicked");
    QCoreApplication::processEvents();
    if (!gameMode.active() || !rootWindow->property("couchMode").toBool()) {
      fail(QStringLiteral("Back to Library ended Game Mode"));
      return;
    }
    // An editor left open in the main window must not send the controller's
    // directions there while the overlay owns focus. Exercise the real key route
    // with an ordinary transient window on offscreen CI; layer-shell mapping is
    // separately checked in the isolated compositor.
    auto* overlay = rootWindow->findChild<QQuickWindow*>("gameModeOverlay");
    auto* overlayPanel = rootWindow->findChild<QObject*>("overlaygameModeControls");
    auto* overlayBack = rootWindow->findChild<QQuickItem*>("overlaygameModeBackButton");
    auto* overlayLeave = rootWindow->findChild<QQuickItem*>("overlaygameModeLeaveButton");
    if (!overlay || !overlayPanel || !overlayBack || !overlayLeave) {
      fail(QStringLiteral("Game Mode overlay controls are missing"));
      return;
    }
    auto* mainPanel = rootWindow->findChild<QObject*>("gameModeControls");
    if (!mainPanel || !settled([mainPanel] {
          return !mainPanel->property("visible").toBool();
        })) {
      fail(QStringLiteral("Main Game Mode controls did not finish closing"));
      return;
    }
    // Home's LEAVE is the library's: with a game running it opens the controls.
    {
      rootWindow->setProperty("homeOpen", true);
      auto* homeDesktop = rootWindow->findChild<QQuickItem*>(QStringLiteral("homeCouchDesktopButton"));
      if (!homeDesktop || !settled([homeDesktop] { return homeDesktop->isVisible() && homeDesktop->isEnabled(); })) {
        fail(QStringLiteral("Home LEAVE is not available in Game Mode"));
        return;
      }
      QMetaObject::invokeMethod(homeDesktop, "clicked");
      if (!settled([mainPanel, safeBack] {
            return mainPanel->property("visible").toBool() && safeBack->hasActiveFocus();
          }) || !gameMode.active()) {
        fail(QStringLiteral("Home LEAVE with a game running did not open the Game Mode controls"));
        return;
      }
      QMetaObject::invokeMethod(safeBack, "clicked");
      if (!settled([mainPanel] { return !mainPanel->property("visible").toBool(); }) ||
          !gameMode.active() || !rootWindow->property("homeOpen").toBool()) {
        fail(QStringLiteral("Back to Library from Home's controls ended Game Mode or left Home"));
        return;
      }
      // B in Settings opened from Home closes only Settings, in Game Mode as on the desktop.
      rootWindow->setProperty("homeOpen", true);
      auto* homeSettings = rootWindow->findChild<QQuickItem*>(QStringLiteral("homeCouchSettingsButton"));
      if (!homeSettings || !settled([homeSettings] { return homeSettings->isVisible(); })) {
        fail(QStringLiteral("Home SETTINGS is not available in Game Mode"));
        return;
      }
      QMetaObject::invokeMethod(homeSettings, "clicked");
      if (!settled([rootWindow] { return rootWindow->property("diagnosticsOpen").toBool(); })) {
        fail(QStringLiteral("Home SETTINGS did not open Settings in Game Mode"));
        return;
      }
      QCoreApplication::processEvents();
      controller.backRequested();
      QCoreApplication::processEvents();
      if (!settled([rootWindow] { return !rootWindow->property("diagnosticsOpen").toBool(); }) ||
          !rootWindow->property("homeOpen").toBool()) {
        fail(QStringLiteral("B in Settings from Home left Home in Game Mode (settings=%1, home=%2, focus=%3)")
                 .arg(rootWindow->property("diagnosticsOpen").toBool())
                 .arg(rootWindow->property("homeOpen").toBool())
                 .arg(qobject_cast<QQuickWindow*>(rootWindow)->activeFocusItem()
                          ? qobject_cast<QQuickWindow*>(rootWindow)->activeFocusItem()->objectName() : QString{}));
        return;
      }
      rootWindow->setProperty("homeOpen", false);
      QCoreApplication::processEvents();
    }
    rootWindow->setProperty("diagnosticsOpen", true);
    // Let the main editor finish its deferred focus before opening another window.
    QCoreApplication::processEvents();
    overlay->setVisible(true);
    overlay->requestActivate();
    if (!settled([overlay] { return overlay->isActive(); })) {
      fail(QStringLiteral("Game Mode overlay window did not gain focus"));
      return;
    }
    QMetaObject::invokeMethod(overlayPanel, "openControls");
    if (!settled([&controller, overlay, overlayBack] {
          return overlay->isActive() && controller.inputEnabled() && overlayBack->hasActiveFocus();
        }) || controller.focusNavigation()) {
      fail(QStringLiteral("Game Mode overlay did not take controller navigation "
                          "(active=%1, input=%2, focus=%3, navigation=%4, item=%5)")
               .arg(overlay->isActive()).arg(controller.inputEnabled())
               .arg(overlayBack->hasActiveFocus()).arg(controller.focusNavigation())
               .arg(overlay->activeFocusItem() ? overlay->activeFocusItem()->objectName() : QString{}));
      return;
    }
    // Each press is one step through the overlay's actions, in the order its menu reads,
    // down to the last and back up to the first.
    QList<QQuickItem*> overlayActions;
    const auto collectActions = [&overlayActions](auto&& self, QQuickItem* item) -> void {
      if (!item->isVisible() || !item->isEnabled()) return;
      if (item->activeFocusOnTab()) overlayActions.append(item);
      for (auto* child : item->childItems()) self(self, child);
    };
    collectActions(collectActions, overlay->contentItem());
    if (overlayActions.size() < 2 || overlayActions.first() != overlayBack ||
        !overlayActions.contains(overlayLeave)) {
      fail(QStringLiteral("Game Mode overlay actions are not Back to Game first, then Leave"));
      return;
    }
    for (int step = 1; step < overlayActions.size(); ++step) {
      controller.focusDirectionRequested(Qt::Key_Down);
      if (!overlayActions[step]->hasActiveFocus()) {
        fail(QStringLiteral("Controller Down on the Game Mode overlay did not move one step to %1")
                 .arg(overlayActions[step]->objectName()));
        return;
      }
    }
    for (int step = overlayActions.size() - 2; step >= 0; --step) {
      controller.focusDirectionRequested(Qt::Key_Up);
      if (!overlayActions[step]->hasActiveFocus()) {
        fail(QStringLiteral("Controller Up on the Game Mode overlay did not move one step to %1")
                 .arg(overlayActions[step]->objectName()));
        return;
      }
    }
    controller.backRequested();
    if (!settled([overlay] { return !overlay->isVisible(); }) || !gameMode.active()) {
      fail(QStringLiteral("Controller Back did not dismiss only the Game Mode overlay"));
      return;
    }
    rootWindow->setProperty("diagnosticsOpen", false);
    rootWindow->requestActivate();
    if (!settled([rootWindow] { return rootWindow->isActive(); })) {
      fail(QStringLiteral("Main window did not regain focus after the overlay check"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
    if (!settled([safeBack] { return safeBack->hasActiveFocus(); })) {
      fail(QStringLiteral("Reopening Game Mode controls lost keyboard focus"));
      return;
    }
    if (leave->property("text").toString() != "RETURN TO DESKTOP") {
      fail(QStringLiteral("Library-only controls do not offer Return to Desktop"));
      return;
    }
    QMetaObject::invokeMethod(back, "clicked");
    if (!settled([mainPanel] { return !mainPanel->property("visible").toBool(); })) {
      fail(QStringLiteral("Game Mode controls did not close before details retention"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, QVariant(0)));
    auto* details = rootWindow->findChild<QQuickItem*>("gameDetails");
    auto* detailsBack = details ? details->findChild<QQuickItem*>("detailsBackButton") : nullptr;
    auto* scroll = details ? details->findChild<QObject*>("detailsScroll") : nullptr;
    auto* flickable =
        scroll ? scroll->property("navigationFlickable").value<QObject*>() : nullptr;
    if (!details || !detailsBack || !flickable ||
        !settled([detailsBack] { return detailsBack->isVisible(); })) {
      fail(QStringLiteral("Retained details fixture did not load"));
      return;
    }
    auto* info = details->findChild<QObject*>("gameInfoSection");
    if (!info) {
      fail(QStringLiteral("Retained details fixture has no game information section"));
      return;
    }
    info->setProperty(
        "entry",
        QVariantMap{
            {"summary",
             QStringLiteral("A long description for the retained details scroll regression.\n")
                 .repeated(40)}});
    info->setProperty("expanded", true);
    const QVariant selection = rootWindow->property("selectedGame");
    details->setProperty("aliasesExpanded", true);
    details->setProperty("romDetailsExpanded", true);
    detailsBack->forceActiveFocus();
    QCoreApplication::processEvents();
    detailsBack->forceActiveFocus();
    if (!settled([detailsBack] { return detailsBack->hasActiveFocus(); })) {
      fail(QStringLiteral("Retained details fixture did not gain keyboard focus"));
      return;
    }
    // Use a real ScrollView offset, with enough content to distinguish retention
    // from focusPrimary() or a newly loaded details page resetting to the top.
    if (!settled([flickable] {
          return flickable->property("contentHeight").toDouble() >
                 flickable->property("height").toDouble() + 10;
        })) {
      fail(QStringLiteral("Retained details fixture has no scrollable content"));
      return;
    }
    const double retainedScroll = 40;
    flickable->setProperty("contentY", retainedScroll);
    for (int cycle = 0; cycle < 3; ++cycle) {
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
          !gameMode.hasSession() || rootWindow->property("couchMode").toBool() != couchBefore ||
          rootWindow->findChild<QQuickItem*>("gameDetails") != details ||
          rootWindow->findChild<QObject*>("gameModeControls") != mainPanel) {
        fail(QStringLiteral("Return to Desktop destroyed the library UI or previous mode"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      if (!settled([&gameMode, rootWindow] {
            return gameMode.active() && !gameMode.busy() &&
                   !rootWindow->property("gameModeNavigationRestoring").toBool();
          }) ||
          rootWindow->findChild<QQuickItem*>("gameDetails") != details ||
          rootWindow->property("selectedGame") != selection ||
          !rootWindow->property("detailOpen").toBool() ||
          !details->property("aliasesExpanded").toBool() ||
          !details->property("romDetailsExpanded").toBool() ||
          !info->property("expanded").toBool() || !detailsBack->hasActiveFocus() ||
          qAbs(flickable->property("contentY").toDouble() - retainedScroll) > 1) {
        fail(QStringLiteral(
            "Resume lost details identity, selection, expanded state, focus or scroll"));
        return;
      }
    }
    auto* manageMenu = details->findChild<QObject*>("detailManageMenu");
    auto* hideAction = details->findChild<QQuickItem*>("hideButton");
    if (!manageMenu || !hideAction) {
      fail(QStringLiteral("Retained details menu fixture is missing"));
      return;
    }
    QMetaObject::invokeMethod(manageMenu, "open");
    if (!settled([manageMenu] { return manageMenu->property("opened").toBool(); })) {
      fail(QStringLiteral("Retained details menu did not open"));
      return;
    }
    hideAction->forceActiveFocus(); // Deliberately select a noninitial menu action.
    for (int cycle = 0; cycle < 2; ++cycle) {
      if (!settled([hideAction] { return hideAction->hasActiveFocus(); })) {
        fail(QStringLiteral("Retained details menu did not focus its noninitial action"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
          !manageMenu->property("opened").toBool()) {
        fail(QStringLiteral("Park closed a surviving details menu"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      if (!settled([&gameMode, rootWindow, hideAction] {
            return gameMode.active() && !gameMode.busy() &&
                   !rootWindow->property("gameModeNavigationRestoring").toBool() &&
                   hideAction->hasActiveFocus();
          }) || !manageMenu->property("opened").toBool() ||
          rootWindow->property("selectedGame") != selection) {
        fail(QStringLiteral("Resume reset a retained details menu's noninitial focus"));
        return;
      }
    }
    QMetaObject::invokeMethod(manageMenu, "close");
    if (!settled([manageMenu] { return !manageMenu->property("visible").toBool(); })) {
      fail(QStringLiteral("Retained details menu did not close"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "closeDetails");
    rootWindow->setProperty("homeOpen", true);
    auto* home = rootWindow->findChild<QQuickItem*>("homeScreen");
    auto* homeLibrary = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), "homeLibraryButton");
    QMetaObject::invokeMethod(rootWindow, "loadSettingsPanel");
    auto* settings = rootWindow->findChild<QQuickItem*>("settingsOverlay");
    // The section sidebar is hidden at compact widths; Close stays available
    // in both layouts while the selected Controls section is checked below.
    auto* controlsSection = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), "closeSettings");
    if (!home || !homeLibrary || !settings || !controlsSection) {
      fail(QStringLiteral("Retained Home and Settings fixtures are missing"));
      return;
    }
    for (bool settingsPage : {false, true}) {
      rootWindow->setProperty("diagnosticsOpen", settingsPage);
      if (settingsPage)
        QMetaObject::invokeMethod(settings, "chooseSection", Q_ARG(QVariant, QVariant(3)));
      auto* pageFocus = settingsPage ? controlsSection : homeLibrary;
      if (!settled([pageFocus] { return pageFocus->isVisible() && pageFocus->isEnabled(); })) {
        fail(QStringLiteral("Retained %1 page did not become visible")
                 .arg(settingsPage ? QStringLiteral("Settings") : QStringLiteral("Home")));
        return;
      }
      QCoreApplication::processEvents();
      pageFocus->forceActiveFocus();
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      // Parked, the desktop shows the page it had before Game Mode; Settings stays open.
      if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
          rootWindow->property("homeOpen") != rootWindow->property("homeBeforeGameMode") ||
          rootWindow->property("diagnosticsOpen").toBool() != settingsPage) {
        fail(QStringLiteral("Park did not show the desktop's own page or lost Settings"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
      if (!settled([&gameMode, rootWindow, pageFocus] {
            return gameMode.active() && !gameMode.busy() &&
                   !rootWindow->property("gameModeNavigationRestoring").toBool() &&
                   pageFocus->hasActiveFocus();
          }) || rootWindow->findChild<QQuickItem*>("homeScreen") != home ||
          rootWindow->findChild<QQuickItem*>("settingsOverlay") != settings ||
          !rootWindow->property("homeOpen").toBool() ||
          rootWindow->property("diagnosticsOpen").toBool() != settingsPage ||
          (settingsPage && settings->property("section").toInt() != 3)) {
        fail(QStringLiteral("Resume lost Home or Settings identity, page or focus"));
        return;
      }
    }
    rootWindow->setProperty("diagnosticsOpen", false);
    rootWindow->setProperty("homeOpen", false);
    QMetaObject::invokeMethod(rootWindow, "openGameModeControls");
    auto* end = rootWindow->findChild<QQuickItem*>("gameModeEndButton");
    if (!end || !settled([end] { return end->isVisible() && end->isEnabled(); })) {
      fail(QStringLiteral("Library-only controls have no explicit End Game Mode action"));
      return;
    }
    QMetaObject::invokeMethod(end, "clicked");
    if (!settled([&gameMode] { return !gameMode.active() && !gameMode.busy(); })) {
      fail(QStringLiteral("End Game Mode did not end the session"));
      return;
    }
    if (rootWindow->property("couchMode").toBool() != couchBefore) {
      fail(QStringLiteral("Leaving Game Mode did not return to the previous mode"));
      return;
    }
    if (rootWindow->property("diagnosticsOpen").toBool()) {
      fail(QStringLiteral("Leaving Game Mode left Settings open"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "closeDetails");
    // Every surface is retained through park/resume, then cleared by explicit
    // End from both active and parked states, including a preexisting couch root.
    auto* couchLibrary = rootWindow->findChild<QQuickItem*>("couchLibrary");
    auto* textKeyboard = rootWindow->findChild<QQuickItem*>("couchTextEntryKeyboard");
    auto* textTarget = rootWindow->findChild<QQuickItem*>("searchField");
    auto* sourcesMenu = rootWindow->findChild<QObject*>("librarySources");
    auto* steamAction = rootWindow->findChild<QQuickItem*>("steamSourceButton");
    if (!couchLibrary || !textKeyboard || !textTarget || !sourcesMenu || !steamAction) {
      fail(QStringLiteral("Retained couch surface fixtures are missing"));
      return;
    }
    for (int surface = 0; surface < 4; ++surface) {
      for (bool endParked : {false, true}) {
        gameMode.enter();
        if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); }) ||
            !rootWindow->property("homeOpen").toBool()) {
          fail(QStringLiteral("Couch surface Game Mode session did not start on Home"));
          return;
        }
        rootWindow->setProperty("homeOpen", false);
        QCoreApplication::processEvents();
        if (surface == 0) {
          QMetaObject::invokeMethod(sourcesMenu, "open");
          if (!settled([sourcesMenu] { return sourcesMenu->property("opened").toBool(); })) {
            fail(QStringLiteral("Retained sources menu did not open"));
            return;
          }
          steamAction->forceActiveFocus();
        } else if (surface == 1) {
          QMetaObject::invokeMethod(rootWindow, "openCouchTextEntry",
                                    Q_ARG(QVariant, QVariant::fromValue(textTarget)),
                                    Q_ARG(QVariant, QVariant("RETAINED TEXT")),
                                    Q_ARG(QVariant, QVariant(false)),
                                    Q_ARG(QVariant, QVariant("Enter text")));
          textKeyboard->setProperty("value", QStringLiteral("unfinished entry"));
        } else {
          QMetaObject::invokeMethod(couchLibrary, surface == 2 ? "openSearch" : "openBrowse");
        }
        const auto surfaceOpen = [&] {
          if (surface == 0) return sourcesMenu->property("opened").toBool();
          if (surface == 1) return rootWindow->property("couchTextEntryOpen").toBool();
          return couchLibrary->property(surface == 2 ? "searchOpen" : "browseOpen").toBool();
        };
        if (!settled([&] {
              return surfaceOpen() && qobject_cast<QQuickWindow*>(rootWindow)->activeFocusItem() &&
                     (surface != 0 || steamAction->hasActiveFocus());
            })) {
          fail(QStringLiteral("Retained couch surface did not open with focus"));
          return;
        }
        QCoreApplication::processEvents();
        auto* retainedFocus = qobject_cast<QQuickWindow*>(rootWindow)->activeFocusItem();
        QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
        if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
            !surfaceOpen()) {
          fail(QStringLiteral("Park cleared a retained couch surface"));
          return;
        }
        QMetaObject::invokeMethod(rootWindow, "toggleGameMode");
        if (!settled([&] {
              return gameMode.active() && !gameMode.busy() &&
                     !rootWindow->property("gameModeNavigationRestoring").toBool() &&
                     retainedFocus->hasActiveFocus();
            }) || !surfaceOpen() ||
            (surface == 1 && textKeyboard->property("value").toString() != "unfinished entry")) {
          fail(QStringLiteral("Resume lost retained menu, keyboard, search or browse state"));
          return;
        }
        if (endParked) {
          gameMode.park();
          if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
              !surfaceOpen()) {
            fail(QStringLiteral("Second park cleared a retained couch surface"));
            return;
          }
        }
        gameMode.exit();
        if (!settled([&] {
              return !gameMode.hasSession() && !gameMode.busy() && !surfaceOpen();
            }) || rootWindow->property("couchTextEntryOpen").toBool() ||
            couchLibrary->property("searchOpen").toBool() ||
            couchLibrary->property("browseOpen").toBool() ||
            rootWindow->property("couchMode").toBool() != couchBefore) {
          fail(QStringLiteral("Explicit End left retained couch navigation open"));
          return;
        }
      }
    }
    // Exercise the combined stop/leave dialog without sending any process signals.
    gameMode.enter();
    if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); })) {
      fail(QStringLiteral("Second Game Mode session did not start"));
      return;
    }
    gameStop.setRowsProvider([] {
      return QVariantList{QVariantMap{{"title", "Fixture Game"}, {"source", "Manual"},
                                     {"appId", "fixture"}, {"installPath", "/fixtures/game"}}};
    });
    gameStop.setSnapshotProvider([] {
      ProcessSnapshot process;
      process.pid = 4242;
      process.procStart = 424200;
      process.comm = "fixture-game";
      process.arguments = {"/fixtures/game/fixture-game"};
      process.exePath = process.arguments.first();
      return QVector<ProcessSnapshot>{process};
    });
    QMetaObject::invokeMethod(rootWindow, "openGameModeControls");
    auto* stop = rootWindow->findChild<QQuickItem*>("gameModeStopAndLeaveButton");
    auto* cancel = rootWindow->findChild<QQuickItem*>("gameModecancelStop");
    auto* panel = rootWindow->findChild<QObject*>("gameModegameStopPanel");
    back = rootWindow->findChild<QQuickItem*>("gameModeBackButton");
    if (!stop || !cancel || !panel || !back
        || !settled([back, stop, &gameStop] { return back->hasActiveFocus() && stop->isVisible() && !gameStop.scanning(); })) {
      fail(QStringLiteral("Running game controls did not default to Back to Library"));
      return;
    }
    QMetaObject::invokeMethod(stop, "clicked");
    if (!settled([cancel] { return cancel->hasActiveFocus(); })) {
      fail(QStringLiteral("Stop and Leave did not focus its safe Cancel action"));
      return;
    }
    QMetaObject::invokeMethod(cancel, "clicked");
    gameStop.finished(true, "Unrelated completed stop", {});
    if (!gameMode.active()) {
      fail(QStringLiteral("Cancel or an unrelated stop ended Game Mode"));
      return;
    }
    QMetaObject::invokeMethod(panel, "beginAll");
    panel->setProperty("pending", true);
    gameStop.finished(false, "Fixture failure", {});
    if (!gameMode.active() || panel->property("pending").toBool()
        || panel->property("resultMessage").toString() != "Fixture failure") {
      fail(QStringLiteral("Failed stop did not keep Game Mode active and show its result"));
      return;
    }
    panel->setProperty("pending", true);
    gameStop.setSnapshotProvider([] { return QVector<ProcessSnapshot>{}; });
    gameStop.finished(true, "Fixture stopped", {});
    if (!settled([&gameMode] { return !gameMode.hasSession() && !gameMode.busy(); })) {
      fail(QStringLiteral("Successful stop did not leave Game Mode"));
      return;
    }
    gameMode.enter();
    if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); })) {
      fail(QStringLiteral("Parked stop fixture did not enter Game Mode"));
      return;
    }
    // Ending the first session unloads its controls. Use the new session's
    // dialog instead of retaining a pointer to the destroyed old object.
    panel = rootWindow->findChild<QObject*>("gameModegameStopPanel");
    if (!panel) {
      fail(QStringLiteral("Parked stop fixture has no current confirmation panel"));
      return;
    }
    QMetaObject::invokeMethod(panel, "beginAll");
    panel->setProperty("pending", true);
    gameMode.park();
    if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
        !panel->property("pending").toBool()) {
      fail(QStringLiteral("Parking cancelled a pending Stop Games and Leave"));
      return;
    }
    QMetaObject::invokeMethod(panel, "beginAll");
    if (!panel->property("pending").toBool()) {
      fail(QStringLiteral("Reopening discarded a pending Stop Games and Leave"));
      return;
    }
    gameStop.finished(false, "Parked fixture failure", {});
    if (!gameMode.parked() || panel->property("pending").toBool() ||
        panel->property("resultMessage").toString() != "Parked fixture failure") {
      fail(QStringLiteral("Failed parked stop did not retain the session and show its result"));
      return;
    }
    panel->setProperty("pending", true);
    gameStop.finished(true, "Parked fixture stopped", {});
    if (!settled([&gameMode] { return !gameMode.hasSession() && !gameMode.busy(); })) {
      fail(QStringLiteral("Successful parked stop did not end the retained session"));
      return;
    }
    // Fullscreen is Game Mode: F11 and Start start it in place, on Home, and leaving
    // with nothing running goes straight back to the desktop library.
    auto* preferences = qmlContext(rootWindow)->contextProperty("Preferences").value<QObject*>();
    auto* toast = rootWindow->findChild<QObject*>(QStringLiteral("toast"));
    if (!preferences || !toast) {
      fail(QStringLiteral("In-place Game Mode fixtures are missing"));
      return;
    }
    gameModeTestCompositor.otherWindows = 0;
    if (rootWindow->property("couchMode").toBool()) {
      // The --couch fixture starts in the plain fullscreen layout without a session.
      QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
      if (!settled([rootWindow] { return !rootWindow->property("couchMode").toBool(); }) ||
          gameMode.hasSession()) {
        fail(QStringLiteral("Leaving plain fullscreen started Game Mode"));
        return;
      }
    }
    // The fixture compositor records window modes without applying them, so the earlier
    // sessions leave Qt's window fullscreen. Start from the desktop window F11 sees.
    rootWindow->showNormal();
    if (!settled([rootWindow] { return rootWindow->windowState() != Qt::WindowFullScreen; })) {
      fail(QStringLiteral("The desktop window did not leave fullscreen before F11"));
      return;
    }
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
    if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); }) ||
        !gameMode.inPlace() || !rootWindow->property("couchMode").toBool() ||
        !rootWindow->property("homeOpen").toBool() ||
        rootWindow->windowState() != Qt::WindowFullScreen) {
      fail(QStringLiteral("F11 did not start Game Mode in place on Home"));
      return;
    }
    if (gameModeTestCompositor.window.workspace != GameModeController::workspace() ||
        gameMode.sessionOutputName() != QStringLiteral("fixture")) {
      fail(QStringLiteral("In-place Game Mode did not take its workspace on the current display"));
      return;
    }
    if (!preferences->property("couchModeEnabled").toBool()) {
      fail(QStringLiteral("F11 Game Mode was not remembered for the next start"));
      return;
    }
    // The guide sees an in-place session as Game Mode, at the fullscreen scale.
    const QJsonObject guide = gameMode.guideState(rootWindow->property("couchMode").toBool());
    if (!guide.value("gameModeActive").toBool() || guide.value("gameModeParked").toBool() ||
        guide.value("scale").toDouble() != 1.7) {
      fail(QStringLiteral("The guide was not told about the in-place Game Mode session"));
      return;
    }
    auto* homeLeave = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(),
                                     "homeCouchDesktopButton");
    if (!homeLeave || !settled([homeLeave] { return homeLeave->isVisible() && homeLeave->isEnabled(); })) {
      fail(QStringLiteral("In-place Game Mode Home has no LEAVE"));
      return;
    }
    QMetaObject::invokeMethod(homeLeave, "clicked");
    if (!settled([&gameMode] { return !gameMode.hasSession() && !gameMode.busy(); }) ||
        rootWindow->property("couchMode").toBool() || rootWindow->property("homeOpen").toBool() ||
        !settled([rootWindow] { return rootWindow->windowState() != Qt::WindowFullScreen; })) {
      fail(QStringLiteral("Leaving in-place Game Mode did not return to the desktop library "
                          "(session=%1, fullscreen layout=%2, home=%3, window state=%4)")
               .arg(gameMode.hasSession()).arg(rootWindow->property("couchMode").toBool())
               .arg(rootWindow->property("homeOpen").toBool()).arg(int(rootWindow->windowState())));
      return;
    }
    if (preferences->property("couchModeEnabled").toBool()) {
      fail(QStringLiteral("Leaving in-place Game Mode was not remembered"));
      return;
    }
    if (gameModeTestCompositor.window.workspace != QStringLiteral("1")) {
      fail(QStringLiteral("Leaving in-place Game Mode did not put the window back on the desktop"));
      return;
    }
    // A start that fails falls back to the plain fullscreen layout and says so.
    gameModeTestCompositor.placeFails = true;
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
    if (!settled([&gameMode, rootWindow] {
          return !gameMode.busy() && rootWindow->property("couchMode").toBool();
        }) || gameMode.hasSession() || rootWindow->windowState() != Qt::WindowFullScreen ||
        !toast->property("message").toString().contains(QStringLiteral("without Game Mode"))) {
      fail(QStringLiteral("A failed in-place start did not fall back to plain fullscreen with a toast "
                          "(session=%1, busy=%2, fullscreen layout=%3, window state=%4, toast=%5, workspace=%6)")
               .arg(gameMode.hasSession()).arg(gameMode.busy())
               .arg(rootWindow->property("couchMode").toBool()).arg(int(rootWindow->windowState()))
               .arg(toast->property("message").toString(), gameModeTestCompositor.window.workspace));
      return;
    }
    gameModeTestCompositor.placeFails = false;
    QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
    if (!settled([rootWindow] { return !rootWindow->property("couchMode").toBool(); }) ||
        gameMode.hasSession()) {
      fail(QStringLiteral("Leaving the plain fullscreen fallback did not return to the desktop"));
      return;
    }
    application.quit();
  });
}

} // namespace SelfTest

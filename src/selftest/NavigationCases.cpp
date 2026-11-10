#include "selftest/SelfTest.h"

#include "selftest/SelfTestSupport.h"
#include "input/ControllerInput.h"
#include "library/LibraryFilterModel.h"
#include "selftest/CouchNavigationContract.h"
#include "selftest/KeyboardTestInput.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointF>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <utility>

namespace SelfTest {

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

// --couch-navigation-test: the couch library, its toolbar and details with the controller and
// the keyboard.
void driveCouchNavigation(QQuickWindow* quickWindow, QGuiApplication& application,
                          ControllerInput& controller) {
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
}

// --controller-navigation-test: the desktop library, its toolbar, filters and details with the
// controller and the keyboard.
void driveControllerNavigation(QQuickWindow* quickWindow, QGuiApplication& application,
                               ControllerInput& controller, bool ownedLayoutTest) {
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

} // namespace

void startNavigation(Harness& harness, AppContext& app) {
  const Options& options = harness.options;
  QQuickWindow* quickWindow = app.quickWindow;
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  if (options.couchNavigationContract) {
    QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
      application.exit(runCouchNavigationContract(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
    });
  }

  if (options.couchNavigationSweep) {
    QTimer::singleShot(150, quickWindow, [quickWindow, &application, &controller] {
      application.exit(runCouchNavigationSweep(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
    });
  }

  if (options.startupNavigationTest) {
    QTimer::singleShot(500, quickWindow, [quickWindow, &application, &controller] {
      application.exit(runStartupNavigationContract(quickWindow, controller) ? EXIT_SUCCESS : EXIT_FAILURE);
    });
  }
  // --details-direction-test starts after this, from windowReady. With that flag, neither
  // drive starts, so only one driver moves focus in the window.
  if (options.detailsDirectionTest) return;
  if (options.couchNavigationTest) {
    driveCouchNavigation(quickWindow, application, controller);
  } else if (options.controllerNavigationTest && !options.couchNavigationContract &&
             !options.couchNavigationSweep && !options.startupNavigationTest) {
    driveControllerNavigation(quickWindow, application, controller, options.ownedLayoutTest);
  }
}

} // namespace SelfTest

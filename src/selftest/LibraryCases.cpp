#include "selftest/SelfTest.h"

#include "app/AppSettings.h"
#include "selftest/SelfTestSupport.h"
#include "gamemode/LaunchHold.h"
#include "input/ControllerInput.h"
#include "library/LibraryFilterModel.h"
#include "selftest/KeyboardTestInput.h"

#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>

#include <memory>

namespace SelfTest {

void driveLibraryPreferences(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  const QString& settingsPath = *app.settingsPath;
  AppSettings& preferences = *app.preferences;
  ControllerInput& controller = *app.controller;
  LibraryFilterModel& library = *app.library;
  QWindow*& rootWindow = *app.rootWindow;
  const bool& gogSettingsTest = harness.options.gogSettingsTest;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  const QString& gogAvailableFolder = harness.fixtures.gogAvailableFolder;
  const QString& gogMissingFolder = harness.fixtures.gogMissingFolder;
  QString& linkedManualId = harness.fixtures.linkedManualId;
  const QString& linkedExecutable = harness.fixtures.linkedExecutable;
  if (gogSettingsTest) QMetaObject::invokeMethod(rootWindow, "loadSettingsPanel");
  auto* timer = new QTimer(rootWindow);
  timer->setInterval(100);
  auto step = std::make_shared<int>(0);
  QObject::connect(timer, &QTimer::timeout, rootWindow, [&, timer, step] {
    auto* window = qobject_cast<QQuickWindow*>(rootWindow);
    const auto find = [window](const QString& name) { return findVisualItem(window->contentItem(), name); };
    const auto fail = [&](const QString& message) { timer->stop(); qCritical().noquote() << message; application.exit(EXIT_FAILURE); };
    const auto press = [&](const QString& name) {
      auto* button = find(name);
      if (!button || !button->isVisible() || !button->isEnabled()) return false;
      button->forceActiveFocus(); controller.acceptRequested(); return true;
    };
    if (gogSettingsTest) {
      auto* field = find("gogLibraryPathField");
      if (!field) { fail("GOG path field missing"); return; }
      if (*step == 0) {
        rootWindow->setProperty("diagnosticsOpen", true);
        field->forceActiveFocus();
        auto* scroll = find("settingsScroll");
        QMetaObject::invokeMethod(rootWindow, "revealInScrollView", Q_ARG(QVariant, QVariant::fromValue(scroll)), Q_ARG(QVariant, QVariant::fromValue(field)));
        ++*step;
      } else if (*step == 1) {
        if (!field->isVisible()) { fail("GOG settings are unavailable"); return; }
        if (rootWindow->property("couchMode").toBool()) {
          field->forceActiveFocus();
          controller.acceptRequested();
          if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("GOG path controller keyboard did not open"); return; }
          QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
        }
        field->setProperty("text", gogAvailableFolder);
        if (rootWindow->property("couchMode").toBool()) {
          field->forceActiveFocus();
          controller.focusDirectionRequested(Qt::Key_Down);
          auto* add = find("gogAddFolderButton");
          if (!add || !add->hasActiveFocus()) { fail("Directional navigation cannot reach Add Folder"); return; }
          controller.acceptRequested();
        } else if (!press("gogAddFolderButton")) { fail("Controller could not add a GOG folder"); return; }
        ++*step;
      } else if (*step == 2) {
        auto* status = find("gogFolderStatus_0");
        if (preferences.gogLibraryPaths() != QStringList{gogAvailableFolder} || !status || status->property("text").toString() != "Available") { fail("Available GOG folder was not saved and described"); return; }
        field->setProperty("text", gogMissingFolder);
        if (!press("gogAddFolderButton")) { fail("Missing GOG folder could not be saved"); return; }
        ++*step;
      } else if (*step == 3) {
        auto* status = find("gogFolderStatus_1");
        if (!status || !status->property("text").toString().startsWith("Unavailable")) { fail("Missing GOG drive has no useful status"); return; }
        if (AppSettings(settingsPath).gogLibraryPaths() != QStringList{gogAvailableFolder, gogMissingFolder}) { fail("GOG folders did not persist"); return; }
        if (!press("gogRemoveFolder_0")) { fail("GOG folder Remove is unreachable"); return; }
        ++*step;
      } else if (*step == 4) {
        if (!field->hasActiveFocus() || preferences.gogLibraryPaths() != QStringList{gogMissingFolder} || !QFileInfo::exists(gogAvailableFolder + "/keep-game.txt")) { fail("Removing a GOG folder lost focus or modified game files"); return; }
        if (AppSettings(settingsPath).gogLibraryPaths() != QStringList{gogMissingFolder}) { fail("Removed GOG folder remained in settings"); return; }
        controller.backRequested();
        if (rootWindow->property("diagnosticsOpen").toBool()) { fail("Controller Back did not close GOG settings"); return; }
        timer->stop(); application.quit();
      }
    } else {
      if (*step == 0) {
        QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
        ++*step;
      } else if (*step == 1) {
        if (!press("detailManageButton")) { fail("Installation choices menu is unreachable"); return; }
        if (!press("installationChoice_1")) { fail("Alternate linked installation is unreachable"); return; }
        if (rootWindow->property("selectedInstallation").toMap().value("source") != "Manual") { fail("Linked installation selection did not change"); return; }
        if (!press("detailManageButton")) { fail("Manage menu is unreachable"); return; }
        auto* preferredButton = find("preferredInstallationButton");
        for (int attempt = 0; preferredButton && !preferredButton->hasActiveFocus() && attempt < 6; ++attempt)
          controller.focusDirectionRequested(Qt::Key_Down);
        if (!preferredButton || !preferredButton->hasActiveFocus()) { fail("Directional navigation cannot reach Make Default"); return; }
        controller.acceptRequested();
        ++*step;
      } else if (*step == 2) {
        const auto preferred = library.preferredInstallation(0);
        if (preferred.value("appId") != linkedManualId || !preferred.value("preferred").toBool()) { fail("Default installation was not saved"); return; }
        QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
        if (rootWindow->property("selectedInstallation").toMap().value("appId") != linkedManualId) { fail("Reopening details lost the default"); return; }
        if (!QFile::rename(linkedExecutable, linkedExecutable + ".disconnected")) { fail("Could not simulate an unavailable manual install"); return; }
        QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
        ++*step;
      } else if (*step == 3) {
        const auto choice = rootWindow->property("selectedInstallation").toMap();
        auto* message = find("preferredUnavailableText");
        if (choice.value("source") != "Demo" || !choice.value("preferredUnavailable").toBool() || !message || !message->isVisible()) { fail("Unavailable default did not produce a usable fallback and explanation"); return; }
        if (!QFile::rename(linkedExecutable + ".disconnected", linkedExecutable)) { fail("Could not reconnect the fixture install"); return; }
        QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
        ++*step;
      } else if (*step == 4) {
        if (rootWindow->property("selectedInstallation").toMap().value("appId") != linkedManualId || QFileInfo::exists(artworkFixture.filePath("launched.txt"))) { fail("Reconnect lost the default or unexpectedly launched a game"); return; }
        controller.backRequested();
        if (rootWindow->property("detailOpen").toBool()) { fail("Controller Back did not close linked details"); return; }
        timer->stop(); application.quit();
      }
    }
  });
  QTimer::singleShot(180, timer, [timer] { timer->start(); });
  QTimer::singleShot(10000, timer, [&application] { qCritical() << "Library preferences fixture timed out"; application.exit(EXIT_FAILURE); });
}

void driveSavedFilter(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  LibraryFilterModel& library = *app.library;
  QWindow*& rootWindow = *app.rootWindow;
  QTimer::singleShot(150, &application, [&application, rootWindow, &library, &controller] {
    const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
    library.setSearchText(QStringLiteral("Aster"));
    QMetaObject::invokeMethod(rootWindow, "openSavedFilters");
    auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("savedFiltersEditor"));
    auto* name = rootWindow->findChild<QQuickItem*>(QStringLiteral("savedFilterName"));
    if (!editor || !name || !editor->isVisible()) { fail("Saved filter editor is missing"); return; }
    auto* clear = name->findChild<QQuickItem*>(QStringLiteral("savedFilterNameClearButton"));
    if (!clear) { fail("Saved filter name has no clear button"); return; }
    name->setProperty("text", "Discard this name");
    name->forceActiveFocus();
    controller.focusDirectionRequested(Qt::Key_Right);
    if (!clear->hasActiveFocus()) { fail("Saved filter clear button is unreachable"); return; }
    controller.acceptRequested();
    if (!name->property("text").toString().isEmpty() || !name->hasActiveFocus() || clear->isVisible()) {
      fail("Saved filter clear button did not clear and return focus"); return;
    }
    name->setProperty("text", "Weekend test");
    QTimer::singleShot(100, &application, [&application, rootWindow, editor, name, &library, &controller, fail] {
      const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
      if (!fieldError.isEmpty()) { fail(fieldError); return; }
      if (rootWindow->property("couchMode").toBool()) {
        name->forceActiveFocus();
        controller.acceptRequested();
        if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("Saved filter name cannot open text entry"); return; }
        QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
      }
      QMetaObject::invokeMethod(editor, "saveCurrent");
      if (library.savedFilters().size() != 1) { fail("Saved filter form could not save: " + library.savedFilterMessage()); return; }
      const QString id = library.savedFilters().first().toMap().value("id").toString();
      library.setSearchText("no matches");
      QMetaObject::invokeMethod(editor, "applyRequested", Q_ARG(QString, id));
      if (library.searchText() != "Aster" || library.rowCount() == 0 || rootWindow->property("savedFiltersOpen").toBool()) {
        fail("Applying a saved filter did not restore the library"); return;
      }
      for (int i = 0; i < 30; ++i) library.saveCurrentFilter(QStringLiteral("View %1").arg(i, 2, 10, QLatin1Char('0')));
      QMetaObject::invokeMethod(rootWindow, "openSavedFilters");
      QMetaObject::invokeMethod(editor, "focusSavedRow", Q_ARG(QVariant, 0), Q_ARG(QVariant, false));
      for (int i = 0; i < 25; ++i) controller.focusDirectionRequested(Qt::Key_Down);
      if (editor->property("focusedSavedRow").toInt() != 25) { fail("Saved filter controller navigation did not scroll through the list"); return; }
      controller.backRequested();
      if (rootWindow->property("savedFiltersOpen").toBool()) { fail("Saved filters cannot close"); return; }
      application.quit();
    });
  });
}

void driveRandomSelection(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  QWindow*& rootWindow = *app.rootWindow;
  QTimer::singleShot(150, &application, [&application, rootWindow, &controller] {
    const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
    const bool couch = rootWindow->property("couchMode").toBool();
    if (couch) {
      auto* library = rootWindow->findChild<QQuickItem*>(QStringLiteral("couchLibrary"));
      QMetaObject::invokeMethod(library, "openBrowse");
    }
    if (!couch) {
      auto* more = rootWindow->findChild<QQuickItem*>("libraryMoreButton");
      if (more) QMetaObject::invokeMethod(more, "clicked");
    }
    auto* pick = rootWindow->findChild<QQuickItem*>(couch ? QStringLiteral("couchRandomGameButton") : QStringLiteral("randomGameButton"));
    if (!pick || !pick->isVisible()) { fail("Random game control is not visible"); return; }
    pick->forceActiveFocus();
    controller.acceptRequested();
    QCoreApplication::processEvents();
    if (!rootWindow->property("detailOpen").toBool() || !rootWindow->property("randomSelection").toBool()) {
      fail("Random game control did not show a selection"); return;
    }
    const QString first = rootWindow->property("selectedGame").toMap().value("appId").toString();
    QTimer::singleShot(100, &application, [&application, rootWindow, &controller, first, fail] {
      auto* another = rootWindow->findChild<QQuickItem*>(QStringLiteral("pickAnotherButton"));
      if (!another || !another->isVisible()) { fail("Pick another is missing"); return; }
      another->forceActiveFocus();
      controller.acceptRequested();
      if (rootWindow->property("selectedGame").toMap().value("appId").toString() == first) {
        fail("Pick another immediately repeated the same game"); return;
      }
      controller.backRequested();
      if (rootWindow->property("detailOpen").toBool()) { fail("Random selection cannot close"); return; }
      application.quit();
    });
  });
}

void driveLaunchingNavigation(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  LaunchHold& launchHold = *app.launchHold;
  QWindow*& rootWindow = *app.rootWindow;
  // While launcher windows are held, the pad only moves through the Starting screen:
  // A opens a window, B lets everything go, and a quick A never lands on Show All.
  QTimer::singleShot(200, &application, [&application, rootWindow, &controller, &launchHold] {
    bool failed = false;
    const auto fail = [&application, &failed](const QString& message) {
      qCritical().noquote() << message;
      failed = true;
      application.exit(EXIT_FAILURE);
    };
    auto* quickWindow = qobject_cast<QQuickWindow*>(rootWindow);
    const auto settle = [] {
      QEventLoop events;
      QTimer::singleShot(60, &events, &QEventLoop::quit);
      events.exec();
    };
    const auto item = [quickWindow](const QString& name) {
      return quickWindow ? findVisualItem(quickWindow->contentItem(), name) : nullptr;
    };
    const auto focused = [quickWindow] { return quickWindow->activeFocusItem(); };
    const auto within = [](QQuickItem* candidate, QQuickItem* container) {
      for (; candidate; candidate = candidate->parentItem())
        if (candidate == container) return true;
      return false;
    };
    const auto expectFocus = [&](QQuickItem* expected, const QString& step) {
      if (!failed && focused() != expected)
        fail(QStringLiteral("Starting screen: %1, focus was %2")
                 .arg(step, focused() ? focused()->objectName() : QStringLiteral("nothing")));
    };
    auto* screen = item(QStringLiteral("launchingScreen"));
    if (!quickWindow || !screen) { fail(QStringLiteral("Starting screen is missing")); return; }

    launchHold.preview(QStringLiteral("Celeste"),
                       {{QStringLiteral("0x1"), 0, QStringLiteral("heroic"), QStringLiteral("Heroic Games Launcher")},
                        {QStringLiteral("0x2"), 0, QStringLiteral("upc.exe"), QStringLiteral("Sign in to Ubisoft Connect")}});
    settle();
    QQuickItem* first = item(QStringLiteral("launchingWindow_0x1"));
    QQuickItem* second = item(QStringLiteral("launchingWindow_0x2"));
    auto* showAll = item(QStringLiteral("launchingShowAllButton"));
    if (!screen->isVisible() || !first || !second || !showAll) {
      fail(QStringLiteral("Starting screen did not list the held windows (visible %1, windows %2 %3, show all %4)")
               .arg(screen->isVisible()).arg(first != nullptr).arg(second != nullptr).arg(showAll != nullptr));
      return;
    }
    expectFocus(first, QStringLiteral("the first window was not focused"));
    controller.focusDirectionRequested(Qt::Key_Down); settle();
    expectFocus(second, QStringLiteral("Down did not reach the second window"));
    controller.focusDirectionRequested(Qt::Key_Down); settle();
    controller.focusDirectionRequested(Qt::Key_Down); settle();
    expectFocus(showAll, QStringLiteral("Down did not stop on Show All Windows"));
    controller.focusDirectionRequested(Qt::Key_Up); settle();
    controller.focusDirectionRequested(Qt::Key_Up); settle();
    controller.focusDirectionRequested(Qt::Key_Left); settle();
    controller.focusDirectionRequested(Qt::Key_Right); settle();
    expectFocus(first, QStringLiteral("Up, Left or Right left the first window"));
    const bool couch = rootWindow->property("couchMode").toBool();
    controller.favoriteRequested(); controller.toolbarRequested(); controller.startRequested(); settle();
    if (!failed && (!launchHold.active() || rootWindow->property("couchMode").toBool() != couch))
      fail(QStringLiteral("Starting screen let Start, Y or X act on the library"));
    controller.focusDirectionRequested(Qt::Key_Down); settle();
    controller.acceptRequested(); settle();
    if (!failed && launchHold.presented() != QStringLiteral("0x2"))
      fail(QStringLiteral("A did not open the focused held window"));
    launchHold.returnToLibrary();
    controller.backRequested(); settle();
    if (!failed && (launchHold.active() || screen->isVisible() || !focused() || within(focused(), screen)))
      fail(QStringLiteral("B did not end the hold and return to the library"));

    // The keyboard moves through the same list, even over a game's page, whose window
    // shortcuts and Play button must not act behind the screen.
    QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, QVariant(0)));
    settle();
    auto* play = item(QStringLiteral("playButton"));
    if (!failed && (!play || !play->hasActiveFocus())) {
      fail(QStringLiteral("Starting screen test could not open a game page"));
      return;
    }
    launchHold.preview(QStringLiteral("Celeste"),
                       {{QStringLiteral("0x1"), 0, QStringLiteral("heroic"), QStringLiteral("Heroic Games Launcher")},
                        {QStringLiteral("0x2"), 0, QStringLiteral("upc.exe"), QStringLiteral("Sign in to Ubisoft Connect")}});
    settle();
    first = item(QStringLiteral("launchingWindow_0x1"));
    second = item(QStringLiteral("launchingWindow_0x2"));
    if (!failed && first && first->mapToScene(QPointF(first->width(), 0)).x() > quickWindow->width())
      fail(QStringLiteral("the window list runs off the screen"));
    expectFocus(first, QStringLiteral("the first window was not focused over a game page"));
    // A page that takes focus late does not keep it.
    play->forceActiveFocus(Qt::OtherFocusReason);
    settle();
    expectFocus(first, QStringLiteral("the game page kept focus behind the screen"));
    deliverKeyboardTestKey(quickWindow, Qt::Key_Down); settle();
    expectFocus(second, QStringLiteral("keyboard Down did not reach the second window"));
    deliverKeyboardTestKey(quickWindow, Qt::Key_Tab); settle();
    expectFocus(showAll, QStringLiteral("Tab did not reach Show All Windows"));
    deliverKeyboardTestKey(quickWindow, Qt::Key_Backtab, Qt::ShiftModifier); settle();
    expectFocus(second, QStringLiteral("Shift+Tab did not return to the second window"));
    deliverKeyboardTestKey(quickWindow, Qt::Key_Left); settle();
    deliverKeyboardTestKey(quickWindow, Qt::Key_Right); settle();
    expectFocus(second, QStringLiteral("keyboard Left or Right left the list"));
    deliverKeyboardTestKey(quickWindow, Qt::Key_Return); settle();
    if (!failed && (launchHold.presented() != QStringLiteral("0x2") ||
                    launchHold.title() != QStringLiteral("Celeste")))
      fail(QStringLiteral("Return did not open the focused window, or reached the game page"));
    launchHold.returnToLibrary();
    // The list comes back with the window that was shown; its row has focus again.
    launchHold.preview(QStringLiteral("Celeste"),
                       {{QStringLiteral("0x1"), 0, QStringLiteral("heroic"), QStringLiteral("Heroic Games Launcher")}});
    settle();
    launchHold.preview(QStringLiteral("Celeste"),
                       {{QStringLiteral("0x1"), 0, QStringLiteral("heroic"), QStringLiteral("Heroic Games Launcher")},
                        {QStringLiteral("0x2"), 0, QStringLiteral("upc.exe"), QStringLiteral("Sign in to Ubisoft")}});
    settle();
    expectFocus(item(QStringLiteral("launchingWindow_0x2")),
                QStringLiteral("focus did not return to the window that was shown"));
    launchHold.cancel();
    settle();
    // The window is active again once the game closes: the page's Play takes focus,
    // not the first control on the page.
    QMetaObject::invokeMethod(rootWindow, "focusCurrentSurface");
    settle();
    expectFocus(play, QStringLiteral("returning to the game page did not focus Play"));

    // Nothing held yet: A does nothing, and Show All Windows is a deliberate move.
    launchHold.preview(QStringLiteral("Hollow Knight"), {});
    settle();
    expectFocus(screen, QStringLiteral("focus did not rest on the screen with no windows"));
    controller.acceptRequested(); settle();
    if (!failed && !launchHold.active()) fail(QStringLiteral("A with no windows ended the hold"));
    controller.focusDirectionRequested(Qt::Key_Down); settle();
    expectFocus(showAll, QStringLiteral("Down did not reach Show All Windows"));
    controller.acceptRequested(); settle();
    if (!failed && launchHold.active()) fail(QStringLiteral("Show All Windows did not end the hold"));

    // The keyboard's Escape lets go too.
    launchHold.preview(QStringLiteral("Hollow Knight"), {});
    settle();
    deliverKeyboardTestKey(quickWindow, Qt::Key_Escape);
    settle();
    if (!failed && launchHold.active()) fail(QStringLiteral("Escape did not end the hold"));
    if (!failed) application.quit();
  });
}

void driveStaleSelection(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  QWindow*& rootWindow = *app.rootWindow;
  // Narrowing the library to a single game leaves the grid holding delegates for the hundred
  // that just left. Moving down from the organize row and pressing A must open the game the
  // person can see, not one of those, and never a row that is no longer there.
  QTimer::singleShot(200, &application, [&application, rootWindow, &controller] {
    const auto fail = [&application](const QString& message) {
      qCritical().noquote() << message;
      application.exit(EXIT_FAILURE);
    };
    auto* library = qmlContext(rootWindow)
                        ->contextProperty(QStringLiteral("Library"))
                        .value<QObject*>();
    auto* status = rootWindow->findChild<QQuickItem*>(QStringLiteral("filtersMenuButton"));
    if (library == nullptr || status == nullptr) {
      fail(QStringLiteral("Stale selection test could not find the library controls"));
      return;
    }
    // Narrow a hundred games down to one, the way choosing a source with a single game does.
    library->setProperty("searchText", QStringLiteral("Black Meridian 3"));
    QCoreApplication::processEvents();
    int visible = 0;
    QMetaObject::invokeMethod(library, "rowCount", Q_RETURN_ARG(int, visible));
    if (visible != 1) {
      fail(QStringLiteral("Stale selection fixture expected one visible game, saw %1").arg(visible));
      return;
    }
    QVariantMap shown;
    QMetaObject::invokeMethod(library, "get", Q_RETURN_ARG(QVariantMap, shown), Q_ARG(int, 0));
    QTimer::singleShot(200, &application, [&application, rootWindow, &controller, status, shown,
                                           fail] {
      status->forceActiveFocus();
      auto* grid = rootWindow->findChild<QQuickItem*>(QStringLiteral("libraryGrid"));
      const auto focusInGrid = [rootWindow, grid] {
        for (auto* item = qobject_cast<QQuickWindow*>(rootWindow)->activeFocusItem();
             item; item = item->parentItem())
          if (item == grid) return true;
        return false;
      };
      for (int step = 0; step < 5 && !focusInGrid(); ++step) {
        controller.focusDirectionRequested(Qt::Key_Down);
        QCoreApplication::processEvents();
      }
      if (!focusInGrid()) {
        fail(QStringLiteral("Down from the filter row did not reach the visible game"));
        return;
      }
      controller.acceptRequested();
      QCoreApplication::processEvents();
      if (!rootWindow->property("detailOpen").toBool()) {
        fail(QStringLiteral("Moving down from the organize row could not open a game"));
        return;
      }
      const QVariantMap opened = rootWindow->property("selectedGame").toMap();
      if (opened.value("title").toString().isEmpty()) {
        fail(QStringLiteral("Down from the organize row opened a game that is not there"));
        return;
      }
      if (opened.value("appId") != shown.value("appId") ||
          opened.value("source") != shown.value("source")) {
        fail(QStringLiteral("Down from the organize row opened %1 rather than the visible %2")
                 .arg(opened.value("title").toString(), shown.value("title").toString()));
        return;
      }
      application.quit();
    });
  });
}

void driveFilterBack(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  ControllerInput& controller = *app.controller;
  QWindow*& rootWindow = *app.rootWindow;
  // Back walks out of the library the way you walked in: out of a console, then a search,
  // then a source, then whatever else is still narrowing the view. Levels that are not
  // active are skipped, so RetroArch inside Nintendo 64 is two presses from all sources.
  QTimer::singleShot(200, &application, [&application, rootWindow, &controller] {
    const auto fail = [&application](const QString& message) {
      qCritical().noquote() << message;
      application.exit(EXIT_FAILURE);
    };
    auto* library = qmlContext(rootWindow)
                        ->contextProperty(QStringLiteral("Library"))
                        .value<QObject*>();
    if (library == nullptr) {
      fail(QStringLiteral("Filter back test could not find the library"));
      return;
    }
    const auto back = [&controller] {
      controller.backRequested();
      QCoreApplication::processEvents();
    };
    const QString system = QStringLiteral("snes");
    library->setProperty("sourceFilters", QStringList{QStringLiteral("RetroArch")});
    library->setProperty("consoleFilter", system);
    library->setProperty("searchText", QStringLiteral("cart"));
    QCoreApplication::processEvents();
    if (library->property("consoleFilter").toString() != system) {
      fail(QStringLiteral("Filter back fixture could not enter a console"));
      return;
    }
    // Out of the console first, keeping the search and the source.
    back();
    if (!library->property("consoleFilter").toString().isEmpty() ||
        library->property("searchText").toString().isEmpty() ||
        library->property("sourceFilters").toStringList().isEmpty()) {
      fail(QStringLiteral("Back did not leave the console on its own"));
      return;
    }
    // Then out of the search, keeping the source.
    back();
    if (!library->property("searchText").toString().isEmpty() ||
        library->property("sourceFilters").toStringList().isEmpty()) {
      fail(QStringLiteral("Back did not clear the search on its own"));
      return;
    }
    // Then out of the source, reaching the whole library.
    back();
    if (!library->property("sourceFilters").toStringList().isEmpty()) {
      fail(QStringLiteral("Back did not return to all sources"));
      return;
    }
    // Anything else still narrowing the view goes together on the next press.
    library->setProperty("completionFilter", QStringLiteral("backlog"));
    library->setProperty("mode", 1);
    QCoreApplication::processEvents();
    back();
    if (!library->property("completionFilter").toString().isEmpty() ||
        library->property("mode").toInt() != 0) {
      fail(QStringLiteral("Back did not clear the remaining filters"));
      return;
    }
    const bool couch = rootWindow->property("couchMode").toBool();
    rootWindow->setProperty("couchMode", false);
    auto* chipSearch = rootWindow->findChild<QQuickItem*>(QStringLiteral("searchField"));
    if (!chipSearch) { fail(QStringLiteral("Search field missing")); return; }
    chipSearch->setProperty("text", QString{});
    library->setProperty("searchText", QStringLiteral("cart"));
    QCoreApplication::processEvents();
    auto* window = qobject_cast<QQuickWindow*>(rootWindow);
    auto* chip = window ? findVisualItem(window->contentItem(), QStringLiteral("librarySearchFilterChip"))
                        : nullptr;
    if (!chip || !chip->isVisible()) { fail(QStringLiteral("Search chip missing")); return; }
    QMetaObject::invokeMethod(chip, "clicked");
    QCoreApplication::processEvents();
    if (!library->property("searchText").toString().isEmpty()) {
      fail(QStringLiteral("Search chip retained a model-only Couch search")); return;
    }
    rootWindow->setProperty("couchMode", couch);
    if (!couch) {
      // The desktop toolbar's visible reset clears all kinds of active filter together.
      auto* search = rootWindow->findChild<QQuickItem*>(QStringLiteral("searchField"));
      auto* clearAll = rootWindow->findChild<QQuickItem*>(QStringLiteral("clearAllLibraryFiltersButton"));
      if (!search || !clearAll) {
        fail(QStringLiteral("Library search or Clear All is missing")); return;
      }
      search->setProperty("text", QStringLiteral("cart"));
      library->setProperty("sourceFilters", QStringList{QStringLiteral("RetroArch")});
      library->setProperty("completionFilter", QStringLiteral("backlog"));
      library->setProperty("availability", 1);
      QCoreApplication::processEvents();
      if (!clearAll->isVisible()) {
        fail(QStringLiteral("Clear All did not appear for active filters")); return;
      }
      QMetaObject::invokeMethod(clearAll, "clicked");
      QCoreApplication::processEvents();
      if (!library->property("searchText").toString().isEmpty() ||
          !library->property("sourceFilters").toStringList().isEmpty() ||
          !library->property("completionFilter").toString().isEmpty() ||
          library->property("availability").toInt() != 0) {
        fail(QStringLiteral("Clear All left part of the library filter active")); return;
      }
    }
    application.quit();
  });
}

} // namespace SelfTest

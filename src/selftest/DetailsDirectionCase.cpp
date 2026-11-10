#include "selftest/SelfTest.h"

#include "input/ControllerInput.h"
#include "library/GameStopService.h"
#include "selftest/SelfTestSupport.h"
#include "tracking/ProcFs.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWindow>

#include <cstdlib>
#include <functional>
#include <memory>

namespace SelfTest {

// --details-direction-test: starts once the window is up, in place of the couch and controller
// navigation drives.
void startDetailsDirection(Harness&, AppContext& app) {
  QGuiApplication& application = *app.application;
  QWindow* rootWindow = *app.rootWindow;
  ControllerInput& controller = *app.controller;
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
}

} // namespace SelfTest

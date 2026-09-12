#include "achievements/AchievementModel.h"
#include "achievements/RetroAchievementsService.h"
#include "achievements/SteamAccountService.h"
#include "app/AppSettings.h"
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
#include "library/BattleNetGameModel.h"
#include "library/CemuGameModel.h"
#include "library/ConsolePortalModel.h"
#include "library/DolphinGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/HeroicGameModel.h"
#include "library/HomeModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LutrisGameModel.h"
#include "library/ManualGameModel.h"
#include "library/MockGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/Shadps4GameModel.h"
#include "library/SteamGameModel.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameInsightsService.h"
#include "metadata/GameMetadata.h"
#include "metadata/ProtonDbService.h"
#include <QQmlProperty>
#include "streaming/SunshineIntegration.h"
#include "theme/OmarchyTheme.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/SessionDatabase.h"
#include "saves/SaveBackups.h"

#include <QAbstractItemModel>
#include <QDebug>
#include <QPainter>
#include <QDir>
#include <QSet>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIcon>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <functional>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSize>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <QWheelEvent>

#include <algorithm>
#include <memory>

namespace {

QString verifyEditorTextFields(QQuickWindow* window, QQuickItem* container,
                               ControllerInput& controller) {
  const auto waitForFocus = [](QQuickItem* item) {
    QElapsedTimer timer;
    timer.start();
    do {
      QEventLoop events;
      QTimer::singleShot(10, &events, &QEventLoop::quit);
      events.exec();
    } while (!item->hasActiveFocus() && timer.elapsed() < 250);
    return item->hasActiveFocus();
  };
  const QSize expectedSize = window->property("testRenderSize").toSize();
  if (expectedSize.isValid() && window->size() != expectedSize)
    return "Editor fixture did not use the requested window size";
  QList<QQuickItem*> fields;
  QString boundsError;
  const auto collect = [&](auto&& self, QQuickItem* item) -> void {
    if (item->isVisible() && item->isEnabled() && item->activeFocusOnTab() && item->width() > 0) {
      const auto rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
      if (rect.left() < -1 || rect.right() > window->width() + 1)
        boundsError = "Editor control extends outside window: " + item->objectName();
    }
    if (item->isVisible() && item->isEnabled() &&
        item->property("echoMode").isValid() && item->property("maximumLength").isValid())
      fields.append(item);
    for (auto* child : item->childItems()) self(self, child);
  };
  collect(collect, container);
  if (!boundsError.isEmpty()) return boundsError;
  if (fields.isEmpty()) return "Editor field coverage found no fields";
  for (auto* field : fields) {
    const QString original = field->property("text").toString();
    field->setProperty("text", "test");
    field->forceActiveFocus();
    controller.focusDirectionRequested(Qt::Key_Right);
    auto* clear = window->activeFocusItem();
    if (!clear || clear == field || clear->property("field").value<QQuickItem*>() != field)
      return "Clear button is unreachable for " + field->objectName();
    controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
    if (!field->property("text").toString().isEmpty() || !field->hasActiveFocus() || clear->isVisible())
      return "Clear did not empty and refocus " + field->objectName();
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
      controller.keyRequested(key, Qt::NoModifier);
      if (!window->property("couchTextEntryOpen").toBool())
        return "Keyboard did not open for " + field->objectName();
      auto* keyboard = window->findChild<QQuickItem*>(QStringLiteral("couchTextEntryKeyboard"));
      if (!keyboard) return "Keyboard fixture is missing";
      auto* grid = window->findChild<QQuickItem*>(QStringLiteral("couchTextEntryGrid"));
      if (!grid) return "Keyboard grid is missing";
      for (const auto& mode : {"upper", "lower", "symbols"}) {
        keyboard->setProperty("keyboardMode", mode);
        QCoreApplication::processEvents();
        QMetaObject::invokeMethod(keyboard, "focusKeyboard");
        if (!waitForFocus(grid)) return "Keyboard grid failed to take focus";
        const int count = grid->property("count").toInt();
        QSet<int> visited{0};
        QList<int> pending{0};
        while (!pending.isEmpty()) {
          const int from = pending.takeFirst();
          for (const auto direction : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
            grid->setProperty("currentIndex", from);
            controller.keyRequested(direction, Qt::NoModifier);
            const int to = grid->property("currentIndex").toInt();
            if (!grid->hasActiveFocus() || to < 0 || to >= count)
              return "Keyboard navigation escaped its grid";
            if (!visited.contains(to)) { visited.insert(to); pending.append(to); }
          }
        }
        if (count < 1 || visited.size() != count) return "Keyboard contains unreachable keys";
      }
      keyboard->setProperty("value", "pad");
      controller.toolbarRequested();
      if (keyboard->property("value").toString() != "pad ") return "Keyboard Space shortcut failed";
      controller.favoriteRequested();
      if (keyboard->property("value").toString() != "pad" || !window->property("couchTextEntryOpen").toBool())
        return "Keyboard Delete shortcut escaped the modal";
      const QString beforeCancel = field->property("text").toString();
      keyboard->setProperty("value", "discard this");
      controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
      if (window->property("couchTextEntryOpen").toBool() ||
          field->property("text").toString() != beforeCancel || !waitForFocus(field))
        return "Keyboard Cancel changed the field or failed to close";
      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
      if (!window->property("couchTextEntryOpen").toBool()) return "Keyboard failed to reopen after Cancel";
      keyboard->setProperty("value", "start accepted");
      controller.startRequested();
      if (window->property("couchTextEntryOpen").toBool() ||
          field->property("text").toString() != "start accepted" || !waitForFocus(field))
        return "Start did not accept and refocus " + field->objectName();
    }
    field->setProperty("text", original);
  }
  return {};
}

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

bool optionSupplied(const QStringList& arguments, const QString& name) {
  return std::ranges::any_of(arguments, [&name](const QString& argument) {
    return argument == name || argument.startsWith(name + QLatin1Char('='));
  });
}

bool parseRenderSize(const QString& value, QSize* result) {
  const QStringList dimensions = value.split(QLatin1Char('x'));
  bool widthValid = false;
  bool heightValid = false;
  const int width = dimensions.value(0).toInt(&widthValid);
  const int height = dimensions.value(1).toInt(&heightValid);
  if (dimensions.size() != 2 || !widthValid || !heightValid || width <= 0 || height <= 0 ||
      width > 16384 || height > 16384) {
    return false;
  }
  *result = QSize(width, height);
  return true;
}

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
      library->setProperty("completionFilter", QString{});
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
          QTimer::singleShot(150, window, [=] {
            QMetaObject::invokeMethod(grid, "positionViewAtBeginning");
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
            QTimer::singleShot(250, window, [=] {
              const qreal y = grid->property("contentY").toReal();
              const qreal first = grid->property("originY").toReal();
              const qreal last = first + qMax(0.0, grid->property("contentHeight").toReal() - grid->height());
              if (visibleCards().isEmpty() || y < first - 1 || y > last + 1) {
                fail(QStringLiteral("Wheel after source filtering hid the cards: y=%1, bounds=%2..%3")
                         .arg(y).arg(first).arg(last));
                return;
              }
              if (last > first + 1 && y <= first + 1) {
                fail(QStringLiteral("Wheel did not scroll the expanded collection"));
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
            });
          });
        });
      });
    });
  });
}

QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
  if (!item) return nullptr;
  if (item->objectName() == name) return item;
  for (auto* child : item->childItems()) if (auto* found = findVisualItem(child, name)) return found;
  return nullptr;
}

bool runRestoreStartup(QGuiApplication& application, OmarchyTheme& theme,
                       const BackupRecovery::Paths& paths, bool couchMode,
                       const std::function<void(QQuickWindow*, BackupStartup*)>& ready = {}) {
  const QString phase = BackupRecovery(paths).status();
  if (phase == "none" || phase == "complete" || phase == "reverted") return true;
  BackupStartup recovery(paths);
  ControllerInput controller;
  QEventLoop loop;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("Theme", &theme);
  engine.rootContext()->setContextProperty("Recovery", &recovery);
  engine.rootContext()->setContextProperty("RecoveryController", &controller);
  bool resolved = false;
  QObject::connect(&recovery, &BackupStartup::resolved, &loop, [&] {
    resolved = true;
    loop.quit();
  });
  engine.loadFromModule("Omakade", "RestoreStartup");
  if (engine.rootObjects().isEmpty()) return false;
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!window) return false;
  window->setProperty("couchMode", couchMode);
  if (couchMode && !ready) window->showFullScreen();
  QObject::connect(window, &QQuickWindow::closing, &loop, [&] {
    if (!recovery.busy()) loop.quit();
  });
  const auto syncInput = [&] {
    controller.setInputEnabled(application.applicationState() == Qt::ApplicationActive &&
                               application.focusWindow() == window);
  };
  QObject::connect(&application, &QGuiApplication::applicationStateChanged, &controller, syncInput);
  QObject::connect(&application, &QGuiApplication::focusWindowChanged, &controller, syncInput);
  syncInput();
  if (!ready) controller.start();
  // Closing the temporary startup window must not queue an application quit
  // before the actual library window has been constructed.
  const bool quitOnClose = application.quitOnLastWindowClosed();
  application.setQuitOnLastWindowClosed(false);
  if (ready) ready(window, &recovery);
  QTimer::singleShot(0, &recovery, &BackupStartup::retry);
  loop.exec();
  window->hide();
  application.setQuitOnLastWindowClosed(quitOnClose);
  return resolved;
}

int testRestoreStartup(QGuiApplication& application, OmarchyTheme& theme, const QString& mode,
                       bool couch, const QSize& size, const QString& screenshot) {
  if (!QStringList{"success", "retry", "undo", "close"}.contains(mode)) return EXIT_FAILURE;
  QTemporaryDir temporary;
  if (!temporary.isValid()) return EXIT_FAILURE;
  const BackupRecovery::Paths paths{temporary.filePath("library.sqlite3"),
      temporary.filePath("config.toml"), temporary.filePath("recovery")};
  BackupPayload original;
  original.createdAt = "2026-09-05T00:00:00Z";
  original.library = {{"collections", QJsonArray{QJsonObject{{"name", "Original"}, {"created_at", 1}}}}};
  BackupPayload incoming = original;
  incoming.library = {{"collections", QJsonArray{QJsonObject{{"name", "Imported"}, {"created_at", 2}}}}};
  QString error;
  if (!BackupDatabase::restore(paths.database, original, BackupDatabase::Mode::Replace, &error) ||
      !BackupRecovery(paths).stage(incoming, BackupDatabase::Mode::Replace, &error)) {
    qCritical().noquote() << error;
    return EXIT_FAILURE;
  }
  QFile journal(paths.state + "/active.json");
  if (!journal.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const QString job = QJsonDocument::fromJson(journal.readAll()).object().value("id").toString();
  const QString stagedPath = paths.state + '/' + job + "/incoming.omakade-backup";
  QFile staged(stagedPath);
  if (!staged.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
  const QByteArray stagedBytes = staged.readAll();
  staged.close();
  if (mode != "success") {
    if (!staged.open(QIODevice::WriteOnly) || staged.write("invalid") != 7) return EXIT_FAILURE;
    staged.close();
  }
  bool failed = false;
  bool sawError = false;
  bool handled = false;
  const bool resolved = runRestoreStartup(application, theme, paths, couch,
      [&](QQuickWindow* window, BackupStartup* recovery) {
        if (size.isValid()) window->resize(size);
        const auto fail = [&, window](const QString& text) {
          qCritical().noquote() << text;
          failed = true;
          window->close();
        };
        QTimer::singleShot(10000, window, [fail] { fail("Restore startup fixture timed out"); });
        QObject::connect(recovery, &BackupStartup::changed, window, [&, window, recovery, fail] {
          if (recovery->busy() || mode == "success" || handled) return;
          handled = true;
          sawError = recovery->canUndo() && recovery->message().contains("changed");
          QTimer::singleShot(80, window, [&, window, recovery, fail] {
            auto* retry = window->findChild<QQuickItem*>("restoreRetryButton");
            auto* undo = window->findChild<QQuickItem*>("restoreUndoButton");
            if (!sawError || !retry || !undo || !retry->hasActiveFocus()) {
              fail("Restore failure did not keep the library closed with usable retry/undo focus");
              return;
            }
            if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) {
              fail("Could not render the restore recovery screen");
              return;
            }
            const auto navigate = [window](int key) {
              QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, QVariant(key)));
            };
            if (mode == "close") { navigate(Qt::Key_Escape); return; }
            navigate(Qt::Key_Down);
            auto* folder = window->findChild<QQuickItem*>("restoreFolderButton");
            if (!folder || !folder->hasActiveFocus()) { fail("Restore Down did not follow the button grid"); return; }
            navigate(Qt::Key_Up);
            navigate(Qt::Key_Right);
            if (!undo->hasActiveFocus()) { fail("Restore controller navigation did not reach Undo"); return; }
            if (mode == "retry") {
              QFile repair(stagedPath);
              if (!repair.open(QIODevice::WriteOnly) || repair.write(stagedBytes) != stagedBytes.size()) {
                fail("Could not repair the test archive"); return;
              }
              repair.close();
              navigate(Qt::Key_Left);
            }
            navigate(Qt::Key_Return);
          });
        });
      });
  if (failed || resolved != (mode != "close") || (mode != "success" && !sawError)) return EXIT_FAILURE;
  BackupPayload actual;
  if (!BackupSnapshot::capture(paths.database, {}, &actual, &error)) return EXIT_FAILURE;
  const QString expected = mode == "success" || mode == "retry" ? "Imported" : "Original";
  if (actual.library.value("collections").toArray().first().toObject().value("name").toString() != expected)
    return EXIT_FAILURE;
  if (resolved) {
    // Prove the temporary startup window did not leave an application quit
    // event that prevents the next window from entering its event loop.
    QQuickWindow nextWindow;
    nextWindow.show();
    bool entered = false;
    QTimer::singleShot(50, &application, [&] { entered = true; application.quit(); });
    application.exec();
    if (!entered) return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
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
  const QString renderOverlay =
      optionValue(application.arguments(), QStringLiteral("--render-overlay"));
  // `--play Source:runner:id` launches one library game, through the running window when
  // there is one, and `--quit` closes the running window. Sunshine app entries use both.
  const QString playKey = optionValue(application.arguments(), QStringLiteral("--play"));
  const bool quitRequest = application.arguments().contains(QStringLiteral("--quit"));
  if (optionSupplied(application.arguments(), QStringLiteral("--render-screenshot")) &&
      screenshotPath.isEmpty()) {
    qCritical() << "--render-screenshot requires a path";
    return EXIT_FAILURE;
  }
  if (!quitRequest && optionSupplied(application.arguments(), QStringLiteral("--play")) &&
      playKey.isEmpty()) {
    qCritical() << "--play requires a launch key";
    return EXIT_FAILURE;
  }
  QSize requestedRenderSize;
  if (optionSupplied(application.arguments(), QStringLiteral("--render-size")) &&
      !parseRenderSize(renderSize, &requestedRenderSize)) {
    qCritical() << "--render-size requires WIDTHxHEIGHT between 1 and 16384";
    return EXIT_FAILURE;
  }
  const QString restoreStartupTest = optionValue(application.arguments(), "--restore-startup-test");
  if (!restoreStartupTest.isEmpty()) {
    return testRestoreStartup(application, theme, restoreStartupTest,
        application.arguments().contains("--couch"), requestedRenderSize, screenshotPath);
  }
  const bool renderMode = !screenshotPath.isEmpty();
  const bool gogSettingsTest = application.arguments().contains("--gog-settings-test");
  const bool linkedPreferenceTest = application.arguments().contains("--linked-preference-test");
  const bool gogSettingsFixture = gogSettingsTest || renderOverlay == "gog-folders";
  const bool linkedPreferenceFixture = linkedPreferenceTest || renderOverlay == "linked-preference" || renderOverlay == "linked-preference-missing";
  const bool backupEditorTest = application.arguments().contains(QStringLiteral("--backup-editor-test"));
  const bool backupFixture = backupEditorTest || renderOverlay == QStringLiteral("backup-editor");
  const bool bulkEditorTest = application.arguments().contains(QStringLiteral("--bulk-editor-test"));
  const bool savedFilterTest = application.arguments().contains(QStringLiteral("--saved-filter-test"));
  const bool randomSelectionTest = application.arguments().contains(QStringLiteral("--random-selection-test"));
  const bool staleSelectionTest =
      application.arguments().contains(QStringLiteral("--stale-selection-test"));
  const bool filterBackTest =
      application.arguments().contains(QStringLiteral("--filter-back-test"));
  const bool artworkEditorTest = application.arguments().contains(QStringLiteral("--artwork-editor-test"));
  const bool manualEditorTest = application.arguments().contains(QStringLiteral("--manual-editor-test"));
  const bool detailsDirectionTest =
      application.arguments().contains(QStringLiteral("--details-direction-test"));
  const bool smokeTest = gogSettingsTest || linkedPreferenceTest || backupEditorTest || bulkEditorTest || savedFilterTest || randomSelectionTest || staleSelectionTest || filterBackTest || artworkEditorTest || manualEditorTest || application.arguments().contains(QStringLiteral("--smoke-test"));
  const bool couchNavigationTest =
      application.arguments().contains(QStringLiteral("--couch-navigation-test"));
  const bool navigationTest = couchNavigationTest ||
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
  const bool stressMode = application.arguments().contains(QStringLiteral("--stress-test"));
  const bool isolatedTest = smokeTest || renderMode || navigationTest || detailsDirectionTest ||
                            consolePortalTest || benchmarkMode || stressMode;
  const bool reducedMotionRequest =
      application.arguments().contains(QStringLiteral("--reduced-motion"));
  const bool couchRequest = application.arguments().contains(QStringLiteral("--couch")) ||
                            SunshineIntegration::streaming();
  if (benchmarkMode) {
    qInfo() << "Theme ready in" << startupTimer.elapsed() << "ms";
  }
  if (quitRequest) {
    return SingleInstance::sendCommand({}, "quit") ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  SingleInstance singleInstance;
  const QByteArray instanceCommand =
      !playKey.isEmpty()                 ? QByteArray("play ") + playKey.toUtf8()
      : couchRequest                     ? QByteArray("activate stream")
                                         : QByteArray("activate");
  if (!isolatedTest && !singleInstance.claimOrNotify(instanceCommand)) {
    return EXIT_SUCCESS;
  }
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
  if (reducedMotionRequest) {
    preferences.setReducedMotion(true);
  }
  const bool startInCouchMode = couchRequest || preferences.couchModeEnabled();
  ControllerInput controller;
  std::unique_ptr<QAbstractItemModel> games;
  std::unique_ptr<LutrisGameModel> lutrisGames;
  std::unique_ptr<HeroicGameModel> heroicGames;
  std::unique_ptr<FaugusGameModel> faugusGames;
  std::unique_ptr<RetroArchGameModel> retroArchGames;
  std::unique_ptr<Pcsx2GameModel> pcsx2Games;
  std::unique_ptr<RyujinxGameModel> ryujinxGames;
  std::unique_ptr<Shadps4GameModel> shadps4Games;
  std::unique_ptr<CemuGameModel> cemuGames;
  std::unique_ptr<DolphinGameModel> dolphinGames;
  std::unique_ptr<BattleNetGameModel> battleNetGames;
  std::unique_ptr<PlaySessionStore> playSessionStore;
  std::unique_ptr<ConsolePortalModel> consolePortals;
  SteamGameModel* steamLibrary = nullptr;
  LutrisGameModel* lutrisLibrary = nullptr;
  HeroicGameModel* heroicLibrary = nullptr;
  FaugusGameModel* faugusLibrary = nullptr;
  RetroArchGameModel* retroArchLibrary = nullptr;
  Pcsx2GameModel* pcsx2Library = nullptr;
  RyujinxGameModel* ryujinxLibrary = nullptr;
  Shadps4GameModel* shadps4Library = nullptr;
  CemuGameModel* cemuLibrary = nullptr;
  DolphinGameModel* dolphinLibrary = nullptr;
  BattleNetGameModel* battleNetLibrary = nullptr;
  QString libraryDatabasePath;
  std::unique_ptr<QTemporaryDir> consoleFixture;
  if (demoMode || stressMode || navigationTest || detailsDirectionTest) {
    games =
        std::make_unique<MockGameModel>(nullptr, stressMode ? 1000 : 100, uninstalledLayoutTest);
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
    ryujinxGames =
        std::make_unique<RyujinxGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    ryujinxLibrary = ryujinxGames.get();
    shadps4Games =
        std::make_unique<Shadps4GameModel>(steamLibrary->databasePath(), playSessionStore.get());
    shadps4Library = shadps4Games.get();
    cemuGames =
        std::make_unique<CemuGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    cemuLibrary = cemuGames.get();
    dolphinGames =
        std::make_unique<DolphinGameModel>(steamLibrary->databasePath(), playSessionStore.get());
    dolphinLibrary = dolphinGames.get();
    battleNetGames =
        std::make_unique<BattleNetGameModel>(steamLibrary->databasePath(), &preferences);
    battleNetLibrary = battleNetGames.get();
    consolePortals = std::make_unique<ConsolePortalModel>();
    consolePortals->addRomModel(retroArchGames.get());
    consolePortals->addRomModel(dolphinGames.get());
    consolePortals->addRomModel(ryujinxGames.get());
    consolePortals->addRomModel(cemuGames.get());
    consolePortals->addRomModel(pcsx2Games.get());
    consolePortals->addRomModel(shadps4Games.get());
  }
  if (consolePortals != nullptr) {
    consolePortals->setCardSystems(preferences.cardSystems());
    QObject::connect(&preferences, &AppSettings::consoleLayoutsChanged, consolePortals.get(),
                     [&preferences, portals = consolePortals.get()] {
                       portals->setCardSystems(preferences.cardSystems());
                     });
  }
  if (navigationTest || renderOverlay.startsWith("library-reflow")) {
    libraryDatabasePath = QStringLiteral(":memory:");
  }
  QTemporaryDir artworkFixture;
  if (gogSettingsFixture || linkedPreferenceFixture || backupFixture || artworkEditorTest ||
      savedFilterTest || bulkEditorTest || renderOverlay == QStringLiteral("saved-filters") ||
      renderOverlay == QStringLiteral("bulk-editor") ||
      renderOverlay == QStringLiteral("session-history")) {
    if (!artworkFixture.isValid()) return EXIT_FAILURE;
    libraryDatabasePath = artworkFixture.filePath(QStringLiteral("library.sqlite"));
  }
  ManualGameModel manualGames(libraryDatabasePath.isEmpty() ? QStringLiteral(":memory:") : libraryDatabasePath);
  UnifiedGameModel unifiedGames(libraryDatabasePath);
  unifiedGames.addSourceModel(games.get());
  unifiedGames.addSourceModel(&manualGames);
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
  if (ryujinxGames != nullptr) {
    unifiedGames.addSourceModel(ryujinxGames.get());
  }
  if (shadps4Games != nullptr) {
    unifiedGames.addSourceModel(shadps4Games.get());
  }
  if (cemuGames != nullptr) {
    unifiedGames.addSourceModel(cemuGames.get());
  }
  if (dolphinGames != nullptr) {
    unifiedGames.addSourceModel(dolphinGames.get());
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
    unifiedGames.setSourceEnabled(QStringLiteral("Ryujinx"), preferences.ryujinxEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("shadPS4"), preferences.shadps4Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Cemu"), preferences.cemuEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Dolphin"), preferences.dolphinEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Battle.net"), preferences.battleNetEnabled());
  };
  applySourcePreferences();
  QObject::connect(&preferences, &AppSettings::sourcesChanged, &unifiedGames,
                   applySourcePreferences);
  if (!playKey.isEmpty()) {
    // No window is running, so launch without showing one. A Sunshine request can arrive before
    // a fresh process has finished rebuilding a missing or stale library cache, so retry after the
    // requested source's asynchronous refresh.
    GameLauncher headlessLauncher;
    headlessLauncher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
    const LaunchKey key = LaunchKey::parse(playKey);
    QString error;
    if (key.source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0 &&
        preferences.pcsx2AutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("PCSX2"), true);
    } else if (key.source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0 &&
               preferences.ryujinxAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Ryujinx"), true);
    } else if (key.source.compare(QStringLiteral("shadPS4"), Qt::CaseInsensitive) == 0 &&
               preferences.shadps4AutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("shadPS4"), true);
    } else if (key.source.compare(QStringLiteral("Cemu"), Qt::CaseInsensitive) == 0 &&
               preferences.cemuAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Cemu"), true);
    } else if (key.source.compare(QStringLiteral("Dolphin"), Qt::CaseInsensitive) == 0 &&
               preferences.dolphinAutoEnabled()) {
      unifiedGames.setSourceEnabled(QStringLiteral("Dolphin"), true);
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
      } else if (key.source.compare(QStringLiteral("Dolphin"), Qt::CaseInsensitive) == 0 &&
                 dolphinLibrary != nullptr &&
                 (preferences.dolphinEnabled() || preferences.dolphinAutoEnabled())) {
        dolphinLibrary->refresh();
        refreshStarted = true;
      } else if (key.source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0 &&
                 battleNetLibrary != nullptr && preferences.battleNetEnabled()) {
        battleNetLibrary->refresh();
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
  std::unique_ptr<QTemporaryDir> demoMetadataDir;
  std::unique_ptr<RetroAchievementsService> retroAchievements;
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
          demoMetadataDir->filePath(QStringLiteral("metadata.sqlite3")), nullptr);
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
  const QString saveFixtureGame = artworkFixture.filePath("save-protection/roms/Test Game.sfc");
  std::unique_ptr<SaveBackups> saveBackupsOwner;
  if (renderOverlay.startsWith("save-backups")) {
    const QString folder = artworkFixture.filePath("save-protection");
    const auto fixtureWrite = [](const QString& path, const QByteArray& data) {
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile file(path);
      return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QString config = folder + "/retroarch.cfg";
    const QString save = folder + "/saves/Snes9x/Test Game.srm";
    if (!fixtureWrite(saveFixtureGame, "fixture ROM") || !fixtureWrite(save, "older progress") ||
        !fixtureWrite(config, "savefile_directory = \"~/saves\"\nsavefiles_in_content_dir = \"false\"\nsort_savefiles_enable = \"true\"\nsort_savefiles_by_content_enable = \"false\"\nauto_overrides_enable = \"false\"\n")) return EXIT_FAILURE;
    saveBackupsOwner = std::make_unique<SaveBackups>(folder, config, folder + "/backups", [] { return false; });
    const bool sharedFixture=renderOverlay.contains("shared");
    if(sharedFixture) {
      const QJsonObject rule{{"source","RetroArch"},{"game",saveFixtureGame},
        {"trees",QJsonArray{folder+"/saves/Snes9x"}},{"shared",true},{"description","Shared memory-card saves"}};
      if(!fixtureWrite(folder+"/.config/omakade/save-layouts.json",
          QJsonDocument(QJsonObject{{"format",1},{"layouts",QJsonArray{rule}}}).toJson()))return EXIT_FAILURE;
    }
    const auto protectFixture=[&] {
      return sharedFixture ? saveBackupsOwner->protectLaunch("RetroArch",saveFixtureGame,"snes9x_libretro.so")
                           : saveBackupsOwner->protect(saveFixtureGame,"snes9x_libretro.so");
    };
    if (!protectFixture() ||
        !fixtureWrite(save, "newer progress") ||
        !protectFixture() ||
        !fixtureWrite(save, "current progress")) return EXIT_FAILURE;
  } else saveBackupsOwner = std::make_unique<SaveBackups>();
  SaveBackups& saveBackups = *saveBackupsOwner;
  saveBackups.setEnabled(preferences.protectRetroArchSaves());
  QObject::connect(&preferences, &AppSettings::protectRetroArchSavesChanged, &saveBackups, [&] {
    saveBackups.setEnabled(preferences.protectRetroArchSaves());
  });
  GameLauncher launcher;
  if (!demoMode && !stressMode && !navigationTest && !detailsDirectionTest) launcher.setSaveBackups(&saveBackups);
  launcher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
  QObject::connect(&preferences, &AppSettings::preferStandaloneEmulatorsChanged, &launcher, [&] {
    launcher.setPreferStandaloneEmulators(preferences.preferStandaloneEmulators());
  });
  if (retroArchLibrary != nullptr) {
    QObject::connect(&preferences, &AppSettings::romFoldersChanged, retroArchLibrary, [&] {
      retroArchLibrary->setConfiguredRomFolders(preferences.romFolders());
      if (preferences.retroArchEnabled()) {
        retroArchLibrary->refresh();
      }
    });
  }
  std::unique_ptr<SunshineIntegration> sunshine;
  if (steamLibrary != nullptr) {
    sunshine = std::make_unique<SunshineIntegration>(&unifiedGames, &preferences);
    sunshine->setIconSource(
        QStringLiteral(":/icons/resources/icons/io.github.tsouth89.Omakade.svg"));
  }

  const QString gogAvailableFolder = artworkFixture.filePath("GOG Games");
  const QString gogMissingFolder = artworkFixture.filePath("Disconnected GOG Games");
  QString linkedManualId;
  const QString linkedExecutable = artworkFixture.filePath("native-game.sh");
  if (gogSettingsFixture) {
    if (!QDir().mkpath(gogAvailableFolder)) return EXIT_FAILURE;
    QFile sentinel(gogAvailableFolder + "/keep-game.txt");
    if (!sentinel.open(QIODevice::WriteOnly) || sentinel.write("game files stay") < 0) return EXIT_FAILURE;
    if (renderMode) {
      if (!preferences.addGogLibraryPath(gogAvailableFolder) || !preferences.addGogLibraryPath(gogMissingFolder)) return EXIT_FAILURE;
    }
  }
  if (linkedPreferenceFixture) {
    QFile executable(linkedExecutable);
    if (!executable.open(QIODevice::WriteOnly) || executable.write("#!/bin/sh\nprintf unexpected > launched.txt\n") < 0) return EXIT_FAILURE;
    executable.close();
    if (!QFile::setPermissions(linkedExecutable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return EXIT_FAILURE;
    linkedManualId = manualGames.saveEntry({{"title", "Aster Vale"}, {"executable", linkedExecutable},
        {"directory", artworkFixture.path()}, {"arguments", QStringList{}}});
    if (linkedManualId.isEmpty() || !unifiedGames.linkGames(0, "Manual", "", linkedManualId)) return EXIT_FAILURE;
    if (renderMode) {
      if (!unifiedGames.setPreferredInstallation(0, "Manual", "", linkedManualId)) return EXIT_FAILURE;
      if (renderOverlay == "linked-preference-missing" && !QFile::rename(linkedExecutable, linkedExecutable + ".disconnected")) return EXIT_FAILURE;
    }
  }
  auto managerPaths = backupPaths;
  QString backupFixturePath;
  if (backupFixture) {
    managerPaths = {libraryDatabasePath, settingsPath, artworkFixture.filePath("recovery")};
    library.saveCurrentFilter("Weekend games");
    BackupPayload sample;
    QString error;
    if (!BackupSnapshot::capture(libraryDatabasePath, preferences.backupSettings(), &sample, &error)) {
      qCritical().noquote() << error; return EXIT_FAILURE;
    }
    sample.settings.insert("gog_library_paths", QJsonArray{artworkFixture.filePath("Offline/GOG Games")});
    backupFixturePath = artworkFixture.filePath("sample.omakade-backup");
    if (!BackupArchive::write(backupFixturePath, sample, &error)) {
      qCritical().noquote() << error; return EXIT_FAILURE;
    }
  }
  BackupManager backups(managerPaths, &preferences, steamLibrary != nullptr || backupFixture);
  HomeModel home(&unifiedGames, libraryDatabasePath);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("Home", &home);
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
  engine.rootContext()->setContextProperty("GogSettingsFixture", gogSettingsFixture);
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
  engine.rootContext()->setContextProperty(QStringLiteral("RyujinxLibrary"), ryujinxLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Shadps4Library"), shadps4Library);
  engine.rootContext()->setContextProperty(QStringLiteral("CemuLibrary"), cemuLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("DolphinLibrary"), dolphinLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("BattleNetLibrary"), battleNetLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &launcher);
  engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
  if (renderOverlay.startsWith("settings-recorder-")) {
    playSessionStore = std::make_unique<PlaySessionStore>(QStringLiteral(":memory:"));
    preferences.setTrackPlaySessions(renderOverlay.endsWith("on"));
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  if (renderOverlay == QStringLiteral("session-history")) {
    const QString connection = QStringLiteral("omakade-session-history-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 older = SessionDatabase::beginSession(
        database, QStringLiteral("/games/demo-0.nes"), QStringLiteral("RetroArch"),
        now - 7200, 10, 10);
    const qint64 recent = SessionDatabase::beginSession(
        database, QStringLiteral("/games/demo-0.nes"), QStringLiteral("RetroArch"),
        now - 1800, 11, 11);
    if (older <= 0 || recent <= 0 ||
        !SessionDatabase::endSession(database, older, now - 5400, 1800) ||
        !SessionDatabase::endSession(database, recent, now - 600, 1200))
      return EXIT_FAILURE;
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  engine.rootContext()->setContextProperty(QStringLiteral("SessionRecorderStatus"), playSessionStore.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("Achievements"), &achievements);
  engine.rootContext()->setContextProperty(QStringLiteral("SteamAccount"), steamAccount.get());
  engine.rootContext()->setContextProperty(QStringLiteral("RetroAchievements"),
                                           retroAchievements.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Insights"), gameInsights.get());
  engine.rootContext()->setContextProperty(QStringLiteral("Metadata"), gameMetadata.get());
  engine.rootContext()->setContextProperty(QStringLiteral("ProtonDB"), &protonDb);
  engine.rootContext()->setContextProperty(QStringLiteral("Sunshine"), sunshine.get());
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"),
                                           (demoMode || stressMode) && !ownedLayoutTest);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());
  engine.rootContext()->setContextProperty(QStringLiteral("OwnedGameCountOverride"),
                                           ownedLayoutTest ? 250 : 0);
  engine.rootContext()->setContextProperty(QStringLiteral("CouchModeRequested"),
                                           startInCouchMode);
  engine.rootContext()->setContextProperty(
      QStringLiteral("CouchLibraryViewOverride"),
      renderOverlay.startsWith(QStringLiteral("couch-grid")) ? QStringLiteral("grid") : QString{});

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
  if (rootWindow != nullptr) {
    new ControllerFocusGuard(&controller, rootWindow);
    // Hold the compositor's idle timer while a launched game runs, since controller input alone
    // never resets it and most emulators do not inhibit for themselves.
    auto* idleInhibitor = new IdleInhibitor(rootWindow, rootWindow);
    QObject::connect(&launcher, &GameLauncher::gameRunningChanged, idleInhibitor,
                     [&launcher, idleInhibitor] {
                       idleInhibitor->setInhibited(launcher.gameRunning());
                     });
    auto* couchCursor = new CouchCursorManager(rootWindow, 1600, rootWindow);
    couchCursor->setObjectName(QStringLiteral("couchCursorManager"));
    QObject::connect(rootWindow, SIGNAL(couchModeChanged()), couchCursor,
                     SLOT(syncCouchMode()));
    QObject::connect(&controller, &ControllerInput::keyRequested, couchCursor,
                     &CouchCursorManager::navigationActivity);
    QObject::connect(&controller, &ControllerInput::focusDirectionRequested, couchCursor,
                     &CouchCursorManager::navigationActivity);
    QObject::connect(&controller, &ControllerInput::favoriteRequested, couchCursor,
                     &CouchCursorManager::navigationActivity);
    QObject::connect(&controller, &ControllerInput::toolbarRequested, couchCursor,
                     &CouchCursorManager::navigationActivity);
    QObject::connect(&controller, &ControllerInput::keyRequested, rootWindow,
                     [&application, &controller, rootWindow](int key, int modifiers) {
                       QWindow* target = application.focusWindow();
                       if (!controller.inputEnabled() || application.applicationState() != Qt::ApplicationActive ||
                           !ControllerFocusGuard::ownsWindow(rootWindow, target)) {
                         return;
                       }
                       const auto keyboardModifiers =
                           static_cast<Qt::KeyboardModifiers>(modifiers);
                       QKeyEvent press(QEvent::KeyPress, key, keyboardModifiers);
                       QKeyEvent release(QEvent::KeyRelease, key, keyboardModifiers);
                       QCoreApplication::sendEvent(target, &press);
                       QCoreApplication::sendEvent(target, &release);
                     });
  }
  if (rootWindow != nullptr && startInCouchMode && !renderMode && !navigationTest && !smokeTest) {
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
    }
    rootWindow->showFullScreen();
  }
  if (rootWindow != nullptr && !renderMode && !navigationTest) {
    const auto activateWindow = [rootWindow] {
      rootWindow->requestActivate();
      QMetaObject::invokeMethod(rootWindow, "focusCurrentSurface");
    };
    QTimer::singleShot(0, rootWindow, activateWindow);
    QTimer::singleShot(160, rootWindow, activateWindow);
  }
  if (auto* quickWindow = qobject_cast<QQuickWindow*>(rootWindow)) {
    if (isolatedTest && requestedRenderSize.isValid()) {
      quickWindow->resize(requestedRenderSize);
      quickWindow->setProperty("testRenderSize", requestedRenderSize);
    }
    if (renderMode) {
      if (renderOverlay.startsWith(QStringLiteral("library-reflow"))) {
        auto* timer = new QTimer(quickWindow);
        timer->setInterval(140);
        auto step = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, quickWindow,
                         [quickWindow, timer, step, renderOverlay, &library, &preferences, &application] {
          auto* grid = quickWindow->findChild<QQuickItem*>("libraryGrid");
          auto* content = grid ? grid->property("contentItem").value<QQuickItem*>() : nullptr;
          if (!grid || !content) { application.exit(EXIT_FAILURE); return; }
          QList<QRectF> seen;
          for (auto* item : content->childItems()) {
            if (!item->property("appId").isValid() || !item->isVisible()) continue;
            const auto bounds = item->mapRectToItem(grid, item->boundingRect());
            if (!bounds.intersects(grid->boundingRect())) continue;
            for (const auto& previous : seen) {
              const auto overlap = previous.intersected(bounds);
              if (overlap.width() > 2 && overlap.height() > 2) {
                qCritical() << "Library delegates overlap after resize/filter" << *step
                            << item->property("index") << bounds << previous;
                application.exit(EXIT_FAILURE); timer->stop(); return;
              }
            }
            seen.append(bounds);
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
      // `--render-overlay=settings|picker` opens an overlay so visual checks can cover it.
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
      if (renderOverlay == QStringLiteral("session-history")) {
        QMetaObject::invokeMethod(quickWindow, "openGame", Q_ARG(QVariant, 0));
        QTimer::singleShot(120, quickWindow, [quickWindow, &application] {
          auto* details = quickWindow->findChild<QObject*>("gameDetails");
          if (!details) {
            application.exit(EXIT_FAILURE);
            return;
          }
          details->setProperty(
              "selectedInstallation",
              QVariantMap{{"source", "RetroArch"},
                          {"installPath", "/games/demo-0.nes"},
                          {"launchTarget", "snes9x_libretro.so"},
                          {"appId", "demo-0"}});
          QTimer::singleShot(80, quickWindow, [quickWindow, &application] {
            auto* button = findVisualItem(quickWindow->contentItem(), "playHistoryButton");
            if (!button || !button->isVisible()) {
              qCritical() << "Play history was not available for the selected game";
              application.exit(EXIT_FAILURE);
              return;
            }
            QMetaObject::invokeMethod(button, "clicked");
            QTimer::singleShot(80, quickWindow, [quickWindow, &application] {
              auto* menu = quickWindow->findChild<QObject*>("playHistoryMenu");
              auto* done = quickWindow->findChild<QQuickItem*>("playHistoryDoneButton");
              auto* first = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_0");
              auto* second = findVisualItem(quickWindow->contentItem(), "playHistoryEntry_1");
              if (!menu || !menu->property("opened").toBool() || !done ||
                  !done->hasActiveFocus() || !first || !second) {
                qCritical() << "Play history did not open with safe focus and recorded sessions";
                application.exit(EXIT_FAILURE);
              }
            });
          });
        });
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
            if (renderOverlay.endsWith("-confirm")) {
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
            const auto point = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
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
          const double expected = qMin(600.0, scroll->property("maximumScrollY").toDouble());
          if (qAbs(scroll->property("contentY").toDouble() - expected) > 1) {
            qCritical() << "Continuous wheel input lost movement" << scroll->property("contentY") << expected;
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
            const auto point = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
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
            wheel(-120);
            const double before = scroll->property("contentY").toDouble();
            wheel(120);
            if (scroll->property("wheelTargetY").toDouble() >= before) {
              qCritical() << "Home wheel reversal retained forward momentum";
              application.exit(EXIT_FAILURE); return;
            }
            QMetaObject::invokeMethod(scroll, "stopWheelScroll");
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
            QMetaObject::invokeMethod(scroll, "stopWheelScroll");
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
        QObject::connect(quickWindow, &QQuickWindow::frameSwapped, quickWindow, [quickWindow, &home, &application] {
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
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &tab);
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
          QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, lastIdentity));
          auto* shelf = screen->findChild<QQuickItem*>("homeQueueShelf");
          const int columns = shelf ? shelf->property("columns").toInt() : 0;
          if (columns <= 0) { application.exit(EXIT_FAILURE); return; }
          const auto aboveIdentity = "queue:" + home.queue()[99 - columns].toMap().value("queueKey").toString();
          QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
          QCoreApplication::sendEvent(quickWindow, &up);
          if (screen->property("focusedIdentity").toString() != aboveIdentity) {
            qCritical() << "Full queue could not navigate to the row above its final tile"
                         << "expected" << aboveIdentity << "actual" << screen->property("focusedIdentity")
                         << "focused" << (quickWindow->activeFocusItem() ? quickWindow->activeFocusItem()->objectName() : QString{})
                         << "columns" << columns;
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
          quickWindow->setProperty("fullQueueChecked", true);
        }, Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
      }
      if (renderOverlay == QStringLiteral("home")) {
        for (const auto* id : {"demo-1", "demo-2", "demo-3"})
          home.enqueue("Demo", "", id);
        library.setSearchText("unmatched-home-original");
        quickWindow->setProperty("homeOpen", true);
        home.refresh();
        QTimer::singleShot(180, quickWindow, [quickWindow, &home, &library, &application] {
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
          QSet<QString> tileControls;
          bool returnedToHeader = false;
          for (int step = 0; step < 150; ++step) {
            QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &tab);
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
          QMetaObject::invokeMethod(screen, "focusHome");
          QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
          QCoreApplication::sendEvent(quickWindow, &down);
          if (screen->property("focusedIdentity") != identity) {
            qCritical() << "Home header did not navigate to the first game";
            application.exit(EXIT_FAILURE);
            return;
          }
          QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
          QCoreApplication::sendEvent(quickWindow, &up);
          if (quickWindow->activeFocusItem() != firstControl) {
            qCritical() << "Home featured game could not return to the header";
            application.exit(EXIT_FAILURE); return;
          }
          QCoreApplication::sendEvent(quickWindow, &down);
          if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedPlay") {
            qCritical() << "Home did not prioritize Play";
            application.exit(EXIT_FAILURE); return;
          }
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          // Demo mode exercises the normal launch route without starting an emulator.
          QCoreApplication::sendEvent(quickWindow, &enter);
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
          QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
          QCoreApplication::sendEvent(quickWindow, &right);
          if (quickWindow->activeFocusItem()->objectName() != "homeFeaturedOpen") {
            qCritical() << "Home Details is not beside Play in navigation";
            application.exit(EXIT_FAILURE); return;
          }
          QCoreApplication::sendEvent(quickWindow, &enter);
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
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &enter);
            QCoreApplication::processEvents();
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &down);
            auto* focused = quickWindow->activeFocusItem();
            if (!focused || focused->property("text").toString() != "REMOVE") {
              qCritical() << "Home queue actions are not reachable using navigation";
              application.exit(EXIT_FAILURE);
              return;
            }
            QCoreApplication::sendEvent(quickWindow, &enter);
            QCoreApplication::processEvents();
            if (home.queue().size() != 2) {
              qCritical() << "Home keyboard remove failed";
              application.exit(EXIT_FAILURE);
              return;
            }
            auto* browse = screen->findChild<QQuickItem*>("homeBrowseAll");
            if (!browse) { application.exit(EXIT_FAILURE); return; }
            browse->forceActiveFocus();
            QCoreApplication::sendEvent(quickWindow, &enter);
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
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QCoreApplication::sendEvent(quickWindow, &enter);
          QTimer::singleShot(120, quickWindow, [quickWindow, library, button, &application, review] {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &down);
            QCoreApplication::sendEvent(quickWindow, &enter);
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
        QTimer::singleShot(120, quickWindow, [quickWindow, renderOverlay, screenshotPath, &application] {
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
          if (renderOverlay != "game-info-empty") {
            entry = {
                {"year", 1997},
                {"rating", 92},
                {"ratingCount", 560},
                {"releaseText", "July 28, 1997"},
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
                  for (const auto* name : {"gameDetailsTitle", "gameIdentitySummary", "gameActivitySummary", "gameActions"}) {
                    auto* item = quickWindow->findChild<QQuickItem*>(name);
                    const auto bounds = item ? item->mapRectToScene(item->boundingRect()) : QRectF{};
                    if (!item || !item->isVisible() || bounds.top() < scroll->mapToScene(QPointF()).y() - 1 ||
                        bounds.bottom() > scroll->mapToScene(QPointF(0, scroll->height())).y() + 1) {
                      qCritical() << "Essential detail information below the fold" << name << bounds;
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
                QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(quickWindow, &press);
                QCoreApplication::sendEvent(quickWindow, &release);
                if (!section->property("expanded").toBool()) {
                  qCritical() << "Game description did not expand with keyboard activation";
                  application.exit(EXIT_FAILURE);
                  return;
                }
                if (renderOverlay != "game-info-expanded") {
                  QCoreApplication::sendEvent(quickWindow, &press);
                  QCoreApplication::sendEvent(quickWindow, &release);
                  if (section->property("expanded").toBool()) {
                    application.exit(EXIT_FAILURE);
                    return;
                  }
                }
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
                QCoreApplication::sendEvent(quickWindow, &down);
                if (!aliasesToggle->hasActiveFocus()) {
                  qCritical() << "Read More did not navigate to Other Names";
                  application.exit(EXIT_FAILURE); return;
                }
                QCoreApplication::sendEvent(quickWindow, &down);
                auto* backlog = findVisualItem(quickWindow->contentItem(), "completionStatus-backlog");
                if (!backlog || !backlog->hasActiveFocus()) {
                  qCritical() << "Description navigation did not reach organization controls"
                              << backlog << quickWindow->activeFocusItem();
                  application.exit(EXIT_FAILURE);
                  return;
                }
                QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
                QCoreApplication::sendEvent(quickWindow, &up);
                if (!aliasesToggle->hasActiveFocus()) { application.exit(EXIT_FAILURE); return; }
                QCoreApplication::sendEvent(quickWindow, &up);
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
                QCoreApplication::sendEvent(quickWindow, &down);
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
          const QStringList sections{"sources", "library", "connections", "controls", "storage", "appearance", "streaming", "about"};
          const int section = sections.indexOf(renderOverlay.mid(9));
          if (page && section >= 0) page->setProperty("section", section);
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
              flickable->setProperty("contentY", flickable->property("contentHeight").toReal() -
                                                     scroll->height());
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
      QTimer::singleShot(renderOverlay == "home-full-queue" ? 6000 : renderOverlay.startsWith("library-reflow") ? 10000 : renderOverlay.startsWith("home-wheel") ? 1300 : 900, quickWindow, [quickWindow, screenshotPath, renderOverlay, &application] {
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
        if (renderOverlay.startsWith(QStringLiteral("settings-"))) {
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
          if (!okay) { application.exit(EXIT_FAILURE); return; }
        }
        if (renderOverlay == "home-delayed") {
          auto* feature = quickWindow->findChild<QQuickItem*>("homeFeaturedSection");
          auto* shelf = quickWindow->findChild<QQuickItem*>("homeRecentShelf");
          const auto rect = [](QQuickItem* item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); };
          if (!feature || !shelf || feature->height() < 100 || shelf->height() < 150 || rect(shelf).top() < rect(feature).bottom()) {
            qCritical() << "Opening Home after startup collapsed its sections";
            application.exit(EXIT_FAILURE); return;
          }
          QList<QRectF> tiles;
          for (auto* child : shelf->childItems()) {
            if (!child->property("game").isValid()) continue;
            const auto bounds = rect(child);
            if (bounds.width() < 80 || bounds.height() < 150 || bounds.left() < rect(shelf).left() - 1 ||
                bounds.right() > rect(shelf).right() + 1 || bounds.bottom() > rect(shelf).bottom() + 1) {
              qCritical() << "Home tile escaped its shelf after resize";
              application.exit(EXIT_FAILURE); return;
            }
            for (const auto& other : tiles) {
              if (bounds.intersects(other)) {
                qCritical() << "Home tiles overlap after delayed loading";
                application.exit(EXIT_FAILURE); return;
              }
            }
            tiles.append(bounds);
          }
          if (tiles.size() != 6) { qCritical() << "Home tiles did not load"; application.exit(EXIT_FAILURE); return; }
        }
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
        [&application, &controller, &startupTimer, benchmarkMode, benchmarkLimitSupplied,
         benchmarkMaxMs, isolatedTest] {
          const qint64 firstFrameMs = startupTimer.elapsed();
          qInfo() << "First frame in" << firstFrameMs << "ms";
          if (!isolatedTest) {
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
        auto* settingsScroll =
            quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsScroll"));
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
            settingsScroll == nullptr || search == nullptr || preferences == nullptr ||
            browsePanel == nullptr || browseCategories == nullptr || browseOptions == nullptr ||
            keyboard == nullptr || keyboardGrid == nullptr || textEntryKeyboard == nullptr ||
            textEntryGrid == nullptr || desktopSearch == nullptr) {
          fail(QStringLiteral("Couch navigation test could not find the couch controls"));
          return;
        }
        preferences->setProperty("couchLibraryView", QStringLiteral("detail"));
        QCoreApplication::processEvents();
        strip->setProperty("currentIndex", 0);
        strip->forceActiveFocus();
        controller.keyRequested(Qt::Key_Right, Qt::NoModifier);
        QTimer::singleShot(50, quickWindow,
                           [quickWindow, &application, &controller, couch, couchCursor, strip, grid,
                            view, show, sourceFilter, sortOrder, layout, favorite, browsePanel,
                            browseCategories, browseOptions, search, settings, settingsScroll,
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
            controller.keyRequested(key, Qt::NoModifier);
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
          strip->forceActiveFocus();
          regressionLibrary->setProperty("searchText", QStringLiteral("omakade-no-matching-game-regression"));
          QCoreApplication::processEvents();
          if (!emptyState->isVisible()) {
            fail(QStringLiteral("Empty couch library did not show its empty state"));
            return;
          }
          regressionLibrary->setProperty("searchText", QString{});
          QCoreApplication::processEvents();
          if (emptyState->isVisible()) {
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
          sendKey(Qt::Key_F11);
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
          sendKey(Qt::Key_Right);
          if (!settings->hasActiveFocus()) {
            fail(QStringLiteral("Controller could not reach couch Settings"));
            return;
          }
          sendKey(Qt::Key_Return);
          QTimer::singleShot(80, quickWindow,
                             [quickWindow, &application, &controller, couch, settingsScroll,
                              fail] {
            if (!quickWindow->property("diagnosticsOpen").toBool()) {
              fail(QStringLiteral("Couch Settings did not open"));
              return;
            }
            auto* settingsPage = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
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
            controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
              controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
                    controller.keyRequested(key, Qt::NoModifier);
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
                controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
                  controller.keyRequested(Qt::Key_F11, Qt::NoModifier);
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
              controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
              if (!rootWindow->property("couchTextEntryOpen").toBool()) {
                qCritical("The controller reached a text field and no keyboard opened");
                application.exit(EXIT_FAILURE);
                return;
              }
              rootWindow->setProperty("couchTextEntryOpen", false);
            }
            controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
            auto* manageInvoker = item("coverEditButton");
            if (identifyPanel->property("opened").toBool() || !manageInvoker || !manageInvoker->hasActiveFocus()) {
              qCritical() << "Closing artwork did not restore the cover button focus";
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
            application.exit(EXIT_SUCCESS);
            });
          }
        };
        (*step)();
      });
    } else if (navigationTest) {
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
                controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
              auto* settingsScroll = item("settingsScroll");
              for (auto* control : {sort, sources, filters, view, more, settings, search}) {
                if (!withinWindow(control)) { fail("Library toolbar extends outside the window"); return; }
              }
              const QString fieldError = verifyEditorTextFields(quickWindow, search, controller);
              if (!fieldError.isEmpty()) { fail(fieldError); return; }
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
              controller.keyRequested(Qt::Key_Down, Qt::NoModifier); settle();
              if (allSources->hasActiveFocus()) { fail("Keyboard Down did not navigate Sources popup"); return; }
              controller.keyRequested(Qt::Key_Up, Qt::NoModifier); settle();
              if (!allSources->hasActiveFocus()) { fail("Keyboard Up did not return to All sources"); return; }
              if (!activate(retro) || model->property("sourceFilters").toStringList() != QStringList{"RetroArch"}) {
                fail("Source selection changed behavior"); return;
              }
              steam->forceActiveFocus();
              controller.keyRequested(Qt::Key_Return, Qt::ShiftModifier);
              settle();
              if (model->property("sourceFilters").toStringList().size() != 2) {
                fail("Sources menu lost additive selection"); return;
              }
              controller.favoriteRequested();
              if (model->property("sourceFilters").toStringList() != QStringList{"RetroArch"}) {
                fail("Controller favorite did not remove a source"); return;
              }
              activate(allSources);
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              if (opened("librarySources") || !sources->hasActiveFocus()) { fail("Sources did not restore focus"); return; }
              if (!activate(filters) || !opened("libraryFilters")) { fail("Filters menu did not open"); return; }
              auto* filterStart = quickWindow->activeFocusItem();
              controller.keyRequested(Qt::Key_Down, Qt::NoModifier); settle();
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
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              if (!opened("libraryFilters")) { fail("Value picker did not return to Filters"); return; }
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              if (!filters->hasActiveFocus()) { fail("Filters did not restore its invoker"); return; }
              if (!activate(sort) || !opened("librarySort")) { fail("Sort menu did not open"); return; }
              controller.keyRequested(Qt::Key_Down, Qt::NoModifier); settle();
              controller.keyRequested(Qt::Key_Return, Qt::NoModifier); settle();
              if (model->property("sortMode").toInt() != 1 || !sort->hasActiveFocus()) { fail("Sort choice was not applied"); return; }
              model->setProperty("sortMode", 0);
              if (!activate(view) || !opened("libraryViewMenu")) { fail("View menu did not open"); return; }
              if (!activate(item("coverSizeButton"))) { fail("Cover size is unreachable"); return; }
              auto* slider = item("coverSizeSlider");
              if (!slider || !slider->hasActiveFocus()) { fail("Cover size did not focus its slider"); return; }
              const double size = slider->property("value").toDouble();
              controller.keyRequested(Qt::Key_Left, Qt::NoModifier);
              if (slider->property("value").toDouble() >= size) { fail("Cover size keyboard input failed"); return; }
              controller.keyRequested(Qt::Key_Right, Qt::NoModifier);
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              if (!opened("libraryViewMenu")) { fail("Cover size did not return to View"); return; }
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              if (!view->hasActiveFocus()) { fail("View did not restore focus"); return; }
              if (!activate(more) || !opened("libraryActions")) { fail("More did not open"); return; }
              auto* first = item("randomGameButton");
              if (!first || !first->hasActiveFocus()) { fail("More initial focus is unstable"); return; }
              for (const char* shortcutName : {"navigationTabForward", "navigationTabBackward"}) {
                first->forceActiveFocus();
                QSet<QString> visited;
                bool returned = false;
                for (int step = 0; step < 20; ++step) {
                  controller.keyRequested(QString(shortcutName) == "navigationTabForward" ? Qt::Key_Tab : Qt::Key_Backtab, Qt::NoModifier);
                  settle();
                  auto* focused = quickWindow->activeFocusItem();
                  if (!focused || !focused->isVisible() || !withinWindow(focused)) { fail("Menu Tab lost usable focus"); return; }
                  visited.insert(focused->objectName());
                  if (focused == first) { returned = true; break; }
                }
                if (!returned || !visited.contains("bulkOrganizationButton") || !visited.contains("savedFiltersButton")
                    || !visited.contains("rescanButton") || !visited.contains("actionMenuCloseButton")) {
                  fail("Menu Tab skipped a command"); return;
                }
              }
              controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
              for (const char* command : {"bulkOrganizationButton", "savedFiltersButton"}) {
                activate(more);
                if (!activate(item(command))) { fail("Editor menu command unavailable"); return; }
                const char* state = QString(command) == "bulkOrganizationButton" ? "bulkOrganizationOpen" : "savedFiltersOpen";
                if (!quickWindow->property(state).toBool()) { fail("Menu did not open its editor"); return; }
                controller.keyRequested(Qt::Key_Escape, Qt::NoModifier); settle();
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
                  [quickWindow, &application, &controller, grid, settingsScroll, fail] {
                    if (!quickWindow->property("diagnosticsOpen").toBool() ||
                        quickWindow->activeFocusItem() == nullptr) {
                      fail(QStringLiteral("Controller Open did not enter Settings"));
                      return;
                    }
                    auto* settingsPage = quickWindow->findChild<QQuickItem*>(QStringLiteral("settingsOverlay"));
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
                        auto* favorite =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("favoriteButton"));
                        auto* manage =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("addToQueueButton"));
                        auto* hide =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("detailManageButton"));
                        auto* gameActions =
                            quickWindow->findChild<QQuickItem*>(QStringLiteral("gameActions"));
                        if (!quickWindow->property("detailOpen").toBool() || play == nullptr ||
                            favorite == nullptr || manage == nullptr || hide == nullptr ||
                            gameActions == nullptr || !play->hasActiveFocus()) {
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
                        if (gameActions->property("columns").toInt() == 2) {
                          controller.focusDirectionRequested(Qt::Key_Down);
                          if (!manage->hasActiveFocus()) {
                            fail(
                                QStringLiteral("Controller Down did not move from Play to Manage"));
                            return;
                          }
                          controller.focusDirectionRequested(Qt::Key_Right);
                          if (!hide->hasActiveFocus()) {
                            fail(QStringLiteral(
                                "Controller Right did not move from Manage to Hide"));
                            return;
                          }
                          controller.focusDirectionRequested(Qt::Key_Up);
                          if (!favorite->hasActiveFocus()) {
                            fail(
                                QStringLiteral("Controller Up did not move from Hide to Favorite"));
                            return;
                          }
                          controller.focusDirectionRequested(Qt::Key_Left);
                        } else {
                          controller.focusDirectionRequested(Qt::Key_Right);
                          controller.focusDirectionRequested(Qt::Key_Right);
                          controller.focusDirectionRequested(Qt::Key_Right);
                          if (!hide->hasActiveFocus()) {
                            fail(QStringLiteral("Controller Right did not traverse game actions"));
                            return;
                          }
                          controller.focusDirectionRequested(Qt::Key_Left);
                          controller.focusDirectionRequested(Qt::Key_Left);
                          controller.focusDirectionRequested(Qt::Key_Left);
                        }
                        if (!play->hasActiveFocus()) {
                          fail(QStringLiteral("Controller could not reverse through game actions"));
                          return;
                        }
                        for (const char* name : {"favoriteButton", "addToQueueButton", "detailManageButton"}) {
                          auto* action = quickWindow->findChild<QQuickItem*>(name);
                          if (!action || qAbs(action->width() - play->width()) > 1) {
                            fail("Game action buttons have unequal widths"); return;
                          }
                        }
                        auto* manageMenuButton = quickWindow->findChild<QQuickItem*>("detailManageButton");
                        manageMenuButton->forceActiveFocus();
                        controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
                        QCoreApplication::processEvents();
                        auto* managePopup = quickWindow->findChild<QObject*>("detailManageMenu");
                        if (!managePopup || !managePopup->property("opened").toBool()) { fail("Game Manage menu did not open"); return; }
                        auto* firstAction = quickWindow->activeFocusItem();
                        for (int step = 0; step < 12; ++step) controller.focusDirectionRequested(Qt::Key_Down);
                        if (!firstAction || quickWindow->activeFocusItem() == firstAction) { fail("Game Manage menu did not traverse"); return; }
                        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
                        QCoreApplication::processEvents();
                        if (!manageMenuButton->hasActiveFocus() || !quickWindow->property("detailOpen").toBool()) { fail("Game Manage Back lost detail context"); return; }
                        play->forceActiveFocus();
                        controller.keyRequested(Qt::Key_Up, Qt::NoModifier);
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
  controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
                   [rootWindow](bool fullscreen) {
                     if (rootWindow == nullptr) {
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
                   [&retroArchLibrary, &pcsx2Library, &ryujinxLibrary, &dolphinLibrary,
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
                     } else if (source == QStringLiteral("RetroArch") &&
                                retroArchLibrary != nullptr && preferences.retroArchEnabled()) {
                       retroArchLibrary->refresh();
                     } else if (source == QStringLiteral("Dolphin") && dolphinLibrary != nullptr &&
                                preferences.dolphinEnabled()) {
                       dolphinLibrary->refresh();
                     }
                   });
  QObject::connect(&singleInstance, &SingleInstance::quitRequested, &application,
                   &QCoreApplication::quit);

  if (steamLibrary != nullptr && preferences.steamEnabled()) {
    QTimer::singleShot(0, steamLibrary, &SteamGameModel::refresh);
  }
  if (lutrisLibrary != nullptr && preferences.lutrisEnabled()) {
    QTimer::singleShot(150, lutrisLibrary, &LutrisGameModel::refresh);
  }
  if (heroicLibrary != nullptr && (preferences.heroicEnabled() || preferences.gogEnabled())) {
    QTimer::singleShot(300, heroicLibrary, &HeroicGameModel::refresh);
  }
  if (faugusLibrary != nullptr && preferences.faugusEnabled()) {
    QTimer::singleShot(450, faugusLibrary, &FaugusGameModel::refresh);
  }
  if (retroArchLibrary != nullptr && preferences.retroArchEnabled() && !consolePortalTest) {
    QTimer::singleShot(600, retroArchLibrary, &RetroArchGameModel::refresh);
  }
  // Sources start disabled and switch on once their emulator is detected, unless
  // the user wrote an explicit pcsx2_enabled/ryujinx_enabled key. Scans only run
  // while the source is enabled or still eligible for automatic detection.
  if (pcsx2Library != nullptr &&
      (preferences.pcsx2Enabled() || preferences.pcsx2AutoEnabled())) {
    QTimer::singleShot(650, pcsx2Library, &Pcsx2GameModel::refresh);
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
    QTimer::singleShot(700, ryujinxLibrary, &RyujinxGameModel::refresh);
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
    QTimer::singleShot(720, shadps4Library, &Shadps4GameModel::refresh);
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
    QTimer::singleShot(760, dolphinLibrary, &DolphinGameModel::refresh);
    QObject::connect(dolphinLibrary, &DolphinGameModel::statusChanged, dolphinLibrary,
                     [&preferences, dolphinLibrary] {
                       if (dolphinLibrary->dolphinDetected() && preferences.dolphinAutoEnabled()) {
                         preferences.setDolphinAutoEnabled(false);
                         preferences.setDolphinEnabled(true);
                       }
                     });
  }
  if (cemuLibrary != nullptr && (preferences.cemuEnabled() || preferences.cemuAutoEnabled())) {
    QTimer::singleShot(740, cemuLibrary, &CemuGameModel::refresh);
    QObject::connect(cemuLibrary, &CemuGameModel::statusChanged, cemuLibrary,
                     [&preferences, cemuLibrary] {
                       if (cemuLibrary->cemuDetected() && preferences.cemuAutoEnabled()) {
                         preferences.setCemuAutoEnabled(false);
                         preferences.setCemuEnabled(true);
                       }
                     });
  }
  if (battleNetLibrary != nullptr && preferences.battleNetEnabled()) {
    QTimer::singleShot(750, battleNetLibrary, &BattleNetGameModel::refresh);
  }

  if (gogSettingsTest || linkedPreferenceTest) {
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
        button->forceActiveFocus(); controller.keyRequested(Qt::Key_Return, Qt::NoModifier); return true;
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
            controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
            if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("GOG path controller keyboard did not open"); return; }
            QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
          }
          field->setProperty("text", gogAvailableFolder);
          if (rootWindow->property("couchMode").toBool()) {
            field->forceActiveFocus();
            controller.focusDirectionRequested(Qt::Key_Down);
            auto* add = find("gogAddFolderButton");
            if (!add || !add->hasActiveFocus()) { fail("Directional navigation cannot reach Add Folder"); return; }
            controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
          controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
          controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
          if (rootWindow->property("detailOpen").toBool()) { fail("Controller Back did not close linked details"); return; }
          timer->stop(); application.quit();
        }
      }
    });
    QTimer::singleShot(180, timer, [timer] { timer->start(); });
    QTimer::singleShot(10000, timer, [&application] { qCritical() << "Library preferences fixture timed out"; application.exit(EXIT_FAILURE); });
  } else if (backupEditorTest) {
    auto* timer = new QTimer(rootWindow);
    timer->setInterval(80);
    auto step = std::make_shared<int>(0);
    const QString exportPath = artworkFixture.filePath("export.omakade-backup");
    QObject::connect(timer, &QTimer::timeout, rootWindow,
        [&, timer, step, exportPath] {
      const auto fail = [&](const QString& message) { timer->stop(); qCritical().noquote() << message; application.exit(EXIT_FAILURE); };
      auto* editor = rootWindow->findChild<QQuickItem*>("backupEditor");
      auto* field = rootWindow->findChild<QQuickItem*>("backupPathField");
      const auto press = [&](const QString& name) {
        auto* button = rootWindow->findChild<QQuickItem*>(name);
        if (!button || !button->isVisible() || !button->isEnabled()) return false;
        button->forceActiveFocus();
        controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
        return true;
      };
      if (!editor || !field) { fail("Backup editor controls are missing"); return; }
      if (*step == 0) {
        rootWindow->setProperty("diagnosticsOpen", true);
        // Backup and restore lives in the About & storage section of the redesigned settings.
        auto* settingsOverlay = rootWindow->findChild<QQuickItem*>("settingsOverlay");
        if (settingsOverlay == nullptr) { fail("Settings panel is missing"); return; }
        settingsOverlay->setProperty("section", 4);
        if (!press("backupSettingsButton") || !rootWindow->property("backupEditorOpen").toBool()) { fail("Settings could not open the backup editor"); return; }
        ++*step;
      } else if (*step == 1) {
        if (rootWindow->property("couchMode").toBool()) {
          field->forceActiveFocus(); controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
          if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("Backup path cannot use controller text entry"); return; }
          QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
        }
        field->setProperty("text", exportPath);
        if (!press("backupExportButton") || editor->property("pendingMode").toString() != "export") { fail("Backup export did not request confirmation"); return; }
        ++*step;
      } else if (*step == 2) {
        if (!press("backupConfirmButton")) { fail("Backup save confirmation is missing"); return; }
        ++*step;
      } else if (*step == 3 && !backups.busy()) {
        BackupPayload exported; QString error;
        if (!BackupArchive::read(exportPath, &exported, &error)) { fail("UI export failed: " + backups.message()); return; }
        field->setProperty("text", backupFixturePath);
        if (!press("backupPreviewButton")) { fail("Backup preview action is missing"); return; }
        ++*step;
      } else if (*step == 4 && !backups.busy()) {
        if (!backups.hasPreview()) { fail("UI preview failed: " + backups.message()); return; }
        auto* text = rootWindow->findChild<QQuickItem*>("backupPreviewText");
        auto* scroll = rootWindow->findChild<QQuickItem*>("backupPreviewScroll");
        if (!text || !scroll) { fail("Backup preview content is missing"); return; }
        text->forceActiveFocus();
        controller.focusDirectionRequested(Qt::Key_Down);
        auto* flickable = scroll->property("contentItem").value<QObject*>();
        if (!flickable || flickable->property("contentY").toReal() <= 0) { fail("Controller could not scroll the backup preview"); return; }
        controller.focusDirectionRequested(Qt::Key_Right);
        auto* merge = rootWindow->findChild<QQuickItem*>("backupMergeButton");
        if (!merge || !merge->hasActiveFocus()) { fail("Preview cannot reach restore choices"); return; }
        controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
        if (editor->property("pendingMode").toString() != "merge" || BackupRecovery(managerPaths).status() != "none") { fail("Merge must await explicit confirmation"); return; }
        ++*step;
      } else if (*step == 5) {
        if (!press("backupCancelButton") || !editor->property("pendingMode").toString().isEmpty()) { fail("Restore confirmation could not cancel"); return; }
        if (!press("backupReplaceButton") || editor->property("pendingMode").toString() != "replace") { fail("Replace confirmation is missing"); return; }
        ++*step;
      } else if (*step == 6) {
        if (!press("backupConfirmButton")) { fail("Replacement could not be confirmed"); return; }
        ++*step;
      } else if (*step == 7 && !backups.busy()) {
        if (!editor->property("queued").toBool() || BackupRecovery(managerPaths).status() != "queued") { fail("Confirmed restore was not queued: " + backups.message()); return; }
        auto* close = rootWindow->findChild<QQuickItem*>("backupCloseAppButton");
        if (!close || !close->hasActiveFocus()) { fail("Queued restore did not focus Close Omakade"); return; }
        timer->stop(); application.quit();
      }
    });
    QTimer::singleShot(150, timer, [timer] { timer->start(); });
    QTimer::singleShot(10000, timer, [&application] { qCritical() << "Backup editor fixture timed out"; application.exit(EXIT_FAILURE); });
  } else if (bulkEditorTest) {
    QTimer::singleShot(150, &application, [&application, rootWindow, &library, &controller] {
      const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
      QMetaObject::invokeMethod(rootWindow, "openBulkOrganization");
      auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkOrganizationEditor"));
      if (!editor || !editor->isVisible()) { fail("Bulk organization editor is missing"); return; }
      QMetaObject::invokeMethod(editor, "focusRow", Q_ARG(QVariant, 0));
      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
      for (int i = 0; i < 25; ++i) controller.focusDirectionRequested(Qt::Key_Down);
      if (editor->property("focusedRow").toInt() != 25) { fail("Bulk selection cannot scroll with a controller"); return; }
      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
      if (library.selectionCount() != 2) { fail("Bulk game selection did not toggle two games"); return; }
      if (editor->property("stacked").toBool()) {
        auto* favorite = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkFavoriteButton"));
        QMetaObject::invokeMethod(editor, "focusRow", Q_ARG(QVariant, library.rowCount() - 1));
        controller.focusDirectionRequested(Qt::Key_Down);
        if (!favorite || !favorite->hasActiveFocus()) { fail("Cannot leave the stacked game list for actions"); return; }
        controller.focusDirectionRequested(Qt::Key_Up);
        if (favorite->hasActiveFocus() || editor->property("focusedRow").toInt() != library.rowCount() - 1) {
          fail("Cannot return from stacked actions to the game list"); return;
        }
      }
      QTimer::singleShot(100, &application, [&application, rootWindow, editor, &library, &controller, fail] {
        const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
        if (!fieldError.isEmpty()) { fail(fieldError); return; }
        if (rootWindow->property("couchMode").toBool()) {
          auto* tags = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkTagsField"));
          if (!tags) { fail("Bulk tags field is missing"); return; }
          tags->forceActiveFocus();
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
          if (!rootWindow->property("couchTextEntryOpen").toBool()) { fail("Bulk tags cannot open controller text entry"); return; }
          QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
        }
        const int before = library.rowCount();
        const QVariantMap changes{{"hidden", true}, {"tagsAdd", "tested"}};
        QMetaObject::invokeMethod(editor, "apply", Q_ARG(QVariant, changes));
        auto* selectAll = rootWindow->findChild<QQuickItem*>(QStringLiteral("bulkSelectAllButton"));
        if (library.selectionCount() != 0 || library.rowCount() != before - 2 || !selectAll || !selectAll->hasActiveFocus()) {
          fail(QStringLiteral("Bulk hiding: selected=%1 rows=%2 before=%3 focus=%4 message=%5")
              .arg(library.selectionCount()).arg(library.rowCount()).arg(before).arg(selectAll && selectAll->hasActiveFocus()).arg(library.bulkMessage())); return;
        }
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
        if (rootWindow->property("bulkOrganizationOpen").toBool()) { fail("Bulk editor cannot close"); return; }
        application.quit();
      });
    });
  } else if (savedFilterTest) {
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
      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
      if (!name->property("text").toString().isEmpty() || !name->hasActiveFocus() || clear->isVisible()) {
        fail("Saved filter clear button did not clear and return focus"); return;
      }
      name->setProperty("text", "Weekend test");
      QTimer::singleShot(100, &application, [&application, rootWindow, editor, name, &library, &controller, fail] {
        const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
        if (!fieldError.isEmpty()) { fail(fieldError); return; }
        if (rootWindow->property("couchMode").toBool()) {
          name->forceActiveFocus();
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
        if (rootWindow->property("savedFiltersOpen").toBool()) { fail("Saved filters cannot close"); return; }
        application.quit();
      });
    });
  } else if (randomSelectionTest) {
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
      controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
      QCoreApplication::processEvents();
      if (!rootWindow->property("detailOpen").toBool() || !rootWindow->property("randomSelection").toBool()) {
        fail("Random game control did not show a selection"); return;
      }
      const QString first = rootWindow->property("selectedGame").toMap().value("appId").toString();
      QTimer::singleShot(100, &application, [&application, rootWindow, &controller, first, fail] {
        auto* another = rootWindow->findChild<QQuickItem*>(QStringLiteral("pickAnotherButton"));
        if (!another || !another->isVisible()) { fail("Pick another is missing"); return; }
        another->forceActiveFocus();
        controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
        if (rootWindow->property("selectedGame").toMap().value("appId").toString() == first) {
          fail("Pick another immediately repeated the same game"); return;
        }
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
        if (rootWindow->property("detailOpen").toBool()) { fail("Random selection cannot close"); return; }
        application.quit();
      });
    });
  } else if (staleSelectionTest) {
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
        controller.focusDirectionRequested(Qt::Key_Down);
        QCoreApplication::processEvents();
        controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
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
  } else if (filterBackTest) {
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
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
      application.quit();
    });
  } else if (artworkEditorTest) {
    QTimer::singleShot(150, &application, [&application, rootWindow, &artworkFixture, &controller] {
      const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
      QMetaObject::invokeMethod(rootWindow, "openGame", Q_ARG(QVariant, 0));
      QMetaObject::invokeMethod(rootWindow, "editArtwork");
      auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("artworkEditor"));
      if (!editor || !editor->isVisible()) { fail("Artwork editor is not visible"); return; }
      QImage fixture(180, 90, QImage::Format_ARGB32);
      fixture.fill(QColor(60, 100, 160, 200));
      const QString path = artworkFixture.filePath(QStringLiteral("artwork.png"));
      if (!fixture.save(path)) { fail("Could not save artwork fixture"); return; }
      for (const QString& kind : {QStringLiteral("cover"), QStringLiteral("hero"), QStringLiteral("logo")}) {
        QMetaObject::invokeMethod(editor, "apply", Q_ARG(QVariant, kind), Q_ARG(QVariant, path));
      }
      const QVariantMap game = rootWindow->property("selectedGame").toMap();
      if (!game.value("customCover").toBool() || !game.value("customHero").toBool() || !game.value("customLogo").toBool()) {
        fail("Artwork editor did not persist all slots"); return;
      }
      QMetaObject::invokeMethod(editor, "reset", Q_ARG(QVariant, QStringLiteral("hero")));
      const QVariantMap reset = rootWindow->property("selectedGame").toMap();
      if (reset.value("customHero").toBool() || !reset.value("customCover").toBool() || !reset.value("customLogo").toBool()) {
        fail("Artwork reset changed other slots"); return;
      }
      QTimer::singleShot(100, &application, [&application, rootWindow, &controller, fail] {
        if (rootWindow->property("couchMode").toBool()) {
          const auto findVisual = [](auto&& self, QQuickItem* item) -> QQuickItem* {
            if (!item) return nullptr;
            if (item->objectName() == QStringLiteral("artworkPath_cover")) return item;
            for (auto* child : item->childItems()) if (auto* found = self(self, child)) return found;
            return nullptr;
          };
          auto* pathField = findVisual(findVisual, qobject_cast<QQuickWindow*>(rootWindow)->contentItem());
          if (!pathField) { fail("Artwork path field is missing"); return; }
          pathField->forceActiveFocus();
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
          if (!rootWindow->property("couchTextEntryOpen").toBool()) {
            fail("Artwork path cannot open controller text entry"); return;
          }
          QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
        }
        controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
        if (rootWindow->property("artworkEditorOpen").toBool()) { fail("Artwork editor cannot close"); return; }
        application.quit();
      });
    });
  } else if (manualEditorTest) {
    QTimer::singleShot(150, &application, [&application, rootWindow, &manualGames, &controller] {
      const auto fail = [&application](const QString& message) { qCritical() << message; application.exit(EXIT_FAILURE); };
      if (!rootWindow) { fail("Manual editor has no window"); return; }
      QMetaObject::invokeMethod(rootWindow, "editManualGame", Q_ARG(QVariant, QString{}));
      auto* editor = rootWindow->findChild<QQuickItem*>(QStringLiteral("manualGameEditor"));
      auto* title = rootWindow->findChild<QQuickItem*>(QStringLiteral("manualTitleField"));
      if (!editor || !title || !editor->isVisible()) { fail("Manual editor is not visible"); return; }
      const QVariantMap draft{{"title", "Editor Test"},
                              {"executable", QStandardPaths::findExecutable(QStringLiteral("true"))},
                              {"directory", QDir::tempPath()},
                              {"arguments", QStringList{"two words", ""}}};
      QMetaObject::invokeMethod(editor, "loadDraft", Q_ARG(QVariant, draft));
      QTimer::singleShot(100, &application, [&application, rootWindow, editor, title, &manualGames, &controller, fail] {
        const QString fieldError = verifyEditorTextFields(qobject_cast<QQuickWindow*>(rootWindow), editor, controller);
        if (!fieldError.isEmpty()) { fail(fieldError); return; }
        if (rootWindow->property("couchMode").toBool()) {
          title->forceActiveFocus();
          controller.keyRequested(Qt::Key_Return, Qt::NoModifier);
          if (!rootWindow->property("couchTextEntryOpen").toBool()) {
            fail("Manual title cannot open controller text entry"); return;
          }
          QMetaObject::invokeMethod(rootWindow, "closeCouchTextEntry", Q_ARG(QVariant, false));
        }
        QMetaObject::invokeMethod(editor, "save");
        if (manualGames.count() != 1 || rootWindow->property("manualEditorOpen").toBool()) {
          fail("Manual editor could not save: " + manualGames.lastError()); return;
        }
        const QString id = manualGames.data(manualGames.index(0), GameRoles::AppId).toString();
        if (manualGames.get(id).value("arguments").toStringList() != QStringList{"two words", ""}) {
          fail("Manual editor changed argument boundaries"); return;
        }
        QMetaObject::invokeMethod(rootWindow, "editManualGame", Q_ARG(QVariant, id));
        title->setProperty("text", "Edited Test");
        QMetaObject::invokeMethod(editor, "save");
        if (manualGames.count() != 1 || manualGames.get(id).value("title").toString() != "Edited Test") {
          fail("Editing a manual game changed its identity"); return;
        }
        application.quit();
      });
    });
  } else if (smokeTest && !renderMode) {
    QTimer::singleShot(600, &application, &QCoreApplication::quit);
  }

  return application.exec();
}

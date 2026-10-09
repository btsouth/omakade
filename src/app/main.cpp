#include "achievements/AchievementModel.h"
#include "achievements/RetroAchievementsService.h"
#include "achievements/SteamAccountService.h"
#include "app/AppSettings.h"
#include "app/CardExport.h"
#include "app/CouchNavigationContract.h"
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
#include "metadata/GameInsightsService.h"
#include "metadata/GameMetadata.h"
#include "metadata/ProtonDbService.h"
#include <QQmlProperty>
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeOverlay.h"
#include "guide/GuidePlugin.h"
#include "guide/InGameGuide.h"
#include "gamemode/GameModeSession.h"
#include "gamemode/GameModeGuideButton.h"
#include "gamemode/GameModeShortcut.h"
#include "streaming/SunshineIntegration.h"
#include "theme/OmarchyTheme.h"
#include "tracking/PlaySessionStore.h"
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
#ifdef OMAKADE_PLATFORM_INPUT_TESTS
#include <qpa/qwindowsysteminterface.h>
#endif

#include <algorithm>
#include <atomic>
#include <memory>

namespace {
// Library-only transport for --game-mode-test. It exercises the real session and
// QML lifecycle offscreen without reaching any desktop, process or audio service.
class LibraryGameModeTestCompositor final : public GameModeCompositor {
public:
  GameModeWindow window{QStringLiteral("0xaa"), QStringLiteral("1"), QStringLiteral("fixture")};
  bool placeFails = false;
  std::atomic<bool> mapped{true};
  std::atomic<bool> remapped{false};
  bool available() override { return true; }
  QVector<GameModeOutput> outputs(QString*) override {
    GameModeOutput output;
    output.name = QStringLiteral("fixture");
    output.enabled = true;
    output.focused = true;
    output.workspace = mapped.load() ? window.workspace : QStringLiteral("1");
    return {output};
  }
  bool setOutputEnabled(const QString&, bool, QString*) override { return true; }
  GameModeWindow windowForPid(qint64) override {
    if (!mapped.load()) return {};
    // A hidden Qt surface maps as a new client on the current desktop workspace.
    if (remapped.exchange(false)) window.workspace = QStringLiteral("1");
    return window;
  }
  int otherWindowsOn(const QString&, qint64) override { return 0; }
  QStringList otherWindowAddressesOn(const QString&, qint64) override { return {}; }
  bool gameWindows(const QString&, qint64, QVector<GameModeGameWindow>* games, QString*) override {
    games->clear();
    return true;
  }
  bool desktopFocus(GameModeDesktopFocus* focus, QString*) override {
    *focus = mapped.load() ? GameModeDesktopFocus{window.output, window.workspace, window.address}
                          : GameModeDesktopFocus{window.output, QStringLiteral("1"), QStringLiteral("0xdd")};
    return true;
  }
  GameModeWindow placeholderForPid(qint64) override { return {}; }
  bool holdPlaceholder(QString*) override { return false; }
  bool placeWindow(const QString&, const QString& workspace, const QString& output, const QString&,
                   QString*) override {
    if (placeFails) return false;
    window.workspace = workspace;
    window.output = output;
    return true;
  }
  bool returnWindow(const QString&, const QString& workspace, const QString&, QString*) override {
    window.workspace = workspace;
    return true;
  }
  bool setWindowMode(const QString&, int mode, int client, QString*) override {
    window.fullscreenMode = mode;
    window.fullscreenClient = client;
    return true;
  }
  bool moveWorkspace(const QString&, const QString&, QString*) override { return true; }
  bool focusWindow(const QString&, QString*) override { return true; }
  bool focusWorkspace(const QString&, QString*) override { return true; }
  bool focusOutput(const QString&, QString*) override { return true; }
};

// A sink that signals nothing, for the render overlays that drive the stop
// confirmation: the overlay has to press the confirm action to prove it works,
// and this is what keeps that from touching a real process.
class NoSignalSink final : public GameStop::SignalSink {
public:
  GameStop::LeverResult terminate(qint64) override { return GameStop::LeverResult::Done; }
  GameStop::LeverResult forceTerminate(qint64) override { return GameStop::LeverResult::Done; }
  GameStop::LeverResult stopWinePrefix(const QString&, bool) override {
    return GameStop::LeverResult::Done;
  }
  GameStop::LeverResult stopFlatpakApp(const QString&) override {
    return GameStop::LeverResult::Done;
  }
};
} // namespace

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
  const auto applyThemeFont = [&application, &theme] {
    QFont font = application.font();
    font.setFamily(theme.fontFamily());
    application.setFont(font);
  };
  QObject::connect(&theme, &OmarchyTheme::themeChanged, &application, applyThemeFont);
  applyThemeFont();
  const QString screenshotPath =
      optionValue(application.arguments(), QStringLiteral("--render-screenshot"));
  const QString renderSize = optionValue(application.arguments(), QStringLiteral("--render-size"));
  QString renderOverlay =
      optionValue(application.arguments(), QStringLiteral("--render-overlay"));
  // `--export-card=<path>` renders the year-in-review card offscreen, writes it, and exits with
  // the outcome. This isolated render path uses synthetic data, so require an explicit
  // fixture flag rather than silently exporting demo statistics as someone's library.
  const QString cardExportPath =
      optionValue(application.arguments(), QStringLiteral("--export-card"));
  if (!cardExportPath.isEmpty() && !application.arguments().contains(QStringLiteral("--stats-fixture"))) {
    qCritical() << "Headless card checks require --stats-fixture. Export your library from Stats > Your Card.";
    return EXIT_FAILURE;
  }
  if (!cardExportPath.isEmpty() && renderOverlay.isEmpty())
    renderOverlay = QStringLiteral("year-in-review");
  // Both the stats screen and the card read the same fixture, which is why they share a flag.
  const bool statsFixture = renderOverlay.startsWith(QStringLiteral("stats"))
                            || renderOverlay.startsWith(QStringLiteral("year-in-review"));
  const bool repairNavigationFixture =
      application.arguments().contains(QStringLiteral("--repair-navigation-fixture"));
  // The Now Playing render fixture keeps the pid of its stand-in game so the check can
  // find that exact row's stop control.
  qint64 nowPlayingFixturePid = 0;
  // `--play Source:runner:id` launches one library game, through the running window when
  // there is one, and `--quit` closes the running window. Sunshine app entries use both.
  const QString playKey = optionValue(application.arguments(), QStringLiteral("--play"));
  const bool quitRequest = application.arguments().contains(QStringLiteral("--quit"));
  // `--game-mode` starts Game Mode in the running window or in a new one. `--game-mode-exit`
  // leaves it, and with no window running it undoes what an interrupted session left changed.
  // `--game-mode-toggle` does whichever applies, which is what a key binding wants.
  const bool gameModeToggleRequest =
      application.arguments().contains(QStringLiteral("--game-mode-toggle"));
  const bool guideToggleRequest = application.arguments().contains(QStringLiteral("--guide-toggle"));
  const QString guideDevice = optionValue(application.arguments(), QStringLiteral("--guide-device"));
  bool gameModeRequest = application.arguments().contains(QStringLiteral("--game-mode"));
  bool gameModeExitRequest = application.arguments().contains(QStringLiteral("--game-mode-exit"));
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
  const bool renderMode = !screenshotPath.isEmpty() || !cardExportPath.isEmpty();
  const bool heroicOwnedFixture = renderMode && renderOverlay == QStringLiteral("heroic-owned");
  bool heroicOwnedDispatchComplete = false;
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
  const bool gameModeTest =
      application.arguments().contains(QStringLiteral("--game-mode-test"));
  const bool artworkEditorTest = application.arguments().contains(QStringLiteral("--artwork-editor-test"));
  const bool manualEditorTest = application.arguments().contains(QStringLiteral("--manual-editor-test"));
  const bool detailsDirectionTest =
      application.arguments().contains(QStringLiteral("--details-direction-test"));
  const bool smokeTest = gogSettingsTest || linkedPreferenceTest || backupEditorTest || bulkEditorTest || savedFilterTest || randomSelectionTest || staleSelectionTest || filterBackTest || gameModeTest || artworkEditorTest || manualEditorTest || application.arguments().contains(QStringLiteral("--smoke-test"));
  const bool couchNavigationTest =
      application.arguments().contains(QStringLiteral("--couch-navigation-test"));
  const bool couchNavigationContract = application.arguments().contains(QStringLiteral("--couch-navigation-contract"));
  const bool startupNavigationTest = application.arguments().contains(QStringLiteral("--startup-navigation-test"));
  const bool navigationTest = couchNavigationTest || couchNavigationContract || startupNavigationTest ||
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
  if (isolatedTest) {
    // Test runs drive SDL's virtual pad. Hide physical gamepads so a controller
    // plugged into a developer machine cannot take focus or change controller counts.
    qputenv("SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT", "0xffff/0xffff");
  }
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
  const QString gameModeStatePath =
      QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)) +
      QStringLiteral("/omakade/game-mode.json");
  if (application.arguments().contains(QStringLiteral("--game-mode-desktop"))) {
    if (SingleInstance::sendCommand({}, "game-mode desktop")) return EXIT_SUCCESS;
    qCritical() << "No running Game Mode session to return from.";
    return EXIT_FAILURE;
  }
  if (guideToggleRequest && SingleInstance::sendCommand({}, "guide toggle " + guideDevice.toUtf8())) return EXIT_SUCCESS;
  if (gameModeToggleRequest) {
    if (SingleInstance::sendCommand({}, "game-mode toggle " + guideDevice.toUtf8())) {
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
  if (startupNavigationTest && application.arguments().contains("--startup-grid")) {
    preferences.setCouchLibraryView(QStringLiteral("grid"));
  }
  const bool startInCouchMode = couchRequest || preferences.couchModeEnabled();
  ControllerInput controller;
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
  Rpcs3GameModel* rpcs3Library = nullptr;
  PpssppGameModel* ppssppLibrary = nullptr;
  RyujinxGameModel* ryujinxLibrary = nullptr;
  Shadps4GameModel* shadps4Library = nullptr;
  CemuGameModel* cemuLibrary = nullptr;
  MelondsGameModel* melondsLibrary = nullptr;
  XeniaGameModel* xeniaLibrary = nullptr;
  DolphinGameModel* dolphinLibrary = nullptr;
  BattleNetGameModel* battleNetLibrary = nullptr;
  QString libraryDatabasePath;
  std::unique_ptr<QTemporaryDir> consoleFixture;
  std::unique_ptr<QTemporaryDir> relocationFixtureDirectory;
  std::unique_ptr<QStandardItemModel> relocationFixture;
  if (demoMode || stressMode || navigationTest || detailsDirectionTest) {
    games =
        std::make_unique<MockGameModel>(nullptr, stressMode ? stressGameCount : 100,
                                        uninstalledLayoutTest, statsFixture || repairNavigationFixture || couchNavigationContract);
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
    consolePortals->addRomModel(melondsGames.get());
    consolePortals->addRomModel(rommGames.get());
    consolePortals->addRomModel(xeniaGames.get());
    consolePortals->addRomModel(pcsx2Games.get());
    consolePortals->addRomModel(rpcs3Games.get());
    consolePortals->addRomModel(ppssppGames.get());
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
      renderOverlay.startsWith(QStringLiteral("session-history")) ||
      statsFixture || heroicOwnedFixture ||
      renderOverlay == QStringLiteral("now-playing") || renderOverlay == "library-repair-controls") {
    if (!artworkFixture.isValid()) return EXIT_FAILURE;
    libraryDatabasePath = artworkFixture.filePath(QStringLiteral("library.sqlite"));
  }
  const QString heroicOwnedDispatchLog = artworkFixture.filePath("heroic-dispatch.log");
  if (heroicOwnedFixture) {
    // Use a real owned row and launcher, but confine external dispatch to a stub.
    const QString root = artworkFixture.filePath("heroic");
    const QString bin = artworkFixture.filePath("bin");
    if (!QDir().mkpath(root + "/store_cache") || !QDir().mkpath(bin)) return EXIT_FAILURE;
    QFile cache(root + "/store_cache/legendary_library.json");
    if (!cache.open(QIODevice::WriteOnly) || cache.write(
        R"({"library":[{"app_name":"owned-fixture","title":"Heroic Owned Fixture"}]})") < 0)
      return EXIT_FAILURE;
    cache.close();
    QFile stub(bin + "/heroic");
    if (!stub.open(QIODevice::WriteOnly) || stub.write(
        "#!/bin/sh\nprintf '%s\\n' \"$#\" \"$@\" >> \"$OMAKADE_HEROIC_DISPATCH_LOG\"\n/bin/sleep 1\n") < 0)
      return EXIT_FAILURE;
    stub.close();
    if (!stub.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
      return EXIT_FAILURE;
    qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
    qputenv("OMAKADE_HEROIC_DISPATCH_LOG", heroicOwnedDispatchLog.toUtf8());
    auto owned = std::make_unique<HeroicGameModel>(libraryDatabasePath);
    owned->refreshFromRoots({root});
    if (owned->rowCount() != 1) return EXIT_FAILURE;
    heroicLibrary = owned.get();
    games = std::move(owned);
    preferences.setHeroicEnabled(true);
    preferences.setCloseAfterLaunch(true);
  }
  if (statsFixture) {
    // Completion state and achievements have to exist before the library model is built, because
    // the model reads the organization table once when it loads. Without them the sections that
    // report progress would draw their empty states in the render check and prove nothing.
    QSqlDatabase fixture;
    if (!SessionDatabase::open(fixture, libraryDatabasePath,
                               QStringLiteral("omakade-stats-state")))
      return EXIT_FAILURE;
    QSqlQuery query(fixture);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS game_organization (source TEXT NOT NULL, runner TEXT NOT "
            "NULL, app_id TEXT NOT NULL, completion_status TEXT NOT NULL DEFAULT '', tags_json "
            "TEXT NOT NULL DEFAULT '[]', pinned INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source, "
            "runner, app_id))")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS achievements (app_id TEXT NOT NULL, api_name TEXT NOT "
            "NULL, title TEXT NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked "
            "INTEGER NOT NULL, unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER "
            "NOT NULL, current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT "
            "NOT NULL, PRIMARY KEY(app_id, api_name))")))
      return EXIT_FAILURE;
    for (const auto& completion : {std::make_tuple("Demo", "demo-1", "completed"),
                                   std::make_tuple("Demo", "demo-2", "playing"),
                                   std::make_tuple("Demo", "demo-3", "abandoned"),
                                   std::make_tuple("Steam", "demo-0", "completed")}) {
      if (!query.exec(QStringLiteral("INSERT OR REPLACE INTO game_organization"
                                     "(source, runner, app_id, completion_status) VALUES('%1', '', "
                                     "'%2', '%3')")
                          .arg(QString::fromUtf8(std::get<0>(completion)),
                               QString::fromUtf8(std::get<1>(completion)),
                               QString::fromUtf8(std::get<2>(completion)))))
        return EXIT_FAILURE;
    }
    // Achievements carry their own unlock times, so the period figures have something to select.
    const qint64 unlockedAt =
        QDateTime(QDate::currentDate().addDays(-2), QTime(20, 0)).toSecsSinceEpoch();
    for (const auto& achievement :
         {std::make_tuple("demo-1", "ACH_FIRST", "First Steps", 1, 4.7),
          std::make_tuple("demo-2", "ACH_LONG", "Long Haul", 1, 11.2),
          std::make_tuple("demo-3", "ACH_OPEN", "Still Untouched", 0, 0.0)}) {
      const QString statement =
          QStringLiteral("INSERT OR REPLACE INTO achievements(app_id, api_name, title, "
                         "description, icon_url, icon_path, unlocked, unlock_time, rarity, hidden, "
                         "current_progress, maximum_progress, source) VALUES('%1', '%2', '%3', '', "
                         "'', '', %4, %5, %6, 0, 0, 1, 'steam-local')")
              .arg(QString::fromUtf8(std::get<0>(achievement)),
                   QString::fromUtf8(std::get<1>(achievement)),
                   QString::fromUtf8(std::get<2>(achievement)))
              .arg(std::get<3>(achievement) != 0 ? 1 : 0)
              .arg(std::get<3>(achievement) != 0 ? unlockedAt : 0)
              .arg(std::get<4>(achievement));
      if (!query.exec(statement)) return EXIT_FAILURE;
    }
    // Genres and ratings reach the library through the metadata layer rather than from a source
    // model, so the sections that report them read these entries. The key is the source, a NUL,
    // the runner, a NUL, and the app id.
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS game_metadata (game_key TEXT PRIMARY KEY, payload TEXT "
            "NOT NULL)")))
      return EXIT_FAILURE;
    const auto metadataKey = [](const QString& source, const QString& appId) {
      return source + QChar::Null + QChar::Null + appId;
    };
    for (const auto& entry :
         {std::make_tuple("Steam", "demo-0",
                          R"({"genres":["Platformer"],"rating":90,"ratingCount":210})"),
          std::make_tuple("Demo", "demo-1",
                          R"({"genres":["Action","Adventure"],"rating":86,"ratingCount":120})"),
          std::make_tuple("Demo", "demo-2",
                          R"({"genres":["Role Playing"],"rating":92,"ratingCount":340})"),
          std::make_tuple("Demo", "demo-3",
                          R"({"genres":["Shooter"],"rating":74,"ratingCount":58})")}) {
      QSqlQuery metadata(fixture);
      metadata.prepare(QStringLiteral(
          "INSERT OR REPLACE INTO game_metadata(game_key, payload) VALUES(?, ?)"));
      metadata.addBindValue(metadataKey(QString::fromUtf8(std::get<0>(entry)),
                                        QString::fromUtf8(std::get<1>(entry))));
      metadata.addBindValue(QString::fromUtf8(std::get<2>(entry)));
      if (!metadata.exec()) return EXIT_FAILURE;
    }
    fixture.close();
    fixture = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("omakade-stats-state"));
  }
  ManualGameModel manualGames(libraryDatabasePath.isEmpty() ? QStringLiteral(":memory:") : libraryDatabasePath);
  UnifiedGameModel unifiedGames(libraryDatabasePath);
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
    unifiedGames.setSourceEnabled(QStringLiteral("RPCS3"), preferences.rpcs3Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("PPSSPP"), preferences.ppssppEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Ryujinx"), preferences.ryujinxEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("shadPS4"), preferences.shadps4Enabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Cemu"), preferences.cemuEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("melonDS"), preferences.melondsEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("RomM"), preferences.rommEnabled());
    unifiedGames.setSourceEnabled(QStringLiteral("Xenia"), preferences.xeniaEnabled());
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
      } else if (key.source.compare(QStringLiteral("RomM"), Qt::CaseInsensitive) == 0 && rommGames && preferences.rommEnabled()) {
        rommGames->refresh();
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
  if (uninstalledLayoutTest || heroicOwnedFixture) {
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
          renderOverlay == "library-repair-controls" || statsFixture
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
  libraryRepair.setLauncher(&launcher);
  libraryRepair.setSaveBackups(&saveBackups);
  // Stopping a running game (issue #53). The rows come from the unified model, not the
  // filtered view, so a filter cannot hide a running game from the global action.
  GameStopService gameStop;
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
  // The figures the Stats screen shows. It reads the same model the library does, so its
  // playtime can never disagree with what a game's card says, and it computes nothing until
  // the screen is open.
  PlayStats stats(&unifiedGames, libraryDatabasePath);
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
  if (cardExportPath.isEmpty()) {
    qInfo() << "Card export: interactive, the app keeps running after a save";
  } else {
    QObject::connect(&cardExport, &CardExport::exportReported, &application,
                     [&application](bool written) {
                       application.exit(written ? EXIT_SUCCESS : EXIT_FAILURE);
                     }, Qt::QueuedConnection);
  }
  // Declared before the engine so the interface never outlives it. Test and render runs
  // get a Game Mode that cannot reach the desktop or the user's files.
  HyprlandGameModeCompositor gameModeCompositor;
  PactlGameModeAudio gameModeAudio;
  OmarchyGameModeNotifications gameModeNotifications;
  LibraryGameModeTestCompositor gameModeTestCompositor;
  QTemporaryDir gameModeFixture;
  GameModeSession gameMode(
      gameModeTest   ? static_cast<GameModeCompositor*>(&gameModeTestCompositor)
      : isolatedTest ? nullptr
                     : &gameModeCompositor,
      isolatedTest ? nullptr : &gameModeAudio, isolatedTest ? nullptr : &gameModeNotifications,
      isolatedTest ? gameModeFixture.filePath(QStringLiteral("game-mode.json"))
                   : configRoot + QStringLiteral("/omakade/game-mode.json"),
      isolatedTest ? gameModeFixture.filePath(QStringLiteral("game-mode-state.json"))
                   : gameModeStatePath);
  // Omarchy keeps personal key bindings in this file and provides the `o.bind` helper the
  // Game Mode binding is written with.
  const QString omarchyPath = qEnvironmentVariable("OMARCHY_PATH");
  const bool onOmarchy =
      QFileInfo(omarchyPath.isEmpty() ? QStringLiteral("/usr/share/omarchy") : omarchyPath)
          .isDir() ||
      QFileInfo(QDir::homePath() + QStringLiteral("/.local/share/omarchy")).isDir();
  GameModeShortcut gameModeShortcut(
      isolatedTest ? QString{} : configRoot + QStringLiteral("/hypr/bindings.lua"), onOmarchy);
  GameModeGuideButton gameModeGuideButton(!isolatedTest);
  GameModeOverlay gameModeOverlay;
  InGameGuide inGameGuide(playSessionStore.get(), &unifiedGames, &gameMode, &gameModeCompositor, &launcher,
                          !isolatedTest && onOmarchy);
  inGameGuide.setInjectedInputEnabled(application.arguments().contains(QStringLiteral("--guide-input-test")));
  inGameGuide.setAchievementDatabase(achievementDatabasePath);
  if (inGameGuide.available()) {
    const auto guidePluginPaths = GuidePlugin::defaultPaths(
        configRoot,
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)),
        QCoreApplication::applicationDirPath());
    inGameGuide.setPluginPaths(guidePluginPaths);
    if (!guideToggleRequest && !gameModeToggleRequest && launcher.trackedGames().isEmpty() &&
        (!playSessionStore || playSessionStore->nowPlaying().isEmpty()))
      GuidePlugin::ensureAsync(guidePluginPaths, &inGameGuide);
    inGameGuide.prepare();
  }
  // Without the guide plugin the shortcut keeps its Game Mode behavior.
  const bool coldGuideRequest = guideToggleRequest ||
                                (gameModeToggleRequest && inGameGuide.usable() && inGameGuide.hasGame());
  if (coldGuideRequest) gameModeRequest = false;
  QQmlApplicationEngine engine;
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
  engine.rootContext()->setContextProperty(QStringLiteral("Rpcs3Library"), rpcs3Library);
  engine.rootContext()->setContextProperty(QStringLiteral("PpssppLibrary"), ppssppLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RyujinxLibrary"), ryujinxLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Shadps4Library"), shadps4Library);
  engine.rootContext()->setContextProperty(QStringLiteral("CemuLibrary"), cemuLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("MelondsLibrary"), melondsLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("RommLibrary"), rommGames.get());
  engine.rootContext()->setContextProperty(QStringLiteral("XeniaLibrary"), xeniaLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("DolphinLibrary"), dolphinLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("BattleNetLibrary"), battleNetLibrary);
  engine.rootContext()->setContextProperty(QStringLiteral("Launcher"), &launcher);
  engine.rootContext()->setContextProperty(QStringLiteral("SaveProtection"), &saveProtection);
  engine.rootContext()->setContextProperty(QStringLiteral("LibraryRepair"), &libraryRepair);
  engine.rootContext()->setContextProperty(QStringLiteral("GameStop"), &gameStop);
  engine.rootContext()->setContextProperty(QStringLiteral("Preferences"), &preferences);
  if (renderOverlay.startsWith("settings-recorder-")) {
    playSessionStore = std::make_unique<PlaySessionStore>(QStringLiteral(":memory:"));
    preferences.setTrackPlaySessions(renderOverlay.endsWith("on"));
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  if (renderOverlay.startsWith(QStringLiteral("session-history"))) {
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
    if (renderOverlay == QStringLiteral("session-history-pages")) {
      for (int i = 0; i < 10; ++i) {
        const auto start = now - 86400 * (i + 1);
        const auto id = SessionDatabase::beginSession(database, "/games/demo-0.nes", "RetroArch", start, 12, 12);
        if (id <= 0 || !SessionDatabase::endSession(database, id, start + 60, 60)) return EXIT_FAILURE;
      }
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  if (statsFixture) {
    // A spread of recorded sessions so every section of the stats screen has something real to
    // draw in a render check: more than one game and source, days apart, different hours, one
    // long session and one short one, and a return after a gap.
    const QString connection = QStringLiteral("omakade-stats-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    struct StatsFixture {
      const char* path;
      const char* source;
      int daysAgo;
      int hour;
      qint64 seconds;
    };
    const StatsFixture fixtures[] = {
        {"/games/demo-0.nes", "RetroArch", 0, 21, 5400},
        {"/games/demo-1.sfc", "RetroArch", 0, 22, 1800},
        {"/games/demo-0.nes", "RetroArch", 1, 20, 2700},
        {"/games/demo-2.iso", "PCSX2", 2, 19, 7200},
        {"/games/demo-0.nes", "RetroArch", 3, 23, 900},
        {"/games/demo-2.iso", "PCSX2", 12, 18, 3600},
    };
    for (const StatsFixture& fixture : fixtures) {
      if (renderOverlay.endsWith(QStringLiteral("empty"))) break;
      const qint64 start =
          QDateTime(QDate::currentDate().addDays(-fixture.daysAgo), QTime(fixture.hour, 0))
              .toSecsSinceEpoch();
      const qint64 id = SessionDatabase::beginSession(
          database, QString::fromLatin1(fixture.path), QString::fromLatin1(fixture.source), start,
          10, 10);
      if (id <= 0 ||
          !SessionDatabase::endSession(database, id, start + fixture.seconds +
                                    (renderOverlay.endsWith("paused") ? 7200 : 0), fixture.seconds))
        return EXIT_FAILURE;
    }
    if (renderOverlay.endsWith(QStringLiteral("error"))) {
      QSqlQuery broken(database);
      if (!broken.exec(QStringLiteral("DROP TABLE play_sessions"))) return EXIT_FAILURE;
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    // The error fixture must retain its missing required table.
    if (!renderOverlay.endsWith(QStringLiteral("error"))) {
      playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
      playSessionStore->setEnabled(preferences.trackPlaySessions());
    }
  }
  if (renderOverlay == QStringLiteral("now-playing")) {
    // A live session for the Now Playing render, backed by a real process so the
    // view exercises the same liveness check it uses in normal runs. The stand-in
    // ignores SIGTERM, exactly like an emulator that only closes when it is forced.
    const QString connection = QStringLiteral("omakade-now-playing-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    auto* fixtureGame = new QProcess(&application);
    fixtureGame->start(QStringLiteral("/bin/sh"),
                       {QStringLiteral("-c"), QStringLiteral("trap '' TERM; sleep 300")});
    if (!fixtureGame->waitForStarted(5000)) return EXIT_FAILURE;
    qint64 procStart = -1;
    for (const ProcessSnapshot& snapshot : ProcFs::listProcesses()) {
      if (snapshot.pid == fixtureGame->processId()) {
        procStart = snapshot.procStart;
        break;
      }
    }
    const bool recorded =
        procStart >= 0 &&
        SessionDatabase::beginSession(
            database, QStringLiteral("/data/Emulation/Games/Xbox/Dante's Inferno (USA)/default.xex"),
            QStringLiteral("Xenia"), QDateTime::currentSecsSinceEpoch() - 754,
            fixtureGame->processId(), procStart) > 0;
    nowPlayingFixturePid = fixtureGame->processId();
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    if (!recorded) return EXIT_FAILURE;
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
  engine.rootContext()->setContextProperty(QStringLiteral("GameMode"), &gameMode);
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeOverlay"), &gameModeOverlay);
  engine.rootContext()->setContextProperty(QStringLiteral("InGameGuide"), &inGameGuide);
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeShortcut"), &gameModeShortcut);
  engine.rootContext()->setContextProperty(QStringLiteral("GameModeGuideButton"),
                                           &gameModeGuideButton);
  engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"),
                                           (demoMode || stressMode) && !ownedLayoutTest && !heroicOwnedFixture);
  engine.rootContext()->setContextProperty(
      QStringLiteral("StatsFixtureView"),
      renderOverlay == QStringLiteral("stats-patterns") ? 1
      : renderOverlay == QStringLiteral("stats-library") ? 2 : 0);
  engine.rootContext()->setContextProperty(QStringLiteral("StartupMilliseconds"),
                                           startupTimer.elapsed());
  engine.rootContext()->setContextProperty(QStringLiteral("AppVersion"),
                                           QCoreApplication::applicationVersion());
  engine.rootContext()->setContextProperty(QStringLiteral("OwnedGameCountOverride"),
                                           ownedLayoutTest ? 250 : 0);
  engine.rootContext()->setContextProperty(QStringLiteral("CouchModeRequested"),
                                           startInCouchMode);
  engine.rootContext()->setContextProperty(QStringLiteral("ColdGameModeRequested"),
                                           gameModeRequest || coldGuideRequest);
  engine.rootContext()->setContextProperty(
      QStringLiteral("CouchLibraryViewOverride"),
      renderOverlay.startsWith(QStringLiteral("couch-grid")) ? QStringLiteral("grid") : QString{});

  if (startupNavigationTest && (application.arguments().contains("--startup-empty") ||
                                application.arguments().contains("--startup-delayed"))) {
    library.setSearchText(QStringLiteral("no-matching-startup-game"));
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
  if (rootWindow != nullptr) {
    new ControllerFocusGuard(&controller, rootWindow);
    // Hold the compositor's idle timer while a launched game runs, since controller input alone
    // never resets it and most emulators do not inhibit for themselves.
    auto* idleInhibitor = new IdleInhibitor(rootWindow, rootWindow);
    // Game Mode holds it for the whole session: browsing with a controller must not let
    // the television blank either.
    const auto syncIdleInhibitor = [&launcher, &gameMode, idleInhibitor] {
      idleInhibitor->setInhibited(gameMode.active() || (launcher.gameRunning() && !gameMode.parked()));
    };
    QObject::connect(&launcher, &GameLauncher::gameRunningChanged, idleInhibitor,
                     syncIdleInhibitor);
    QObject::connect(&gameMode, &GameModeSession::stateChanged, idleInhibitor,
                     syncIdleInhibitor);
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
  if (rootWindow != nullptr && startInCouchMode && !gameModeRequest && !coldGuideRequest && !renderMode && (!navigationTest || startupNavigationTest) && !smokeTest) {
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
  if (rootWindow != nullptr && !gameModeRequest && !coldGuideRequest && !renderMode && (!navigationTest || startupNavigationTest)) {
    const auto activateWindow = [rootWindow, &gameMode] {
      if (gameMode.hasSession() || gameMode.busy()) return;
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
#ifdef OMAKADE_PLATFORM_INPUT_TESTS
                QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
                    quickWindow, QEvent::KeyPress, key, modifiers);
                QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
                    quickWindow, QEvent::KeyRelease, key, modifiers);
#else
                auto* shortcut = quickWindow->findChild<QObject*>(
                    forward ? "navigationTabForward" : "navigationTabBackward");
                if (!shortcut || !shortcut->property("enabled").toBool() ||
                    !QMetaObject::invokeMethod(shortcut, "activated")) {
                  qCritical() << "Relocation dialog Tab shortcut unavailable" << key << modifiers;
                  application.exit(EXIT_FAILURE);
                  return;
                }
#endif
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
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Enter,Qt::NoModifier);
                QCoreApplication::sendEvent(quickWindow,&enter);
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
            auto* library = findVisualItem(quickWindow->contentItem(), "homeLibraryButton");
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
            auto* couchHomeButton = quickWindow->findChild<QQuickItem*>("homeLibraryButton");
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
          if (home.recent().size() > 1 && !home.queue().isEmpty()) {
            const QVariantMap card = home.recent().at(1).toMap();
            const QString identity = card.value("identity").toString();
            QMetaObject::invokeMethod(screen, "focusIdentity", Q_ARG(QVariant, identity));
            auto* upNext = findVisualItem(quickWindow->contentItem(),
                                          QStringLiteral("homeTileAction-") + identity);
            controller.focusDirectionRequested(Qt::Key_Down);
            if (!upNext || quickWindow->activeFocusItem() != upNext || !upNext->isVisible()) {
              qCritical() << "Down from a Home card did not reveal its Up Next action";
              application.exit(EXIT_FAILURE); return;
            }
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
              qCritical() << "Down from Up Next did not continue to the following shelf";
              application.exit(EXIT_FAILURE); return;
            }
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
          if (renderOverlay != "game-info-empty" && renderOverlay != "game-info-rom-only") {
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
            QKeyEvent pageDown(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
            QCoreApplication::sendEvent(quickWindow, &pageDown);
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
        auto* homeScreen = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeScreen"));
        auto* homeAppHeader = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeAppHeader"));
        auto* couchHomeLibrary = quickWindow->findChild<QQuickItem*>(QStringLiteral("homeLibraryButton"));
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
          // Header navigation follows the visible rows, rather than wrapping Right
          // into a different row. Reach Settings vertically, then along the header.
          sendKey(Qt::Key_Up);
          auto* home = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchHomeButton"));
          if (home && home->hasActiveFocus()) sendKey(Qt::Key_Right);
          auto* stats = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchStatsButton"));
          auto* desktop = quickWindow->findChild<QQuickItem*>(QStringLiteral("couchDesktopButton"));
          if (stats && stats->hasActiveFocus()) sendKey(Qt::Key_Left);
          if (desktop && desktop->hasActiveFocus()) sendKey(Qt::Key_Left);
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
            const auto expectedColumns = [content](bool running) {
              const qreal width = content->width();
              return width < 300 ? 1 : width < 620 ? 2
                   : width < 1140 && running ? 3 : width < 1140 ? 4 : running ? 5 : 4;
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
    } else if (navigationTest && !couchNavigationContract && !startupNavigationTest) {
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
              controller.keyRequested(towardSearch, Qt::NoModifier);
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
                if (keyboard) controller.keyRequested(direction, Qt::NoModifier);
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
                  if (keyboard) controller.keyRequested(Qt::Key_Up, Qt::NoModifier);
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
              auto* repairInFilters = item("libraryRepairButton");
              if (repairInFilters && repairInFilters->isVisible()) {
                fail("Repair Library remained in the Filters popup"); return;
              }
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
                if (!returned || !visited.contains("libraryRepairButton") ||
                    !visited.contains("bulkOrganizationButton") || !visited.contains("savedFiltersButton")
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
                        const auto expectedColumns = [detailsContent](bool running) {
                          const qreal width = detailsContent->width();
                          return width < 300 ? 1 : width < 620 ? 2
                               : width < 1140 && running ? 3 : width < 1140 ? 4 : running ? 5 : 4;
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
  QObject::connect(&singleInstance, &SingleInstance::gameModeToggleRequested, &gameMode,
                   [&gameMode, &inGameGuide, rootWindow](const QString& node) {
                     if (inGameGuide.opened() || (inGameGuide.usable() && inGameGuide.hasGame())) { inGameGuide.toggle(node, true); return; }
                     // The shortcut parks or resumes the complete library session.
                     if (rootWindow == nullptr ||
                         !QMetaObject::invokeMethod(rootWindow, "toggleGameMode")) {
                       gameMode.toggle();
                     }
                   });
  // The shell was unreachable when the guide was requested: do what the shortcut did before the guide.
  QObject::connect(&inGameGuide, &InGameGuide::summonFailed, &gameMode, [&gameMode, rootWindow] {
    if (rootWindow == nullptr || !QMetaObject::invokeMethod(rootWindow, "toggleGameMode")) gameMode.toggle();
  });
  QObject::connect(&singleInstance, &SingleInstance::guideToggleRequested, &inGameGuide,
                   [&inGameGuide](const QString& node) { inGameGuide.toggle(node, true); });
  QObject::connect(&inGameGuide, &InGameGuide::libraryRequested, &application, [rootWindow, &gameModeCompositor, &application] {
    if (!rootWindow) return;
    rootWindow->show(); rootWindow->requestActivate();
    QTimer::singleShot(150, &application, [&gameModeCompositor] {
      const auto window = gameModeCompositor.windowForPid(QCoreApplication::applicationPid());
      if (window.valid()) gameModeCompositor.focusWindow(window.address);
    });
  });
  if (coldGuideRequest) QTimer::singleShot(0, &inGameGuide, [&inGameGuide, guideDevice] { inGameGuide.toggle(guideDevice, true); });
  if (coldGuideRequest) application.setQuitOnLastWindowClosed(false);
  gameMode.setTemporaryWindow(gameModeRequest);
  if (rootWindow != nullptr) {
    const auto windowStateBeforePreparation =
        std::make_shared<Qt::WindowState>(rootWindow->windowState());
    QObject::connect(&gameMode, &GameModeSession::windowVisibilityRequested, rootWindow,
                     [rootWindow](bool visible) {
                       // The controller snapshots desktop focus before asking to map
                       // a cold root. Request its native mode while it is still hidden.
                       if (visible) rootWindow->setWindowState(Qt::WindowFullScreen);
                       rootWindow->setVisible(visible);
                     });
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
  if (rpcs3Library != nullptr &&
      (preferences.rpcs3Enabled() || preferences.rpcs3AutoEnabled())) {
    QTimer::singleShot(675, rpcs3Library, &Rpcs3GameModel::refresh);
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
    QTimer::singleShot(690, ppssppLibrary, &PpssppGameModel::refresh);
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
    QTimer::singleShot(750, melondsLibrary, &MelondsGameModel::refresh);
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
    QTimer::singleShot(745, xeniaLibrary, &XeniaGameModel::refresh);
    QObject::connect(xeniaLibrary, &XeniaGameModel::statusChanged, xeniaLibrary,
                     [&preferences, xeniaLibrary] {
                       if (xeniaLibrary->xeniaDetected() && preferences.xeniaAutoEnabled()) {
                         preferences.setXeniaAutoEnabled(false);
                         preferences.setXeniaEnabled(true);
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
        auto* flickable = scroll->property("contentItem").value<QObject*>();
        if (!flickable) { fail("Backup preview viewport is missing"); return; }
        // Preview data can arrive before the ScrollView has laid out its text.
        // Wait for an overflowing viewport before testing the controller gesture;
        // the existing fixture deadline still bounds this readiness check.
        if (!rootWindow->isActive() || !scroll->isVisible() ||
            flickable->property("height").toReal() <= 0 ||
            flickable->property("contentHeight").toReal() <=
                flickable->property("height").toReal()) {
          return;
        }
        text->forceActiveFocus();
        controller.focusDirectionRequested(Qt::Key_Down);
        if (flickable->property("contentY").toReal() <= 0) { fail("Controller could not scroll the backup preview"); return; }
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
  } else if (gameModeTest) {
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
      QObject::disconnect(mappingStateCheck);
      rootWindow->setWindowState(originalState);
      gameModeTestCompositor.mapped.store(true);
      rootWindow->show();
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
        fail(QStringLiteral("Game Mode did not open Couch Mode"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "toggleCouchMode");
      auto* leave = rootWindow->findChild<QQuickItem*>(QStringLiteral("gameModeLeaveButton"));
      auto* safeBack = rootWindow->findChild<QQuickItem*>(QStringLiteral("gameModeBackButton"));
      if (leave == nullptr || safeBack == nullptr || !settled([safeBack] { return safeBack->hasActiveFocus(); })) {
        fail(QStringLiteral("Switching modes in Game Mode did not focus Back to Library"));
        return;
      }
      QMetaObject::invokeMethod(rootWindow, "setCouchMode", Q_ARG(QVariant, QVariant(false)));
      QCoreApplication::processEvents();
      if (!rootWindow->property("couchMode").toBool() || !gameMode.active()) {
        fail(QStringLiteral("Switching to desktop left Couch Mode while Game Mode was on"));
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
      controller.keyRequested(Qt::Key_Down, Qt::NoModifier);
      if (!overlayLeave->hasActiveFocus()) {
        fail(QStringLiteral("Controller Down did not reach Leave on the Game Mode overlay"));
        return;
      }
      controller.keyRequested(Qt::Key_Up, Qt::NoModifier);
      if (!overlayBack->hasActiveFocus()) {
        fail(QStringLiteral("Controller Up did not return to Back to Game"));
        return;
      }
      controller.keyRequested(Qt::Key_Escape, Qt::NoModifier);
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
        if (!settled([&gameMode] { return gameMode.parked() && !gameMode.busy(); }) ||
            !rootWindow->property("homeOpen").toBool() ||
            rootWindow->property("diagnosticsOpen").toBool() != settingsPage) {
          fail(QStringLiteral("Park lost the retained Home or Settings page"));
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
          if (!settled([&gameMode] { return gameMode.active() && !gameMode.busy(); })) {
            fail(QStringLiteral("Couch surface Game Mode session did not start"));
            return;
          }
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
      application.quit();
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
      const QString imageUrl = QUrl::fromLocalFile(path).toString();
      const auto waitForImageStatus = [](QQuickItem* image, int status) {
        if (!image) return false;
        QElapsedTimer timer;
        timer.start();
        while (image->property("status").toInt() != status && timer.elapsed() < 1500) {
          QEventLoop events;
          QTimer::singleShot(10, &events, &QEventLoop::quit);
          events.exec();
        }
        return image->property("status").toInt() == status;
      };
      for (const QString& kind : {QStringLiteral("cover"), QStringLiteral("hero"), QStringLiteral("logo")}) {
        auto* field = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPath_") + kind);
        auto* preview = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPathPreview_") + kind);
        if (!field || !preview) {
          if (kind == QStringLiteral("logo") && !rootWindow->property("couchMode").toBool())
            continue;
          fail("Artwork preview is missing beside its path field"); return;
        }
        field->setProperty("text", imageUrl);
        if (!waitForImageStatus(preview, 1) || !preview->isVisible()) {
          fail("Artwork path did not update its live preview"); return;
        }
      }
      auto* coverField = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPath_cover"));
      auto* coverPreview = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPathPreview_cover"));
      auto* coverFallback = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkEffectivePreview_cover"));
      auto* coverMessage = findVisualItem(qobject_cast<QQuickWindow*>(rootWindow)->contentItem(), QStringLiteral("artworkPreviewMessage_cover"));
      if (!coverField || !coverPreview || !coverFallback || !coverMessage) {
        fail("Cover preview status controls are missing"); return;
      }
      coverField->setProperty("text", QUrl::fromLocalFile(
          artworkFixture.filePath(QStringLiteral("missing.png"))).toString());
      if (!waitForImageStatus(coverPreview, 3) || !coverFallback->isVisible() ||
          !coverMessage->property("text").toString().contains(QStringLiteral("CAN'T LOAD"),
                                                                  Qt::CaseInsensitive)) {
        fail("An unreadable artwork path did not show its fallback and warning"); return;
      }
      coverField->setProperty("text", imageUrl);
      if (!waitForImageStatus(coverPreview, 1)) {
        fail("Artwork preview did not recover when a valid path was restored"); return;
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

  const int result = application.exec();
  if (heroicOwnedFixture && !heroicOwnedDispatchComplete) {
    qCritical() << "Owned Heroic action quit before dispatch verification completed";
    return EXIT_FAILURE;
  }
  return result;
}

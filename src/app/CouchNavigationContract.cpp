#include "app/CouchNavigationContract.h"
#include "input/ControllerInput.h"
#include "input/KeyDelivery.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <SDL3/SDL.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
void settle(int ms = 20) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

bool until(const std::function<bool()>& ready, int timeout = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout) settle();
  return ready();
}

QString name(QQuickItem* item) {
  return item ? item->objectName() : QStringLiteral("no focus");
}

bool within(QQuickItem* item, QQuickItem* container) {
  for (; item; item = item->parentItem()) if (item == container) return true;
  return false;
}

class VirtualPad {
public:
  explicit VirtualPad(ControllerInput& controller, bool startController = true) {
    const int previousCount = controller.controllerCount();
    if (startController) controller.start();
    require(until([] { return SDL_WasInit(SDL_INIT_GAMEPAD) != 0; }),
            QStringLiteral("SDL gamepad initialization failed"));
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Omakade navigation regression controller";
    // Isolated test runs only allow this VID/PID, so physical pads stay out of the test.
    desc.vendor_id = 0xffff;
    desc.product_id = 0xffff;
    id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, QString::fromUtf8(SDL_GetError()));
    joystick = SDL_OpenJoystick(id);
    require(joystick != nullptr, QString::fromUtf8(SDL_GetError()));
    require(until([&controller, previousCount] { return controller.controllerCount() == previousCount + 1; }),
            QStringLiteral("Production ControllerInput did not discover the virtual gamepad"));
  }
  ~VirtualPad() {
    if (joystick) SDL_CloseJoystick(joystick);
    if (id) SDL_DetachVirtualJoystick(id);
  }
  void button(SDL_GamepadButton button, int holdMs = 20) {
    require(SDL_SetJoystickVirtualButton(joystick, button, true), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle(holdMs);
    require(SDL_SetJoystickVirtualButton(joystick, button, false), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
  }
  void direction(int key, bool analog) {
    if (!analog) {
      button(key == Qt::Key_Up ? SDL_GAMEPAD_BUTTON_DPAD_UP
             : key == Qt::Key_Down ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
             : key == Qt::Key_Left ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
      return;
    }
    const int axis = key == Qt::Key_Left || key == Qt::Key_Right
                         ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_LEFTY;
    const Sint16 value = key == Qt::Key_Left || key == Qt::Key_Up ? -30000 : 30000;
    require(SDL_SetJoystickVirtualAxis(joystick, axis, value), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
    require(SDL_SetJoystickVirtualAxis(joystick, axis, 0), QString::fromUtf8(SDL_GetError()));
    SDL_UpdateJoysticks();
    settle();
  }
private:
  SDL_JoystickID id = 0;
  SDL_Joystick* joystick = nullptr;
};

void keyboard(QQuickWindow* window, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  deliverKey(window, key, mods);
  settle();
}
} // namespace

bool runStartupNavigationContract(QQuickWindow* window, ControllerInput& controller) {
  try {
    // Do not request activation or assign focus here. Startup must do both itself.
    require(until([&] { return window->isActive() && controller.inputEnabled(); }),
            QStringLiteral("Cold launch did not acquire window input ownership"));
    const bool couch = window->property("couchMode").toBool();
    const auto arguments = QCoreApplication::arguments();
    const bool grid = arguments.contains("--startup-grid");
    const bool empty = arguments.contains("--startup-empty") || arguments.contains("--startup-delayed");
    auto* games = window->findChild<QQuickItem*>(couch ? (grid ? "couchGameGrid" : "couchGameStrip") : "libraryGrid");
    auto* initial = empty ? window->findChild<QQuickItem*>(couch ? "couchSettingsButton" : "emptyClearButton") : games;
    require(initial && initial->isVisible() && initial->isEnabled() && initial->hasActiveFocus(),
            QStringLiteral("Cold launch did not focus a usable destination: focus=%1")
                .arg(name(window->activeFocusItem())));
    // This fixture may attach a controller, but it must not start production input.
    // The same first-frame startup connection used by a normal launch must do that.
    require(until([] { return SDL_WasInit(SDL_INIT_GAMEPAD) != 0; }),
            QStringLiteral("Cold launch did not start controller discovery"));
    std::unique_ptr<VirtualPad> pad;
    if (arguments.contains("--startup-controller")) pad = std::make_unique<VirtualPad>(controller, false);
    require(initial->hasActiveFocus(), QStringLiteral("Startup controller discovery lost initial focus"));
    const auto send = [&](int key) {
      if (!pad) keyboard(window, key);
      else if (key == Qt::Key_Return) pad->button(SDL_GAMEPAD_BUTTON_SOUTH);
      else pad->direction(key, false);
    };
    if (!empty) {
      const int before = games->property("currentIndex").toInt();
      send(Qt::Key_Right);
      require(games->property("currentIndex").toInt() == before + 1,
              QStringLiteral("First startup arrow did not select the next game"));
      send(Qt::Key_Return);
      require(window->property("detailOpen").toBool(),
              QStringLiteral("Cold launch could not open the selected game without a mouse"));
    } else if (couch) {
      send(Qt::Key_Left);
      require(name(window->activeFocusItem()) == "couchHomeButton",
              QStringLiteral("First empty-library arrow did not reach Home"));
    } else {
      send(Qt::Key_Return);
      require(until([&] { return games->property("count").toInt() > 0 && games->hasActiveFocus(); }),
              QStringLiteral("Cold launch could not clear filters without a mouse"));
    }
    if (arguments.contains("--startup-delayed") && couch) {
      QObject* library = qmlContext(window)->contextProperty("Library").value<QObject*>();
      library->setProperty("searchText", QString());
      settle();
      require(name(window->activeFocusItem()) == "couchHomeButton",
              QStringLiteral("Delayed results stole the first-input destination"));
      auto* home = window->activeFocusItem();
      send(Qt::Key_Down);
      require(window->activeFocusItem() && window->activeFocusItem() != home
                  && window->activeFocusItem()->isVisible() && window->activeFocusItem()->isEnabled(),
              QStringLiteral("Navigation stopped after delayed results arrived"));
    }
    qInfo() << "Cold launch navigation passed without mouse input or test-assigned focus";
    return true;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Cold launch navigation failed:" << error.what();
    return false;
  }
}

bool runCouchNavigationContract(QQuickWindow* window, ControllerInput& controller) {
  try {
    const auto item = [window](const char* objectName) {
      auto* result = window->findChild<QQuickItem*>(QString::fromLatin1(objectName));
      require(result != nullptr, QStringLiteral("Missing %1").arg(QString::fromLatin1(objectName)));
      return result;
    };
    auto* couch = item("couchLibrary");
    auto* grid = item("couchGameGrid");
    auto* strip = item("couchGameStrip");
    // Views are focus scopes: Qt may focus their current delegate while the view
    // owns navigation. Treat either representation as the same destination.
    const auto focused = [&] {
      if (grid->isVisible() && grid->hasActiveFocus()) return grid;
      if (strip->isVisible() && strip->hasActiveFocus()) return strip;
      return window->activeFocusItem();
    };
    QObject* preferences = qmlContext(window)->contextProperty("Preferences").value<QObject*>();
    QObject* library = qmlContext(window)->contextProperty("Library").value<QObject*>();
    require(preferences && library, QStringLiteral("Missing isolated library fixture"));
    window->setProperty("homeOpen", false);
    window->requestActivate();
    require(until([&] { return window->isActive() && controller.inputEnabled(); }),
            QStringLiteral("Test window never acquired input ownership"));
    VirtualPad pad(controller);
    const QList<QQuickItem*> headers = {item("couchHomeButton"), item("couchSettingsButton"),
                                       item("couchDesktopButton"), item("couchStatsButton")};
    const QList<QQuickItem*> filters = {item("couchConsoleButton"), item("couchShowButton"),
        item("couchSourceButton"), item("couchSortButton"), item("couchConsoleViewButton"),
        item("couchLayoutButton"), item("couchSearchButton"), item("couchFiltersButton")};
    const QList<QQuickItem*> actions = {item("couchViewButton"), item("couchFavoriteButton")};
    item("couchShowButton")->forceActiveFocus();
    require(item("couchShowButton")->hasActiveFocus(),
            QStringLiteral("Cannot focus SHOW: couch visible=%1 enabled=%2, mode=%3 home=%4 stats=%5 details=%6 focus=%7")
                .arg(couch->isVisible()).arg(couch->isEnabled()).arg(window->property("couchMode").toBool())
                .arg(window->property("homeOpen").toBool()).arg(window->property("statsOpen").toBool())
                .arg(window->property("detailOpen").toBool()).arg(name(window->activeFocusItem())));
    pad.direction(Qt::Key_Up, false);
    require(headers.contains(window->activeFocusItem()),
            QStringLiteral("D-pad Up from SHOW does not reach the header; focus=%1").arg(name(window->activeFocusItem())));
    {
      auto* source = item("couchSourceButton");
      source->forceActiveFocus();
      VirtualPad secondPad(controller);
      settle();
      require(source->hasActiveFocus(), QStringLiteral("Controller discovery stole toolbar focus"));
      secondPad.direction(Qt::Key_Up, false);
      require(headers.contains(window->activeFocusItem()), QStringLiteral("Controller switching undid navigation"));
      source->forceActiveFocus();
      pad.direction(Qt::Key_Up, false);
      require(headers.contains(window->activeFocusItem()), QStringLiteral("Returning to the first controller lost navigation"));
    }
    require(until([&] { return controller.controllerCount() == 1; }), QStringLiteral("Virtual controller disconnect was not handled"));
    const auto center = [couch](QQuickItem* control) {
      return control->mapToItem(couch, QPointF(control->width() / 2, control->height() / 2));
    };
    int assertions = 0;
    for (const QString& layout : {QStringLiteral("detail"), QStringLiteral("grid")}) {
      preferences->setProperty("couchLibraryView", layout);
      library->setProperty("expandConsoles", true);
      library->setProperty("mode", 0);
      library->setProperty("sourceFilter", QString());
      settle();
      auto* games = layout == "grid" ? grid : strip;
      for (int state = 0; state < 3; ++state) {
        library->setProperty("mode", state);
        library->setProperty("sourceFilter", state == 1 ? QStringLiteral("Demo") : QString());
        library->setProperty("consoleFilter", state == 1 ? QStringLiteral("nes") : QString());
        library->setProperty("searchText", state == 2 ? QStringLiteral("no-matching-navigation-contract-game") : QString());
        settle(50);
        require(filters.first()->isVisible() == (state == 1), QStringLiteral("Console-return fixture is missing"));
        require((games->property("count").toInt() == 0) == (state == 2), QStringLiteral("Unexpected fixture game count"));
        // Group the actual rendered controls into rows, then independently check every
        // directional edge against their visual order. No QML navigation helper is called.
        QList<QQuickItem*> controls;
        for (auto* control : headers + filters + actions)
          if (control->isVisible() && control->isEnabled()) controls.append(control);
        std::sort(controls.begin(), controls.end(), [&](auto* a, auto* b) {
          const auto ac = center(a), bc = center(b);
          return qAbs(ac.y() - bc.y()) > 3 ? ac.y() < bc.y() : ac.x() < bc.x();
        });
        QList<QList<QQuickItem*>> rows;
        for (auto* control : controls) {
          if (rows.isEmpty() || qAbs(center(rows.last().first()).y() - center(control).y()) > 3)
            rows.append(QList<QQuickItem*>{});
          rows.last().append(control);
        }
        auto tabOrder = controls;
        if (games->property("count").toInt() > 0) tabOrder.append(games);
        tabOrder.first()->forceActiveFocus();
        for (int i = 1; i <= tabOrder.size(); ++i) {
          keyboard(window, Qt::Key_Tab);
          require(focused() == tabOrder[i % tabOrder.size()],
                  QStringLiteral("Tab %1 state=%2 step=%3 expected %4, got %5")
                      .arg(layout).arg(state).arg(i).arg(name(tabOrder[i % tabOrder.size()])).arg(name(window->activeFocusItem())));
        }
        for (int i = tabOrder.size() - 1; i >= 0; --i) {
          keyboard(window, Qt::Key_Backtab, Qt::ShiftModifier);
          require(focused() == tabOrder[i],
                  QStringLiteral("Shift+Tab expected %1, got %2").arg(name(tabOrder[i]), name(window->activeFocusItem())));
        }
        for (int input = 0; input < 3; ++input) {
          const auto send = [&](int key) {
            require(controller.inputEnabled(), QStringLiteral("Input ownership lost during navigation"));
            if (input == 0) keyboard(window, key); else pad.direction(key, input == 2);
          };
          QHash<QQuickItem*, QList<QQuickItem*>> graph;
          for (int r = 0; r < rows.size(); ++r) {
            for (int col = 0; col < rows[r].size(); ++col) {
              auto* source = rows[r][col];
              for (int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right}) {
                auto* expected = source;
                if (key == Qt::Key_Left && col > 0) expected = rows[r][col - 1];
                if (key == Qt::Key_Right && col + 1 < rows[r].size()) expected = rows[r][col + 1];
                const int adjacent = key == Qt::Key_Up ? r - 1 : r + 1;
                if ((key == Qt::Key_Up || key == Qt::Key_Down) && adjacent >= 0 && adjacent < rows.size()) {
                  expected = *std::min_element(rows[adjacent].begin(), rows[adjacent].end(), [&](auto* a, auto* b) {
                    return qAbs(center(a).x() - center(source).x()) < qAbs(center(b).x() - center(source).x());
                  });
                } else if (key == Qt::Key_Down && r == rows.size() - 1 && games->property("count").toInt() > 0) {
                  expected = games;
                }
                source->forceActiveFocus();
                send(key);
                require(focused() == expected,
                    QStringLiteral("%1 %2 state=%3 input=%4: %5 key=%6 expected %7, got %8")
                        .arg(window->width()).arg(window->height()).arg(state).arg(input)
                        .arg(name(source)).arg(key).arg(name(expected)).arg(name(window->activeFocusItem())));
                graph[source].append(focused());
                ++assertions;
              }
            }
          }
          // Ensure the links form one reachable surface, including the game view.
          QSet<QQuickItem*> reached;
          QList<QQuickItem*> pending{headers.first()};
          while (!pending.isEmpty()) {
            auto* current = pending.takeFirst();
            if (reached.contains(current)) continue;
            reached.insert(current);
            pending.append(graph.value(current));
          }
          for (auto* control : controls)
            require(reached.contains(control), QStringLiteral("Unreachable control: %1").arg(name(control)));
          if (games->property("count").toInt() > 0) {
            require(reached.contains(games), QStringLiteral("Game view unreachable from header"));
            games->setProperty("currentIndex", 0);
            games->forceActiveFocus();
            send(Qt::Key_Up);
            require(controls.contains(window->activeFocusItem()), QStringLiteral("Game view cannot return to controls"));
            auto* returnControl = window->activeFocusItem();
            send(Qt::Key_Down);
            require(games->hasActiveFocus(), QStringLiteral("Game controls cannot return to games"));
            send(Qt::Key_Up);
            require(window->activeFocusItem() == returnControl, QStringLiteral("Game view lost its return control"));
            games->forceActiveFocus();
            games->setProperty("currentIndex", 0);
            const int count = games->property("count").toInt();
            if (count > 1) {
              send(Qt::Key_Right);
              require(games->property("currentIndex").toInt() == 1, QStringLiteral("Game Right did not advance selection"));
              send(Qt::Key_Left);
              require(games->property("currentIndex").toInt() == 0, QStringLiteral("Game Left did not restore selection"));
            }
            if (games == grid && count > grid->property("columnCount").toInt()) {
              send(Qt::Key_Down);
              require(grid->property("currentIndex").toInt() == grid->property("columnCount").toInt(),
                      QStringLiteral("Grid Down did not advance one row"));
              send(Qt::Key_Up);
              require(grid->hasActiveFocus() && grid->property("currentIndex").toInt() == 0,
                      QStringLiteral("Grid Up left the games before reaching the first row"));
            }
          }
        }
      }
      library->setProperty("mode", 0);
      library->setProperty("sourceFilter", QString());
      library->setProperty("searchText", QString());
      settle(50);
      for (auto* start : {games, actions.first()}) {
        if (!start->isVisible() || !start->isEnabled()) continue;
        start->forceActiveFocus();
        library->setProperty("searchText", QStringLiteral("no-matching-navigation-contract-game"));
        settle(50);
        auto* focus = window->activeFocusItem();
        require(focus && focus->isVisible() && focus->isEnabled() && (headers.contains(focus) || filters.contains(focus)),
                QStringLiteral("Empty results stranded focus on %1").arg(name(focus)));
        library->setProperty("searchText", QString());
        settle(50);
      }
    }
    library->setProperty("searchText", QString());
    library->setProperty("consoleFilter", QString());
    library->setProperty("sourceFilter", QString());
    library->setProperty("mode", 0);
    settle(50);
    // Confirm/back also go through SDL. Dialogs must retain focus and restore their opener.
    for (const auto& entry : {std::pair{"couchSearchButton", "searchOpen"},
                             std::pair{"couchFiltersButton", "browseOpen"}}) {
      auto* opener = item(entry.first);
      opener->forceActiveFocus();
      pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
      require(couch->property(entry.second).toBool(), QStringLiteral("Controller did not open %1").arg(entry.second));
      for (int key : {Qt::Key_Up, Qt::Key_Right, Qt::Key_Down, Qt::Key_Left}) {
        pad.direction(key, false);
        auto* focus = window->activeFocusItem();
        require(within(focus, item(entry.second == QStringLiteral("searchOpen") ? "couchKeyboard" : "couchBrowsePanel")),
                QStringLiteral("Dialog navigation escaped into the library"));
      }
      {
        auto* beforeConnection = window->activeFocusItem();
        VirtualPad secondPad(controller);
        settle();
        require(window->activeFocusItem() == beforeConnection, QStringLiteral("Controller connection stole dialog focus"));
      }
      require(until([&] { return controller.controllerCount() == 1; }), QStringLiteral("Dialog controller disconnect was not handled"));
      pad.button(SDL_GAMEPAD_BUTTON_EAST);
      require(!couch->property(entry.second).toBool() && opener->hasActiveFocus(),
              QStringLiteral("Dialog Back did not restore %1").arg(name(opener)));
    }
    // Header destinations and game details are entered and exited using actual pad buttons.
    for (const auto& entry : {std::pair{"couchHomeButton", "homeOpen"},
                             std::pair{"couchSettingsButton", "diagnosticsOpen"},
                             std::pair{"couchStatsButton", "statsOpen"}}) {
      item(entry.first)->forceActiveFocus();
      pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
      require(until([&] { return window->property(entry.second).toBool(); }), QStringLiteral("Destination did not open"));
      pad.direction(Qt::Key_Down, false);
      auto* focus = window->activeFocusItem();
      require(focus && focus->isVisible() && focus->isEnabled() && !headers.contains(focus) && !filters.contains(focus),
              QStringLiteral("Destination retained library focus: %1").arg(entry.second));
      pad.button(SDL_GAMEPAD_BUTTON_EAST);
      // Settings opens over the library, so Back returns to the button that opened it. Home and
      // Stats replace the library, so Back returns to the games.
      auto* returnTo = entry.second == QStringLiteral("diagnosticsOpen") ? item(entry.first) : grid;
      require(until([&] { return !window->property(entry.second).toBool() && returnTo->hasActiveFocus(); }),
              QStringLiteral("Destination Back did not restore focus: %1 (focus %2)")
                  .arg(entry.second, name(window->activeFocusItem())));
    }
    grid->setProperty("currentIndex", 0);
    grid->forceActiveFocus();
    pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
    require(until([&] { return window->property("detailOpen").toBool(); }), QStringLiteral("Game details did not open"));
    pad.direction(Qt::Key_Right, false);
    require(item("favoriteButton")->hasActiveFocus(), QStringLiteral("Game details action row cannot be traversed"));
    pad.button(SDL_GAMEPAD_BUTTON_EAST);
    require(until([&] { return !window->property("detailOpen").toBool() && grid->hasActiveFocus(); }),
            QStringLiteral("Game details Back did not restore games"));
    // Holding and releasing a controller must leave navigation usable.
    grid->forceActiveFocus();
    grid->setProperty("currentIndex", 0);
    pad.button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 370);
    require(grid->property("currentIndex").toInt() >= 2, QStringLiteral("Held D-pad did not repeat"));
    const int releasedIndex = grid->property("currentIndex").toInt();
    settle(150);
    require(grid->property("currentIndex").toInt() == releasedIndex, QStringLiteral("D-pad repeated after release"));
    pad.button(SDL_GAMEPAD_BUTTON_NORTH);
    auto* layoutControl = item("couchLayoutButton");
    require(layoutControl->hasActiveFocus(), QStringLiteral("Controller toolbar shortcut did not reach the controls"));
    pad.button(SDL_GAMEPAD_BUTTON_NORTH);
    require(grid->hasActiveFocus(), QStringLiteral("Controller toolbar shortcut did not return to games"));
    pad.direction(Qt::Key_Up, false);
    require(layoutControl->hasActiveFocus(), QStringLiteral("Toolbar shortcut lost its return control"));
    keyboard(window, Qt::Key_F6);
    require(grid->hasActiveFocus(), QStringLiteral("Keyboard toolbar shortcut did not return to games"));
    pad.button(SDL_GAMEPAD_BUTTON_START);
    require(until([&] { return !window->property("couchMode").toBool(); }), QStringLiteral("Controller Start did not leave Couch mode"));
    pad.button(SDL_GAMEPAD_BUTTON_START);
    require(until([&] { return window->property("couchMode").toBool() && grid->hasActiveFocus(); }),
            QStringLiteral("Controller Start did not restore Couch game focus"));
    qInfo() << "Couch navigation contract passed:" << assertions << "directional edges, keyboard/D-pad/analog,"
            << "detail/grid, populated/console/empty, dialogs/destinations/details, shortcuts, hotplug, held input";
    return true;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Couch navigation contract:" << error.what();
    return false;
  }
}

namespace {
QString describe(QQuickItem* item) {
  if (!item) return QStringLiteral("no focus");
  QString label = item->objectName();
  const QString text = item->property("text").toString();
  if (!text.isEmpty()) label += (label.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("\"%1\"").arg(text);
  if (label.isEmpty()) label = QString::fromLatin1(item->metaObject()->className());
  return label;
}

bool shown(QQuickItem* item) {
  if (!item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0) return false;
  for (auto* step = item; step; step = step->parentItem())
    if (step->opacity() <= 0.01) return false;
  return true;
}

void collect(QQuickItem* item, QList<QQuickItem*>& out) {
  if (!item->isVisible()) return;
  const QVariant navigation = item->property("controllerNavigation");
  const bool destination = !navigation.isValid() || navigation.toBool() ||
                           item->property("spatialFocusDestination").toBool() ||
                           item->property("controllerVerticalNavigation").toBool();
  if (item->activeFocusOnTab() && shown(item) && destination) out.append(item);
  for (auto* child : item->childItems()) collect(child, out);
}

QList<QQuickItem*> focusables(QQuickItem* container) {
  QList<QQuickItem*> out;
  for (auto* child : container->childItems()) collect(child, out);
  // A focus scope (a game grid) stands for its delegates.
  QList<QQuickItem*> result;
  for (auto* item : out) {
    bool insideAnother = false;
    for (auto* other : out)
      if (other != item && within(item, other)) { insideAnother = true; break; }
    if (!insideAnother) result.append(item);
  }
  return result;
}

QRectF sceneRect(QQuickItem* item) { return item->mapRectToScene(item->boundingRect()); }

// How much of the item the user can actually see: the window and every clipping ancestor.
QRectF seen(QQuickItem* item, QQuickWindow* window, QString* clipper = nullptr) {
  QRectF rect = sceneRect(item).intersected(QRectF(0, 0, window->width(), window->height()));
  for (auto* step = item->parentItem(); step; step = step->parentItem()) {
    if (!step->clip()) continue;
    rect = rect.intersected(sceneRect(step));
    if (rect.isEmpty() && clipper && clipper->isEmpty())
      *clipper = describe(step) + QStringLiteral(" at ") + QString::number(sceneRect(step).y()) +
                 QStringLiteral(" ") + QString::number(sceneRect(step).height());
  }
  return rect;
}

bool onScreen(QQuickItem* item, QQuickWindow* window) {
  const QRectF full = sceneRect(item);
  const QRectF visible = seen(item, window);
  if (visible.isEmpty()) return false;
  // Large panels count once a fair part is in view; controls need most of themselves.
  return visible.width() >= std::min(full.width(), window->width() * 0.5) * 0.6 &&
         visible.height() >= std::min(full.height(), window->height() * 0.5) * 0.6;
}

QQuickItem* scrollAncestor(QQuickItem* item) {
  for (auto* step = item->parentItem(); step; step = step->parentItem())
    if (step->inherits("QQuickFlickable") &&
        step->property("contentHeight").toReal() > step->height() + 1)
      return step;
  return nullptr;
}

// Something focusable lies squarely in this direction: it overlaps the current control
// across the direction of travel and starts beyond it.
QQuickItem* lineNeighbour(QQuickItem* current, int key, const QList<QQuickItem*>& items) {
  const QRectF from = sceneRect(current);
  QQuickItem* best = nullptr;
  qreal bestGap = 1e9;
  for (auto* item : items) {
    if (item == current || within(current, item) || within(item, current) || !shown(item)) continue;
    const QRectF to = sceneRect(item);
    qreal gap = -1;
    bool overlap = false;
    if (key == Qt::Key_Up || key == Qt::Key_Down) {
      overlap = std::min(from.right(), to.right()) - std::max(from.left(), to.left()) > 4;
      gap = key == Qt::Key_Up ? from.top() - to.bottom() : to.top() - from.bottom();
    } else {
      overlap = std::min(from.bottom(), to.bottom()) - std::max(from.top(), to.top()) > 4;
      gap = key == Qt::Key_Left ? from.left() - to.right() : to.left() - from.right();
    }
    if (overlap && gap >= -1 && gap < bestGap) { best = item; bestGap = gap; }
  }
  return best;
}

// How far an item lies beyond the current one in this direction, when it overlaps it
// across the direction of travel; negative when it does not.
qreal lineGap(QQuickItem* current, QQuickItem* item, int key) {
  const QRectF from = sceneRect(current), to = sceneRect(item);
  if (key == Qt::Key_Up || key == Qt::Key_Down) {
    if (std::min(from.right(), to.right()) - std::max(from.left(), to.left()) <= 4) return -1;
    return key == Qt::Key_Up ? from.top() - to.bottom() : to.top() - from.bottom();
  }
  if (std::min(from.bottom(), to.bottom()) - std::max(from.top(), to.top()) <= 4) return -1;
  return key == Qt::Key_Left ? from.left() - to.right() : to.left() - from.right();
}

QString keyName(int key) {
  return key == Qt::Key_Up ? QStringLiteral("Up") : key == Qt::Key_Down ? QStringLiteral("Down")
       : key == Qt::Key_Left ? QStringLiteral("Left") : QStringLiteral("Right");
}

struct Sweep {
  QQuickWindow* window;
  VirtualPad& pad;
  QStringList failures;
  int presses = 0;

  // The D-pad through SDL and the production controller code, as a player presses it.
  void press(int key) { pad.direction(key, false); }

  void surface(const QString& name, QQuickItem* container) {
    settle(60);
    if (!container || !shown(container)) { failures << QStringLiteral("%1: not shown").arg(name); return; }
    auto items = focusables(container);
    if (items.isEmpty()) { failures << QStringLiteral("%1: nothing to focus").arg(name); return; }
    auto* start = window->activeFocusItem();
    if (!within(start, container)) {
      failures << QStringLiteral("%1: opened with focus outside it (%2)").arg(name, describe(start));
      start = items.first();
    }
    QHash<QQuickItem*, QSet<QQuickItem*>> edges;
    const auto owner = [&items](QQuickItem* focus) -> QQuickItem* {
      for (auto* item : items) if (within(focus, item)) return item;
      return nullptr;
    };
    for (int sourceIndex = 0; sourceIndex < items.size(); ++sourceIndex) {
      auto* source = items[sourceIndex];
      for (int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right}) {
        source->forceActiveFocus(Qt::TabFocusReason);
        // Scrolled into view the way the app does when focus arrives by the pad.
        QMetaObject::invokeMethod(window, "revealNavigationItem",
                                  Q_ARG(QVariant, QVariant::fromValue<QObject*>(container)),
                                  Q_ARG(QVariant, QVariant::fromValue<QObject*>(source)));
        settle(5);
        if (!within(window->activeFocusItem(), source)) break;  // Not focusable after all.
        auto* scroll = scrollAncestor(source);
        press(key);
        ++presses;
        auto* focus = window->activeFocusItem();
        const QString where = QStringLiteral("%1: %2 from %3").arg(name, keyName(key), describe(source));
        if (!within(focus, container)) {
          failures << QStringLiteral("%1 left the screen for %2").arg(where, describe(focus));
          continue;
        }
        auto* landed = owner(focus);
        if (!landed) { landed = focus; items.append(focus); }  // A control the scan missed.
        edges[source].insert(landed);
        if (landed != source && !onScreen(landed, window)) {
          QString clipper;
          const QRectF full = sceneRect(landed), visible = seen(landed, window, &clipper);
          failures << QStringLiteral("%1 focused %2 off screen (at %3,%4 %5x%6, %7x%8 visible, clipped by %9)")
                          .arg(where, describe(landed)).arg(full.x()).arg(full.y()).arg(full.width())
                          .arg(full.height()).arg(visible.width()).arg(visible.height()).arg(clipper);
        }
        // Text fields and sliders use Left and Right themselves.
        // One press, one step: skipping the nearest control in line for one further on is
        // how a press handled twice shows up.
        if (landed != source && !within(landed, source) && !within(source, landed)) {
          auto* nearest = lineNeighbour(source, key, items);
          const qreal landedGap = lineGap(source, landed, key);
          // Up and Down stay on a scrolling page while it has more that way (Main.qml).
          const bool pageFirst = scroll && (key == Qt::Key_Up || key == Qt::Key_Down) && nearest &&
                            !within(nearest, scroll) && within(landed, scroll);
          if (nearest && nearest != landed && !pageFirst && landedGap >= 0 &&
              landedGap > lineGap(source, nearest, key) + 1)
            failures << QStringLiteral("%1 skipped %2 and landed on %3").arg(where, describe(nearest), describe(landed));
        }
        const bool textCursor = (key == Qt::Key_Left || key == Qt::Key_Right) &&
                                (source->property("cursorPosition").isValid() ||
                                 source->property("controllerVerticalNavigation").toBool());
        if (landed == source && !textCursor) {
          if (auto* missed = lineNeighbour(source, key, items))
            failures << QStringLiteral("%1 stayed put, %2 is right there").arg(where, describe(missed));
        }
        if (scroll && !within(landed, scroll) && scroll->property("contentHeight").toReal() > scroll->height() + 1) {
          const qreal y = scroll->property("contentY").toReal();
          const qreal top = scroll->property("originY").toReal();
          if (key == Qt::Key_Up && y > top + 1)
            failures << QStringLiteral("%1 went above %2 without scrolling it to the top").arg(where, describe(scroll));
        }
      }
    }
    // Reachable from where the screen starts, by direction alone.
    QSet<QQuickItem*> reached;
    QList<QQuickItem*> pending{owner(start) ? owner(start) : items.first()};
    while (!pending.isEmpty()) {
      auto* item = pending.takeFirst();
      if (reached.contains(item)) continue;
      reached.insert(item);
      for (auto* next : edges.value(item)) pending.append(next);
    }
    // OMAKADE_SWEEP_GRAPH=<screen name> prints where each control's presses lead.
    if (qEnvironmentVariableIsSet("OMAKADE_SWEEP_GRAPH") && name.contains(qEnvironmentVariable("OMAKADE_SWEEP_GRAPH"))) {
      qInfo().noquote() << "start" << describe(start);
      for (auto* item : items) {
        QStringList targets;
        for (auto* next : edges.value(item)) targets << describe(next);
        qInfo().noquote() << describe(item) << sceneRect(item) << "->" << targets.join(", ");
      }
    }
    for (auto* item : items)
      if (!reached.contains(item) && edges.contains(item))
        failures << QStringLiteral("%1: %2 cannot be reached with the d-pad").arg(name, describe(item));
    qInfo().noquote() << QStringLiteral("Sweep %1: %2 controls").arg(name).arg(items.size());
  }
};
}  // namespace

bool runCouchNavigationSweep(QQuickWindow* window, ControllerInput& controller) {
  try {
    const auto item = [window](const char* objectName) {
      auto* result = window->findChild<QQuickItem*>(QString::fromLatin1(objectName));
      require(result != nullptr, QStringLiteral("Missing %1").arg(QString::fromLatin1(objectName)));
      return result;
    };
    window->requestActivate();
    require(until([&] { return window->isActive() && controller.inputEnabled(); }),
            QStringLiteral("Test window never acquired input ownership"));
    VirtualPad pad(controller);
    Sweep sweep{window, pad, {}};
    const auto open = [&](const char* property, bool value) {
      window->setProperty(property, value);
      settle(80);
    };
    const auto back = [&](const QString& what, const std::function<bool()>& expected) {
      pad.button(SDL_GAMEPAD_BUTTON_EAST);
      settle(80);
      if (!expected())
        sweep.failures << QStringLiteral("Back from %1 went to the wrong place: home=%2 settings=%3 stats=%4 details=%5 focus=%6")
                              .arg(what).arg(window->property("homeOpen").toBool())
                              .arg(window->property("diagnosticsOpen").toBool())
                              .arg(window->property("statsOpen").toBool())
                              .arg(window->property("detailOpen").toBool())
                              .arg(describe(window->activeFocusItem()));
    };
    auto* home = item("homeScreen");
    auto* settings = item("settingsOverlay");
    const bool couch = window->property("couchMode").toBool();

    // Library, Home and Stats, as each opens.
    if (couch) sweep.surface(QStringLiteral("Library"), item("couchLibrary"));
    open("homeOpen", true);
    QMetaObject::invokeMethod(home, "focusHome");
    sweep.surface(QStringLiteral("Home"), home);
    open("homeOpen", false);
    open("statsOpen", true);
    if (auto* stats = window->findChild<QQuickItem*>(QStringLiteral("statsScreen")))
      sweep.surface(QStringLiteral("Stats"), stats);
    open("statsOpen", false);

    // Every Settings page.
    open("diagnosticsOpen", true);
    const QVariantList sections = settings->property("sections").toList();
    for (const QVariant& entry : sections) {
      const auto page = entry.toMap();
      QMetaObject::invokeMethod(settings, "chooseSection", Q_ARG(QVariant, page.value("section")));
      settle(80);
      sweep.surface(QStringLiteral("Settings > %1").arg(page.value("label").toString()), settings);
    }
    open("diagnosticsOpen", false);

    // Back goes one screen, with the pad, from screens opened over Home.
    open("homeOpen", true);
    QMetaObject::invokeMethod(home, "focusHome");
    auto* homeSettings = item(couch ? "homeCouchSettingsButton" : "homeSettingsButton");
    homeSettings->forceActiveFocus();
    pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
    require(until([&] { return window->property("diagnosticsOpen").toBool(); }),
            QStringLiteral("Home SETTINGS did not open Settings"));
    back(QStringLiteral("Settings opened from Home"), [&] {
      return !window->property("diagnosticsOpen").toBool() && window->property("homeOpen").toBool() &&
             homeSettings->hasActiveFocus();
    });
    // From wherever focus is in Settings, Back closes only Settings.
    for (const QVariant& entry : sections) {
      const auto page = entry.toMap();
      for (int index = 0;; ++index) {
        homeSettings->forceActiveFocus();
        pad.button(SDL_GAMEPAD_BUTTON_SOUTH);
        require(until([&] { return window->property("diagnosticsOpen").toBool(); }),
                QStringLiteral("Home SETTINGS did not open Settings"));
        QMetaObject::invokeMethod(settings, "chooseSection", Q_ARG(QVariant, page.value("section")));
        settle(80);
        const auto controls = focusables(settings);
        if (index >= controls.size()) {
          window->setProperty("diagnosticsOpen", false);
          settle(80);
          break;
        }
        controls[index]->forceActiveFocus(Qt::TabFocusReason);
        settle(10);
        const QString from = describe(window->activeFocusItem());
        back(QStringLiteral("Settings > %1 at %2, opened from Home").arg(page.value("label").toString(), from), [&] {
          return !window->property("diagnosticsOpen").toBool() && window->property("homeOpen").toBool();
        });
        if (window->property("diagnosticsOpen").toBool()) {
          // A page inside Settings goes back to its list first; one more Back leaves.
          back(QStringLiteral("Settings > %1 at %2, second Back").arg(page.value("label").toString(), from), [&] {
            return !window->property("diagnosticsOpen").toBool() && window->property("homeOpen").toBool();
          });
          window->setProperty("diagnosticsOpen", false);
        }
        window->setProperty("homeOpen", true);
        settle(40);
      }
    }
    QMetaObject::invokeMethod(home, "focusHome");
    back(QStringLiteral("Home"), [&] { return !window->property("homeOpen").toBool(); });

    for (const auto& failure : sweep.failures) qCritical().noquote() << failure;
    if (!sweep.failures.isEmpty()) {
      qCritical().noquote() << QStringLiteral("Couch navigation sweep: %1 problems in %2 presses")
                                   .arg(sweep.failures.size()).arg(sweep.presses);
      return false;
    }
    qInfo().noquote() << QStringLiteral("Couch navigation sweep passed: %1 presses").arg(sweep.presses);
    return true;
  } catch (const std::exception& error) {
    qCritical().noquote() << "Couch navigation sweep:" << error.what();
    return false;
  }
}

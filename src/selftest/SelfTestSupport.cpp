#include "selftest/SelfTestSupport.h"

#include "input/ControllerInput.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>

void requestControllerCommand(ControllerInput& controller, int key) {
  switch (key) {
  case Qt::Key_Return: case Qt::Key_Enter: controller.acceptRequested(); break;
  case Qt::Key_Escape: controller.backRequested(); break;
  case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
    controller.focusDirectionRequested(key); break;
  default: qFatal("Unknown controller test command");
  }
}

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
    controller.acceptRequested();
    if (!field->property("text").toString().isEmpty() || !field->hasActiveFocus() || clear->isVisible())
      return "Clear did not empty and refocus " + field->objectName();
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
      requestControllerCommand(controller, key);
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
            controller.focusDirectionRequested(direction);
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
      controller.backRequested();
      if (window->property("couchTextEntryOpen").toBool() ||
          field->property("text").toString() != beforeCancel || !waitForFocus(field))
        return "Keyboard Cancel changed the field or failed to close";
      controller.acceptRequested();
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

QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
  if (!item) return nullptr;
  if (item->objectName() == name) return item;
  for (auto* child : item->childItems()) if (auto* found = findVisualItem(child, name)) return found;
  return nullptr;
}

int expectedDetailActionColumns(qreal width, qreal scale, bool running) {
  if (!running) return width < 300 ? 1 : width < 620 ? 2 : 4;
  const auto fits = [width, scale](int count) {
    return count * 220 * scale + (count - 1) * 10 <= width;
  };
  return fits(5) ? 5 : fits(3) ? 3 : fits(2) ? 2 : 1;
}

QString clippedDetailAction(QQuickItem* content, QQuickItem* actions) {
  const qreal right = content->mapToScene(QPointF(content->width(), 0)).x();
  for (QQuickItem* action : actions->childItems()) {
    if (!action->isVisible()) continue;
    if (action->mapToScene(QPointF(action->width(), 0)).x() > right + 0.5)
      return action->objectName();
  }
  return {};
}

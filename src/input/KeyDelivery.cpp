#include "input/KeyDelivery.h"

#include <QEvent>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

void deliverKey(QWindow* window, int key, Qt::KeyboardModifiers modifiers) {
  if (window == nullptr) return;
  QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
      window, QEvent::KeyPress, key, modifiers);
  QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
      window, QEvent::KeyRelease, key, modifiers);
}

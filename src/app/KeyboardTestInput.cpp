#include "app/KeyboardTestInput.h"

#include <QEvent>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

void deliverKeyboardTestKey(QWindow* window, int key, Qt::KeyboardModifiers modifiers) {
  if (window == nullptr) return;
  QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
      window, QEvent::KeyPress, key, modifiers);
  QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
      window, QEvent::KeyRelease, key, modifiers);
}

void deliverKeyboardTestText(QWindow* window, const QString& text) {
  if (window == nullptr) return;
  for (const QChar character : text) {
    const int key = character.toUpper().unicode();
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
        window, QEvent::KeyPress, key, Qt::NoModifier, QString(character));
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(
        window, QEvent::KeyRelease, key, Qt::NoModifier, QString(character));
  }
}

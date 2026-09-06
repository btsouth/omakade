#include "input/ControllerFocusGuard.h"
#include "input/ControllerInput.h"
#include <QGuiApplication>
#include <QWindow>

ControllerFocusGuard::ControllerFocusGuard(ControllerInput* input, QWindow* window)
    : QObject(window), m_input(input), m_window(window) {
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  connect(app, &QGuiApplication::applicationStateChanged, this, [this] { sync(); });
  connect(app, &QGuiApplication::focusWindowChanged, this, [this] { sync(); });
  connect(window, &QWindow::activeChanged, this, [this] { sync(); });
  connect(window, &QWindow::visibilityChanged, this, [this] { sync(); });
  sync();
}
bool ControllerFocusGuard::ownsWindow(QWindow* root, QWindow* focused) {
  for (QWindow* window = focused; window; window = window->transientParent())
    if (window == root)
      return true;
  return false;
}
void ControllerFocusGuard::sync() {
  m_input->setWindowFocused(m_window && m_window->isVisible() &&
                            m_window->visibility() != QWindow::Minimized &&
                            QGuiApplication::applicationState() == Qt::ApplicationActive &&
                            ownsWindow(m_window, QGuiApplication::focusWindow()));
}

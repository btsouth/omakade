#include "input/CouchCursorManager.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWindow>

CouchCursorManager::CouchCursorManager(QWindow* window, int idleTimeoutMs, QObject* parent)
    : QObject(parent), m_window(window) {
  m_idleTimer.setSingleShot(true);
  m_idleTimer.setInterval(idleTimeoutMs);
  connect(&m_idleTimer, &QTimer::timeout, this, [this] {
    if (m_couchMode) {
      setCursorHidden(true);
    }
  });
  qGuiApp->installEventFilter(this);
  syncCouchMode();
}

CouchCursorManager::~CouchCursorManager() {
  qGuiApp->removeEventFilter(this);
  setCursorHidden(false);
}

bool CouchCursorManager::cursorHidden() const { return m_cursorHidden; }

void CouchCursorManager::syncCouchMode() {
  const bool couchMode = m_window != nullptr && m_window->property("couchMode").toBool();
  if (m_couchMode == couchMode) {
    return;
  }

  m_couchMode = couchMode;
  m_idleTimer.stop();
  setCursorHidden(false);
  if (m_couchMode) {
    m_idleTimer.start();
  }
}

void CouchCursorManager::navigationActivity() {
  if (!m_couchMode) {
    return;
  }
  m_idleTimer.stop();
  setCursorHidden(true);
}

bool CouchCursorManager::eventFilter(QObject* watched, QEvent* event) {
  if (m_couchMode) {
    switch (event->type()) {
    case QEvent::KeyPress:
      navigationActivity();
      break;
    case QEvent::MouseMove: {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const QPointF position = mouseEvent->globalPosition();
      if (!m_haveMousePosition || position != m_lastMousePosition) {
        m_lastMousePosition = position;
        m_haveMousePosition = true;
        pointerActivity();
      }
      break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
      pointerActivity();
      break;
    default:
      break;
    }
  }
  return QObject::eventFilter(watched, event);
}

void CouchCursorManager::pointerActivity() {
  if (!m_couchMode) {
    return;
  }
  setCursorHidden(false);
  m_idleTimer.start();
}

void CouchCursorManager::setCursorHidden(bool hidden) {
  if (m_cursorHidden == hidden) {
    return;
  }
  m_cursorHidden = hidden;
  if (m_cursorHidden) {
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
  } else {
    QGuiApplication::restoreOverrideCursor();
  }
  emit cursorHiddenChanged();
}

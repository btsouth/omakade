#pragma once

#include <QObject>
#include <QPointF>
#include <QTimer>

class QEvent;
class QWindow;

class CouchCursorManager final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool cursorHidden READ cursorHidden NOTIFY cursorHiddenChanged)

public:
  explicit CouchCursorManager(QWindow* window, int idleTimeoutMs = 1600, QObject* parent = nullptr);
  ~CouchCursorManager() override;

  [[nodiscard]] bool cursorHidden() const;

public slots:
  void syncCouchMode();
  void navigationActivity();

signals:
  void cursorHiddenChanged();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void pointerActivity();
  void setCursorHidden(bool hidden);

  QWindow* m_window = nullptr;
  QTimer m_idleTimer;
  QPointF m_lastMousePosition;
  bool m_haveMousePosition = false;
  bool m_couchMode = false;
  bool m_cursorHidden = false;
};

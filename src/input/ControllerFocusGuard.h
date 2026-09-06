#pragma once
#include <QObject>
#include <QPointer>
class QWindow;
class ControllerInput;

// Routes controller actions only to Omakade or an owned dialog.
class ControllerFocusGuard final : public QObject {
public:
  ControllerFocusGuard(ControllerInput* input, QWindow* window);
  static bool ownsWindow(QWindow* root, QWindow* focused);

private:
  void sync();
  ControllerInput* m_input;
  QPointer<QWindow> m_window;
};

#pragma once

#include <QObject>
#include <QPointer>

class QWindow;
struct wl_registry;
struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;

// Holds a Wayland idle inhibitor on the Omakade window while a game runs.
//
// Controllers are not input devices as far as the compositor is concerned, so playing a game
// with a gamepad never resets the idle timer and the screensaver fires mid-session. Steam and
// most SDL games inhibit idle themselves; emulators mostly do not. Omakade started the game, so
// it inhibits on their behalf: the compositor honors an inhibitor from any mapped surface, not
// just the focused one, and omarchy-shell, hypridle, swayidle, KDE, and GNOME all respect it.
class IdleInhibitor final : public QObject {
  Q_OBJECT

public:
  explicit IdleInhibitor(QWindow* window, QObject* parent = nullptr);
  ~IdleInhibitor() override;

  // True when the session is Wayland and the compositor offers idle-inhibit.
  [[nodiscard]] bool isSupported() const;
  [[nodiscard]] bool isInhibiting() const;
  void setInhibited(bool inhibited);

private:
  void initialize();
  void apply();
  void release();

  QPointer<QWindow> m_window;
  bool m_wanted = false;
  bool m_initialized = false;
  wl_registry* m_registry = nullptr;
  zwp_idle_inhibit_manager_v1* m_manager = nullptr;
  zwp_idle_inhibitor_v1* m_inhibitor = nullptr;
};

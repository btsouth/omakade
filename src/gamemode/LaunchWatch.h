#pragma once

#include "gamemode/LaunchHold.h"

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

// Some launchers show nothing while their game starts: Xodus Nexus starts a game with no
// window of its own, and a large game can take a minute to open. Outside a launch hold,
// Omakade says the game is starting until a window of it opens. Nothing is moved or held.
class LaunchWatch final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)

public:
  // A null lookup reads procfs.
  explicit LaunchWatch(GameModeCompositor* compositor, LaunchHold::ProcessLookup lookup = {},
                       QObject* parent = nullptr);
  ~LaunchWatch() override;

  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] QString title() const { return m_target.title; }

  void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }
  void setTimeout(int milliseconds) { m_timeout.setInterval(milliseconds); }

  // Starts watching after the game is launched. `game` carries what LaunchHold::begin
  // takes. False when windows cannot be listed, so nothing would end the watch.
  Q_INVOKABLE bool begin(const QVariantMap& game);
  Q_INVOKABLE void cancel();

signals:
  void changed();
  // The game opened a window, or the launcher opened one of its own to show.
  void shown();
  // Nothing opened in time. The game may still be starting.
  void timedOut();

private:
  struct Scan {
    int generation = 0;
    bool ok = false;
    bool shown = false;
  };
  void scan();
  void finishScan();
  void stop();

  GameModeCompositor* m_compositor = nullptr;
  LaunchHold::ProcessLookup m_lookup;
  LaunchHoldTarget m_target;
  // Windows open before the launch, which say nothing about it.
  QStringList m_ignored;
  int m_generation = 0;
  bool m_active = false;
  QTimer m_poll;
  QTimer m_timeout;
  QFutureWatcher<Scan> m_scanWatcher;
};

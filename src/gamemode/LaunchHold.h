#pragma once

#include "gamemode/GameModePorts.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

// The game a hold is waiting for, from the library entry that was launched.
struct LaunchHoldTarget {
  QString title;
  QString steamAppId;
  // Folder the game is installed in, or the file an emulator opens.
  QString installPath;
  // The process Omakade started. Only an emulator or native game is that process
  // itself; a launcher such as Heroic or Lutris shows its own windows from it.
  qint64 pid = 0;
  bool directProcess = false;
  // A prefix only this game runs in, as Xodus Nexus gives each game. Any program in it
  // is the game, wherever it runs from.
  QString winePrefix;
};

struct LaunchHoldProcess {
  qint64 parent = 0;
  QString exePath;
  QStringList arguments;
  QString steamAppId;
  // Wine shows a game inside its prefix as C:\..., resolved under this folder.
  QString winePrefix;
};

// While a game starts in Game Mode, the windows a launcher opens first (its client,
// updates, splash screens, sign-ins) wait out of sight, and Omakade keeps focus. The
// game's own window ends the hold: it goes fullscreen and is focused, and the launcher's
// windows stay out of sight until Game Mode ends. A held window that needs an answer can
// be shown, and a hold that is cancelled or finds no game lets everything go.
class LaunchHold final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)
  // {address, windowClass, title} for each held window, oldest first.
  Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)
  Q_PROPERTY(QString presented READ presented NOTIFY changed)

public:
  using ProcessLookup = std::function<std::optional<LaunchHoldProcess>(qint64)>;

  // `workspace` is the Game Mode workspace. A null lookup reads procfs.
  LaunchHold(GameModeCompositor* compositor, QString workspace, ProcessLookup lookup = {},
             QObject* parent = nullptr);
  ~LaunchHold() override;

  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] QString title() const { return m_target.title; }
  [[nodiscard]] QVariantList windows() const;
  [[nodiscard]] QString presented() const { return m_presented; }

  void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }
  void setTimeout(int milliseconds) { m_timeout.setInterval(milliseconds); }

  // Starts holding before the game is launched. `game` carries title, source, appId,
  // installPath and launchTarget from the library. False when the compositor cannot hold windows.
  Q_INVOKABLE bool begin(const QVariantMap& game);
  // The process the launch started, once it is known.
  Q_INVOKABLE void launched(qint64 pid);
  // Lets everything go now, leaving focus where it is.
  Q_INVOKABLE void cancel();
  // Game Mode is leaving or parking and puts held windows back itself: stop at once,
  // without touching any window.
  Q_INVOKABLE void abandon();
  // Shows a held window over the library and focuses it.
  Q_INVOKABLE void present(const QString& address);
  // Omakade has focus again after a shown window: that window waits with the others
  // again, if it is still open, and the library goes back in front.
  Q_INVOKABLE void returnToLibrary();

  // Shows the Launching screen with these windows and holds nothing, for renders.
  void preview(const QString& title, const QVector<GameModeHeldWindow>& windows);

  [[nodiscard]] static bool isGameWindow(const GameModeHeldWindow& window,
                                         const LaunchHoldTarget& target,
                                         const ProcessLookup& lookup);
  [[nodiscard]] static LaunchHoldTarget targetFor(const QVariantMap& game);
  [[nodiscard]] static std::optional<LaunchHoldProcess> readProcess(qint64 pid);

signals:
  void changed();
  // The game's window is in front; the hold is over.
  void gameShown();
  // The hold ended without finding the game: cancelled, timed out or failed.
  void released();

private:
  struct Scan {
    int generation = 0;
    bool ok = false;
    QVector<GameModeHeldWindow> windows;
    QString game;
  };
  void stop();
  void scan();
  void finishScan();
  void release(const QString& game);
  void setWindows(const QVector<GameModeHeldWindow>& windows);

  GameModeCompositor* m_compositor = nullptr;
  QString m_workspace;
  ProcessLookup m_lookup;
  LaunchHoldTarget m_target;
  QVector<GameModeHeldWindow> m_windows;
  // Windows an earlier launch left held once its game opened. They wait for Game Mode to
  // end, not for this game. Windows held for any other reason, such as Steam starting in
  // the background, are listed for the next launch.
  QStringList m_kept;
  QStringList m_ignored;
  // Tells a scan that finishes after its hold ended from one for the current hold.
  int m_generation = 0;
  QString m_presented;
  bool m_active = false;
  QTimer m_poll;
  QTimer m_timeout;
  QFutureWatcher<Scan> m_scanWatcher;
  QFutureWatcher<void> m_actionWatcher;
};

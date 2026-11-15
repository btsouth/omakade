#pragma once

#include "gamemode/GameModePorts.h"

#include <QByteArray>
#include <QDateTime>
#include <QFutureWatcher>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

// When the Steam account is used on another computer, Steam signs out here and opens its
// Refresh Sign In window, which asks for a password or a QR code. A controller cannot get
// through it, but Steam usually signs back in from its saved login when it restarts. While
// Game Mode is on, this watches for that window and restarts Steam when asked.
class SteamSignIn final : public QObject {
  Q_OBJECT
  // Steam's Refresh Sign In window is open.
  Q_PROPERTY(bool open READ open NOTIFY changed)
  Q_PROPERTY(bool restarting READ restarting NOTIFY changed)

public:
  enum class Restart { SignedIn, NotSignedIn, StillRunning };
  using Restarter = std::function<Restart()>;

  // A null compositor never finds the window.
  explicit SteamSignIn(GameModeCompositor* compositor, QObject* parent = nullptr);
  ~SteamSignIn() override;

  [[nodiscard]] bool open() const { return !m_window.address.isEmpty(); }
  [[nodiscard]] bool restarting() const { return m_restarting; }

  // Watches while Game Mode is on. Hyprland's window events start each look; without
  // them, it looks every poll interval.
  void setWatching(bool watching);
  void setPollInterval(int milliseconds) { m_poll.setInterval(milliseconds); }
  // Off, it only polls, as test runs do so no desktop's events reach them.
  void setHyprlandEvents(bool enabled) { m_hyprlandEvents = enabled; }
  void setRestarter(Restarter restarter) { m_restarter = std::move(restarter); }
  // The window titles that count, for tests. Unset, they are read from Steam's files.
  void setTitles(const QStringList& titles) { m_titles = titles; }

  // Quits Steam, starts it again in the background and waits for it to sign in.
  Q_INVOKABLE void restartSteam();
  // Brings Steam's Refresh Sign In window in front on the Game Mode workspace.
  Q_INVOKABLE void showWindow();

  // Steam's own name for the window, in English and in the language Steam is set to.
  [[nodiscard]] static QStringList readTitles();
  [[nodiscard]] static bool isRefreshSignIn(const GameModeHeldWindow& window,
                                            const QStringList& titles);
  // The language in Steam's registry.vdf, such as "english" or "german".
  [[nodiscard]] static QString languageIn(const QByteArray& registry);
  // The window's title in one of Steam's shared_<language>-json.js files.
  [[nodiscard]] static QString titleIn(const QByteArray& localization);
  // Whether Steam's connection_log.txt records a sign-in no earlier than `since`.
  [[nodiscard]] static bool signedInSince(const QByteArray& connectionLog, const QDateTime& since);

signals:
  void changed();
  // A Refresh Sign In window opened that was not open before.
  void found();
  // A restart finished: signedIn is false when Steam still needs someone to sign in.
  void restarted(bool signedIn);

private:
  struct Scan {
    int generation = 0;
    bool ok = false;
    GameModeHeldWindow window;
    QStringList titles;
  };
  void scan();
  void finishScan();
  void finishRestart();
  void listen();

  GameModeCompositor* m_compositor = nullptr;
  Restarter m_restarter;
  QStringList m_titles;
  GameModeHeldWindow m_window;
  bool m_watching = false;
  bool m_hyprlandEvents = true;
  bool m_restarting = false;
  bool m_rescan = false;
  int m_generation = 0;
  QTimer m_poll;
  QTimer m_soon;
  QLocalSocket m_events;
  QByteArray m_eventBuffer;
  QFutureWatcher<Scan> m_scanWatcher;
  QFutureWatcher<Restart> m_restartWatcher;
  QFuture<void> m_action;
};

#pragma once

#include "gamemode/GameModeController.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class QScreen;

// Game Mode as the interface sees it: one switch that puts the fullscreen controller
// layout on a chosen display with its sound output, and puts the desktop back when it is
// switched off.
//
// A session starts one of two ways. `enter` uses the configured display, sound output and
// notification choice. `enterInPlace` (F11, Start, the header button, a stream or a
// remembered fullscreen start) stays on the current display and leaves sound and
// notifications alone; it only keeps the screen awake while a game runs.
//
// Desktop changes run on a worker thread through GameModeController. This object keeps
// the choices, reports progress, and tells the window when to enter and leave the layout.
class GameModeSession final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool parked READ parked NOTIFY stateChanged)
  Q_PROPERTY(bool hasSession READ hasSession NOTIFY stateChanged)
  // The session, or the start in progress, came from enterInPlace.
  Q_PROPERTY(bool inPlace READ inPlace NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  // False when the compositor cannot be managed, which leaves only the current display.
  Q_PROPERTY(bool displayManaged READ displayManaged NOTIFY devicesChanged)
  Q_PROPERTY(bool soundManaged READ soundManaged NOTIFY devicesChanged)
  Q_PROPERTY(bool notificationsManaged READ notificationsManaged NOTIFY devicesChanged)
  Q_PROPERTY(QString displayLabel READ displayLabel NOTIFY devicesChanged)
  Q_PROPERTY(QString soundLabel READ soundLabel NOTIFY devicesChanged)
  Q_PROPERTY(int displayChoices READ displayChoices NOTIFY devicesChanged)
  Q_PROPERTY(int soundChoices READ soundChoices NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList displayOptions READ displayOptions NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList soundOptions READ soundOptions NOTIFY devicesChanged)
  Q_PROPERTY(int displayIndex READ displayIndex NOTIFY devicesChanged)
  Q_PROPERTY(int soundIndex READ soundIndex NOTIFY devicesChanged)
  Q_PROPERTY(QString sessionDisplayLabel READ sessionDisplayLabel NOTIFY devicesChanged)
  // The connector name of the display the session is on, empty when unmanaged. The Game
  // Mode overlay is placed on that screen so it covers the game rather than the main window.
  Q_PROPERTY(QString sessionOutputName READ sessionOutputName NOTIFY stateChanged)
  Q_PROPERTY(QString sessionSoundLabel READ sessionSoundLabel NOTIFY devicesChanged)
  Q_PROPERTY(bool silenceNotifications READ silenceNotifications WRITE setSilenceNotifications
                 NOTIFY devicesChanged)
  Q_PROPERTY(bool warmSteam READ warmSteam WRITE setWarmSteam NOTIFY devicesChanged)
  // False when Steam is not installed, or in test runs that must not start it.
  Q_PROPERTY(bool steamAvailable READ steamAvailable NOTIFY devicesChanged)
  Q_PROPERTY(bool holdLaunchers READ holdLaunchers WRITE setHoldLaunchers NOTIFY devicesChanged)

public:
  // Null ports leave that part of the desktop alone. `settingsPath` holds the choices and
  // `statePath` the record of what a running session changed.
  GameModeSession(GameModeCompositor* compositor, GameModeAudio* audio,
                  GameModeNotifications* notifications, const QString& settingsPath,
                  const QString& statePath, QObject* parent = nullptr);
  ~GameModeSession() override;

  [[nodiscard]] bool active() const { return m_active; }
  // A parked scan still serializes changes internally, but Resume stays available.
  [[nodiscard]] bool busy() const { return m_busy && m_change != Change::RefreshParked; }
  [[nodiscard]] bool parked() const { return m_parked; }
  [[nodiscard]] bool hasSession() const { return m_active || m_parked; }
  [[nodiscard]] bool inPlace() const { return m_inPlace; }
  // An in-place session keeps the screen awake only while a game runs, so a handheld
  // browsing its library can still sleep. A configured session holds it throughout.
  [[nodiscard]] bool keepsScreenAwake(bool gameRunning) const;
  // What the in-game guide is told about Game Mode. Its scale follows the fullscreen
  // layout, which every session uses, in place or configured.
  [[nodiscard]] QJsonObject guideState(bool fullscreenLayout) const;
  void setTemporaryWindow(bool temporary);
  // The window exists only for Game Mode and is hidden, not returned, when it parks.
  Q_INVOKABLE bool temporaryWindow() const { return m_controller.temporaryWindow(); }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] bool displayManaged() const { return m_displayManaged; }
  [[nodiscard]] bool soundManaged() const { return m_soundManaged; }
  [[nodiscard]] bool notificationsManaged() const { return m_notificationsManaged; }
  [[nodiscard]] QString displayLabel() const;
  [[nodiscard]] QString soundLabel() const;
  [[nodiscard]] int displayChoices() const;
  [[nodiscard]] int soundChoices() const;
  [[nodiscard]] QVariantList displayOptions() const;
  [[nodiscard]] QVariantList soundOptions() const;
  [[nodiscard]] int displayIndex() const;
  [[nodiscard]] int soundIndex() const;
  [[nodiscard]] QString sessionDisplayLabel() const;
  [[nodiscard]] QString sessionOutputName() const { return m_output; }
  [[nodiscard]] QString sessionSoundLabel() const;
  [[nodiscard]] bool silenceNotifications() const { return m_settings.silenceNotifications; }
  void setSilenceNotifications(bool value);
  [[nodiscard]] bool warmSteam() const { return m_settings.warmSteam; }
  void setWarmSteam(bool value);
  [[nodiscard]] bool steamAvailable() const { return static_cast<bool>(m_startSteam); }
  enum class SteamStart { Failed, Running, Started };
  // Called when a session starts or resumes with warmSteam on. Unset, the setting is
  // hidden and nothing is started.
  void setSteamStarter(std::function<SteamStart()> start);
  // Stops holding the windows Steam opens as it starts in the background. A launch hold
  // that takes over lists them; otherwise release brings them back.
  Q_INVOKABLE void endSteamHold(bool release);
  [[nodiscard]] bool holdLaunchers() const { return m_settings.holdLaunchers; }
  void setHoldLaunchers(bool value);
  [[nodiscard]] const GameModeSettings& settings() const { return m_settings; }

  // Rereads the connected displays and sound outputs. Also undoes a session that an
  // earlier run left behind, the first time it is called.
  Q_INVOKABLE void refresh();
  // Steps to the next display or sound output. The first choice is always the one that
  // changes nothing: the current display, the current sound output.
  Q_INVOKABLE void cycleDisplay();
  Q_INVOKABLE void cycleSound();
  Q_INVOKABLE void selectDisplay(int index);
  Q_INVOKABLE void selectSound(int index);
  Q_INVOKABLE void enter();
  // Starts a session on the display Omakade is on, with sound and notifications
  // untouched. Resuming a parked session keeps the kind it started as.
  Q_INVOKABLE void enterInPlace();
  Q_INVOKABLE void exit();
  Q_INVOKABLE void park();
  Q_INVOKABLE void focusGame();
  Q_INVOKABLE void toggle();
  // Gives Omakade's window keyboard focus through the compositor. A game that has focus
  // keeps it otherwise, and the Game Mode controls would open behind it.
  Q_INVOKABLE void focusWindow();
  // Counts the other windows on the Game Mode workspace and reports them through
  // workspaceChecked, so the Game Mode key can tell whether leaving strands a game.
  Q_INVOKABLE void checkWorkspace();
  // Leaves Game Mode before the process ends. Blocks until the desktop is put back.
  Q_INVOKABLE void shutdown();

  [[nodiscard]] static QString outputLabel(const GameModeOutput& output);
  // The configured choices with the display, sound and notification overrides of an
  // in-place start applied. Launcher hold and Steam warm-up still apply.
  [[nodiscard]] static GameModeSettings inPlaceSettings(GameModeSettings settings);
  [[nodiscard]] static GameModeSettings loadSettings(const QString& path);
  [[nodiscard]] static bool saveSettings(const QString& path, const GameModeSettings& settings);

signals:
  void stateChanged();
  void devicesChanged();
  // The desktop is ready: the window should enter the fullscreen layout now.
  void entering();
  // GUI-only layout preparation; visibility, native mode and focus stay unchanged.
  void preparing(bool retainNavigation);
  void preparationCancelled();
  void entered();
  // The window should leave the fullscreen layout; the desktop is put back right after.
  void leaving(bool retainNavigation);
  void exited();
  // Capture navigation on the GUI thread before any park effects or window unmap.
  void parking();
  void parkedOnDesktop();
  void resumed();
  void windowVisibilityRequested(bool visible);
  // The display the next hidden root opens on, sent before windowVisibilityRequested.
  void openOutputRequested(const QString& output);
  // The placeholder window that keeps Omakade's place in the desktop layout should be
  // shown or hidden. Emitted from the worker thread.
  void placeholderRequested(bool visible);
  void workspaceChecked(int otherWindows);
  void failed(const QString& message);
  // An in-place start failed: the window shows the plain fullscreen layout instead.
  // Sent after preparationCancelled and before failed.
  void fullscreenFallback();
  // Something worth a toast that is not a failure to start.
  void notice(const QString& message);

private:
  struct Devices {
    bool displayManaged = false;
    bool soundManaged = false;
    bool notificationsManaged = false;
    QVector<GameModeOutput> outputs;
    QVector<GameModeSink> sinks;
    QString defaultSink;
    GameModeController::Result recovered;
    bool ranRecovery = false;
  };
  void setBusy(bool busy);
  void setStatus(const QString& text);
  void persist();
  void startSteam();
  void finishRefresh();
  void finishChange();
  void startChange();
  void refreshParked();
  enum class Change { Enter, Exit, Park, Resume, RefreshParked };
  void start(bool inPlace);
  // Rejects choices a session is using. An in-place session does not use them.
  [[nodiscard]] bool choicesLocked() const;
  void screenRemoved(QScreen* screen);
  [[nodiscard]] int currentDisplayIndex() const;
  [[nodiscard]] int currentSoundIndex() const;

  GameModeCompositor* m_compositor = nullptr;
  GameModeAudio* m_audio = nullptr;
  GameModeNotifications* m_notifications = nullptr;
  QString m_settingsPath;
  GameModeController m_controller;
  GameModeSettings m_settings;
  std::function<SteamStart()> m_startSteam;
  // Ends holding Steam's windows if no launch has taken over by then.
  QTimer m_steamHoldTimer;
  bool m_steamHolding = false;
  // Windows already held when Steam started, which are not Steam's to bring back.
  QStringList m_heldBeforeSteam;
  QVector<GameModeOutput> m_outputs;
  QVector<GameModeSink> m_sinks;
  QFutureWatcher<Devices> m_refreshWatcher;
  QFutureWatcher<GameModeController::Result> m_changeWatcher;
  QFuture<void> m_focusFuture;
  QFutureWatcher<int> m_workspaceWatcher;
  QString m_statusText;
  QString m_output;
  QString m_defaultSink;
  bool m_displayManaged = false;
  bool m_soundManaged = false;
  bool m_notificationsManaged = false;
  bool m_active = false;
  bool m_busy = false;
  bool m_shuttingDown = false;
  bool m_parked = false;
  bool m_inPlace = false;
  bool m_parkUiLeft = false;
  QString m_lastParkError;
  Change m_change = Change::Enter;
  bool m_resumeAfterRefresh = false;
  bool m_exitAfterChange = false;
  QTimer m_parkTimer;
  bool m_recoveryChecked = false;
  bool m_refreshPending = false;
  bool m_changePending = false;
};

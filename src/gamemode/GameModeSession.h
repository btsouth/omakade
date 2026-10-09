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

// Game Mode as the interface sees it: one switch that puts Couch Mode on a chosen display
// with its sound output, and puts the desktop back when it is switched off.
//
// Desktop changes run on a worker thread through GameModeController. This object keeps
// the choices, reports progress, and tells the window when to enter and leave Couch Mode.
class GameModeSession final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool parked READ parked NOTIFY stateChanged)
  Q_PROPERTY(bool hasSession READ hasSession NOTIFY stateChanged)
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
  void setTemporaryWindow(bool temporary);
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
  Q_INVOKABLE void exit();
  Q_INVOKABLE void park();
  void showLibrary();
  Q_INVOKABLE void focusGame();
  Q_INVOKABLE void toggle();
  // Gives Omakade's window keyboard focus through the compositor. A game that has focus
  // keeps it otherwise, and the Game Mode controls would open behind it.
  Q_INVOKABLE void focusWindow();
  // Counts the other windows on the Game Mode workspace and reports them through
  // workspaceChecked, so the Game Mode key can tell whether leaving strands a game.
  Q_INVOKABLE void checkWorkspace();
  // Leaves Game Mode before the process ends. Blocks until the desktop is put back.
  void shutdown();

  [[nodiscard]] static QString outputLabel(const GameModeOutput& output);
  [[nodiscard]] static GameModeSettings loadSettings(const QString& path);
  [[nodiscard]] static bool saveSettings(const QString& path, const GameModeSettings& settings);

signals:
  void stateChanged();
  void devicesChanged();
  // The desktop is ready: the window should enter Couch Mode now.
  void entering();
  // GUI-only layout preparation; visibility, native mode and focus stay unchanged.
  void preparing(bool retainNavigation);
  void preparationCancelled();
  void entered();
  // The window should leave Couch Mode; the desktop is put back right after.
  void leaving(bool retainNavigation);
  void exited();
  // Capture navigation on the GUI thread before any park effects or window unmap.
  void parking();
  void parkedOnDesktop();
  void resumed();
  void libraryShown();
  void gameFocused(bool ok);
  void windowVisibilityRequested(bool visible);
  // The placeholder window that keeps Omakade's place in the desktop layout should be
  // shown or hidden. Emitted from the worker thread.
  void placeholderRequested(bool visible);
  void workspaceChecked(int otherWindows);
  void failed(const QString& message);
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
  void finishRefresh();
  void finishChange();
  void startChange();
  void refreshParked();
  enum class Change { Enter, Exit, Park, Resume, RefreshParked, ShowLibrary };
  void screenRemoved(QScreen* screen);
  [[nodiscard]] int currentDisplayIndex() const;
  [[nodiscard]] int currentSoundIndex() const;

  GameModeCompositor* m_compositor = nullptr;
  GameModeAudio* m_audio = nullptr;
  GameModeNotifications* m_notifications = nullptr;
  QString m_settingsPath;
  GameModeController m_controller;
  GameModeSettings m_settings;
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
  bool m_parked = false;
  bool m_parkUiLeft = false;
  QString m_lastParkError;
  Change m_change = Change::Enter;
  bool m_resumeAfterRefresh = false;
  bool m_libraryAfterRefresh = false;
  bool m_exitAfterChange = false;
  QTimer m_parkTimer;
  bool m_recoveryChecked = false;
  bool m_refreshPending = false;
  bool m_changePending = false;
};

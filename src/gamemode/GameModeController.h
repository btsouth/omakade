#pragma once

#include "gamemode/GameModePorts.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

struct GameModeSettings {
  // Empty name and description mean the display Omakade is already on.
  QString outputName;
  QString outputDescription;
  // Empty keeps the current sound output.
  QString sinkName;
  bool silenceNotifications = true;
};

// Everything a session changed, written to disk before each change so an interrupted
// session can still be undone by the next start or by `omakade --game-mode-exit`.
enum class GameModePhase { Ended, Active, DesktopRetained };

struct GameModeState {
  GameModePhase phase = GameModePhase::Active;
  qint64 ownerStart = -1;
  bool temporaryWindow = false;
  bool desktopPending = false;
  // Fatal retention failures must finish cleanup before another resume can start.
  bool retentionEnding = false;
  QString focusedWorkspace;
  QString focusedWindow;
  QString lastGameWindow;
  QVector<GameModeGameWindow> games;
  QVector<GameModeStream> mutedStreams;
  qint64 ownerPid = 0;
  QString output; // display the session is on; empty when unmanaged
  bool enabledOutput = false;
  QString outputWorkspace;   // what that display showed before
  QString focusedOutput;     // display that had focus before
  QString windowWorkspace;   // where Omakade's window was
  int windowFullscreen = -1; // -1 for an older journal without window modes
  int windowFullscreenClient = -1;
  // Set before the window is moved, so a move that fails halfway still gets focus put back.
  bool windowPlaced = false;
  bool libraryPresented = false; // a parked library owns its desktop restoration
  // A placeholder window holds the main window's place in the layout.
  bool placeholder = false;
  QString previousSink;
  QString sessionSink;
  bool silencedNotifications = false;

  [[nodiscard]] QJsonObject toJson() const;
  [[nodiscard]] static bool fromJson(const QJsonObject& object, GameModeState* state);
};

// Enters and leaves Game Mode. Every call is synchronous and bounded, so the
// application runs it off the GUI thread.
//
// The promise is narrow on purpose: entering either completes or undoes itself, and
// leaving puts back exactly what entering changed. A display that was already on stays
// on, a sound output the user switched during the session is left alone, and nothing
// outside the session's own display, window and workspace is touched.
class GameModeController {
public:
  struct Result {
    bool ok = false;
    bool resumedGame = false;
    QString error;
    // Things that could not be put back or were skipped, for the status line and the log.
    QStringList notes;
    QString output;
  };
  using Sleep = std::function<void(int milliseconds)>;
  using OwnerAlive = std::function<bool(qint64 pid)>;
  // Shows or hides the placeholder window. Called from the worker thread, so it must not
  // wait on the GUI thread.
  using Placeholder = std::function<void(bool visible)>;

  // The workspace Game Mode owns. It exists only while a session is on it.
  [[nodiscard]] static QString workspace();

  GameModeController(GameModeCompositor* compositor, GameModeAudio* audio,
                     GameModeNotifications* notifications, const QString& statePath,
                     Sleep sleep = {}, OwnerAlive ownerAlive = {});

  // Without one, the window is moved to Game Mode and back and the compositor decides
  // where it lands on return.
  void setPlaceholder(Placeholder placeholder) { m_placeholder = std::move(placeholder); }

  [[nodiscard]] Result enter(const GameModeSettings& settings, qint64 windowPid);
  [[nodiscard]] Result exit(qint64 windowPid);
  [[nodiscard]] Result park(qint64 windowPid);
  [[nodiscard]] Result showLibrary(qint64 windowPid);
  [[nodiscard]] Result resume(const GameModeSettings& settings, qint64 windowPid);
  [[nodiscard]] Result refreshParked();
  [[nodiscard]] bool focusRetainedGame();
  void setTemporaryWindow(bool temporary) { m_temporaryWindow = temporary; }
  void setWindowVisibility(std::function<void(bool)> callback) {
    m_windowVisibility = std::move(callback);
  }

  // Runs after recording game presentation, before returning the desktop. Nonblocking.
  void setBeforeParkRestore(std::function<void()> callback) {
    m_beforeParkRestore = std::move(callback);
  }

  // Undoes a session left behind by a process that is gone. A no-op without one.
  [[nodiscard]] Result recover();

  [[nodiscard]] bool active() const { return m_active; }
  [[nodiscard]] bool parked() const { return m_parked; }
  [[nodiscard]] GameModePhase phase() const {
    return m_active   ? GameModePhase::Active
           : m_parked ? GameModePhase::DesktopRetained
                      : GameModePhase::Ended;
  }
  [[nodiscard]] const GameModeState& state() const { return m_state; }

  // Picks the configured display: by description first, since connector names move
  // between ports and reboots, then by connector. Returns -1 when it is not connected.
  [[nodiscard]] static int findOutput(const QVector<GameModeOutput>& outputs, const QString& name,
                                      const QString& description);

private:
  [[nodiscard]] bool managed() const;
  [[nodiscard]] bool save(const GameModeState& state) const;
  [[nodiscard]] bool load(GameModeState* state) const;
  void forget() const;
  // Shared by leaving, by a failed entry, and by recovery. Returns false when something
  // that needed undoing could not be undone.
  bool restore(GameModeState& state, qint64 windowPid, bool ownerGone, QStringList* notes,
               bool retained = false);
  bool muteGames(QString* error);
  bool unmuteGames(QStringList* notes);
  bool captureDesktop(GameModeState* state, QString* error);
  bool exposeGames(QStringList* notes, bool focus);
  bool finishRetention(qint64 windowPid, QStringList* notes);
  void visibility(bool visible) const;

  [[nodiscard]] bool waitFor(const std::function<bool()>& ready, int timeoutMs,
                             int stepMs = 250) const;
  void showPlaceholder(bool visible) const;

  GameModeCompositor* m_compositor = nullptr;
  GameModeAudio* m_audio = nullptr;
  GameModeNotifications* m_notifications = nullptr;
  QString m_statePath;
  Sleep m_sleep;
  OwnerAlive m_ownerAlive;
  Placeholder m_placeholder;
  GameModeState m_state;
  GameModeSettings m_sessionSettings;
  bool m_active = false;
  bool m_parked = false;
  bool m_temporaryWindow = false;
  std::function<void(bool)> m_windowVisibility;
  std::function<void()> m_beforeParkRestore;
};

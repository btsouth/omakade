#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The pieces of the desktop Game Mode touches, behind interfaces so the session logic
// can be exercised without a compositor, a sound server, or a shell.

// One compositor output, enabled or not.
struct GameModeOutput {
  int id = -1;
  QString name;        // connector, such as HDMI-A-2
  QString description; // make, model and serial; stable when connectors renumber
  bool enabled = false;
  bool focused = false;
  int width = 0;
  int height = 0;
  double scale = 1;
  int transform = 0;
  // Selector for the workspace the output currently shows. Empty when disabled.
  QString workspace;
};

struct GameModeSink {
  QString name;
  QString description;
};

// Omakade's own window as the compositor sees it.
struct GameModeWindow {
  QString address;
  QString workspace;
  QString output;
  bool floating = false;
  bool xwayland = false;
  // Fullscreen or maximized.
  bool fullscreen = false;
  int fullscreenMode = 0;
  int fullscreenClient = 0;
  [[nodiscard]] bool valid() const { return !address.isEmpty(); }
};

// Identity witnesses come from procfs, never a process name or launcher tree.
struct GameModeProcess {
  qint64 pid = 0;
  qint64 procStart = -1;
  QString steamAppId;
  // Ambiguity witnesses only: these never authorize muting.
  QString winePrefix;
  QString flatpakAppId;
  [[nodiscard]] bool valid() const { return pid > 0 && procStart >= 0; }
  bool operator==(const GameModeProcess& other) const {
    return pid == other.pid && procStart == other.procStart;
  }
};
struct GameModeGameWindow {
  QString address;
  GameModeProcess process;
  // Hyprland's independent compositor and client modes, captured before parking.
  int fullscreen = 0;
  int fullscreenClient = 0;
};
struct GameModeDesktopFocus {
  QString output;
  QString workspace;
  QString address;
};
struct GameModeStream {
  quint32 index = 0;
  GameModeProcess process;
  // PipeWire object.serial is unique for the lifetime of a stream. A reusable name,
  // node id or module-stream-restore.id is not a safe token.
  QString token;
  bool muted = false;
  [[nodiscard]] bool sameStream(const GameModeStream& other) const {
    return index == other.index && process == other.process && !token.isEmpty() &&
           token == other.token;
  }
};

class GameModeCompositor {
public:
  virtual ~GameModeCompositor() = default;
  // True when outputs, workspaces and windows can be managed. Without it Game Mode
  // stays on the current display and leaves window placement to the compositor.
  [[nodiscard]] virtual bool available() = 0;
  [[nodiscard]] virtual QVector<GameModeOutput> outputs(QString* error = nullptr) = 0;
  virtual bool setOutputEnabled(const QString& name, bool enabled, QString* error = nullptr) = 0;
  [[nodiscard]] virtual GameModeWindow windowForPid(qint64 pid) = 0;
  // How many mapped windows on `workspace` belong to a process other than `pid`. A game
  // or its launcher left there would be stranded on a workspace that is going away.
  [[nodiscard]] virtual int otherWindowsOn(const QString& workspace, qint64 pid) = 0;
  // Addresses of the mapped windows otherWindowsOn counts, so a session can bring them
  // home instead of leaving them on the workspace that is going away.
  [[nodiscard]] virtual QStringList otherWindowAddressesOn(const QString& workspace,
                                                           qint64 pid) = 0;
  // False means unknown, not an empty workspace. Only mapped game processes,
  // excluding launcher windows, may witness a retained game.
  virtual bool gameWindows(const QString&, qint64, QVector<GameModeGameWindow>*, QString* error) {
    if (error)
      *error = QStringLiteral("Safe game process discovery is unavailable.");
    return false;
  }
  virtual bool processAlive(const GameModeProcess&) { return false; }
  virtual bool desktopFocus(GameModeDesktopFocus*, QString* error) {
    if (error)
      *error = QStringLiteral("Desktop focus discovery is unavailable.");
    return false;
  }
  // Moves the entire existing workspace without following it, then verifies placement.
  virtual bool moveWorkspace(const QString&, const QString&, QString* error) {
    if (error)
      *error = QStringLiteral("Workspace relocation is unavailable.");
    return false;
  }
  // Omakade's placeholder window, which keeps the main window's place in the layout for
  // the session. Invalid until the compositor has mapped it.
  [[nodiscard]] virtual GameModeWindow placeholderForPid(qint64 pid) = 0;
  // Makes the placeholder open out of sight, so mapping it does not disturb the layout.
  virtual bool holdPlaceholder(QString* error = nullptr) = 0;
  // Focuses `output`, moves the window to `workspace` there, and focuses the window. With
  // a `placeholder`, that window first takes the main window's exact place in the layout.
  virtual bool placeWindow(const QString& address, const QString& workspace, const QString& output,
                           const QString& placeholder, QString* error = nullptr) = 0;
  // Fullscreens on the output's currently visible workspace, where frame callbacks
  // are available. The final dedicated-workspace move happens after preparation.
  virtual bool prepareWindow(const QString& address, const QString& visibleWorkspace,
                             const QString& output, const QString& placeholder,
                             QString* error = nullptr) {
    return placeWindow(address, visibleWorkspace, output, placeholder, error);
  }
  // Moves the window without following it. With a `placeholder`, the window trades places
  // with it instead and so returns to the exact spot it left.
  virtual bool returnWindow(const QString& address, const QString& workspace,
                            const QString& placeholder, QString* error = nullptr) = 0;
  virtual bool focusWindow(const QString& address, QString* error = nullptr) = 0;
  // Move the library to the landing workspace before focusing it. Focusing a
  // library left beside a frozen fullscreen game would reveal that game again.
  bool moveLibraryToDesktop(qint64 pid, const GameModeDesktopFocus& desktop,
                            QString* error = nullptr);
  virtual bool setWindowMode(const QString&, int, int, QString* error = nullptr) {
    if (error)
      *error = QStringLiteral("Window presentation cannot be restored.");
    return false;
  }
  // After the library UI settles, restore this game's recorded modes and focus it.
  // Only the owner's root and the verified game window may be changed.
  virtual bool focusGameWindow(const GameModeGameWindow&, qint64, QString* error = nullptr) {
    if (error)
      *error = QStringLiteral("Retained game presentation is unavailable.");
    return false;
  }
  virtual bool focusWorkspace(const QString& workspace, QString* error = nullptr) = 0;
  virtual bool focusOutput(const QString& name, QString* error = nullptr) = 0;
};

class GameModeAudio {
public:
  virtual ~GameModeAudio() = default;
  [[nodiscard]] virtual bool available() = 0;
  [[nodiscard]] virtual QVector<GameModeSink> sinks(QString* error = nullptr) = 0;
  [[nodiscard]] virtual QString defaultSink() = 0;
  virtual bool setDefaultSink(const QString& name, QString* error = nullptr) = 0;
  virtual bool streams(QVector<GameModeStream>*, QString* error) {
    if (error)
      *error = QStringLiteral("Safe audio stream discovery is unavailable.");
    return false;
  }
  // Implementations re-read and validate the complete stream identity before writing.
  virtual bool setStreamMuted(const GameModeStream&, bool, QString* error) {
    if (error)
      *error = QStringLiteral("Safe audio stream muting is unavailable.");
    return false;
  }
};

class GameModeNotifications {
public:
  virtual ~GameModeNotifications() = default;
  [[nodiscard]] virtual bool available() = 0;
  // Sets `silenced` and returns true when the current state could be read.
  [[nodiscard]] virtual bool silenced(bool* silenced) = 0;
  virtual bool setSilenced(bool silenced) = 0;
};

#pragma once

#include "gamemode/GameModePorts.h"

#include <QByteArray>
#include <QJsonObject>
#include <atomic>

// The real desktop behind Game Mode's interfaces. Each call runs one short, bounded
// command, so a stalled compositor or sound server cannot hang a session change.

// Hyprland through `hyprctl`. Outputs and windows are changed with `hyprctl eval`, which
// only exists with Hyprland's Lua configuration; an older Hyprland reports unavailable
// and Game Mode stays on the current display.
class HyprlandGameModeCompositor final : public GameModeCompositor {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] QVector<GameModeOutput> outputs(QString* error = nullptr) override;
  bool setOutputEnabled(const QString& name, bool enabled, QString* error = nullptr) override;
  [[nodiscard]] GameModeWindow windowForPid(qint64 pid) override;
  // Proton games: the tracked process is not always the window owner, the class is.
  [[nodiscard]] GameModeWindow windowForClass(const QString& windowClass);
  [[nodiscard]] int otherWindowsOn(const QString& workspace, qint64 pid) override;
  [[nodiscard]] QStringList otherWindowAddressesOn(const QString& workspace, qint64 pid) override;
  bool gameWindows(const QString& workspace, qint64 owner, QVector<GameModeGameWindow>* windows,
                   QString* error) override;
  bool processAlive(const GameModeProcess& process) override;
  bool desktopFocus(GameModeDesktopFocus* focus, QString* error) override;
  bool moveWorkspace(const QString& workspace, const QString& output, QString* error) override;
  [[nodiscard]] static QString moveWorkspaceScript(const QString& workspace, const QString& output);
  [[nodiscard]] GameModeWindow placeholderForPid(qint64 pid) override;
  bool holdPlaceholder(QString* error = nullptr) override;
  bool prepareColdWindow(QString* error = nullptr);
  [[nodiscard]] static QString coldWindowScript();
  bool placeWindow(const QString& address, const QString& workspace, const QString& output,
                   const QString& placeholder, QString* error = nullptr) override;
  bool prepareWindow(const QString& address, const QString& workspace, const QString& output,
                     const QString& placeholder, QString* error = nullptr) override;
  bool presentWindow(const QString& address, QString* error = nullptr) override;
  [[nodiscard]] static QString prepareScript(const QString& address, const QString& workspace,
                                             const QString& output, const QString& placeholder = {});
  bool returnWindow(const QString& address, const QString& workspace, const QString& placeholder,
                    QString* error = nullptr) override;
  bool focusWindow(const QString& address, QString* error = nullptr) override;
  bool setWindowMode(const QString& address, int mode, int clientMode,
                     QString* error = nullptr) override;
  bool focusGameWindow(const GameModeGameWindow& game, qint64 ownerPid,
                       QString* error = nullptr) override;
  bool focusWorkspace(const QString& workspace, QString* error = nullptr) override;
  bool focusOutput(const QString& name, QString* error = nullptr) override;

  // Parses `hyprctl -j monitors all`.
  [[nodiscard]] static QVector<GameModeOutput> parseOutputs(const QByteArray& json,
                                                            QString* error = nullptr);
  // The title of the placeholder window. The compositor tells it from the main window by
  // this, since both carry Omakade's window class.
  [[nodiscard]] static QString placeholderTitle();
  // Finds Omakade's window in `hyprctl -j clients`, preferring its own window class over
  // any other window the process owns. With `placeholder`, finds the placeholder instead.
  [[nodiscard]] static GameModeWindow parseWindow(const QByteArray& clientsJson,
                                                  const QVector<GameModeOutput>& outputs,
                                                  qint64 pid, bool placeholder = false);
  // The selector a dispatcher accepts for a workspace object: "3", "name:couch" or
  // "special:scratchpad". Empty for the placeholder a disabled output reports.
  [[nodiscard]] static QString workspaceSelector(const QJsonObject& workspace);
  // Addresses of the mapped, other-pid windows on `workspace`, parsed once from
  // `hyprctl -j clients` and shared by both window queries.
  [[nodiscard]] static QStringList otherWindowAddresses(const QByteArray& clientsJson,
                                                        const QString& workspace, qint64 pid);
  // Counts what otherWindowsOn reports, from `hyprctl -j clients`.
  [[nodiscard]] static int countOtherWindows(const QByteArray& clientsJson,
                                             const QString& workspace, qint64 pid);
  // A double-quoted Lua string literal. Names come from EDID and user configuration, so
  // they are never pasted into a script unescaped.
  [[nodiscard]] static QString luaString(const QString& value);
  [[nodiscard]] static bool validAddress(const QString& address);
  [[nodiscard]] static QString outputScript(const QString& name, bool enabled);
  [[nodiscard]] static QString holdScript();
  [[nodiscard]] static QString placeScript(const QString& address, const QString& workspace,
                                           const QString& output, const QString& placeholder = {});
  [[nodiscard]] static QString returnScript(const QString& address, const QString& workspace);
  [[nodiscard]] static QString tradeScript(const QString& address, const QString& placeholder);
  [[nodiscard]] static QString focusGameScript(const GameModeGameWindow& game,
                                               const QString& ownerAddress);

private:
  bool eval(const QString& script, QString* error);
  // -1 unknown, 0 unavailable, 1 available. Checked once per process.
  std::atomic<int> m_available{-1};
};

// PipeWire or PulseAudio through `pactl`.
class PactlGameModeAudio final : public GameModeAudio {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] QVector<GameModeSink> sinks(QString* error = nullptr) override;
  [[nodiscard]] QString defaultSink() override;
  bool setDefaultSink(const QString& name, QString* error = nullptr) override;

  bool streams(QVector<GameModeStream>* streams, QString* error) override;
  bool setStreamMuted(const GameModeStream& stream, bool muted, QString* error) override;
  // Parsing alone leaves procStart unknown; streams() fills it from procfs.
  [[nodiscard]] static bool parseStreams(const QByteArray& json, QVector<GameModeStream>* streams,
                                         QString* error = nullptr);

  // Parses `pactl -f json list sinks`.
  [[nodiscard]] static QVector<GameModeSink> parseSinks(const QByteArray& json,
                                                        QString* error = nullptr);
};

// Omarchy's do-not-disturb switch through `omarchy-shell`. Absent outside Omarchy, where
// notifications are simply left alone.
class OmarchyGameModeNotifications final : public GameModeNotifications {
public:
  [[nodiscard]] bool available() override;
  [[nodiscard]] bool silenced(bool* silenced) override;
  bool setSilenced(bool silenced) override;
};

#pragma once

#include <QLocalServer>
#include <QObject>

// Owns the per-user local socket that keeps one Omakade window open. A second launch
// forwards a short command instead of opening another window: "activate" raises the
// window, "play <key>" launches a library game, "rescan <source>" asks a source model
// to re-import, "game-mode enter", "game-mode exit" and "game-mode toggle" switch Game
// Mode, and "quit" closes Omakade.
class SingleInstance final : public QObject {
  Q_OBJECT

public:
  explicit SingleInstance(const QString& serverName = {}, QObject* parent = nullptr);
  [[nodiscard]] static QString defaultServerName();
  [[nodiscard]] bool claimOrNotify(const QByteArray& command = "activate");
  // Delivers a command to a running instance. Returns false when none is listening.
  [[nodiscard]] static bool sendCommand(const QString& serverName, const QByteArray& command);

signals:
  void activationRequested(bool fullscreen);
  void playRequested(const QString& launchKey);
  void rescanRequested(const QString& source);
  void gameModeRequested(bool enter);
  void gameModeToggleRequested();
  void gameModeDesktopRequested();
  void quitRequested();
  void trackingStorageFailed();
  // The recorder's durable recovery journal is full or was found damaged, so refused writes
  // are not currently guaranteed to survive a crash. Surfaced alongside the persistent
  // recorder status, not only in the daemon log.
  void journalProtectionDegraded();

private:
  QString m_serverName;
  QLocalServer m_server;
};

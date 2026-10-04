#pragma once

#include "guide/GuideInput.h"
#include "guide/GuideActions.h"
#include <QPointer>
#include <QLocalSocket>
#include <QJsonObject>
#include <QLocalServer>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class GameModeSession;
class GameLauncher;
class HyprlandGameModeCompositor;
class PlaySessionStore;
class UnifiedGameModel;

class InGameGuide final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool opened READ opened NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)
public:
  InGameGuide(PlaySessionStore* sessions, UnifiedGameModel* library, GameModeSession* gameMode,
              HyprlandGameModeCompositor* compositor, GameLauncher* launcher, bool enabled, QObject* parent = nullptr);
  ~InGameGuide() override;
  bool opened() const { return m_opened; }
  bool available() const { return m_enabled; }
  bool hasGame();
  Q_INVOKABLE bool toggle(const QString& node = {});
  Q_INVOKABLE void close();
  // Only enabled explicitly in isolated acceptance runs, through the authenticated socket.
  void setInjectedInputEnabled(bool enabled);
signals:
  void changed();
  void libraryRequested();
private:
  friend class InGameGuideTests;
  void refreshGame();
  QJsonObject payload() const;
  bool setPaused(bool paused);
  void stopGuard();
  void poll();
  void shell(const QStringList& arguments, std::function<void(bool, QByteArray)> done = {});
  void runShellCommand();
  void message(const QJsonObject& message);
  void send(const QJsonObject& message);
  void toast(const QString& title, const QString& detail = {});
  void finishClose(bool hide);
  PlaySessionStore* m_sessions;
  UnifiedGameModel* m_library;
  GameModeSession* m_gameMode;
  GameLauncher* m_launcher;
  HyprlandGameModeCompositor* m_compositor;
  bool m_enabled = false, m_opened = false, m_opening = false, m_paused = false;
  bool m_restoreFocus = true;
  bool m_pauseWhileOpen = true, m_injectedInput = false, m_polling = false;
  QVariantMap m_session, m_metadata, m_quitSession;
  QString m_output, m_family = "keyboard", m_token, m_socketPath;
  GuideInput m_input;
  int m_testPadWriter = -1;
  QProcess m_guard;
  QLocalSocket m_mango;
  GuideActions::Tree m_resumeTree, m_quitTree;
  QPointer<QLocalSocket> m_peer;
  QString m_grabWarning;
  bool m_forceReady = false, m_hudVisible = false;
  QLocalServer m_server;
  QTimer m_poll;
  struct Command { QStringList arguments; std::function<void(bool, QByteArray)> done; };
  QQueue<Command> m_commands;
  bool m_shellRunning = false;
};

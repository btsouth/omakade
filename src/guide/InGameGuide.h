#pragma once

#include "guide/GuideInput.h"
#include <QJsonObject>
#include <QLocalServer>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class GameModeSession;
class HyprlandGameModeCompositor;
class PlaySessionStore;
class UnifiedGameModel;

class InGameGuide final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool opened READ opened NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)
public:
  InGameGuide(PlaySessionStore* sessions, UnifiedGameModel* library, GameModeSession* gameMode,
              HyprlandGameModeCompositor* compositor, bool enabled, QObject* parent = nullptr);
  ~InGameGuide() override;
  bool opened() const { return m_opened; }
  bool available() const { return m_enabled; }
  bool hasGame();
  Q_INVOKABLE bool toggle(const QString& node = {});
  Q_INVOKABLE void close();
  // Only enabled explicitly in isolated acceptance runs, through the authenticated socket.
  void setInjectedInputEnabled(bool enabled) { m_injectedInput = enabled; }
signals:
  void changed();
  void libraryRequested();
private:
  void refreshGame();
  QJsonObject payload() const;
  bool setPaused(bool paused);
  void stopGuard();
  void poll();
  void shell(const QStringList& arguments, std::function<void(bool, QByteArray)> done = {});
  void message(const QJsonObject& message);
  void finishClose(bool hide);
  PlaySessionStore* m_sessions;
  UnifiedGameModel* m_library;
  GameModeSession* m_gameMode;
  HyprlandGameModeCompositor* m_compositor;
  bool m_enabled = false, m_opened = false, m_opening = false, m_paused = false;
  bool m_pauseWhileOpen = true, m_injectedInput = false, m_polling = false;
  QVariantMap m_session, m_metadata;
  QString m_output, m_family = "keyboard", m_token, m_socketPath;
  GuideInput m_input;
  QProcess m_guard;
  QLocalServer m_server;
  QTimer m_poll;
};

#pragma once

#include "guide/GuideInput.h"
#include "guide/GuideArt.h"
#include "guide/GuideActions.h"
#include "gamemode/GameModePorts.h"
#include "guide/GuidePlugin.h"
#include <QJsonArray>
#include <QPointer>
#include <QLocalSocket>
#include <QJsonObject>
#include <QLocalServer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QElapsedTimer>
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
  bool parked() const { return m_parked; }
  bool showing() const { return m_opened || m_opening; }
  bool available() const { return m_enabled; }
  bool hasGame();
  void prepare() { refreshGame(); }
  void setSnapshot(const QVariantMap& session, const QVariantMap& metadata, const QString& output,
                   const GameModeWindow& window = {});
  // The guide plugin is installed and enabled, so opening the guide can show something.
  // Without it the shortcut and controller button keep their Game Mode behavior.
  Q_INVOKABLE bool usable() const { return m_enabled && GuidePlugin::usable(m_pluginPaths); }
  void setPluginPaths(const GuidePlugin::Paths& paths) { m_pluginPaths = paths; }
  // With fallback, a guide the shell cannot show emits summonFailed so the caller can do
  // what the shortcut did before the guide.
  Q_INVOKABLE bool toggle(const QString& node = {}, bool fallback = false);
  Q_INVOKABLE void close();
  // Only enabled explicitly in isolated acceptance runs, through the authenticated socket.
  void setInjectedInputEnabled(bool enabled);
  void setDesktopEnvironment(const QProcessEnvironment& environment) { m_environment = environment; }
  void setContext(const QJsonObject& context);
  void restoreComplete(bool ok);
  void libraryUnavailable();
  void parkComplete(bool ok);
  void setAchievementDatabase(const QString& path) { m_achievementDatabase = path; }
signals:
  void changed();
  void libraryRequested();
  void parkRequested();
  void restoreRequested();
  // The shell could not show the guide. The game is already resumed and the pads released.
  void summonFailed();
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
  void finishClose(bool hide, bool retainPause = false);
  void parkNow();
  void finishPark();
  void restoreWindow(std::function<void(bool)> done);
  QProcessEnvironment m_environment = QProcessEnvironment::systemEnvironment();
  QJsonObject m_context, m_lastPayload;
  bool m_parked = false, m_parking = false, m_restoring = false, m_managedRetained = false;
  bool m_waitingManagedPark = false;
  bool m_libraryAfterPark = false;
  quint64 m_parkGeneration = 0, m_restoreGeneration = 0;
  QString m_restoreNode;
  bool m_restoreFallback = false;
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
  GuideArt m_art;
  QString m_achievementDatabase;
  static QJsonArray achievementItems(const QString& appId, const QString& path);
  GameModeWindow gameWindow(const QVariantMap& session) const;
  int m_testPadWriter = -1;
  QPointer<QProcess> m_guard;
  QString m_anrToken;
  bool m_restoreTookPause = false;
  QJsonArray m_guardPins;
  QElapsedTimer m_summonClock;
  bool m_refreshing = false;
  GameModeWindow m_window;
  QJsonArray m_achievements;
  QString m_achievementKey;
  void cacheAchievements();
  GuideActions::Tree m_resumeTree, m_quitTree;
  QPointer<QLocalSocket> m_peer;
  QString m_grabWarning;
  GuidePlugin::Paths m_pluginPaths;
  bool m_forceReady = false;
  QLocalServer m_server;
  QTimer m_poll;
  struct Command { QStringList arguments; std::function<void(bool, QByteArray)> done; };
  QQueue<Command> m_commands;
  bool m_shellRunning = false;
};

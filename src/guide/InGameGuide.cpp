#include "guide/InGameGuide.h"
#include "guide/GuidePayload.h"
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeSession.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "launch/GameLauncher.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/ProcFs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <unistd.h>

namespace {
QString executable(const QString& name) {
  const auto adjacent = QCoreApplication::applicationDirPath() + '/' + name;
  return QFileInfo(adjacent).isExecutable() ? adjacent : QStandardPaths::findExecutable(name);
}
QByteArray hyprland(const QString& query) {
  QProcess process;
  process.start("hyprctl", {"-j", query});
  if (!process.waitForFinished(1500)) { process.kill(); process.waitForFinished(); return {}; }
  return process.exitCode() == 0 ? process.readAllStandardOutput() : QByteArray{};
}
QString preferenceKey(const QVariantMap& session) {
  return QString::fromLatin1(session.value("source").toString().toUtf8().toHex()) + '/' +
         QString::fromLatin1(session.value("path").toString().toUtf8().toHex());
}
} // namespace

InGameGuide::InGameGuide(PlaySessionStore* sessions, UnifiedGameModel* library,
                          GameModeSession* gameMode, HyprlandGameModeCompositor* compositor,
                          bool enabled, QObject* parent)
    : QObject(parent), m_sessions(sessions), m_library(library), m_gameMode(gameMode),
      m_compositor(compositor), m_enabled(enabled) {
  m_socketPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                 QStringLiteral("/omakade-guide-%1").arg(::getuid());
  m_server.setSocketOptions(QLocalServer::UserAccessOption);
  if (enabled) {
    QLocalServer::removeServer(m_socketPath);
    m_enabled = m_server.listen(m_socketPath);
  }
  connect(&m_server, &QLocalServer::newConnection, this, [this] {
    while (auto* socket = m_server.nextPendingConnection()) {
      auto buffer = std::make_shared<QByteArray>();
      auto authenticated = std::make_shared<QString>();
      connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, authenticated] {
        buffer->append(socket->readAll());
        if (buffer->size() > 16384) { socket->abort(); return; }
        while (buffer->contains('\n')) {
          const auto end = buffer->indexOf('\n');
          const auto data = QJsonDocument::fromJson(buffer->left(end)).object();
          buffer->remove(0, end + 1);
          if (data.value("version") != QJsonValue(GuidePayload::kVersion) ||
              data.value("token").toString() != m_token || m_token.isEmpty()) continue;
          *authenticated = m_token;
          message(data);
        }
      });
      connect(socket, &QLocalSocket::disconnected, this, [this, socket, authenticated] {
        if (!authenticated->isEmpty() && *authenticated == m_token && (m_opened || m_opening)) finishClose(true);
        socket->deleteLater();
      });
    }
  });
  connect(&m_input, &GuideInput::lost, this, &InGameGuide::close);
  connect(&m_input, &GuideInput::action, this, [this](const QString& action, const QString& family) {
    if (!m_opened && !m_opening) return;
    if (m_family != family) {
      m_family = family;
      shell({"shell", "call", "omakade.guide", "update", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))});
    }
    shell({"shell", "call", "omakade.guide", "input", action});
  });
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &InGameGuide::poll);
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &InGameGuide::close);
  connect(&m_guard, &QProcess::finished, this, [this] {
    if (m_paused) { m_paused = false; finishClose(true); }
  });
}

InGameGuide::~InGameGuide() { finishClose(false); }

void InGameGuide::refreshGame() {
  if (m_sessions) m_sessions->refreshNowPlaying();
  const auto previous = m_session;
  const auto sessions = m_sessions ? m_sessions->nowPlaying() : QVariantList{};
  const auto active = QJsonDocument::fromJson(hyprland("activewindow")).object();
  const auto focusedPid = active.value("pid").toInteger();
  QVariantMap chosen;
  for (const auto& value : sessions) {
    const auto session = value.toMap();
    if (session.value("pid").toLongLong() == focusedPid ||
        (m_opened && session.value("pid") == previous.value("pid") &&
         session.value("procStart") == previous.value("procStart"))) { chosen = session; break; }
  }
  if (chosen.isEmpty() && !sessions.isEmpty()) chosen = sessions.first().toMap();
  // Steam games are not emulator recorder sessions. Attribute them by the process's exact
  // Steam app id and a library installation, never by Steam's launcher process tree.
  ProcessSnapshot steam;
  if (chosen.isEmpty() && (m_opened || active.value("fullscreen").toInt() != 0)) {
    const auto pid = m_opened ? previous.value("pid").toLongLong() : focusedPid;
    for (const auto& process : ProcFs::listProcesses(true)) {
      if (process.pid == pid && !process.steamAppId.isEmpty() && process.comm != "steam" &&
          process.comm != "steamwebhelper") { steam = process; break; }
    }
  }
  m_metadata.clear();
  if (m_library) {
    for (int row = 0; row < m_library->rowCount(); ++row) {
      const auto index = m_library->index(row);
      for (const auto& value : m_library->installations(row)) {
        const auto installation = value.toMap();
        const bool match = !chosen.isEmpty()
            ? installation.value("source") == chosen.value("source") &&
              (installation.value("installPath") == chosen.value("path") ||
               installation.value("appId") == chosen.value("path") ||
               installation.value("launchTarget") == chosen.value("path"))
            : steam.pid > 0 && installation.value("source") == "Steam" &&
              installation.value("appId").toString() == steam.steamAppId;
        if (!match) continue;
        for (const int role : {GameRoles::Title, GameRoles::CoverPath, GameRoles::HeroPath,
                               GameRoles::PlaytimeSeconds, GameRoles::AchievementsTotal,
                               GameRoles::AchievementsUnlocked, GameRoles::Tags}) {
          const auto data = index.data(role);
          if (data.isValid()) m_metadata.insert(QString::fromUtf8(GameRoles::names().value(role)), data);
        }
        m_metadata.insert("kind", GameLauncher::isEmulatorSourceName(installation.value("source").toString()) ? "emulator" : "native");
        if (chosen.isEmpty()) {
          chosen = {{"pid", steam.pid}, {"procStart", steam.procStart}, {"source", "Steam"},
                    {"path", steam.steamAppId}, {"name", index.data(GameRoles::Title)}};
        }
        break;
      }
      if (!m_metadata.isEmpty()) break;
    }
  }
  m_session = chosen;
  if (chosen.value("pid") != previous.value("pid") || chosen.value("procStart") != previous.value("procStart")) {
    stopGuard();
    const auto tags = m_metadata.value("tags").toStringList();
    bool online = false;
    for (const auto& tag : tags) if (tag.compare("online", Qt::CaseInsensitive) == 0 || tag.compare("multiplayer", Qt::CaseInsensitive) == 0) online = true;
    QSettings preferences;
    m_pauseWhileOpen = preferences.value("guide/pause/" + preferenceKey(chosen), !online).toBool();
  }
  m_output = m_gameMode ? m_gameMode->sessionOutputName() : QString{};
  if (!chosen.isEmpty() && m_compositor) {
    const auto window = m_compositor->windowForPid(chosen.value("pid").toLongLong());
    if (!window.output.isEmpty()) m_output = window.output;
  }
}

bool InGameGuide::hasGame() {
  if (!m_enabled) return false;
  refreshGame();
  return !m_session.isEmpty();
}

QJsonObject InGameGuide::payload() const {
  auto data = GuidePayload::build(m_session, m_metadata, m_output, m_family, m_pauseWhileOpen, m_paused);
  data.insert("backend", QJsonObject{{"socket", m_socketPath}, {"token", m_token}});
  return data;
}

bool InGameGuide::setPaused(bool paused) {
  if (!paused) { stopGuard(); return true; }
  if (m_paused) return true;
  const auto pid = m_session.value("pid").toLongLong();
  const auto start = m_session.value("procStart").toLongLong();
  if (!ProcFs::processAlive(pid, start) || start <= 0) return false;
  m_guard.start(executable("omakade-guide-guard"));
  if (!m_guard.waitForStarted(1500)) return false;
  const auto request = QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", pid}, {"start", start}}).toJson(QJsonDocument::Compact) + '\n';
  m_guard.write(request);
  m_guard.waitForBytesWritten(1000);
  if (!m_guard.waitForReadyRead(1500) || !QJsonDocument::fromJson(m_guard.readAllStandardOutput()).object().value("ok").toBool()) {
    stopGuard();
    return false;
  }
  m_paused = true;
  return true;
}

void InGameGuide::stopGuard() {
  m_paused = false;
  if (m_guard.state() == QProcess::NotRunning) return;
  m_guard.closeWriteChannel();
  if (!m_guard.waitForFinished(2000)) { m_guard.terminate(); m_guard.waitForFinished(1000); }
}

bool InGameGuide::toggle(const QString& node) {
  if (!m_enabled) return false;
  if (m_opened || m_opening) { close(); return true; }
  refreshGame();
  m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_family = node.isEmpty() ? "keyboard" : "generic";
  QString pad, error;
  if (!m_input.grab(node, &pad, &error)) {
    qWarning().noquote() << "Guide:" << error;
    m_token.clear();
    return true;
  }
  if (!node.isEmpty()) m_family = pad;
  if (!m_session.isEmpty() && m_pauseWhileOpen && !setPaused(true)) {
    m_pauseWhileOpen = false;
    qWarning("Guide: pause unavailable; the game remains running.");
  }
  m_opening = true;
  m_poll.start();
  const auto token = m_token;
  shell({"shell", "summon", "omakade.guide", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))},
        [this, token](bool ok, const QByteArray& reply) {
          if (token != m_token) return;
          if (!ok || reply.trimmed() == "unknown" || reply.trimmed() == "false" || reply.trimmed() == "error") finishClose(true);
        });
  return true;
}

void InGameGuide::close() { finishClose(true); }

void InGameGuide::finishClose(bool hide) {
  const bool hadGuide = m_opened || m_opening;
  m_opened = m_opening = false;
  m_token.clear();
  m_poll.stop();
  m_input.release();
  stopGuard();
  emit changed();
  if (hide && hadGuide) shell({"shell", "hide", "omakade.guide"});
}

void InGameGuide::message(const QJsonObject& data) {
  const auto action = data.value("action").toString();
  if (action == "opened") { m_opening = false; m_opened = true; emit changed(); }
  else if (action == "closed") finishClose(false);
  else if (action == "pause-while-open" && m_opened) {
    m_pauseWhileOpen = data.value("value").toBool();
    if (!setPaused(m_pauseWhileOpen)) m_pauseWhileOpen = false;
    QSettings preferences;
    preferences.setValue("guide/pause/" + preferenceKey(m_session), m_pauseWhileOpen);
    shell({"shell", "call", "omakade.guide", "update", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))});
  } else if (action == "inject" && m_injectedInput && m_opened) {
    const auto event = data.value("value").toObject();
    m_input.inject(event.value("type").toInt(), event.value("code").toInt(), event.value("value").toInt());
  } else if (action == "desktop" || action == "library") {
    close();
    if (action == "desktop" && m_gameMode) m_gameMode->park();
    else emit libraryRequested();
  } else if (action == "quit-confirmed" && m_sessions && m_opened) {
    stopGuard();
    m_sessions->stopSession(m_session.value("pid").toLongLong(), m_session.value("procStart").toLongLong());
    close();
  }
}

void InGameGuide::poll() {
  if (m_polling || (!m_opened && !m_opening)) return;
  m_polling = true;
  const auto token = m_token;
  shell({"shell", "call", "omakade.guide", "state", ""}, [this, token](bool ok, const QByteArray& reply) {
    m_polling = false;
    if (token != m_token) return;
    const auto state = QJsonDocument::fromJson(reply).object();
    if (!ok || state.value("token").toString() != token ||
        (!state.value("opened").toBool() && !state.value("opening").toBool())) {
      finishClose(true);
      return;
    }
    refreshGame();
    shell({"shell", "call", "omakade.guide", "update", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))});
  });
}

void InGameGuide::shell(const QStringList& arguments, std::function<void(bool, QByteArray)> done) {
  auto* process = new QProcess(this);
  connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
    if (done) done(status == QProcess::NormalExit && code == 0, process->readAllStandardOutput());
    process->deleteLater();
  });
  connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) { if (done) done(false, {}); process->deleteLater(); }
  });
  QTimer::singleShot(2500, process, [process] { process->kill(); });
  process->start("omarchy-shell", arguments);
}

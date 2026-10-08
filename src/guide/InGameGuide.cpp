#include "guide/InGameGuide.h"
#include "achievements/AchievementModel.h"
#include "guide/GuidePayload.h"
#include "saves/SaveLayouts.h"
#include "saves/SaveSetStore.h"
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeSession.h"
#include "library/GameRoles.h"
#include "library/UnifiedGameModel.h"
#include "launch/GameLauncher.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/ProcFs.h"
#include "tracking/SessionStopper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QUrl>
#include <QDateTime>
#include <QLocale>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QJsonObject>
#include <functional>
#include <memory>
#include <QSettings>
#include <QSaveFile>
#include <QElapsedTimer>
#include <csignal>
#include <fcntl.h>
#include <linux/input.h>
#include <QStandardPaths>
#include <QUuid>
#include <unistd.h>
#include <chrono>

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
                          GameLauncher* launcher, bool enabled, QObject* parent)
    : QObject(parent), m_sessions(sessions), m_library(library), m_gameMode(gameMode),
      m_launcher(launcher), m_compositor(compositor), m_enabled(enabled) {
  m_socketPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                 QStringLiteral("/omakade-guide-%1").arg(::getuid());
  m_server.setSocketOptions(QLocalServer::UserAccessOption);
  if (enabled) {
    // A second Omakade whose temp dir hides the first from SingleInstance must not take
    // over a live guide socket: the shell would lose the running app's pad input.
    QLocalSocket probe;
    probe.connectToServer(m_socketPath);
    if (probe.waitForConnected(250)) {
      qWarning("Guide: another Omakade owns %s; the guide stays with it.", qPrintable(m_socketPath));
      m_enabled = false;
    } else {
      QLocalServer::removeServer(m_socketPath);
      m_enabled = m_server.listen(m_socketPath);
    }
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
          if (data.value("action") == "opened") { *authenticated = m_token; m_peer = socket; }
          message(data);
          if (data.value("action") == "inject") { socket->write("{\"ok\":true}\n"); socket->flush(); }
        }
      });
      connect(socket, &QLocalSocket::disconnected, this, [this, socket, authenticated] {
        if (!authenticated->isEmpty() && *authenticated == m_token && (m_opened || m_opening)) finishClose(true);
        socket->deleteLater();
      });
    }
  });
  connect(&m_input, &GuideInput::action, this, [this](const QString& action, const QString& family) {
    if (!m_opened && !m_opening) return;
    if (m_opening) {
      if (action == "b" || action == "guide" || action == "start") close();
      return;
    }
    if (m_family != family) {
      m_family = family;
      shell({"shell", "call", "omakade.guide", "update", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))});
    }
    const auto received = QString::number(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (m_injectedInput && qEnvironmentVariableIsSet("OMAKADE_GUIDE_LEGACY_INPUT"))
      shell({"shell", "call", "omakade.guide", "input", QString::fromUtf8(QJsonDocument(QJsonObject{{"action", action}, {"receivedNs", received}}).toJson(QJsonDocument::Compact))});
    else send({{"type", "input"}, {"action", action}, {"receivedNs", received}});
  });
  connect(&m_art, &GuideArt::ready, this, [this](const QString& appId) {
    if ((m_opened || m_opening) && GuideArt::appId(m_session.value("source").toString(), m_metadata) == appId)
      send({{"type", "update"}, {"payload", payload()}});
  });
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &InGameGuide::poll);
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &InGameGuide::close);
  connect(&m_guard, &QProcess::finished, this, [this] {
    if (m_paused) { m_resumeTree.signal(SIGCONT); m_resumeTree.clear(); m_paused = false; finishClose(true); }
  });
}

InGameGuide::~InGameGuide() { finishClose(false); if (m_testPadWriter >= 0) ::close(m_testPadWriter); }

void InGameGuide::setInjectedInputEnabled(bool enabled) {
  m_injectedInput = enabled;
  if (enabled && qEnvironmentVariableIsSet("OMAKADE_GUIDE_TEST_UNGRABBABLE")) {
    GuideInput::Access access;
    access.scan = [] { return QList<GuideListener::Controller>{{"event-test", "test-pad", "Test pad", false}}; };
    access.open = [this](const QString&) {
          int pipe[2];
          if (::pipe2(pipe, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
          if (m_testPadWriter >= 0) ::close(m_testPadWriter);
          m_testPadWriter = pipe[1];
          return pipe[0];
        };
    access.grab = [](int) { return false; };
    m_input.setAccess(std::move(access));
  }
}

void InGameGuide::refreshGame() {
  if (m_opened && !m_quitSession.isEmpty()) return;
  if (m_sessions) m_sessions->refreshNowPlaying();
  const auto previous = m_session;
  auto sessions = m_sessions ? m_sessions->nowPlaying() : QVariantList{};
  // The launcher can describe games even when play-session recording is switched off.
  if (m_launcher) {
    for (const auto& value : m_launcher->trackedGames()) {
      const auto owned = value.toMap();
      const auto source = owned.value("source").toString();
      if (source != "Manual" && !GameLauncher::isEmulatorSourceName(source)) continue;
      if (!m_compositor || !m_compositor->windowForPid(owned.value("pid").toLongLong()).valid()) continue;
      bool present = false;
      for (auto& value : sessions) {
        auto session = value.toMap();
        if (session.value("pid") == owned.value("pid") && session.value("procStart") == owned.value("procStart")) {
          present = true;
          for (const auto& key : {"saveContext", "mangoSocket"}) if (owned.contains(key)) session.insert(key, owned.value(key));
          value = session;
        }
      }
      if (!present) sessions.append(owned);
    }
  }
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
              (installation.value("appId") == chosen.value("appId") ||
               installation.value("installPath") == chosen.value("path") ||
               installation.value("appId") == chosen.value("path") ||
               installation.value("launchTarget") == chosen.value("path"))
            : steam.pid > 0 && installation.value("source") == "Steam" &&
              installation.value("appId").toString() == steam.steamAppId;
        if (!match) continue;
        for (const int role : {GameRoles::Title, GameRoles::CoverPath, GameRoles::HeroPath, GameRoles::LogoPath,
                               GameRoles::PlaytimeSeconds, GameRoles::AchievementsTotal,
                               GameRoles::AchievementsUnlocked, GameRoles::Tags}) {
          if (role == GameRoles::PlaytimeSeconds && installation.value("source") == "Manual") continue;
          const auto data = index.data(role);
          if (data.isValid()) m_metadata.insert(QString::fromUtf8(GameRoles::names().value(role)), data);
        }
        m_metadata.insert("appId", installation.value("appId"));
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
  if (m_enabled) m_art.prefetch(m_session.value("source").toString(), m_metadata);
  if (chosen.value("pid") != previous.value("pid") || chosen.value("procStart") != previous.value("procStart")) {
    stopGuard();
    m_hudVisible = false;
    m_mango.abort();
    const auto tags = m_metadata.value("tags").toStringList();
    bool online = false;
    for (const auto& tag : tags) if (tag.compare("online", Qt::CaseInsensitive) == 0 || tag.compare("multiplayer", Qt::CaseInsensitive) == 0) online = true;
    QSettings preferences;
    m_pauseWhileOpen = preferences.value("guide/pause/" + preferenceKey(chosen), !online).toBool();
  }
  m_output = m_gameMode ? m_gameMode->sessionOutputName() : QString{};
  if (!chosen.isEmpty() && m_compositor) {
    const auto window = gameWindow(chosen);
    if (!window.output.isEmpty()) m_output = window.output;
  }
}

bool InGameGuide::hasGame() {
  if (!m_enabled) return false;
  refreshGame();
  return !m_session.isEmpty();
}

// The running Steam game's achievements for the guide's list: unlocked newest first, then
// locked by how common they are. Read-only, so the library's own model is left alone.
QJsonArray InGameGuide::achievementItems(const QString& appId) const {
  QJsonArray items;
  if (appId.isEmpty() || m_achievementDatabase.isEmpty() || !QFileInfo::exists(m_achievementDatabase)) return items;
  const auto connection = QStringLiteral("omakade-guide-achievements");
  {
    auto database = QSqlDatabase::contains(connection) ? QSqlDatabase::database(connection, false)
                                                       : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(m_achievementDatabase);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=200"));
    if (!database.isOpen() && !database.open()) return items;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT title, description, icon_url, icon_path, unlocked, unlock_time, rarity, hidden "
                                 "FROM achievements WHERE app_id = ? ORDER BY unlocked DESC, unlock_time DESC, rarity DESC, title LIMIT 300"));
    query.addBindValue(appId);
    if (!query.exec()) return items;
    while (query.next()) {
      const bool unlocked = query.value(4).toBool();
      const bool hidden = query.value(7).toBool();
      QJsonObject item{{"title", query.value(0).toString()}, {"unlocked", unlocked}, {"hidden", hidden},
                       {"rarity", query.value(6).toDouble()}};
      // Hidden achievements keep their secret until unlocked.
      if (unlocked || !hidden) item.insert("description", query.value(1).toString());
      const auto iconPath = query.value(3).toString();
      const QUrl iconUrl(query.value(2).toString());
      if (!iconPath.isEmpty() && QFileInfo::exists(iconPath)) item.insert("icon", QUrl::fromLocalFile(iconPath).toString());
      else if (AchievementModel::acceptsIconUrl(iconUrl)) item.insert("icon", iconUrl.toString());
      const auto time = query.value(5).toLongLong();
      if (unlocked && time > 0) item.insert("when", QLocale().toString(QDateTime::fromSecsSinceEpoch(time).date(), QStringLiteral("d MMM yyyy")));
      items.append(item);
    }
  }
  return items;
}

// The game's window: by its process, or for Steam games by the steam_app_<id> class.
GameModeWindow InGameGuide::gameWindow(const QVariantMap& session) const {
  if (!m_compositor) return {};
  auto window = m_compositor->windowForPid(session.value("pid").toLongLong());
  if (!window.valid() && session.value("source") == "Steam") {
    const auto appId = GuideArt::appId("Steam", {{"appId", session.value("path")}});
    if (!appId.isEmpty()) window = m_compositor->windowForClass("steam_app_" + appId);
  }
  return window;
}

QJsonObject InGameGuide::payload() const {
  auto data = GuidePayload::build(m_session, m_metadata, m_output, m_family, m_pauseWhileOpen, m_paused);
  auto model = data.value("data").toObject();
  if (!m_session.isEmpty()) {
    auto game = model.value("game").toObject();
    game.insert("forceReady", m_forceReady);
    const auto context = QJsonObject::fromVariantMap(m_session.value("saveContext").toMap());
    const auto layout = resolveSaveLayout(context, QDir::homePath(), QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/retroarch/retroarch.cfg");
    game.insert("canBackup", !context.isEmpty() && layout.valid());
    const auto items = achievementItems(GuideArt::appId(m_session.value("source").toString(), m_metadata));
    if (!items.isEmpty()) {
      auto achievements = game.value("achievements").toObject();
      int unlocked = 0;
      for (const auto& item : items) unlocked += item.toObject().value("unlocked").toBool() ? 1 : 0;
      achievements.insert("total", achievements.value("total").toInt(int(items.size())));
      achievements.insert("unlocked", achievements.value("unlocked").toInt(unlocked));
      achievements.insert("items", items);
      game.insert("achievements", achievements);
    }
    model.insert("game", game);
    QSettings settings;
    const bool steam = m_session.value("source") == "Steam";
    QFile sockets("/proc/net/unix");
    const bool hooked = !steam && !m_session.value("mangoSocket").toString().isEmpty() &&
        sockets.open(QIODevice::ReadOnly) && sockets.readAll().contains(("@" + m_session.value("mangoSocket").toString() + '\n').toUtf8());
    model.insert("performance", QJsonObject{{"mangohud", hooked},
        {"hud", settings.value("guide/hud", "off").toString()}, {"limit", settings.value("guide/limit", 0).toInt()},
        {"setupHint", QStandardPaths::findExecutable("mangohud").isEmpty() ? "Install the mangohud package to see frame rate"
                      : steam ? "Steam launch option: MANGOHUD=1 %command%" : "Launch this game again from Omakade"},
        {"nextLaunch", true}});
  }
  data.insert("data", model);
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
  const auto request = QJsonDocument(QJsonObject{{"action", "pause"}, {"pid", pid}, {"start", start}, {"recoverable", true}}).toJson(QJsonDocument::Compact) + '\n';
  m_guard.write(request);
  m_guard.waitForBytesWritten(1000);
  // Pin each identity before acknowledging the guard's SIGSTOP. This also covers
  // guard death between stopping a process and delivering the final reply.
  QJsonArray pinned;
  QByteArray pending;
  QElapsedTimer deadline; deadline.start();
  bool ok = false;
  while (deadline.elapsed() < 2500) {
    if (!m_guard.bytesAvailable() && !m_guard.waitForReadyRead(qMax(1, 2500 - int(deadline.elapsed())))) break;
    pending += m_guard.readAllStandardOutput();
    while (pending.contains('\n')) {
      const auto end = pending.indexOf('\n');
      const auto reply = QJsonDocument::fromJson(pending.left(end)).object(); pending.remove(0, end + 1);
      if (reply.contains("pin")) {
        pinned.append(reply.value("pin"));
        if (!m_resumeTree.adopt(pinned)) { stopGuard(); return false; }
        m_guard.write("pin-ok\n"); m_guard.waitForBytesWritten(200);
      } else if (reply.contains("ok")) { ok = reply.value("ok").toBool(); break; }
    }
    if (ok || m_guard.state() == QProcess::NotRunning) break;
  }
  if (!ok) { stopGuard(); return false; }
  m_paused = true;
  return true;
}

void InGameGuide::stopGuard() {
  m_paused = false;
  m_resumeTree.signal(SIGCONT); m_resumeTree.clear();
  if (m_guard.state() == QProcess::NotRunning) return;
  m_guard.closeWriteChannel();
  if (!m_guard.waitForFinished(2000)) { m_guard.terminate(); m_guard.waitForFinished(1000); }
}

bool InGameGuide::toggle(const QString& node) {
  if (!m_enabled) return false;
  if (m_opened || m_opening) { close(); return true; }
  refreshGame();
  m_restoreFocus = true;
  if (!m_session.isEmpty() && m_compositor &&
      ProcFs::processAlive(m_session.value("pid").toLongLong(), m_session.value("procStart").toLongLong())) {
    const auto window = gameWindow(m_session);
    if (window.valid()) m_compositor->focusWindow(window.address);
  }
  m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_family = node.isEmpty() ? "keyboard" : "generic";
  QString pad, error;
  m_forceReady = false; m_quitTree.clear(); m_quitSession.clear();
  m_input.grab(node, &pad, &error);
  m_grabWarning = error;
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
  m_polling = false;
  m_commands.clear();
  m_input.release();
  stopGuard();
  emit changed();
  if (hide && hadGuide) shell({"shell", "hide", "omakade.guide"});
  if (hadGuide && m_restoreFocus && !m_session.isEmpty()) {
    const auto game = m_session;
    QTimer::singleShot(100, this, [this, game] {
      if (m_opened || m_opening || !m_compositor || (m_gameMode && m_gameMode->parked()) ||
          !ProcFs::processAlive(game.value("pid").toLongLong(), game.value("procStart").toLongLong())) return;
      const auto window = gameWindow(game);
      if (window.valid()) m_compositor->focusWindow(window.address);
    });
  }
}

void InGameGuide::message(const QJsonObject& data) {
  const auto action = data.value("action").toString();
  if (action == "input-ack" && m_injectedInput) {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    qInfo().noquote() << "GUIDE_LATENCY_MS" << (now - data.value("value").toObject().value("receivedNs").toString().toLongLong()) / 1000000.0;
  }
  if (action == "opened") { m_opening = false; m_opened = true; emit changed(); }
  if (action == "opened" && !m_grabWarning.isEmpty()) toast(m_grabWarning);
  else if (action == "input-family" && m_opened && data.value("value") == "keyboard") m_family = "keyboard";
  else if (action == "closed") finishClose(false);
  else if (action == "pause-while-open" && m_opened) {
    m_pauseWhileOpen = data.value("value").toBool();
    if (!setPaused(m_pauseWhileOpen)) m_pauseWhileOpen = false;
    QSettings preferences;
    preferences.setValue("guide/pause/" + preferenceKey(m_session), m_pauseWhileOpen);
    shell({"shell", "call", "omakade.guide", "update", QString::fromUtf8(QJsonDocument(payload()).toJson(QJsonDocument::Compact))});
  } else if (action == "inject" && m_injectedInput && m_opened) {
    const auto event = data.value("value").toObject();
    if (m_testPadWriter >= 0) {
      input_event raw{};
      raw.type = event.value("type").toInt(); raw.code = event.value("code").toInt(); raw.value = event.value("value").toInt();
      ::write(m_testPadWriter, &raw, sizeof(raw));
    } else m_input.inject(event.value("type").toInt(), event.value("code").toInt(), event.value("value").toInt());
  } else if (action == "identify" && m_opened) {
    QString error;
    const bool ok = m_input.identify(data.value("value").toString(), &error);
    toast(ok ? "Controller identified" : "Controller could not be identified", error);
  } else if (action == "desktop" || action == "library") {
    m_restoreFocus = false;
    close();
    if (action == "desktop") {
      if (m_gameMode && m_gameMode->active()) m_gameMode->park();
      else QTimer::singleShot(150, this, [] { QProcess::startDetached("hyprctl", {"dispatch", "hl.dsp.focus({workspace=\"empty\"})"}); });
    } else emit libraryRequested();
  } else if (action == "backup" && m_opened && !m_session.value("saveContext").toMap().isEmpty()) {
    const auto context = QJsonObject::fromVariantMap(m_session.value("saveContext").toMap());
    const auto layout = resolveSaveLayout(context, QDir::homePath(), QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/retroarch/retroarch.cfg");
    if (!layout.valid()) { toast("Backup unavailable", layout.error); return; }
    const bool wasPaused = m_paused;
    if (!setPaused(true)) { toast("Backup unavailable", "The emulator could not be paused safely"); return; }
    SaveSetStore store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/guide-backups", [] { return false; });
    store.setPolicy(10, 2LL * 1024 * 1024 * 1024);
    QString error;
    const bool ok = store.snapshot(context.value("game").toString(), context, layout, &error);
    if (!wasPaused) stopGuard();
    toast(ok ? "Saves backed up" : "Backup failed", error);
  } else if (action == "steam-overlay" && m_opened && m_session.value("source") == "Steam") {
    if (!m_compositor) return;
    const auto window = gameWindow(m_session);
    const auto game = m_session;
    close();
    if (!window.valid()) { toast("Steam overlay unavailable", "Press Shift+Tab in the game"); return; }
    // The overlay hook in the game reads X11 key events, so it needs a real Shift press,
    // not Tab with a Shift flag. Wait until the resumed game holds focus, then send it once.
    auto attempts = std::make_shared<int>(0);
    auto send = std::make_shared<std::function<void()>>();
    *send = [this, window, game, attempts, send] {
      if (m_opened || m_opening || !ProcFs::processAlive(game.value("pid").toLongLong(), game.value("procStart").toLongLong()) ||
          gameWindow(game).address != window.address) return;
      m_compositor->focusWindow(window.address);
      QByteArray active;
      QProcess probe; probe.start("hyprctl", {"-j", "activewindow"}); probe.waitForFinished(500);
      active = probe.readAllStandardOutput();
      if (QJsonDocument::fromJson(active).object().value("address").toString() != window.address) {
        if (++*attempts < 8) QTimer::singleShot(100, this, *send);
        else toast("Steam overlay unavailable", "Press Shift+Tab in the game");
        return;
      }
      const auto xdotool = QStandardPaths::findExecutable("xdotool");
      if (!xdotool.isEmpty() && window.xwayland) {
        // XTEST into the game's XWayland server: the same events a keyboard makes.
        QProcess::startDetached(xdotool, {"key", "--delay", "60", "shift+Tab"});
        return;
      }
      const auto target = QStringLiteral("address:%1").arg(window.address);
      const QStringList steps{
        QStringLiteral("hl.dsp.send_key_state({mods=\"\",key=\"Shift_L\",state=\"down\",window=\"%1\"})").arg(target),
        QStringLiteral("hl.dsp.send_key_state({mods=\"SHIFT\",key=\"Tab\",state=\"down\",window=\"%1\"})").arg(target),
        QStringLiteral("hl.dsp.send_key_state({mods=\"SHIFT\",key=\"Tab\",state=\"up\",window=\"%1\"})").arg(target),
        QStringLiteral("hl.dsp.send_key_state({mods=\"\",key=\"Shift_L\",state=\"up\",window=\"%1\"})").arg(target)};
      for (int i = 0; i < steps.size(); ++i)
        QTimer::singleShot(i * 40, this, [step = steps[i]] { QProcess::startDetached("hyprctl", {"dispatch", step}); });
    };
    QTimer::singleShot(250, this, *send);
  } else if ((action == "quit-confirmed" || action == "force-quit") && m_opened) {
    stopGuard();
    if (action == "force-quit") {
      if (!m_forceReady) return;
      if (ProcFs::processAlive(m_quitSession.value("pid").toLongLong(), m_quitSession.value("procStart").toLongLong()))
        m_quitTree.pin(m_quitSession.value("pid").toLongLong(), m_quitSession.value("procStart").toLongLong());
      const auto pid = m_quitSession.value("pid").toLongLong();
      const bool stopped = m_sessions && m_sessions->forceStopSession(pid, m_quitSession.value("procStart").toLongLong());
      m_quitTree.signal(SIGKILL, stopped ? pid : -1); close(); return;
    }
    if (!m_quitTree.pin(m_session.value("pid").toLongLong(), m_session.value("procStart").toLongLong())) { toast("Game is no longer running"); return; }
    m_quitSession = m_session;
    const auto pid = m_quitSession.value("pid").toLongLong();
    const bool stopped = m_sessions && m_sessions->stopSession(pid, m_quitSession.value("procStart").toLongLong());
    m_quitTree.signal(SIGTERM, stopped ? pid : -1);
    const auto token = m_token;
    QTimer::singleShot(5000, this, [this, token] {
      if (token != m_token) return;
      if (!m_quitTree.alive()) { close(); return; }
      m_forceReady = true;
      send({{"type", "update"}, {"payload", payload()}});
      toast("Game is still running", "Force quit is now available");
    });
  } else if ((action == "hud" || action == "limit" || action == "enable-mangohud") && m_opened) {
    if (action == "enable-mangohud") {
      toast("MangoHud setup", QStandardPaths::findExecutable("mangohud").isEmpty() ? "Install it with: sudo pacman -S mangohud"
            : m_session.value("source") == "Steam" ? "Add MANGOHUD=1 %command% to the game's Steam launch options" : "Launch the game again from Omakade");
      return;
    }
    QSettings settings;
    if (action == "hud") {
      const auto level = data.value("value").toString();
      if (!QStringList{"off", "fps", "frametime", "full"}.contains(level)) return;
      settings.setValue("guide/hud", level);
      const auto command = GuideActions::mangoVisibilityCommand(m_hudVisible, level != "off");
      if (!command.isEmpty() && !m_session.value("mangoSocket").toString().isEmpty()) {
        if (m_mango.state() != QLocalSocket::ConnectedState) {
          m_mango.abort();
          m_mango.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
          m_mango.connectToServer(m_session.value("mangoSocket").toString());
          m_mango.waitForConnected(200);
        }
        if (m_mango.state() == QLocalSocket::ConnectedState && m_mango.write(command) == command.size() && m_mango.waitForBytesWritten(200)) m_hudVisible = level != "off";
        else toast("MangoHud control unavailable");
      }
    } else {
      const int limit = data.value("value").toInt(-1);
      if (limit < 0 || limit > 1000) return;
      settings.setValue("guide/limit", limit);
    }
    toast("Setting saved", "HUD detail and frame limit apply at next launch");
    send({{"type", "update"}, {"payload", payload()}});
  }
}

void InGameGuide::send(const QJsonObject& data) {
  if (!m_peer || m_peer->state() != QLocalSocket::ConnectedState) return;
  m_peer->write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
  m_peer->flush();
}
void InGameGuide::toast(const QString& title, const QString& detail) {
  send({{"type", "toast"}, {"title", title}, {"detail", detail}});
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
  m_commands.enqueue({arguments, std::move(done)});
  runShellCommand();
}

void InGameGuide::runShellCommand() {
  if (m_shellRunning || m_commands.isEmpty()) return;
  m_shellRunning = true;
  const auto command = m_commands.dequeue();
  auto* process = new QProcess(this);
  auto completed = std::make_shared<bool>(false);
  const auto finish = [this, process, command, completed](bool ok) {
    if (*completed) return;
    *completed = true;
    if (command.done) command.done(ok, process->readAllStandardOutput());
    process->deleteLater();
    m_shellRunning = false;
    QTimer::singleShot(0, this, &InGameGuide::runShellCommand);
  };
  connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
    finish(status == QProcess::NormalExit && code == 0);
  });
  connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) finish(false);
  });
  QTimer::singleShot(2500, process, [process] { process->kill(); });
  process->start("omarchy-shell", command.arguments);
}

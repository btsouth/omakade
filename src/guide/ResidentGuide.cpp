#include "guide/ResidentGuide.h"
#include "guide/GuideClient.h"
#include "guide/GuideEnvironment.h"
#include "gamemode/GameModeDesktop.h"
#include "app/SingleInstance.h"
#include "tracking/SessionDatabase.h"
#include "tracking/SessionDisplay.h"
#include "tracking/ProcFs.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent>
#include <memory>
#include <QMutex>
#include <QMutexLocker>
#include <unistd.h>
#include <chrono>

namespace {
QJsonArray sessionIdentities(const QJsonArray& sessions) {
  QJsonArray result;
  for (const auto& value : sessions) { auto session = value.toObject(); session.remove("elapsedSeconds"); result.append(session); }
  return result;
}
struct Snapshot { QVariantMap session, metadata; QString output; GameModeWindow window; bool locked = false; };
// Read only the focused process, never the whole environment or a whole-process Steam scan.
QVariantMap focusedSteam(qint64 pid) {
  static QHash<QString, QVariantMap> cache;
  if (pid <= 1 || !ProcFs::processRunning(pid)) return {};
  QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!stat.open(QIODevice::ReadOnly)) return {};
  const auto raw = stat.readAll(); const auto fields = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ');
  if (fields.size() < 20) return {};
  const auto start = fields[19].toLongLong();
  const auto key = QString::number(pid) + ":" + QString::number(start);
  if (cache.contains(key)) return cache.value(key);
  if (cache.size() >= 64) cache.clear();
  cache.insert(key, {});
  QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
  if (!comm.open(QIODevice::ReadOnly)) return {};
  const auto name = comm.readAll().trimmed(); if (name == "steam" || name == "steamwebhelper") return {};
  QFile environment(QStringLiteral("/proc/%1/environ").arg(pid));
  if (!environment.open(QIODevice::ReadOnly)) return {};
  QString id;
  for (const auto& field : environment.read(1024 * 1024).split('\0'))
    if (field.startsWith("SteamAppId=")) id = QString::fromLatin1(field.mid(11));
  if (GuideArt::appId("Steam", {{"appId", id}}).isEmpty() || !ProcFs::processAlive(pid, start)) return {};
  const QVariantMap result{{"pid", pid}, {"procStart", start}, {"source", "Steam"}, {"path", id}};
  cache.insert(key, result); return result;
}
Snapshot snapshot(const QJsonObject& active, const QJsonArray& clients, const QJsonArray& monitors,
                  const QJsonArray& published) {
  static QMutex mutex; QMutexLocker lock(&mutex);
  static QByteArray previousKey;
  static Snapshot previous;
  const auto databasePath = SessionDatabase::defaultDatabasePath();
  const QFileInfo databaseFile(databasePath), wal(databasePath + "-wal");
  const auto key = QJsonDocument(QJsonObject{{"active", active}, {"clients", clients}, {"monitors", monitors}, {"published", published},
    {"dbTime", QString::number(databaseFile.lastModified().toMSecsSinceEpoch())}, {"dbSize", QString::number(databaseFile.size())},
    {"walTime", QString::number(wal.lastModified().toMSecsSinceEpoch())}, {"walSize", QString::number(wal.size())}}).toJson(QJsonDocument::Compact);
  bool locked = false;
  // Lock ownership is checked even when the session/database cache is unchanged.
  for (const auto& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (QFileInfo("/proc/" + pid).ownerId() != uint(::getuid())) continue;
    QFile comm("/proc/" + pid + "/comm");
    if (comm.open(QIODevice::ReadOnly) && QList<QByteArray>{"hyprlock", "swaylock", "gtklock", "waylock"}.contains(comm.readAll().trimmed())) { locked = true; break; }
  }
  if (key == previousKey && (previous.session.isEmpty() || ProcFs::processAlive(previous.session.value("pid").toLongLong(), previous.session.value("procStart").toLongLong()))) {
    previous.locked = locked; return previous;
  }
  Snapshot result; result.locked = locked;
  auto candidates = published.toVariantList();
  const auto connection = "omakade-guide-snapshot-" + QUuid::createUuid().toString();
  {
    auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
    database.setDatabaseName(SessionDatabase::defaultDatabasePath());
    database.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=30");
    if (database.open()) {
      for (const auto& row : SessionDatabase::openSessions(database)) {
        if (!ProcFs::processAlive(row.pid, row.procStart)) continue;
        candidates.append(QVariantMap{{"pid", row.pid}, {"procStart", row.procStart}, {"path", row.gamePath},
          {"source", row.source}, {"name", SessionDisplay::titleForGamePath(row.gamePath)},
          {"elapsedSeconds", row.seconds + qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - (row.heartbeatAt > 0 ? row.heartbeatAt : row.startedAt))}});
      }
      const auto focused = active.value("pid").toInteger();
      for (const auto& candidate : candidates) {
        const auto session = candidate.toMap();
        if (session.value("pid").toLongLong() == focused && ProcFs::processAlive(focused, session.value("procStart").toLongLong())) { result.session = session; break; }
      }
      // A running game behind another window remains a guide target. Require its actual window.
      if (result.session.isEmpty()) for (const auto& candidate : candidates) {
        const auto session = candidate.toMap();
        for (const auto& value : clients)
          if (value.toObject().value("pid").toInteger() == session.value("pid").toLongLong() &&
              ProcFs::processAlive(session.value("pid").toLongLong(), session.value("procStart").toLongLong())) { result.session = session; break; }
        if (!result.session.isEmpty()) break;
      }
      if (result.session.isEmpty() && active.value("fullscreen").toInt() != 0) result.session = focusedSteam(focused);
      if (result.session.value("source") == "Steam") {
        const auto id = result.session.value("path").toString();
        QSqlQuery query(database); query.prepare("SELECT games.title, installations.cover_path, installations.hero_path, installations.logo_path FROM games JOIN installations USING(app_id) WHERE app_id = ?"); query.addBindValue(id);
        if (query.exec() && query.next()) { result.metadata = {{"title", query.value(0)}, {"appId", id}, {"coverPath", query.value(1)}, {"heroPath", query.value(2)}, {"logoPath", query.value(3)}}; result.session.insert("name", query.value(0)); }
        else result.session.clear(); // Exact library installation required, as before.
        if (!result.session.isEmpty()) {
          QSqlQuery tags(database); tags.prepare("SELECT tags_json FROM game_organization WHERE source = 'Steam' AND app_id = ?"); tags.addBindValue(id);
          QStringList values;
          if (tags.exec()) while (tags.next()) for (const auto& value : QJsonDocument::fromJson(tags.value(0).toByteArray()).array()) values.append(value.toString());
          result.metadata.insert("tags", values);
        }
      }
    }
  }
  QSqlDatabase::removeDatabase(connection);
  // GUI launch tracking works even when recording or the database is unavailable.
  if (result.session.isEmpty()) for (const auto& candidate : published) {
    const auto session = candidate.toObject().toVariantMap();
    if (!ProcFs::processAlive(session.value("pid").toLongLong(), session.value("procStart").toLongLong())) continue;
    for (const auto& value : clients) if (value.toObject().value("pid").toInteger() == session.value("pid").toLongLong()) { result.session = session; break; }
    if (!result.session.isEmpty()) break;
  }
  const auto publishedMetadata = result.session.value("metadata").toMap();
  for (auto it = publishedMetadata.cbegin(); it != publishedMetadata.cend(); ++it) result.metadata.insert(it.key(), it.value());
  if (!result.session.isEmpty() && !result.session.contains("path"))
    result.session.insert("path", result.session.value("source") == "Steam" ? result.session.value("appId") : result.session.value("installPath"));
  for (const auto& value : clients) {
    const auto client = value.toObject();
    if (client.value("pid").toInteger() != result.session.value("pid").toLongLong()) continue;
    result.window.address = client.value("address").toString();
    const int monitor = client.value("monitor").toInt(-1);
    for (const auto& value : monitors) if (value.toObject().value("id").toInt(-2) == monitor) result.output = value.toObject().value("name").toString();
    result.window.output = result.output;
    if (!result.session.isEmpty() && result.session.value("name").toString().isEmpty()) result.session.insert("name", client.value("title").toString());
    break;
  }
  if (!result.session.isEmpty()) {
    static QHash<QString, QVariantMap> performanceSources;
    const auto identity = QString::number(result.session.value("pid").toLongLong()) + ":" + QString::number(result.session.value("procStart").toLongLong());
    if (!performanceSources.contains(identity)) {
      if (performanceSources.size() >= 64) performanceSources.clear();
      performanceSources.insert(identity, GuideActions::performanceSource(result.session.value("pid").toLongLong(), result.session.value("procStart").toLongLong()));
    }
    result.session.insert("performanceSource", performanceSources.value(identity));
  }
  previousKey = key; previous = result; return result;
}
bool calmDesktop(const QProcessEnvironment& environment, const QJsonArray& published) {
  QJsonObject active;
  QJsonArray clients;
  for (const auto& query : {QString("clients"), QString("activewindow")}) {
    QProcess process; process.setProcessEnvironment(environment);
    process.start("hyprctl", {"-j", query});
    if (!process.waitForFinished(300)) { process.kill(); process.waitForFinished(100); return false; }
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || document.isNull()) return false;
    if (query == "clients") clients = document.array(); else active = document.object();
  }
  const auto current = snapshot(active, clients, {}, published);
  return current.session.isEmpty() && !current.locked;
}
}

ResidentGuide::ResidentGuide(QObject* parent) : QObject(parent), m_guide(nullptr, nullptr, nullptr, nullptr, nullptr, true) {
  const auto paths = GuidePlugin::defaultPaths(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
      QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation), QCoreApplication::applicationDirPath());
  m_guide.setPluginPaths(paths);
  QFile config(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omakade/config.toml");
  const bool couch = config.open(QIODevice::ReadOnly) && QRegularExpression("(?m)^couch_mode_enabled\\s*=\\s*true\\s*$").match(QString::fromUtf8(config.read(128 * 1024))).hasMatch();
  m_guide.setContext({{"scale", couch ? 1.7 : 1.0}});
  m_guide.setAchievementDatabase(SessionDatabase::defaultDatabasePath());
  m_guide.setInjectedInputEnabled(QCoreApplication::arguments().contains("--guide-input-test"));
  m_control.setSocketOptions(QLocalServer::UserAccessOption);
  if (!m_guide.available()) return;
  QLocalServer::removeServer(GuideClient::socketPath());
  if (!m_control.listen(GuideClient::socketPath())) { qWarning("Guide: resident control unavailable"); return; }
  connect(&m_control, &QLocalServer::newConnection, this, [this] {
    while (auto* socket = m_control.nextPendingConnection()) {
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        if (buffer->size() > 65536) { socket->abort(); return; }
        if (!buffer->contains('\n')) return;
        const auto data = QJsonDocument::fromJson(buffer->left(buffer->indexOf('\n'))).object();
        buffer->clear();
        const auto respond = [this, socket, data] {
          socket->write(QJsonDocument(command(data)).toJson(QJsonDocument::Compact) + '\n'); socket->flush(); socket->disconnectFromServer();
        };
        const auto action = data.value("action").toString();
        const bool reconcile = action == "shortcut" && !m_guide.showing() && !m_guide.hasGame();
        if ((!m_ready || reconcile) && !m_environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() &&
            (action == "shortcut" || action == "toggle")) {
          // A cache miss is reconciled from queries started after this request. An
          // already-running stale poll must not turn a newly launched game into GUI fallback.
          const int needed = m_refreshGeneration + (reconcile ? 1 : 0);
          auto replied = std::make_shared<bool>(false);
          connect(this, &ResidentGuide::snapshotReady, socket, [this, respond, needed, replied] {
            if (*replied) return;
            if (m_refreshGeneration < needed) { QTimer::singleShot(0, this, &ResidentGuide::refresh); return; }
            *replied = true; respond();
          });
          // End ownership before the client's 2 s deadline. A late snapshot
          // cannot summon a guide after the caller has selected its fallback.
          QTimer::singleShot(1500, socket, [socket, replied] {
            if (*replied) return;
            *replied = true;
            socket->write("{\"result\":\"preparing\"}\n"); socket->flush(); socket->disconnectFromServer();
          });
          refresh();
        } else respond();
      });
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      QTimer::singleShot(2500, socket, [socket] { socket->abort(); socket->deleteLater(); });
    }
  });
  connect(&m_guide, &InGameGuide::summonFailed, this, &ResidentGuide::fallback);
  connect(&m_guide, &InGameGuide::libraryRequested, this, [this] { launchLibrary(false); });
  const auto libraryCommand = [this](const QByteArray& command) {
    auto* socket = new QLocalSocket(this);
    connect(socket, &QLocalSocket::connected, socket, [socket, command] { socket->write(command); socket->flush(); socket->disconnectFromServer(); });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QLocalSocket::errorOccurred, this, [this, socket] { m_guide.libraryUnavailable(); socket->deleteLater(); });
    QTimer::singleShot(2000, socket, [this, socket] { socket->abort(); m_guide.libraryUnavailable(); socket->deleteLater(); });
    socket->connectToServer(SingleInstance::defaultServerName());
  };
  connect(&m_guide, &InGameGuide::parkRequested, this, [libraryCommand] { libraryCommand("game-mode desktop"); });
  connect(&m_guide, &InGameGuide::restoreRequested, this, [libraryCommand] { libraryCommand("game-mode enter"); });
  m_refresh.setInterval(5000);
  connect(&m_refresh, &QTimer::timeout, this, &ResidentGuide::refresh);
  m_debounce.setSingleShot(true); m_debounce.setInterval(40);
  connect(&m_debounce, &QTimer::timeout, this, &ResidentGuide::refresh);
  connect(&m_events, &QLocalSocket::connected, this, [this] { m_reconnect.stop(); refresh(); });
  connect(&m_events, &QLocalSocket::readyRead, this, [this] {
    m_eventBuffer += m_events.readAll();
    if (m_eventBuffer.size() > 65536) m_eventBuffer.clear();
    while (m_eventBuffer.contains('\n')) {
      const int end = m_eventBuffer.indexOf('\n');
      const auto event = m_eventBuffer.left(end).split('>').first(); m_eventBuffer.remove(0, end + 1);
      if (event.startsWith("activewindow") || event == "openwindow" || event == "closewindow" || event == "fullscreen" ||
          event.startsWith("workspace") || event.startsWith("monitor") || event.startsWith("lock")) {
        ++*m_desktopGeneration; m_debounce.start();
      }
    }
  });
  connect(&m_events, &QLocalSocket::disconnected, this, [this] { m_reconnect.start(); });
  connect(&m_events, &QLocalSocket::errorOccurred, this, [this] { m_reconnect.start(); });
  m_reconnect.setInterval(5000);
  connect(&m_reconnect, &QTimer::timeout, this, &ResidentGuide::connectEvents);
  connectEvents(); refresh();
}

void ResidentGuide::connectEvents() {
  if (m_resolving || m_events.state() == QLocalSocket::ConnectedState) return;
  m_resolving = true;
  auto* watcher = new QFutureWatcher<QProcessEnvironment>(this);
  connect(watcher, &QFutureWatcher<QProcessEnvironment>::finished, this, [this, watcher] {
    if (m_environment != watcher->result()) ++*m_desktopGeneration;
    m_environment = watcher->result(); watcher->deleteLater(); m_resolving = false;
    m_guide.setDesktopEnvironment(m_environment);
    const auto signature = m_environment.value("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty()) { m_reconnect.start(); return; }
    m_events.abort();
    m_events.connectToServer(m_environment.value("XDG_RUNTIME_DIR") + "/hypr/" + signature + "/.socket2.sock");
    refresh();
  });
  watcher->setFuture(QtConcurrent::run([environment = m_environment] { return GuideEnvironment::resolve(environment); }));
}

void ResidentGuide::launchLibrary(bool fallback) {
  const auto adjacent = QCoreApplication::applicationDirPath() + "/omakade";
  const auto executable = QFileInfo(adjacent).isExecutable() ? adjacent : QString("omakade");
  // Hyprland owns the launched library and its descendants, outside sessiond's
  // service cgroup. A recorder restart must never kill the user's game.
  auto quotedExecutable = executable; quotedExecutable.replace('\'', QString("'\\''"));
  const auto command = "'" + quotedExecutable + "'" + (fallback ? " --game-mode-fallback" : "");
  auto* process = new QProcess(this); process->setProcessEnvironment(m_environment);
  connect(process, &QProcess::finished, process, [process](int code, QProcess::ExitStatus status) {
    if (status != QProcess::NormalExit || code != 0 || process->readAllStandardOutput().trimmed() != "ok")
      qWarning("Guide: Hyprland could not launch the library");
    process->deleteLater();
  });
  connect(process, &QProcess::errorOccurred, process, [process] { process->deleteLater(); });
  QTimer::singleShot(2000, process, [process] { process->kill(); });
  process->start("hyprctl", {"eval", "hl.exec_cmd(" + HyprlandGameModeCompositor::luaString(command) + ")"});
}
void ResidentGuide::fallback() { launchLibrary(true); }
QJsonObject ResidentGuide::command(const QJsonObject& data) {
  const auto action = data.value("action").toString();
  QJsonObject reply{{"result", "handled"}};
  if (action == "status") {
    reply.insert("opened", m_guide.opened()); reply.insert("usable", m_guide.usable()); reply.insert("hasGame", m_guide.hasGame()); reply.insert("ready", m_ready);
  } else if (action == "prepare") refresh();
  else if (action == "publish") {
    const auto published = data.value("sessions").toArray();
    const bool changed = sessionIdentities(published) != sessionIdentities(m_published);
    if (changed) ++*m_desktopGeneration;
    m_published = published; m_guide.setContext(data.value("context").toObject());
    if (changed || !m_ready) refresh();
  }
  else if (action == "parked") m_guide.parkComplete(data.value("ok").toBool());
  else if (action == "restored") m_guide.restoreComplete(data.value("ok").toBool());
  else if (action == "close") m_guide.close();
  else if (action == "shortcut" || action == "toggle") {
    if (m_environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) return {{"result", "fallback"}};
    const auto requested = data.value("requestNs").toString().toLongLong();
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    qInfo("Guide timing: resident request origin=%s ipc_ms=%.3f", qPrintable(action), requested > 0 ? (now - requested) / 1000000.0 : 0.0);
    if (m_locked) return {{"result", "locked"}};
    if (!m_ready) return {{"result", "preparing"}};
    if (!m_guide.showing() && (!m_guide.hasGame() || !m_guide.usable())) {
      // A disabled plugin returns ownership to 1.15 Game Mode, including a
      // guide-owned parked pause. Let its toggle restore the window exactly once.
      if (m_guide.parked()) m_guide.close();
      return {{"result", "fallback"}};
    }
    if (action == "toggle" && !m_guide.usable()) return {{"result", "unavailable"}};
    m_guide.toggle(data.value("node").toString(), action == "shortcut");
  } else reply.insert("result", "unavailable");
  return reply;
}
void ResidentGuide::refresh() {
  if (m_environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
    ++m_refreshGeneration; m_ready = false; emit snapshotReady(); return;
  }
  if (m_guide.showing()) return;
  if (m_refreshing) { m_refreshPending = true; return; }
  m_refreshing = true;
  ++m_refreshGeneration;
  struct Queries { QJsonObject active; QJsonArray clients, monitors; int left = 3; bool ok = true; };
  auto queries = std::make_shared<Queries>();
  for (const auto& query : {QString("activewindow"), QString("clients"), QString("monitors")}) {
    auto* process = new QProcess(this);
    auto done = std::make_shared<bool>(false);
    const auto complete = [this, process, queries, query, done] {
      if (*done) return;
      *done = true;
      const auto document = QJsonDocument::fromJson(process->readAllStandardOutput());
      queries->ok = queries->ok && process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0 && !document.isNull();
      if (query == "activewindow") queries->active = document.object();
      else if (query == "clients") queries->clients = document.array(); else queries->monitors = document.array();
      process->deleteLater();
      if (--queries->left) return;
      if (!queries->ok) {
        m_refreshing = false; m_ready = false;
        emit snapshotReady();
        if (m_refreshPending) { m_refreshPending = false; m_debounce.start(); }
        return; // Unknown compositor state is never permission to provision mid-game.
      }
      auto* watcher = new QFutureWatcher<Snapshot>(this);
      connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); m_refreshing = false; m_ready = true; m_locked = result.locked;
        m_guide.setSnapshot(result.session, result.metadata, result.output, result.window);
        if (m_guide.hasGame()) m_refresh.start(); else m_refresh.stop();
        emit snapshotReady();
        if (m_refreshPending) { m_refreshPending = false; m_debounce.start(); }
        // This is the only provisioning path. Never mutate a plugin directory mid-game.
        const bool provision = !m_provisioned && !m_provisioning && m_provisionRetry->attempts < 3 && result.session.isEmpty() &&
                               !m_environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty();
        if (provision) {
          m_provisioning = true;
          GuidePlugin::ensureAsync(GuidePlugin::defaultPaths(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation), QCoreApplication::applicationDirPath()), this, [this](bool ok) {
              m_provisioned = ok;
              if (ok || m_provisionRetry->attempts >= 3) m_provisioning = false;
              else QTimer::singleShot(5000, this, [this] { m_provisioning = false; refresh(); });
            }, m_provisionRetry, m_environment, [environment = m_environment, published = m_published,
                generation = m_desktopGeneration, observed = m_desktopGeneration->load()] {
              // A new GUI publication or window event invalidates the captured sessions.
              return calmDesktop(environment, published) && generation->load() == observed;
            });
        }
      });
      watcher->setFuture(QtConcurrent::run([queries, published = m_published] { return snapshot(queries->active, queries->clients, queries->monitors, published); }));
    };
    connect(process, &QProcess::finished, this, [complete] { complete(); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) complete(); });
    QTimer::singleShot(1000, process, [process] { process->kill(); });
    process->setProcessEnvironment(m_environment);
    process->start("hyprctl", {"-j", query});
  }
}

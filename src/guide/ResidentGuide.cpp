#include "guide/ResidentGuide.h"
#include "guide/GuideClient.h"
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
#include <QUuid>
#include <QtConcurrent>
#include <memory>
#include <unistd.h>
#include <chrono>

namespace {
struct Snapshot { QVariantMap session, metadata; QString output; GameModeWindow window; bool locked = false; };
// Read only the focused process, never the whole environment or a whole-process Steam scan.
QVariantMap focusedSteam(qint64 pid) {
  if (pid <= 1 || !ProcFs::processRunning(pid)) return {};
  QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!stat.open(QIODevice::ReadOnly)) return {};
  const auto raw = stat.readAll(); const auto fields = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ');
  if (fields.size() < 20) return {};
  const auto start = fields[19].toLongLong();
  QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
  if (!comm.open(QIODevice::ReadOnly)) return {};
  const auto name = comm.readAll().trimmed(); if (name == "steam" || name == "steamwebhelper") return {};
  QFile environment(QStringLiteral("/proc/%1/environ").arg(pid));
  if (!environment.open(QIODevice::ReadOnly)) return {};
  QString id;
  for (const auto& field : environment.read(1024 * 1024).split('\0'))
    if (field.startsWith("SteamAppId=")) id = QString::fromLatin1(field.mid(11));
  if (GuideArt::appId("Steam", {{"appId", id}}).isEmpty() || !ProcFs::processAlive(pid, start)) return {};
  return {{"pid", pid}, {"procStart", start}, {"source", "Steam"}, {"path", id}};
}
Snapshot snapshot(const QJsonObject& active, const QJsonArray& clients, const QJsonArray& monitors,
                  const QJsonArray& published) {
  Snapshot result;
  // Lock policy is prepared off the input thread. This scan never happens on a Home press.
  for (const auto& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (QFileInfo("/proc/" + pid).ownerId() != uint(::getuid())) continue;
    QFile comm("/proc/" + pid + "/comm");
    if (comm.open(QIODevice::ReadOnly) && QList<QByteArray>{"hyprlock", "swaylock", "gtklock", "waylock"}.contains(comm.readAll().trimmed())) { result.locked = true; break; }
  }
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
  return result;
}
}

ResidentGuide::ResidentGuide(QObject* parent) : QObject(parent), m_guide(nullptr, nullptr, nullptr, nullptr, nullptr, true) {
  const auto paths = GuidePlugin::defaultPaths(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
      QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation), QCoreApplication::applicationDirPath());
  m_guide.setPluginPaths(paths);
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
        if ((!m_ready || reconcile) && (action == "shortcut" || action == "toggle")) {
          // A cache miss is reconciled from queries started after this request. An
          // already-running stale poll must not turn a newly launched game into GUI fallback.
          const int needed = m_refreshGeneration + (reconcile ? 1 : 0);
          auto replied = std::make_shared<bool>(false);
          connect(this, &ResidentGuide::snapshotReady, socket, [this, respond, needed, replied] {
            if (*replied) return;
            if (m_refreshGeneration < needed) { QTimer::singleShot(0, this, &ResidentGuide::refresh); return; }
            *replied = true; respond();
          });
          refresh();
        } else respond();
      });
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      QTimer::singleShot(2500, socket, [socket] { socket->abort(); socket->deleteLater(); });
    }
  });
  connect(&m_guide, &InGameGuide::summonFailed, this, &ResidentGuide::fallback);
  connect(&m_guide, &InGameGuide::libraryRequested, this, [] { QProcess::startDetached("omakade", {}); });
  m_refresh.setInterval(1000); connect(&m_refresh, &QTimer::timeout, this, &ResidentGuide::refresh); m_refresh.start(); refresh();
}

void ResidentGuide::fallback() {
  const auto adjacent = QCoreApplication::applicationDirPath() + "/omakade";
  QProcess::startDetached(QFileInfo(adjacent).isExecutable() ? adjacent : "omakade", {"--game-mode-fallback"});
}
QJsonObject ResidentGuide::command(const QJsonObject& data) {
  const auto action = data.value("action").toString();
  QJsonObject reply{{"result", "handled"}};
  if (action == "status") {
    reply.insert("opened", m_guide.opened()); reply.insert("usable", m_guide.usable()); reply.insert("hasGame", m_guide.hasGame()); reply.insert("ready", m_ready);
  } else if (action == "prepare") refresh();
  else if (action == "publish") { m_published = data.value("sessions").toArray(); refresh(); }
  else if (action == "close") m_guide.close();
  else if (action == "shortcut" || action == "toggle") {
    const auto requested = data.value("requestNs").toString().toLongLong();
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    qInfo("Guide timing: resident request origin=%s ipc_ms=%.3f", qPrintable(action), requested > 0 ? (now - requested) / 1000000.0 : 0.0);
    if (m_locked) return {{"result", "locked"}};
    if (!m_ready) return {{"result", "preparing"}};
    if (action == "shortcut" && !m_guide.showing() && (!m_guide.hasGame() || !m_guide.usable())) return {{"result", "fallback"}};
    if (action == "toggle" && !m_guide.usable()) return {{"result", "unavailable"}};
    m_guide.toggle(data.value("node").toString(), action == "shortcut");
  } else reply.insert("result", "unavailable");
  return reply;
}
void ResidentGuide::refresh() {
  if (m_refreshing || m_guide.showing()) return;
  m_refreshing = true;
  ++m_refreshGeneration;
  struct Queries { QJsonObject active; QJsonArray clients, monitors; int left = 3; };
  auto queries = std::make_shared<Queries>();
  for (const auto& query : {QString("activewindow"), QString("clients"), QString("monitors")}) {
    auto* process = new QProcess(this);
    auto done = std::make_shared<bool>(false);
    const auto complete = [this, process, queries, query, done] {
      if (*done) return;
      *done = true;
      const auto document = QJsonDocument::fromJson(process->readAllStandardOutput());
      if (query == "activewindow") queries->active = document.object();
      else if (query == "clients") queries->clients = document.array(); else queries->monitors = document.array();
      process->deleteLater();
      if (--queries->left) return;
      auto* watcher = new QFutureWatcher<Snapshot>(this);
      connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); m_refreshing = false; m_ready = true; m_locked = result.locked;
        m_guide.setSnapshot(result.session, result.metadata, result.output, result.window);
        emit snapshotReady();
        // This is the only provisioning path. Never mutate a plugin directory mid-game.
        const bool provision = !m_provisioned && result.session.isEmpty();
        m_provisioned = true;
        if (provision) {
          GuidePlugin::ensureAsync(GuidePlugin::defaultPaths(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation), QCoreApplication::applicationDirPath()), this);
        }
      });
      watcher->setFuture(QtConcurrent::run([queries, published = m_published] { return snapshot(queries->active, queries->clients, queries->monitors, published); }));
    };
    connect(process, &QProcess::finished, this, [complete] { complete(); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) complete(); });
    QTimer::singleShot(1000, process, [process] { process->kill(); });
    process->start("hyprctl", {"-j", query});
  }
}

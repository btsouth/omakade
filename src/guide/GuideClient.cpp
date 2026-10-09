#include "guide/GuideClient.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QLockFile>
#include <unistd.h>
#include <memory>

QString GuideClient::socketPath() {
  return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/omakade-guide-control-%1").arg(::getuid());
}

void GuideClient::ensureResident(QObject* owner) {
  if (owner->property("guideStarting").toBool()) return;
  owner->setProperty("guideStarting", true);
  request({{"action", "status"}}, owner, [owner](const QString& result, const QJsonObject& reply) {
    const auto run = [owner](const QStringList& arguments, std::function<void(bool)> done) {
      auto* process = new QProcess(owner);
      auto completed = std::make_shared<bool>(false);
      const auto finish = [process, completed, done](bool ok) {
        if (*completed) return;
        *completed = true; process->deleteLater(); done(ok);
      };
      QObject::connect(process, &QProcess::finished, owner, [finish](int code, QProcess::ExitStatus status) { finish(code == 0 && status == QProcess::NormalExit); });
      QObject::connect(process, &QProcess::errorOccurred, owner, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(false); });
      QTimer::singleShot(1000, process, [process] { process->kill(); });
      process->start("systemctl", QStringList{"--user"} + arguments);
    };
    const auto start = [owner, run, result, reply] {
      if (result == "handled" && reply.contains("ready")) { owner->setProperty("guideStarting", false); return; }
      run({"is-active", "--quiet", "omakade-sessiond.service"}, [owner, run](bool active) {
        run({active ? "try-restart" : "start", "omakade-sessiond.service"}, [owner](bool) { owner->setProperty("guideStarting", false); });
      });
    };
    // Reenable only a previously enabled 1.15 unit. Do not enable a unit the
    // user disabled or migrate unrelated user-unit links.
    const auto oldLink = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         "/systemd/user/default.target.wants/omakade-sessiond.service";
    if (QFileInfo(oldLink).isSymLink()) run({"reenable", "omakade-sessiond.service"}, [start](bool) { start(); });
    else start();
  });
}

void GuideClient::request(const QJsonObject& command, QObject* owner,
                          std::function<void(const QString&, const QJsonObject&)> done) {
  auto* socket = new QLocalSocket(owner);
  auto pending = std::make_shared<QByteArray>();
  auto completed = std::make_shared<bool>(false);
  const auto finish = [socket, done, completed](const QString& result, const QJsonObject& data) {
    if (*completed) return;
    *completed = true;
    socket->abort(); socket->deleteLater();
    if (done) done(result, data);
  };
  QObject::connect(socket, &QLocalSocket::connected, owner, [socket, command] {
    socket->write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
  });
  QObject::connect(socket, &QLocalSocket::readyRead, owner, [socket, pending, finish] {
    pending->append(socket->readAll());
    if (pending->size() > 65536) { finish("unavailable", {}); return; }
    if (!pending->contains('\n')) return;
    const auto reply = QJsonDocument::fromJson(pending->left(pending->indexOf('\n'))).object();
    finish(reply.value("result").toString("unavailable"), reply);
  });
  QObject::connect(socket, &QLocalSocket::errorOccurred, owner, [finish](QLocalSocket::LocalSocketError) { finish("unavailable", {}); });
  QTimer::singleShot(command.value("action") == "shortcut" || command.value("action") == "toggle" ? 2000 : 500, socket, [finish] { finish("unavailable", {}); });
  socket->connectToServer(socketPath());
}

void GuideClient::requestShortcut(const QString& node, QObject* owner,
                                  std::function<void(const QString&, const QJsonObject&)> done) {
  if (owner->property("guideShortcutPending").toBool()) {
    if (done) done("handled", {});
    return;
  }
  owner->setProperty("guideShortcutPending", true);
  auto* retry = new QTimer(owner);
  retry->setSingleShot(true);
  auto clock = std::make_shared<QElapsedTimer>(); clock->start();
  auto started = std::make_shared<bool>(false);
  QObject::connect(retry, &QTimer::timeout, owner, [retry, clock, started, node, owner, done] {
    request({{"action", "shortcut"}, {"node", node}}, owner,
            [retry, clock, started, owner, done](const QString& result, const QJsonObject& reply) {
      if (result != "unavailable" || clock->elapsed() >= 1800) {
        owner->setProperty("guideShortcutPending", false);
        retry->deleteLater(); if (done) done(result, reply); return;
      }
      if (!*started) {
        *started = true;
        ensureResident(owner);
      }
      retry->start(100);
    });
  });
  retry->start(0);
}

QString GuideClient::routeShortcut(const QString& node) {
  QLockFile lock(socketPath() + ".shortcut-lock");
  // Separate keyboard shortcut processes share the same in-flight request.
  if (!lock.tryLock()) return "handled";
  QEventLoop loop;
  QString result;
  QElapsedTimer elapsed; elapsed.start();
  requestShortcut(node, &loop, [&loop, &result](const QString& reply, const QJsonObject&) { result = reply; loop.quit(); });
  loop.exec();
  qInfo("Guide timing: shortcut IPC elapsed_ms=%lld result=%s", elapsed.elapsed(), qPrintable(result));
  return result;
}

GuideClient::GuideClient(bool enabled, QObject* parent) : QObject(parent), m_enabled(enabled) {
  if (!enabled) return;
  auto* timer = new QTimer(this); timer->setInterval(1000);
  const auto status = [this] {
    request({{"action", "status"}}, this, [this](const QString&, const QJsonObject& data) {
      const bool opened = data.value("opened").toBool();
      m_usable = data.value("usable").toBool(); m_hasGame = data.value("hasGame").toBool();
      if (opened != m_opened) { m_opened = opened; emit changed(); }
    });
  };
  connect(timer, &QTimer::timeout, this, status); timer->start(); status();
}
bool GuideClient::toggle(const QString& node, bool fallback) {
  if (!m_enabled) return false;
  const auto done = [this, fallback](const QString& result, const QJsonObject&) {
    if (fallback && (result == "fallback" || result == "unavailable" || result == "preparing")) emit summonFailed();
  };
  if (fallback) requestShortcut(node, this, done);
  else request({{"action", "toggle"}, {"node", node}}, this, done);
  return true;
}
void GuideClient::close() { if (m_enabled) request({{"action", "close"}}, this); }
void GuideClient::publish(const QVariantList& sessions, const QJsonObject& context) {
  if (m_enabled) request({{"action", "publish"}, {"sessions", QJsonArray::fromVariantList(sessions)}, {"context", context}}, this);
}

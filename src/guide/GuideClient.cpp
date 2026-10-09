#include "guide/GuideClient.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <unistd.h>
#include <memory>

QString GuideClient::socketPath() {
  return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/omakade-guide-control-%1").arg(::getuid());
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

QString GuideClient::routeShortcut(const QString& node) {
  QEventLoop loop;
  QString result;
  QElapsedTimer elapsed; elapsed.start();
  request({{"action", "shortcut"}, {"node", node}}, &loop,
          [&loop, &result](const QString& reply, const QJsonObject&) { result = reply; loop.quit(); });
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
  request({{"action", fallback ? "shortcut" : "toggle"}, {"node", node}}, this,
          [this, fallback](const QString& result, const QJsonObject&) {
            if (fallback && result == "fallback") emit summonFailed();
          });
  return true;
}
void GuideClient::close() { if (m_enabled) request({{"action", "close"}}, this); }
void GuideClient::publish(const QVariantList& sessions) {
  if (m_enabled) request({{"action", "publish"}, {"sessions", QJsonArray::fromVariantList(sessions)}}, this);
}

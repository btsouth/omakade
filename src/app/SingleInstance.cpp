#include "app/SingleInstance.h"

#include "tracking/SessionDatabase.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QLocalSocket>

namespace {
bool deliver(const QString& serverName, const QByteArray& command) {
  QLocalSocket socket;
  socket.connectToServer(serverName, QIODevice::WriteOnly);
  if (!socket.waitForConnected(120)) {
    return false;
  }
  socket.write(command);
  socket.flush();
  socket.waitForBytesWritten(120);
  return true;
}
// Owner-only access. Not QLocalServer::UserAccessOption: it binds elsewhere and renames
// the socket into place, which replaces a live instance instead of failing to listen.
bool listenOwnerOnly(QLocalServer& server, const QString& serverName) {
  if (!server.listen(serverName)) {
    return false;
  }
  QFile::setPermissions(server.fullServerName(), QFile::ReadOwner | QFile::WriteOwner);
  return true;
}
} // namespace

QString SingleInstance::defaultServerName() { return SessionDatabase::appServerName(); }

SingleInstance::SingleInstance(const QString& serverName, QObject* parent)
    : QObject(parent), m_serverName(serverName.isEmpty() ? defaultServerName() : serverName) {
  connect(&m_server, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
      // The client writes one command and closes, so act once the whole message is in.
      auto* buffer = new QByteArray;
      connect(socket, &QLocalSocket::readyRead, this,
              [socket, buffer] { buffer->append(socket->readAll()); });
      connect(socket, &QLocalSocket::disconnected, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        const QByteArray command = buffer->trimmed();
        delete buffer;
        if (command.startsWith("play ")) {
          emit playRequested(QString::fromUtf8(command.mid(5)).trimmed());
        } else if (command.startsWith("rescan ")) {
          // Sent by omakade-sessiond when an emulator whose own playtime is only
          // written on exit has ended a session.
          emit rescanRequested(QString::fromUtf8(command.mid(7)).trimmed());
        } else if (command == "tracking-storage-error") {
          emit trackingStorageFailed();
        } else if (command == "tracking-journal-full" || command == "tracking-journal-corrupt") {
          emit journalProtectionDegraded();
        } else if (command == "game-mode enter" || command == "game-mode exit") {
          emit gameModeRequested(command.endsWith("enter"));
        } else if (command == "game-mode desktop") {
          emit gameModeDesktopRequested();
        } else if (command == "game-mode return") {
          emit gameModeReturnRequested();
        } else if (command.startsWith("game-mode toggle")) {
          emit gameModeToggleRequested(QString::fromUtf8(command.mid(16)).trimmed());
        } else if (command == "fullscreen toggle") {
          emit fullscreenToggleRequested();
        } else if (command.startsWith("guide toggle")) {
          emit guideToggleRequested(QString::fromUtf8(command.mid(12)).trimmed());
        } else if (command == "clips-changed") {
          // Sent by omakade-sessiond when it saves a highlight clip.
          emit clipsChanged();
        } else if (command == "quit") {
          emit quitRequested();
        } else if (command.contains("activate")) {
          // "activate stream" comes from a launch inside a Sunshine session.
          emit activationRequested(command.contains("stream"));
        }
        socket->deleteLater();
      });
    }
  });
}

bool SingleInstance::sendCommand(const QString& serverName, const QByteArray& command) {
  if (!serverName.isEmpty()) {
    return deliver(serverName, command);
  }
  const QString current = defaultServerName();
  if (!current.isEmpty() && deliver(current, command)) {
    return true;
  }
  const QString legacy = SessionDatabase::legacyAppServerName();
  return !legacy.isEmpty() && deliver(legacy, command);
}

bool SingleInstance::claimOrNotify(const QByteArray& command) {
  // An Omakade started before an upgrade still owns the window on the old temp dir socket.
  if (m_serverName == defaultServerName()) {
    const QString legacy = SessionDatabase::legacyAppServerName();
    if (!legacy.isEmpty() && deliver(legacy, command)) {
      return false;
    }
  }
  if (m_serverName.isEmpty()) {
    qWarning("Running without single-instance activation: no usable XDG_RUNTIME_DIR.");
    return true;
  }
  if (listenOwnerOnly(m_server, m_serverName)) {
    return true;
  }
  if (sendCommand(m_serverName, command)) {
    return false;
  }
  if (m_server.serverError() != QAbstractSocket::AddressInUseError) {
    // An unwritable runtime directory must not leave the user with no window at all.
    qWarning() << "Running without single-instance activation:" << m_server.errorString();
    return true;
  }
  QLocalServer::removeServer(m_serverName);
  if (!listenOwnerOnly(m_server, m_serverName)) {
    qWarning() << "Running without single-instance activation:" << m_server.errorString();
  }
  return true;
}

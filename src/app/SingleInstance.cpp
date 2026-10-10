#include "app/SingleInstance.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocalSocket>

#include <unistd.h>

QString SingleInstance::defaultServerName() {
  return QStringLiteral("omakade-%1").arg(getuid());
}

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
        } else if (command.startsWith("game-mode return")) {
          emit gameModeReturnRequested(QString::fromUtf8(command.mid(16)).trimmed());
        } else if (command.startsWith("game-mode toggle")) {
          emit gameModeToggleRequested(QString::fromUtf8(command.mid(16)).trimmed());
        } else if (command.startsWith("guide toggle")) {
          emit guideToggleRequested(QString::fromUtf8(command.mid(12)).trimmed());
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
  QLocalSocket socket;
  socket.connectToServer(serverName.isEmpty() ? defaultServerName() : serverName,
                         QIODevice::WriteOnly);
  if (!socket.waitForConnected(120)) {
    return false;
  }
  socket.write(command);
  socket.flush();
  socket.waitForBytesWritten(120);
  return true;
}

bool SingleInstance::claimOrNotify(const QByteArray& command) {
  if (m_server.listen(m_serverName)) {
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
  if (!m_server.listen(m_serverName)) {
    qWarning() << "Running without single-instance activation:" << m_server.errorString();
  }
  return true;
}

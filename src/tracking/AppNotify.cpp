#include "tracking/AppNotify.h"

#include "tracking/SessionDatabase.h"

#include <QLocalSocket>

namespace AppNotify {

namespace {
bool deliver(const QString& name, const QByteArray& command, int timeoutMs) {
  QLocalSocket socket;
  socket.connectToServer(name, QIODevice::WriteOnly);
  if (!socket.waitForConnected(timeoutMs)) {
    return false;
  }
  socket.write(command);
  socket.flush();
  socket.waitForBytesWritten(timeoutMs);
  return true;
}
} // namespace

bool send(const QByteArray& command, int timeoutMs) {
  if (deliver(SessionDatabase::appServerName(), command, timeoutMs)) return true;
  const QString legacy = SessionDatabase::legacyAppServerName();
  return !legacy.isEmpty() && deliver(legacy, command, timeoutMs);
}

} // namespace AppNotify

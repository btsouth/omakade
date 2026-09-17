#include "tracking/DiscordPresence.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QtEndian>

namespace DiscordPresence {
namespace {
constexpr quint32 kOpHandshake = 0;
constexpr quint32 kOpFrame = 1;
constexpr int kHandshakeTimeoutMs = 300;
constexpr int kCommandTimeoutMs = 200;
// Discord increments the socket suffix when an earlier name is taken, most often by
// a second client. Six is what the reference clients search.
constexpr int kSocketCount = 6;
// A presence line is short by nature; anything longer than this is not a presence
// frame and reading more of it would only waste time.
constexpr int kMaxFrameBytes = 64 * 1024;

QByteArray littleEndian32(quint32 value) {
  QByteArray bytes(4, Qt::Uninitialized);
  qToLittleEndian(value, bytes.data());
  return bytes;
}

quint32 readLittleEndian32(const QByteArray& bytes) {
  if (bytes.size() < 4) {
    return 0;
  }
  return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()));
}

// Discord only ever shows the app name it has on file, so the client id selects
// which registered application the presence is published under. It is not a secret.
QString applicationId() {
  const QString configured =
      qEnvironmentVariable("OMAKADE_DISCORD_CLIENT_ID").trimmed();
  return configured;
}
} // namespace

QStringList socketCandidates(const QString& runtimeDirectory) {
  QStringList candidates;
  if (runtimeDirectory.isEmpty()) {
    return candidates;
  }
  const QString base = QDir(runtimeDirectory).filePath(QStringLiteral("discord-ipc"));
  for (int index = 0; index < kSocketCount; ++index) {
    candidates.append(QStringLiteral("%1-%2").arg(base).arg(index));
  }
  // A sandboxed client sees its own runtime directory, so the app-id directories
  // below the real one can hold the socket too.
  const QDir runtime(runtimeDirectory);
  const QStringList sandboxes = runtime.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& sandbox : sandboxes) {
    const QDir sandboxDir(runtime.filePath(sandbox));
    const QStringList nested = sandboxDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QStringList appIds = nested.isEmpty() ? QStringList{sandbox} : nested;
    for (const QString& appId : appIds) {
      const QString directory = sandboxDir.filePath(appId);
      for (int index = 0; index < kSocketCount; ++index) {
        candidates.append(
            QStringLiteral("%1/discord-ipc-%2").arg(directory).arg(index));
      }
    }
  }
  return candidates;
}

QByteArray frame(quint32 opcode, const QByteArray& payload) {
  return littleEndian32(opcode) + littleEndian32(static_cast<quint32>(payload.size())) + payload;
}

QByteArray handshakePayload(const QString& clientId) {
  QJsonObject object;
  object.insert(QStringLiteral("v"), 1);
  object.insert(QStringLiteral("client_id"), clientId);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray activityPayload(const QJsonObject& activity, const QString& nonce, qint64 pid) {
  QJsonObject args;
  args.insert(QStringLiteral("pid"), pid);
  if (activity.isEmpty()) {
    // Discord reads an absent activity as "clear the presence".
    args.insert(QStringLiteral("activity"), QJsonValue::Null);
  } else {
    args.insert(QStringLiteral("activity"), activity);
  }
  QJsonObject root;
  root.insert(QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY"));
  root.insert(QStringLiteral("args"), args);
  root.insert(QStringLiteral("nonce"), nonce);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject sessionActivity(const QString& name, const QString& source, qint64 startedAt,
                            int running) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QString trimmedSource = source.trimmed();
  QJsonObject activity;
  activity.insert(QStringLiteral("details"), trimmed);
  // Discord's second line reads as the party state, so it carries the source, or
  // how many games are running when there is more than one.
  if (running > 1) {
    activity.insert(QStringLiteral("state"),
                    trimmedSource.isEmpty()
                        ? QStringLiteral("%1 games").arg(running)
                        : QStringLiteral("%1 games via %2").arg(running).arg(trimmedSource));
  } else if (!trimmedSource.isEmpty()) {
    activity.insert(QStringLiteral("state"), trimmedSource);
  }
  if (startedAt > 0) {
    QJsonObject timestamps;
    timestamps.insert(QStringLiteral("start"), startedAt);
    activity.insert(QStringLiteral("timestamps"), timestamps);
  }
  return activity;
}

Client::Client(QString clientId, QStringList sockets)
    : m_clientId(clientId.trimmed()), m_sockets(std::move(sockets)) {
  if (m_sockets.isEmpty()) {
    m_sockets = socketCandidates(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
  }
}

Client::~Client() = default;

bool Client::sendFrame(quint32 opcode, const QByteArray& payload) {
  if (m_socket.state() != QLocalSocket::ConnectedState) {
    return false;
  }
  const QByteArray bytes = frame(opcode, payload);
  if (m_socket.write(bytes) != bytes.size()) {
    return false;
  }
  // flush() hands the frame to the socket, so the write buffer is empty afterwards
  // and waitForBytesWritten reports false on nothing pending. What matters is that
  // the bytes were accepted and the socket is still up.
  m_socket.flush();
  m_socket.waitForBytesWritten(kCommandTimeoutMs);
  return m_socket.state() == QLocalSocket::ConnectedState;
}

QByteArray Client::readFrame(int timeoutMs) {
  // Read the header, then exactly the announced length, so a partial write from
  // Discord cannot leave the stream misaligned for the next command.
  // Buffered bytes are consumed before waiting: a frame that arrived in one piece,
  // or a second frame read after the first, leaves nothing to wait for, and
  // waitForReadyRead only reports new data.
  const auto take = [&](int count) {
    QByteArray collected;
    while (collected.size() < count) {
      const int available = static_cast<int>(m_socket.bytesAvailable());
      if (available > 0) {
        collected.append(m_socket.read(count - collected.size()));
        continue;
      }
      if (m_socket.state() != QLocalSocket::ConnectedState ||
          !m_socket.waitForReadyRead(timeoutMs)) {
        return QByteArray{};
      }
    }
    return collected;
  };
  const QByteArray header = take(8);
  if (header.size() != 8) {
    return {};
  }
  const quint32 length = readLittleEndian32(header.mid(4, 4));
  if (length > kMaxFrameBytes) {
    return {};
  }
  return take(static_cast<int>(length));
}

bool Client::ensureConnected() {
  if (m_socket.state() == QLocalSocket::ConnectedState) {
    return m_handshaked;
  }
  for (const QString& candidate : m_sockets) {
    if (!QFileInfo::exists(candidate)) {
      continue;
    }
    m_socket.connectToServer(candidate, QIODevice::ReadWrite);
    if (!m_socket.waitForConnected(kHandshakeTimeoutMs)) {
      m_socket.abort();
      continue;
    }
    if (!sendFrame(kOpHandshake, handshakePayload(m_clientId))) {
      m_socket.abort();
      continue;
    }
    const QByteArray response = readFrame(kHandshakeTimeoutMs);
    if (response.isEmpty()) {
      m_socket.abort();
      continue;
    }
    const QJsonObject ready = QJsonDocument::fromJson(response).object();
    // A refused handshake (a bad client id, or Discord not accepting the request)
    // answers with an error instead of READY.
    if (ready.value(QStringLiteral("evt")).toString() != QStringLiteral("READY")) {
      m_socket.abort();
      continue;
    }
    m_handshaked = true;
    return true;
  }
  return false;
}

bool Client::setActivity(const QJsonObject& activity) {
  if (!configured()) {
    return false;
  }
  if (!ensureConnected()) {
    return false;
  }
  const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!sendFrame(kOpFrame,
                 activityPayload(activity, nonce, QCoreApplication::applicationPid()))) {
    // The socket died between polls or Discord was closed mid-session. Drop the
    // connection so the next call reconnects from scratch.
    m_socket.abort();
    m_handshaked = false;
    return false;
  }
  const QByteArray response = readFrame(kCommandTimeoutMs);
  if (response.isEmpty()) {
    m_socket.abort();
    m_handshaked = false;
    return false;
  }
  const QJsonObject reply = QJsonDocument::fromJson(response).object();
  if (reply.contains(QStringLiteral("evt"))) {
    // An error event instead of a command reply.
    return false;
  }
  return reply.value(QStringLiteral("nonce")).toString() == nonce;
}

} // namespace DiscordPresence

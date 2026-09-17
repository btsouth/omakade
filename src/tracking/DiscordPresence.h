#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>
#include <QStringList>

// Opt-in Discord Rich Presence, driven by the recorder so the presence matches the
// session the recorder is actually tracking. Discord is reached over its own local
// socket: a handshake naming Omakade's Discord application, then SET_ACTIVITY
// frames. Everything here is best effort. Discord not running, refusing the
// handshake or closing mid-session must never disturb recording, so no call can
// block for long and no failure is reported to the user.
namespace DiscordPresence {

// Discord's socket is called discord-ipc-0 in the runtime directory, with the
// suffix incremented when another client already holds that name. A Flatpak or
// Snap install is sandboxed into its own runtime directory, which is why the app
// id directories are searched too.
[[nodiscard]] QStringList socketCandidates(const QString& runtimeDirectory);

// Discord's IPC framing: a little-endian opcode, a little-endian payload length,
// then that many bytes of UTF-8 JSON. Opcode 0 is the handshake, opcode 1 carries
// every command afterwards.
[[nodiscard]] QByteArray frame(quint32 opcode, const QByteArray& payload);
[[nodiscard]] QByteArray handshakePayload(const QString& clientId);

// The SET_ACTIVITY command. Discord matches a response to its request by nonce, so
// one is always sent. An empty activity clears the presence.
[[nodiscard]] QByteArray activityPayload(const QJsonObject& activity, const QString& nonce,
                                         qint64 pid);

// The activity for a running session. An empty object means there is nothing to
// show. Only the game name and its source are published: no file paths, no
// personal details.
[[nodiscard]] QJsonObject sessionActivity(const QString& name, const QString& source,
                                         qint64 startedAt, int running);

class Client {
public:
  // A client with no application id is inert, so an unconfigured or disabled
  // install costs nothing.
  Client(QString clientId, QStringList sockets = {});
  ~Client();

  [[nodiscard]] bool configured() const { return !m_clientId.isEmpty(); }

  // Sends the activity, connecting and handshaking first when needed. An empty
  // activity clears the presence. Returns whether Discord accepted it.
  bool setActivity(const QJsonObject& activity);

private:
  bool ensureConnected();
  [[nodiscard]] bool sendFrame(quint32 opcode, const QByteArray& payload);
  [[nodiscard]] QByteArray readFrame(int timeoutMs);

  QString m_clientId;
  QStringList m_sockets;
  QLocalSocket m_socket;
  bool m_handshaked = false;
};

} // namespace DiscordPresence

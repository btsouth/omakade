#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

struct RommCredentialResult {
  bool success = false;
  QByteArray token;
  QString error;
};
// Blocking keyring operations. Call from a worker, never from the GUI thread.
// Empty token removes the entry. Credentials are scoped to the normalized server URL.
class RommCredentials final {
public:
  static RommCredentialResult load(const QUrl& server);
  static RommCredentialResult store(const QUrl& server, const QByteArray& token);
};

#include "sources/romm/RommCredentials.h"
#include "app/SecretService.h"
#include "sources/romm/RommCatalog.h"
#include <QMutexLocker>
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
const SecretSchema* schema() {
  static const SecretSchema* value =
      secret_schema_new("io.github.tsouth89.Omakade.RomM", SECRET_SCHEMA_NONE, "server",
                        SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return value;
}
} // namespace
RommCredentialResult RommCredentials::load(const QUrl& server) {
  const auto normalized = RommCatalog::serverUrl(server);
  if (normalized.isEmpty())
    return {false, {}, "Invalid RomM server."};
  QMutexLocker lock(&secretServiceLock());
  GError* error = nullptr;
  auto* value = secret_password_lookup_sync(schema(), nullptr, &error, "server",
                                            normalized.toEncoded().constData(), nullptr);
  QByteArray token = value ? QByteArray(value) : QByteArray{};
  if (value)
    secret_password_free(value);
  if (error) {
    g_error_free(error);
    return {false, {}, "Could not read the RomM token from the system keyring."};
  }
  if (!token.isEmpty() && !RommCatalog::validToken(token))
    return {false, {}, "The saved RomM Client API Token is invalid."};
  return {true, token, {}};
}
RommCredentialResult RommCredentials::store(const QUrl& server, const QByteArray& token) {
  const auto normalized = RommCatalog::serverUrl(server);
  if (normalized.isEmpty() || (!token.isEmpty() && !RommCatalog::validToken(token)))
    return {false, {}, "Check the RomM server and Client API Token."};
  QMutexLocker lock(&secretServiceLock());
  GError* error = nullptr;
  bool okay = true;
  if (token.isEmpty()) {
    secret_password_clear_sync(schema(), nullptr, &error, "server",
                               normalized.toEncoded().constData(), nullptr);
  } else {
    okay = secret_password_store_sync(
        schema(), SECRET_COLLECTION_DEFAULT, "Omakade RomM Client API Token", token.constData(),
        nullptr, &error, "server", normalized.toEncoded().constData(), nullptr);
  }
  if (error) {
    g_error_free(error);
    okay = false;
  }
  return {okay, {}, okay ? QString{} : "Could not update the RomM token in the system keyring."};
}

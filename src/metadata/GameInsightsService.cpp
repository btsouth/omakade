#include "app/SecretService.h"
#include <QMutexLocker>
#include "metadata/GameInsightsService.h"

#include "app/AppSettings.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtConcurrent>

#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
constexpr auto kSecretService = "igdb-client-secret";
constexpr qsizetype kMaximumResponseBytes = 1024 * 1024;
constexpr qint64 kCacheLifetimeSeconds = 30 * 24 * 60 * 60;

const SecretSchema* insightsSchema() {
  static const SecretSchema* schema =
      secret_schema_new("io.github.tsouth89.Omakade.IGDB", SECRET_SCHEMA_NONE, "service",
                        SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return schema;
}

InsightsSecretResult secretOperation(int action, QByteArray value) {
  // libsecret builds its GObject types on first use and cannot take two threads at once.
  QMutexLocker keyring(&secretServiceLock());
  GError* error = nullptr;
  InsightsSecretResult result;
  if (action == 0 || action == 3) {
    gchar* password = secret_password_lookup_sync(insightsSchema(), nullptr, &error, "service",
                                                  kSecretService, nullptr);
    result.success = error == nullptr;
    result.found = password != nullptr;
    if (password != nullptr) {
      if (action == 3) {
        result.secret = QByteArray(password);
      }
      secret_password_free(password);
    }
  } else if (action == 1) {
    result.success = secret_password_store_sync(
        insightsSchema(), SECRET_COLLECTION_DEFAULT, "Omakade IGDB client secret",
        value.constData(), nullptr, &error, "service", kSecretService, nullptr);
    result.found = result.success;
  } else {
    secret_password_clear_sync(insightsSchema(), nullptr, &error, "service", kSecretService,
                               nullptr);
    result.success = error == nullptr;
  }
  if (error != nullptr) {
    result.error = QString::fromUtf8(error->message);
    g_error_free(error);
  }
  value.fill('\0');
  return result;
}

int hoursFor(int seconds) {
  return seconds <= 0 ? 0 : qMax(1, qRound(static_cast<double>(seconds) / 3600.0));
}
} // namespace

GameInsightsService::GameInsightsService(const QString& databasePath, AppSettings* settings,
                                         QObject* parent)
    : QObject(parent), m_settings(settings),
      m_connectionName(QStringLiteral("omakade-insights-%1").arg(QUuid::createUuid().toString())) {
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  m_database.open();
  connect(&m_secretWatcher, &QFutureWatcher<InsightsSecretResult>::finished, this,
          &GameInsightsService::finishSecretOperation);
  if (m_settings != nullptr) {
    connect(m_settings, &AppSettings::igdbClientIdChanged, this, [this] {
      if (!clientId().isEmpty() && !m_hasClientSecret && !m_busy) {
        beginSecretOperation(SecretAction::Detect);
      }
      emit changed();
    });
  }
  // Without a client ID there is nothing to look up, so skip the keyring at startup.
  if (!clientId().isEmpty()) {
    beginSecretOperation(SecretAction::Detect);
  }
}

GameInsightsService::~GameInsightsService() {
  if (m_secretWatcher.isRunning()) {
    m_secretWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

QString GameInsightsService::clientId() const {
  return m_settings == nullptr ? QString{} : m_settings->igdbClientId();
}
bool GameInsightsService::hasClientSecret() const { return m_hasClientSecret; }
bool GameInsightsService::configured() const { return !clientId().isEmpty() && m_hasClientSecret; }
bool GameInsightsService::busy() const { return m_busy; }
bool GameInsightsService::available() const {
  return m_insight.criticScore >= 0 || m_insight.rushedSeconds > 0 || m_insight.normalSeconds > 0 ||
         m_insight.completeSeconds > 0;
}
QString GameInsightsService::statusText() const { return m_statusText; }
int GameInsightsService::criticScore() const { return m_insight.criticScore; }
int GameInsightsService::criticReviewCount() const { return m_insight.criticReviewCount; }
int GameInsightsService::rushedHours() const { return hoursFor(m_insight.rushedSeconds); }
int GameInsightsService::normalHours() const { return hoursFor(m_insight.normalSeconds); }
int GameInsightsService::completeHours() const { return hoursFor(m_insight.completeSeconds); }
int GameInsightsService::timeSampleCount() const { return m_insight.timeSampleCount; }

void GameInsightsService::setClientId(const QString& value) {
  if (m_busy) return;
  m_accessToken.fill('\0'); m_accessToken.clear(); m_accessTokenExpiry = 0;
  if (m_settings != nullptr) {
    m_settings->setIgdbClientId(value);
  }
}

void GameInsightsService::storeClientSecret(QString secret) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9]{20,128}$"));
  QString normalized = secret.trimmed();
  if (clientId().isEmpty() || !valid.match(normalized).hasMatch()) {
    secret.fill(QChar::Null);
    normalized.fill(QChar::Null);
    m_statusText = clientId().isEmpty() ? QStringLiteral("Save the IGDB client ID first")
                                        : QStringLiteral("That IGDB client secret is invalid");
    emit changed();
    return;
  }
  QByteArray bytes = normalized.toLatin1();
  secret.fill(QChar::Null);
  normalized.fill(QChar::Null);
  beginSecretOperation(SecretAction::Store, bytes);
  bytes.fill('\0');
}

void GameInsightsService::removeCredentials() { beginSecretOperation(SecretAction::Remove); }

void GameInsightsService::loadSteam(const QString& appId) {
  clearCurrent();
  m_appId = appId;
  if (IgdbApi::steamMappingQuery(appId).isEmpty()) {
    m_statusText.clear();
    emit changed();
    return;
  }
  const bool cached = loadCache(appId);
  if (cached) {
    m_statusText = m_insight.gameId > 0 ? QStringLiteral("Cached IGDB data")
                                        : QStringLiteral("IGDB has no entry for this game");
  } else if (!configured()) {
    m_statusText = QStringLiteral("Connect IGDB in settings for game insights");
  }
  emit changed();
  if (configured() &&
      (!cached || m_updatedAt < QDateTime::currentSecsSinceEpoch() - kCacheLifetimeSeconds)) {
    refreshSteam(appId);
  }
}

void GameInsightsService::refreshSteam(const QString& appId) {
  if (m_busy || !configured() || IgdbApi::steamMappingQuery(appId).isEmpty()) {
    return;
  }
  m_appId = appId;
  m_refreshAppId = appId;
  // Twitch app tokens last weeks. Reuse the one from this session instead of reading the
  // client secret and requesting a new token for every game.
  if (!m_accessToken.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_accessTokenExpiry - 60) {
    m_busy = true;
    m_statusText = QStringLiteral("Refreshing IGDB");
    emit changed();
    requestMapping();
    return;
  }
  beginSecretOperation(SecretAction::Lookup);
}

void GameInsightsService::beginSecretOperation(SecretAction action, const QByteArray& value) {
  if (m_busy || m_secretWatcher.isRunning()) {
    return;
  }
  if (action == SecretAction::Store || action == SecretAction::Remove) {
    m_accessToken.fill('\0'); m_accessToken.clear(); m_accessTokenExpiry = 0;
  }
  m_secretAction = action;
  m_busy = true;
  emit changed();
  m_secretWatcher.setFuture(QtConcurrent::run([action, secret = QByteArray(value)]() mutable {
    return secretOperation(static_cast<int>(action), std::move(secret));
  }));
}

void GameInsightsService::finishSecretOperation() {
  InsightsSecretResult result = m_secretWatcher.future().takeResult();
  if (!result.success) {
    result.secret.fill('\0');
    fail(result.error.isEmpty() ? QStringLiteral("Secret Service is unavailable") : result.error);
    return;
  }
  if (m_secretAction == SecretAction::Detect) {
    m_hasClientSecret = result.found;
    m_busy = false;
    m_statusText = configured() ? QStringLiteral("IGDB connection ready") : QString{};
    emit changed();
  } else if (m_secretAction == SecretAction::Store) {
    m_hasClientSecret = true;
    m_busy = false;
    m_statusText = QStringLiteral("IGDB credentials saved securely");
    emit changed();
  } else if (m_secretAction == SecretAction::Remove) {
    m_hasClientSecret = false;
    if (m_settings != nullptr) {
      m_settings->setIgdbClientId({});
    }
    m_busy = false;
    m_statusText = QStringLiteral("IGDB connection removed");
    emit changed();
  } else if (result.secret.isEmpty()) {
    m_hasClientSecret = false;
    fail(QStringLiteral("Add an IGDB client secret in settings"));
  } else {
    if (!m_catalogQuery.isEmpty() || m_refreshAppId == m_appId) {
      requestToken(std::move(result.secret));
    } else {
      result.secret.fill('\0');
      m_busy = false;
      loadSteam(m_appId);
    }
  }
  result.secret.fill('\0');
}

void GameInsightsService::requestToken(QByteArray secret) {
  QByteArray form = "client_id=" + clientId().toUtf8() + "&client_secret=" + secret +
                    "&grant_type=client_credentials";
  secret.fill('\0');
  QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token")));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/x-www-form-urlencoded"));
  m_statusText = QStringLiteral("Connecting to IGDB");
  emit changed();
  sendRequest(request, form, RequestKind::Token);
  form.fill('\0');
}

void GameInsightsService::requestMapping() {
  QNetworkRequest request(QUrl(QStringLiteral("https://api.igdb.com/v4/external_games")));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setRawHeader("Client-ID", clientId().toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_accessToken);
  request.setRawHeader("Accept", "application/json");
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
  sendRequest(request, IgdbApi::steamMappingQuery(m_refreshAppId), RequestKind::Mapping);
}

void GameInsightsService::requestGame() {
  QNetworkRequest request(QUrl(QStringLiteral("https://api.igdb.com/v4/games")));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setRawHeader("Client-ID", clientId().toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_accessToken);
  request.setRawHeader("Accept", "application/json");
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
  sendRequest(request, IgdbApi::gameQuery(m_insight.gameId), RequestKind::Game);
}

void GameInsightsService::requestTime() {
  QNetworkRequest request(QUrl(QStringLiteral("https://api.igdb.com/v4/game_time_to_beats")));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setRawHeader("Client-ID", clientId().toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_accessToken);
  request.setRawHeader("Accept", "application/json");
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
  sendRequest(request, IgdbApi::timeToBeatQuery(m_insight.gameId), RequestKind::Time);
}

void GameInsightsService::sendRequest(const QNetworkRequest& request, const QByteArray& body,
                                      RequestKind kind) {
  QNetworkReply* reply = m_network.post(request, body);
  reply->setProperty("kind", static_cast<int>(kind));
  m_buffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_buffers[reply];
    const qsizetype remaining = kMaximumResponseBytes - buffer.size();
    buffer.append(reply->read(remaining + 1));
    if (buffer.size() > kMaximumResponseBytes) {
      reply->setProperty("tooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] { finishRequest(reply); });
}

void GameInsightsService::finishRequest(QNetworkReply* reply) {
  QByteArray contents = m_buffers.take(reply);
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const bool failed = reply->error() != QNetworkReply::NoError || status < 200 || status >= 300 ||
                      reply->property("tooLarge").toBool();
  const RequestKind kind = static_cast<RequestKind>(reply->property("kind").toInt());
  reply->deleteLater();
  if (m_catalogQuery.isEmpty() && m_refreshAppId != m_appId) {
    m_busy = false;
    const QString currentAppId = m_appId;
    loadSteam(currentAppId);
    return;
  }
  if (failed) {
    if (kind == RequestKind::Token || status == 401 || status == 403) {
      m_accessToken.fill('\0');
      m_accessToken.clear();
      m_accessTokenExpiry = 0;
    }
    fail(kind == RequestKind::Token && (status == 400 || status == 401)
             ? QStringLiteral("IGDB rejected those credentials")
             : QStringLiteral("IGDB could not refresh game insights"));
    return;
  }
  QString error;
  if (kind == RequestKind::Token) {
    const QJsonObject token = QJsonDocument::fromJson(contents).object();
    m_accessToken = token.value(QStringLiteral("access_token")).toString().toLatin1();
    m_accessTokenExpiry = QDateTime::currentSecsSinceEpoch() +
                          qBound<qint64>(0, token.value(QStringLiteral("expires_in")).toInteger(),
                                         60LL * 24 * 60 * 60);
    contents.fill('\0');
    if (m_accessToken.isEmpty()) {
      fail(QStringLiteral("IGDB returned an invalid access token"));
    } else {
      if (!m_catalogQuery.isEmpty()) sendCatalog();
      else requestMapping();
    }
  } else if (kind == RequestKind::Catalog) {
    if (!QJsonDocument::fromJson(contents).isArray()) { fail(QStringLiteral("IGDB returned invalid data")); return; }
    m_catalogQuery.clear();
    m_busy = false;
    m_statusText = QStringLiteral("IGDB connection working");
    emit changed();
    emit catalogFinished(contents, {});
  } else if (kind == RequestKind::Mapping) {
    qint64 gameId = 0;
    if (!IgdbApi::parseSteamMapping(contents, &gameId, &error)) {
      if (contents.trimmed() == "[]") {
        // Remember the miss so this game does not ask IGDB again on every visit.
        m_insight = {};
        m_updatedAt = QDateTime::currentSecsSinceEpoch();
        persist();
        m_busy = false;
        m_statusText = QStringLiteral("IGDB has no entry for this game");
        emit changed();
        return;
      }
      fail(error);
    } else {
      m_insight = {};
      m_insight.gameId = gameId;
      requestGame();
    }
  } else if (kind == RequestKind::Game) {
    if (!IgdbApi::parseGame(contents, &m_insight, &error)) {
      fail(error);
    } else {
      requestTime();
    }
  } else {
    if (!IgdbApi::parseTimeToBeat(contents, &m_insight, &error) &&
        !contents.trimmed().startsWith("[]")) {
      fail(error);
      return;
    }
    m_updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!persist()) {
      fail(QStringLiteral("Omakade could not cache IGDB data"));
      return;
    }
    m_busy = false;
    m_statusText = QStringLiteral("Updated from IGDB");
    emit changed();
  }
}

void GameInsightsService::fail(const QString& message) {
  const bool catalog = !m_catalogQuery.isEmpty();
  m_catalogQuery.clear();
  m_busy = false;
  m_statusText = message;
  emit changed();
  if (catalog) emit catalogFinished({}, message);
}

void GameInsightsService::clearCurrent() {
  m_insight = {};
  m_insight.criticScore = -1;
  m_updatedAt = 0;
}

bool GameInsightsService::loadCache(const QString& appId) {
  if (!m_database.isOpen()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT provider_game_id, title, critic_score, critic_review_count, rushed_seconds, "
      "normal_seconds, complete_seconds, time_sample_count, updated_at FROM game_insights WHERE "
      "source = 'Steam' AND app_id = ? AND provider = 'igdb'"));
  query.addBindValue(appId);
  if (!query.exec() || !query.next()) {
    return false;
  }
  m_insight = {.gameId = query.value(0).toLongLong(),
               .title = query.value(1).toString(),
               .criticScore = query.value(2).toInt(),
               .criticReviewCount = query.value(3).toInt(),
               .rushedSeconds = query.value(4).toInt(),
               .normalSeconds = query.value(5).toInt(),
               .completeSeconds = query.value(6).toInt(),
               .timeSampleCount = query.value(7).toInt()};
  m_updatedAt = query.value(8).toLongLong();
  return true;
}

bool GameInsightsService::persist() {
  if (!m_database.isOpen()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT OR REPLACE INTO game_insights(source, app_id, provider, provider_game_id, title, "
      "critic_score, critic_review_count, rushed_seconds, normal_seconds, complete_seconds, "
      "time_sample_count, updated_at) VALUES('Steam', ?, 'igdb', ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
  query.addBindValue(m_refreshAppId);
  query.addBindValue(m_insight.gameId);
  query.addBindValue(m_insight.title);
  query.addBindValue(m_insight.criticScore);
  query.addBindValue(m_insight.criticReviewCount);
  query.addBindValue(m_insight.rushedSeconds);
  query.addBindValue(m_insight.normalSeconds);
  query.addBindValue(m_insight.completeSeconds);
  query.addBindValue(m_insight.timeSampleCount);
  query.addBindValue(m_updatedAt);
  return query.exec();
}

bool GameInsightsService::requestCatalog(const QByteArray& query, const QString& endpoint) {
  if (endpoint != "games" && endpoint != "external_games" && endpoint != "popularity_primitives") return false;
  if (m_busy || !configured() || query.isEmpty()) return false;
  m_catalogQuery = query;
  m_catalogEndpoint = endpoint;
  if (!m_accessToken.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_accessTokenExpiry - 60) {
    m_busy = true;
    emit changed();
    sendCatalog();
  } else beginSecretOperation(SecretAction::Lookup);
  return true;
}

void GameInsightsService::sendCatalog() {
  QNetworkRequest request(QUrl(QStringLiteral("https://api.igdb.com/v4/") + m_catalogEndpoint));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setRawHeader("Client-ID", clientId().toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_accessToken);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
  sendRequest(request, m_catalogQuery, RequestKind::Catalog);
}

void GameInsightsService::saveCredentials(const QString& id, QString secret) {
  if (m_busy || !m_settings) return;
  if (!QRegularExpression("^[A-Za-z0-9]{5,64}$").match(id.trimmed()).hasMatch()) {
    secret.fill(QChar::Null); fail(QStringLiteral("That Twitch client ID is invalid")); return;
  }
  if (!secret.trimmed().isEmpty() && !QRegularExpression("^[A-Za-z0-9]{20,128}$").match(secret.trimmed()).hasMatch()) {
    secret.fill(QChar::Null); fail(QStringLiteral("That IGDB client secret is invalid")); return;
  }
  m_busy = true; // suppress the intermediate keyring lookup while saving both fields
  m_settings->setIgdbClientId(id);
  m_accessToken.fill('\0'); m_accessToken.clear(); m_accessTokenExpiry = 0;
  m_busy = false;
  if (secret.trimmed().isEmpty()) beginSecretOperation(SecretAction::Detect);
  else storeClientSecret(secret);
  secret.fill(QChar::Null);
}
void GameInsightsService::testConnection() {
  requestCatalog("fields id; limit 1;");
}

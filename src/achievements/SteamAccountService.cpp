#include "achievements/SteamAccountService.h"

#include "app/AppSettings.h"
#include "sources/steam/SteamScanner.h"
#include "sources/steam/ValveKeyValues.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>

#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
const SecretSchema* steamSchema() {
  static const SecretSchema* schema =
      secret_schema_new("io.github.tsouth89.Omakade.Steam", SECRET_SCHEMA_NONE, "service",
                        SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return schema;
}

const SecretSchema* legacySteamSchema() {
  static const SecretSchema* schema =
      secret_schema_new("io.github.omakade.Steam", SECRET_SCHEMA_NONE, "service",
                        SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return schema;
}

constexpr auto kSecretService = "steam-web-api";
constexpr qsizetype kMaximumApiResponseBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumOwnedGamesResponseBytes = 16 * 1024 * 1024;
constexpr qint64 kAchievementRefreshSeconds = 15 * 60;

SteamSecretResult lookupSecret(bool includeSecret) {
  GError* error = nullptr;
  gchar* password = secret_password_lookup_sync(steamSchema(), nullptr, &error, "service",
                                                kSecretService, nullptr);
  if (error == nullptr && password == nullptr) {
    password = secret_password_lookup_sync(legacySteamSchema(), nullptr, &error, "service",
                                           kSecretService, nullptr);
  }
  SteamSecretResult result;
  if (error != nullptr) {
    result.error = QString::fromUtf8(error->message);
    g_error_free(error);
  } else {
    result.success = true;
    if (password != nullptr) {
      result.found = true;
      if (includeSecret) {
        result.secret = QByteArray(password);
      }
      secret_password_free(password);
    }
  }
  return result;
}

QString apiStateName(SteamApiState state) {
  switch (state) {
  case SteamApiState::Ready:
    return QStringLiteral("connected");
  case SteamApiState::Offline:
    return QStringLiteral("offline");
  case SteamApiState::InvalidKey:
    return QStringLiteral("invalid-key");
  case SteamApiState::PrivateProfile:
    return QStringLiteral("private");
  case SteamApiState::RateLimited:
    return QStringLiteral("rate-limited");
  case SteamApiState::RemoteError:
    return QStringLiteral("error");
  }
  return QStringLiteral("error");
}

QString messageForState(SteamApiState state, const QString& detail = {}) {
  switch (state) {
  case SteamApiState::Offline:
    return QStringLiteral("Steam is unreachable. Showing cached achievement data.");
  case SteamApiState::InvalidKey:
    return QStringLiteral("Steam rejected the API key. Replace it in settings.");
  case SteamApiState::PrivateProfile:
    return QStringLiteral("Steam achievement details are private for this profile.");
  case SteamApiState::RateLimited:
    return QStringLiteral("Steam is rate limiting requests. Try again later.");
  case SteamApiState::RemoteError:
    return detail.isEmpty() ? QStringLiteral("Steam could not refresh achievements.") : detail;
  case SteamApiState::Ready:
    return {};
  }
  return detail;
}
} // namespace

SteamAccountService::SteamAccountService(const QString& databasePath, AppSettings* settings,
                                         QObject* parent)
    : QObject(parent), m_settings(settings),
      m_connectionName(
          QStringLiteral("omakade-steam-account-%1").arg(reinterpret_cast<quintptr>(this))) {
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  m_database.open();

  if (m_settings != nullptr && m_settings->steamId().isEmpty()) {
    const QString discovered = discoverSteamId();
    if (!discovered.isEmpty()) {
      m_settings->setSteamId(discovered);
    }
  }
  loadOwnedGameCount();
  connect(&m_secretWatcher, &QFutureWatcher<SteamSecretResult>::finished, this,
          &SteamAccountService::finishSecretOperation);
  beginSecretOperation(SecretAction::Detect);
}

SteamAccountService::~SteamAccountService() {
  if (m_secretWatcher.isRunning()) {
    m_secretWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

QString SteamAccountService::steamId() const {
  return m_settings == nullptr ? QString{} : m_settings->steamId();
}

bool SteamAccountService::hasApiKey() const { return m_hasApiKey; }

bool SteamAccountService::busy() const { return m_busy; }

QString SteamAccountService::statusText() const { return m_statusText; }

QString SteamAccountService::state() const { return m_state; }

int SteamAccountService::ownedGameCount() const { return m_ownedGameCount; }

void SteamAccountService::setSteamId(const QString& steamId) {
  if (m_settings == nullptr) {
    return;
  }
  const QString before = m_settings->steamId();
  m_settings->setSteamId(steamId);
  if (before != m_settings->steamId()) {
    loadOwnedGameCount();
    emit accountChanged();
    emit ownedGamesUpdated();
    setStatus(QStringLiteral("local"), QStringLiteral("Steam ID saved"));
  } else if (steamId.trimmed() != before) {
    setStatus(QStringLiteral("error"),
              QStringLiteral("Enter your 17-digit Steam ID. It starts with 7656119."));
  }
}

bool SteamAccountService::reportBusy() {
  if (!m_busy && !m_secretWatcher.isRunning()) {
    return false;
  }
  setStatus(m_state, QStringLiteral("Steam is still busy. Try again in a moment."));
  return true;
}

void SteamAccountService::storeApiKey(QString apiKey) {
  static const QRegularExpression valid(QStringLiteral("^[A-Fa-f0-9]{32}$"));
  if (reportBusy()) {
    apiKey.fill(QChar::Null);
    return;
  }
  QString normalized = apiKey.trimmed();
  if (!valid.match(normalized).hasMatch()) {
    apiKey.fill(QChar::Null);
    normalized.fill(QChar::Null);
    setStatus(QStringLiteral("invalid-key"),
              QStringLiteral("A Steam Web API key is 32 hexadecimal characters"));
    return;
  }
  QByteArray secret = normalized.toLatin1();
  apiKey.fill(QChar::Null);
  normalized.fill(QChar::Null);
  beginSecretOperation(SecretAction::Store, secret);
  secret.fill('\0');
}

void SteamAccountService::removeApiKey() {
  if (reportBusy()) {
    return;
  }
  beginSecretOperation(SecretAction::Remove);
}

void SteamAccountService::refreshAchievements(const QString& appId) {
  bool numericAppId = false;
  appId.toULongLong(&numericAppId);
  if (m_busy || !numericAppId) {
    return;
  }
  if (steamId().isEmpty()) {
    setStatus(QStringLiteral("setup"), QStringLiteral("Enter your Steam ID before connecting"));
    return;
  }
  if (!m_hasApiKey) {
    setStatus(QStringLiteral("setup"), QStringLiteral("Add a Steam Web API key in settings"));
    return;
  }
  m_refreshAppId = appId;
  beginSecretOperation(SecretAction::LookupForRefresh);
}

void SteamAccountService::refreshAchievementsIfStale(const QString& appId) {
  bool numericAppId = false;
  appId.toULongLong(&numericAppId);
  if (!numericAppId) {
    return;
  }
  if (m_busy) {
    m_pendingAutoRefreshAppId = appId;
    return;
  }
  if (!m_hasApiKey || steamId().isEmpty() || !m_database.isOpen()) {
    return;
  }

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT updated_at FROM achievement_summary WHERE app_id = ? AND source = 'steam-web'"));
  query.addBindValue(appId);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (query.exec() && query.next() &&
      now - query.value(0).toLongLong() < kAchievementRefreshSeconds) {
    return;
  }
  refreshAchievements(appId);
}

void SteamAccountService::refreshOwnedGames() {
  if (reportBusy()) {
    return;
  }
  if (steamId().isEmpty()) {
    setStatus(QStringLiteral("setup"), QStringLiteral("Enter your Steam ID before syncing games"));
    return;
  }
  if (!m_hasApiKey) {
    setStatus(QStringLiteral("setup"), QStringLiteral("Add a Steam Web API key in settings"));
    return;
  }
  beginSecretOperation(SecretAction::LookupForOwnedGames);
}

QString SteamAccountService::discoverSteamId() {
  for (const QString& root : SteamScanner::discoverSteamRoots()) {
    ValveKeyValues values;
    if (!ValveKeyValuesParser::parseFile(root + QStringLiteral("/config/loginusers.vdf"),
                                         &values)) {
      continue;
    }
    const ValveKeyValues* users = values.object(QStringLiteral("users"));
    if (users == nullptr) {
      continue;
    }
    QString fallback;
    for (auto iterator = users->objects.cbegin(); iterator != users->objects.cend(); ++iterator) {
      bool numeric = false;
      iterator.key().toULongLong(&numeric);
      if (!numeric) {
        continue;
      }
      fallback = iterator.key();
      if (iterator.value().value(QStringLiteral("MostRecent")).toInt() == 1) {
        return iterator.key();
      }
    }
    if (!fallback.isEmpty()) {
      return fallback;
    }
  }
  return {};
}

void SteamAccountService::beginSecretOperation(SecretAction action, const QByteArray& value) {
  if (m_busy || m_secretWatcher.isRunning()) {
    return;
  }
  m_secretAction = action;
  setBusy(true);
  m_secretWatcher.setFuture(QtConcurrent::run([action, secretValue = QByteArray(value)]() mutable {
    if (action == SecretAction::Detect || action == SecretAction::LookupForRefresh ||
        action == SecretAction::LookupForOwnedGames) {
      return lookupSecret(action != SecretAction::Detect);
    }
    GError* error = nullptr;
    bool success = false;
    if (action == SecretAction::Store) {
      success = secret_password_store_sync(steamSchema(), SECRET_COLLECTION_DEFAULT,
                                           "Omakade Steam Web API key", secretValue.constData(),
                                           nullptr, &error, "service", kSecretService, nullptr);
    } else {
      secret_password_clear_sync(steamSchema(), nullptr, &error, "service", kSecretService,
                                 nullptr);
      if (error == nullptr) {
        secret_password_clear_sync(legacySteamSchema(), nullptr, &error, "service",
                                   kSecretService, nullptr);
      }
      success = error == nullptr;
    }
    SteamSecretResult result{.success = success, .found = success, .secret = {}, .error = {}};
    if (error != nullptr) {
      result.error = QString::fromUtf8(error->message);
      g_error_free(error);
    }
    secretValue.fill('\0');
    return result;
  }));
}

void SteamAccountService::finishSecretOperation() {
  SteamSecretResult result = m_secretWatcher.future().takeResult();
  if (!result.success) {
    setBusy(false);
    setStatus(QStringLiteral("secret-service"),
              result.error.isEmpty() ? QStringLiteral("Secret Service is unavailable")
                                     : result.error);
    return;
  }

  if (m_secretAction == SecretAction::Detect) {
    m_hasApiKey = result.found;
    emit accountChanged();
    setBusy(false);
    setStatus(m_hasApiKey ? QStringLiteral("connected") : QStringLiteral("local"),
              m_hasApiKey ? QStringLiteral("Steam connection ready")
                          : QStringLiteral("Using Steam's local achievement cache"));
  } else if (m_secretAction == SecretAction::Store) {
    m_hasApiKey = true;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("connected"), QStringLiteral("Steam API key saved securely"));
  } else if (m_secretAction == SecretAction::Remove) {
    m_hasApiKey = false;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("local"), QStringLiteral("Steam API key removed"));
  } else if (result.secret.isEmpty()) {
    m_hasApiKey = false;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("setup"), QStringLiteral("Add a Steam Web API key in settings"));
  } else if (m_secretAction == SecretAction::LookupForRefresh) {
    startApiRequests(std::move(result.secret));
  } else {
    startOwnedGamesRequest(std::move(result.secret));
  }
  result.secret.fill('\0');
}

void SteamAccountService::startOwnedGamesRequest(QByteArray apiKey) {
  const QString requestSteamId = steamId();
  QUrl url;
  url.setScheme(QStringLiteral("https"));
  url.setHost(SteamAchievementApi::authenticatedHost());
  url.setPath(QStringLiteral("/IPlayerService/GetOwnedGames/v1/"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("key"), QString::fromLatin1(apiKey));
  query.addQueryItem(QStringLiteral("steamid"), requestSteamId);
  query.addQueryItem(QStringLiteral("include_appinfo"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("include_played_free_games"), QStringLiteral("1"));
  url.setQuery(query);
  QNetworkRequest request(url);
  request.setTransferTimeout(20000);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Omakade/%1").arg(QCoreApplication::applicationVersion()));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  reply->setProperty("steamId", requestSteamId);
  m_responseBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_responseBuffers[reply];
    const qsizetype remaining = kMaximumOwnedGamesResponseBytes - buffer.size();
    if (remaining <= 0) {
      reply->setProperty("responseTooLarge", true);
      reply->abort();
      return;
    }
    buffer.append(reply->read(remaining + 1));
    if (buffer.size() > kMaximumOwnedGamesResponseBytes) {
      reply->setProperty("responseTooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] { handleOwnedGamesReply(reply); });
  apiKey.fill('\0');
  setStatus(QStringLiteral("refreshing"), QStringLiteral("Syncing owned Steam games"));
}

void SteamAccountService::handleOwnedGamesReply(QNetworkReply* reply) {
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QString requestSteamId = reply->property("steamId").toString();
  QByteArray contents = m_responseBuffers.take(reply);
  if (contents.size() <= kMaximumOwnedGamesResponseBytes) {
    contents.append(reply->read(kMaximumOwnedGamesResponseBytes + 1 - contents.size()));
  }
  const bool tooLarge = reply->property("responseTooLarge").toBool() ||
                        contents.size() > kMaximumOwnedGamesResponseBytes;
  const SteamApiState responseState = tooLarge
                                          ? SteamApiState::RemoteError
                                          : SteamAchievementApi::classifyHttpResponse(
                                                status, reply->error() != QNetworkReply::NoError);
  reply->deleteLater();
  if (requestSteamId != steamId()) {
    setBusy(false);
    setStatus(QStringLiteral("setup"), QStringLiteral("Steam ID changed; sync canceled"));
    return;
  }
  if (responseState != SteamApiState::Ready) {
    setBusy(false);
    setStatus(apiStateName(responseState),
              tooLarge ? QStringLiteral("Steam returned an unexpectedly large response")
                       : SteamOwnedGamesApi::messageForState(responseState));
    return;
  }
  QVector<SteamOwnedGameRecord> games;
  QString error;
  const SteamApiState state = SteamOwnedGamesApi::parse(contents, &games, &error);
  if (state != SteamApiState::Ready) {
    setBusy(false);
    setStatus(apiStateName(state), SteamOwnedGamesApi::messageForState(state, error));
    return;
  }
  if (!persistOwnedGames(requestSteamId, games)) {
    setBusy(false);
    setStatus(QStringLiteral("error"), QStringLiteral("Could not cache owned Steam games"));
    return;
  }
  m_ownedGameCount = games.size();
  setBusy(false);
  setStatus(QStringLiteral("connected"),
            QStringLiteral("Synced %1 owned Steam games").arg(m_ownedGameCount));
  emit ownedGamesUpdated();
}

void SteamAccountService::startApiRequests(QByteArray apiKey) {
  m_api = {};
  m_api.pending = 3;
  struct Endpoint {
    QString host;
    QString path;
    QString kind;
    bool usesKey = false;
  };
  const Endpoint endpoints[] = {
      {SteamAchievementApi::authenticatedHost(),
       QStringLiteral("/ISteamUserStats/GetPlayerAchievements/v1/"), QStringLiteral("player"),
       true},
      {SteamAchievementApi::authenticatedHost(),
       QStringLiteral("/ISteamUserStats/GetSchemaForGame/v2/"), QStringLiteral("schema"), true},
      {QStringLiteral("api.steampowered.com"),
       QStringLiteral("/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v2/"),
       QStringLiteral("rarity"), false},
  };
  for (const Endpoint& endpoint : endpoints) {
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint.host);
    url.setPath(endpoint.path);
    QUrlQuery query;
    if (endpoint.usesKey) {
      query.addQueryItem(QStringLiteral("key"), QString::fromLatin1(apiKey));
    }
    if (endpoint.kind == QStringLiteral("player")) {
      query.addQueryItem(QStringLiteral("steamid"), steamId());
      query.addQueryItem(QStringLiteral("appid"), m_refreshAppId);
      query.addQueryItem(QStringLiteral("l"), QStringLiteral("english"));
    } else if (endpoint.kind == QStringLiteral("schema")) {
      query.addQueryItem(QStringLiteral("appid"), m_refreshAppId);
      query.addQueryItem(QStringLiteral("l"), QStringLiteral("english"));
    } else {
      query.addQueryItem(QStringLiteral("gameid"), m_refreshAppId);
    }
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Omakade/%1").arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = m_network.get(request);
    reply->setProperty("kind", endpoint.kind);
    m_responseBuffers.insert(reply, {});
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
      QByteArray& buffer = m_responseBuffers[reply];
      if (buffer.size() > kMaximumApiResponseBytes) {
        return;
      }
      const qsizetype remaining = kMaximumApiResponseBytes - buffer.size();
      buffer.append(reply->read(remaining + 1));
      if (buffer.size() > kMaximumApiResponseBytes) {
        reply->setProperty("responseTooLarge", true);
        reply->abort();
      }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleApiReply(reply); });
  }
  apiKey.fill('\0');
  setStatus(QStringLiteral("refreshing"), QStringLiteral("Refreshing Steam achievements"));
}

void SteamAccountService::handleApiReply(QNetworkReply* reply) {
  const QString kind = reply->property("kind").toString();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QByteArray contents = m_responseBuffers.take(reply);
  if (contents.size() <= kMaximumApiResponseBytes) {
    contents.append(reply->read(kMaximumApiResponseBytes + 1 - contents.size()));
  }
  const bool tooLarge =
      reply->property("responseTooLarge").toBool() || contents.size() > kMaximumApiResponseBytes;
  const SteamApiState responseState = tooLarge
                                          ? SteamApiState::RemoteError
                                          : SteamAchievementApi::classifyHttpResponse(
                                                status, reply->error() != QNetworkReply::NoError);
  if (kind == QStringLiteral("player")) {
    m_api.player = contents;
  } else if (kind == QStringLiteral("schema")) {
    m_api.schema = contents;
  } else {
    m_api.rarity = contents;
  }
  if (kind == QStringLiteral("player") && responseState != SteamApiState::Ready && !tooLarge &&
      SteamAchievementApi::isNoStatsResponse(contents)) {
    // Steam answers HTTP 400 for games without achievements. That is a normal state, not a
    // failure, and it must be cached so details pages stop re-requesting it.
    m_api.noStats = true;
  } else if (kind != QStringLiteral("rarity") && responseState != SteamApiState::Ready &&
             m_api.failure == SteamApiState::Ready) {
    m_api.failure = responseState;
    m_api.error =
        tooLarge ? QStringLiteral("Steam returned an unexpectedly large response") : QString{};
  }
  reply->deleteLater();
  --m_api.pending;
  if (m_api.pending == 0) {
    finishApiRequests();
  }
}

void SteamAccountService::finishApiRequests() {
  if (m_api.failure != SteamApiState::Ready) {
    setBusy(false);
    setStatus(apiStateName(m_api.failure), messageForState(m_api.failure, m_api.error));
    return;
  }
  if (m_api.noStats) {
    const bool cached = persistAchievements({});
    setBusy(false);
    setStatus(QStringLiteral("connected"),
              cached ? QStringLiteral("This game has no Steam achievements")
                     : QStringLiteral("Could not cache Steam achievements"));
    if (cached) {
      emit achievementsUpdated(m_refreshAppId);
    }
    return;
  }
  SteamAchievementApiResult result;
  QString error;
  const SteamApiState state =
      SteamAchievementApi::parse(m_api.player, m_api.schema, m_api.rarity, &result, &error);
  if (state != SteamApiState::Ready) {
    setBusy(false);
    setStatus(apiStateName(state), messageForState(state, error));
    return;
  }
  if (!persistAchievements(result)) {
    setBusy(false);
    setStatus(QStringLiteral("error"), QStringLiteral("Could not cache Steam achievements"));
    return;
  }
  setBusy(false);
  setStatus(QStringLiteral("connected"),
            QStringLiteral("Updated %1 Steam achievements").arg(result.total));
  emit achievementsUpdated(m_refreshAppId);
}

bool SteamAccountService::persistAchievements(const SteamAchievementApiResult& result) {
  if (!m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO achievement_summary(app_id, unlocked, total, source, updated_at) VALUES(?, ?, "
      "?, 'steam-web', ?) ON CONFLICT(app_id) DO UPDATE SET unlocked = excluded.unlocked, total = "
      "excluded.total, source = excluded.source, updated_at = excluded.updated_at"));
  query.addBindValue(m_refreshAppId);
  query.addBindValue(result.unlocked);
  query.addBindValue(result.total);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  bool okay = query.exec();
  query.prepare(QStringLiteral("DELETE FROM achievements WHERE app_id = ?"));
  query.addBindValue(m_refreshAppId);
  okay = okay && query.exec();
  for (const SteamAchievementRecord& achievement : result.achievements) {
    query.prepare(QStringLiteral(
        "INSERT INTO achievements(app_id, api_name, title, description, icon_url, icon_path, "
        "unlocked, unlock_time, rarity, hidden, current_progress, maximum_progress, source) VALUES("
        "?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?, ?, 'steam-web') ON CONFLICT(app_id, api_name) DO "
        "UPDATE SET title = excluded.title, description = excluded.description, icon_url = "
        "excluded.icon_url, unlocked = excluded.unlocked, unlock_time = excluded.unlock_time, "
        "rarity = excluded.rarity, hidden = excluded.hidden, current_progress = "
        "excluded.current_progress, maximum_progress = excluded.maximum_progress, source = "
        "excluded.source"));
    query.addBindValue(m_refreshAppId);
    query.addBindValue(achievement.apiName);
    query.addBindValue(achievement.title);
    query.addBindValue(achievement.description);
    query.addBindValue(achievement.iconUrl);
    query.addBindValue(achievement.unlocked);
    query.addBindValue(achievement.unlockTime);
    query.addBindValue(achievement.rarity);
    query.addBindValue(achievement.hidden);
    query.addBindValue(achievement.currentProgress);
    query.addBindValue(achievement.maximumProgress);
    okay = okay && query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  return true;
}

bool SteamAccountService::persistOwnedGames(const QString& requestSteamId,
                                            const QVector<SteamOwnedGameRecord>& games) {
  if (!m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM owned_games WHERE steam_id = ?"));
  query.addBindValue(requestSteamId);
  bool okay = query.exec();
  const qint64 syncedAt = QDateTime::currentSecsSinceEpoch();
  for (const SteamOwnedGameRecord& game : games) {
    query.prepare(QStringLiteral(
        "INSERT INTO games(app_id, title) VALUES(?, ?) ON CONFLICT(app_id) DO UPDATE SET "
        "title = excluded.title"));
    query.addBindValue(game.appId);
    query.addBindValue(game.title);
    okay = okay && query.exec();
    query.prepare(
        QStringLiteral("INSERT INTO owned_games(steam_id, app_id, playtime_minutes, synced_at) "
                       "VALUES(?, ?, ?, ?)"));
    query.addBindValue(requestSteamId);
    query.addBindValue(game.appId);
    query.addBindValue(game.playtimeMinutes);
    query.addBindValue(syncedAt);
    okay = okay && query.exec();
  }
  if (!okay || !m_database.commit()) {
    m_database.rollback();
    return false;
  }
  return true;
}

void SteamAccountService::loadOwnedGameCount() {
  if (!m_database.isOpen()) {
    return;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("SELECT COUNT(*) FROM owned_games WHERE steam_id = ?"));
  query.addBindValue(steamId());
  if (query.exec() && query.next()) {
    m_ownedGameCount = query.value(0).toInt();
  } else {
    m_ownedGameCount = 0;
  }
}

void SteamAccountService::setBusy(bool busy) {
  if (m_busy == busy) {
    return;
  }
  m_busy = busy;
  emit busyChanged();
  if (!m_busy && !m_pendingAutoRefreshAppId.isEmpty()) {
    const QString appId = m_pendingAutoRefreshAppId;
    m_pendingAutoRefreshAppId.clear();
    QTimer::singleShot(0, this, [this, appId] { refreshAchievementsIfStale(appId); });
  }
}

void SteamAccountService::setStatus(const QString& state, const QString& text) {
  m_state = state;
  m_statusText = text;
  emit statusChanged();
}

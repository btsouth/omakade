#include "achievements/RetroAchievementsService.h"

#include "app/AppSettings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QtConcurrent>

#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
const SecretSchema* retroAchievementsSchema() {
  static const SecretSchema* schema =
      secret_schema_new("io.github.tsouth89.Omakade.RetroAchievements", SECRET_SCHEMA_NONE,
                        "service", SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return schema;
}

constexpr auto kSecretService = "retroachievements-web-api";
constexpr qsizetype kMaximumResponseBytes = 8 * 1024 * 1024;
constexpr qint64 kAchievementRefreshSeconds = 15 * 60;
constexpr qint64 kConsoleCacheLifetimeSeconds = 30LL * 24 * 60 * 60;
constexpr qint64 kHashListCacheLifetimeSeconds = 7LL * 24 * 60 * 60;

RetroAchievementsSecretResult lookupSecret(bool includeSecret) {
  GError* error = nullptr;
  gchar* password = secret_password_lookup_sync(retroAchievementsSchema(), nullptr, &error,
                                                "service", kSecretService, nullptr);
  RetroAchievementsSecretResult result;
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

QString apiStateName(RetroAchievementsApiState state) {
  switch (state) {
  case RetroAchievementsApiState::Ready:
    return QStringLiteral("connected");
  case RetroAchievementsApiState::Offline:
    return QStringLiteral("offline");
  case RetroAchievementsApiState::InvalidKey:
    return QStringLiteral("invalid-key");
  case RetroAchievementsApiState::RateLimited:
    return QStringLiteral("rate-limited");
  case RetroAchievementsApiState::RemoteError:
    return QStringLiteral("error");
  }
  return QStringLiteral("error");
}

QString messageForState(RetroAchievementsApiState state, const QString& detail = {}) {
  switch (state) {
  case RetroAchievementsApiState::Offline:
    return QStringLiteral("RetroAchievements is unreachable. Showing cached achievement data.");
  case RetroAchievementsApiState::InvalidKey:
    return QStringLiteral("RetroAchievements rejected the Web API key. Replace it in settings.");
  case RetroAchievementsApiState::RateLimited:
    return QStringLiteral("RetroAchievements is rate limiting requests. Try again later.");
  case RetroAchievementsApiState::RemoteError:
    return detail.isEmpty() ? QStringLiteral("RetroAchievements could not refresh achievements.")
                            : detail;
  case RetroAchievementsApiState::Ready:
    return {};
  }
  return detail;
}

} // namespace

RetroAchievementsService::RetroAchievementsService(const QString& databasePath,
                                                   AppSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings),
      m_connectionName(QStringLiteral("omakade-retroachievements-%1")
                           .arg(reinterpret_cast<quintptr>(this))) {
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  if (m_database.open()) {
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS retroachievements_consoles ("
                              "name TEXT PRIMARY KEY, console_id INTEGER NOT NULL, updated_at "
                              "INTEGER NOT NULL)"));
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS retroachievements_hashes ("
                              "console_id INTEGER NOT NULL, md5 TEXT NOT NULL, game_id INTEGER "
                              "NOT NULL, title TEXT NOT NULL, PRIMARY KEY(console_id, md5))"));
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS retroachievements_hash_refresh ("
                              "console_id INTEGER PRIMARY KEY, updated_at INTEGER NOT NULL)"));
  }
  connect(&m_secretWatcher, &QFutureWatcher<RetroAchievementsSecretResult>::finished, this,
          &RetroAchievementsService::finishSecretOperation);
  connect(&m_hashWatcher, &QFutureWatcher<std::optional<QByteArray>>::finished, this,
          &RetroAchievementsService::finishHashing);
  if (m_settings != nullptr && !m_settings->retroAchievementsUsername().isEmpty()) {
    beginSecretOperation(SecretAction::Detect);
  } else if (m_settings != nullptr) {
    connect(m_settings, &AppSettings::retroAchievementsUsernameChanged, this,
            &RetroAchievementsService::startDetectOnUsernameChanged, Qt::QueuedConnection);
  }
}

RetroAchievementsService::~RetroAchievementsService() {
  if (m_secretWatcher.isRunning()) {
    m_secretWatcher.waitForFinished();
  }
  if (m_hashWatcher.isRunning()) {
    m_hashWatcher.waitForFinished();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

QString RetroAchievementsService::username() const {
  return m_settings == nullptr ? QString{} : m_settings->retroAchievementsUsername();
}

bool RetroAchievementsService::hasApiKey() const { return m_hasApiKey; }
bool RetroAchievementsService::busy() const { return m_busy; }
QString RetroAchievementsService::statusText() const { return m_statusText; }
QString RetroAchievementsService::state() const { return m_state; }

void RetroAchievementsService::setUsername(const QString& username) {
  if (m_settings == nullptr) {
    return;
  }
  const QString before = m_settings->retroAchievementsUsername();
  m_settings->setRetroAchievementsUsername(username);
  if (before != m_settings->retroAchievementsUsername()) {
    // Cached achievement_summary/achievements rows are keyed by app_id, not by account, so a
    // stale row from the previous account would otherwise look like valid, up-to-date progress
    // for whichever RetroArch game is opened next.
    clearCachedAchievements();
    emit accountChanged();
    setStatus(QStringLiteral("local"), QStringLiteral("RetroAchievements username saved"));
  } else if (username.trimmed() != before) {
    setStatus(QStringLiteral("error"),
              QStringLiteral("Enter a valid RetroAchievements username"));
  }
}

void RetroAchievementsService::clearCachedAchievements() {
  if (!m_database.isOpen()) {
    return;
  }
  QSqlQuery query(m_database);
  query.exec(QStringLiteral("DELETE FROM achievements WHERE source = 'retroachievements'"));
  query.exec(QStringLiteral("DELETE FROM achievement_summary WHERE source = 'retroachievements'"));
}

bool RetroAchievementsService::reportBusy() {
  if (!m_busy && !m_secretWatcher.isRunning()) {
    return false;
  }
  setStatus(m_state, QStringLiteral("RetroAchievements is still busy. Try again in a moment."));
  return true;
}

void RetroAchievementsService::storeApiKey(QString apiKey) {
  static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{10,100}$"));
  if (reportBusy()) {
    apiKey.fill(QChar::Null);
    return;
  }
  QString normalized = apiKey.trimmed();
  if (!valid.match(normalized).hasMatch()) {
    apiKey.fill(QChar::Null);
    normalized.fill(QChar::Null);
    setStatus(QStringLiteral("invalid-key"),
              QStringLiteral("That doesn't look like a RetroAchievements Web API key"));
    return;
  }
  QByteArray secret = normalized.toLatin1();
  apiKey.fill(QChar::Null);
  normalized.fill(QChar::Null);
  beginSecretOperation(SecretAction::Store, secret);
  secret.fill('\0');
}

void RetroAchievementsService::removeApiKey() {
  if (reportBusy()) {
    return;
  }
  beginSecretOperation(SecretAction::Remove);
}

void RetroAchievementsService::refreshAchievements(const QString& gameId) {
  if (gameId.isEmpty() || m_busy) {
    return;
  }
  if (username().isEmpty()) {
    setStatus(QStringLiteral("setup"),
              QStringLiteral("Enter your RetroAchievements username before connecting"));
    return;
  }
  if (!m_hasApiKey) {
    setStatus(QStringLiteral("setup"), QStringLiteral("Add a RetroAchievements Web API key"));
    return;
  }
  m_pending = PendingRefresh{.gameId = gameId, .contentPath = {}, .consoleName = {}, .hash = {}};
  beginSecretOperation(SecretAction::LookupForRefresh);
}

void RetroAchievementsService::refreshAchievementsIfStale(const QString& gameId) {
  if (gameId.isEmpty()) {
    return;
  }
  if (m_busy) {
    m_pendingAutoRefreshGameId = gameId;
    return;
  }
  if (!m_hasApiKey || username().isEmpty() || !m_database.isOpen()) {
    return;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("SELECT updated_at FROM achievement_summary WHERE app_id = ? AND "
                               "source = 'retroachievements'"));
  query.addBindValue(gameId);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (query.exec() && query.next() &&
      now - query.value(0).toLongLong() < kAchievementRefreshSeconds) {
    return;
  }
  refreshAchievements(gameId);
}

void RetroAchievementsService::beginSecretOperation(SecretAction action,
                                                     const QByteArray& value) {
  if (m_busy || m_secretWatcher.isRunning()) {
    return;
  }
  m_secretAction = action;
  setBusy(true);
  m_secretWatcher.setFuture(QtConcurrent::run([action, secretValue = QByteArray(value)]() mutable {
    if (action == SecretAction::Detect || action == SecretAction::LookupForRefresh) {
      return lookupSecret(action != SecretAction::Detect);
    }
    GError* error = nullptr;
    bool success = false;
    if (action == SecretAction::Store) {
      success = secret_password_store_sync(retroAchievementsSchema(), SECRET_COLLECTION_DEFAULT,
                                           "Omakade RetroAchievements Web API key",
                                           secretValue.constData(), nullptr, &error, "service",
                                           kSecretService, nullptr);
    } else {
      secret_password_clear_sync(retroAchievementsSchema(), nullptr, &error, "service",
                                 kSecretService, nullptr);
      success = error == nullptr;
    }
    RetroAchievementsSecretResult result{
        .success = success, .found = success, .secret = {}, .error = {}};
    if (error != nullptr) {
      result.error = QString::fromUtf8(error->message);
      g_error_free(error);
    }
    secretValue.fill('\0');
    return result;
  }));
}

void RetroAchievementsService::startDetectOnUsernameChanged() {
  beginSecretOperation(SecretAction::Detect);
}

void RetroAchievementsService::finishSecretOperation() {
  RetroAchievementsSecretResult result = m_secretWatcher.future().takeResult();
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
              m_hasApiKey ? QStringLiteral("RetroAchievements connection ready")
                          : QStringLiteral("Add a RetroAchievements Web API key in settings"));
  } else if (m_secretAction == SecretAction::Store) {
    m_hasApiKey = true;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("connected"), QStringLiteral("RetroAchievements key saved securely"));
  } else if (m_secretAction == SecretAction::Remove) {
    m_hasApiKey = false;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("local"), QStringLiteral("RetroAchievements key removed"));
  } else if (result.secret.isEmpty()) {
    m_hasApiKey = false;
    emit accountChanged();
    setBusy(false);
    setStatus(QStringLiteral("setup"), QStringLiteral("Add a RetroAchievements Web API key"));
  } else {
    startRefreshPipeline(std::move(result.secret));
  }
  result.secret.fill('\0');
}

void RetroAchievementsService::startRefreshPipeline(QByteArray apiKey) {
  m_activeApiKey = std::move(apiKey);
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT content_path, system FROM retroarch_games WHERE game_id = ?"));
  query.addBindValue(m_pending.gameId);
  if (!query.exec() || !query.next()) {
    fail(QStringLiteral("error"), QStringLiteral("This RetroArch game is no longer in the library"));
    return;
  }
  m_pending.contentPath = query.value(0).toString();
  const QString system = query.value(1).toString();
  const RetroAchievementsConsole console = RetroAchievementsHasher::consoleFor(system);
  if (console.rule == RetroAchievementsHashRule::Unsupported) {
    fail(QStringLiteral("unsupported"),
        QStringLiteral("RetroAchievements matching isn't supported for this system yet"));
    return;
  }
  m_pending.consoleName = console.raConsoleName;
  // Hashing reads the whole ROM (or a zip entry) from disk, which can be large, so it runs on a
  // worker thread rather than blocking the GUI thread here.
  setStatus(QStringLiteral("refreshing"), QStringLiteral("Hashing the ROM file"));
  m_hashWatcher.setFuture(QtConcurrent::run(&RetroAchievementsHasher::hashFile,
                                            m_pending.contentPath, console.rule));
}

void RetroAchievementsService::finishHashing() {
  const std::optional<QByteArray> hash = m_hashWatcher.future().result();
  if (!hash.has_value()) {
    fail(QStringLiteral("error"), QStringLiteral("Could not read this ROM file to hash it"));
    return;
  }
  m_pending.hash = *hash;

  QSqlQuery consoleQuery(m_database);
  consoleQuery.prepare(
      QStringLiteral("SELECT console_id, updated_at FROM retroachievements_consoles WHERE name = ?"));
  consoleQuery.addBindValue(m_pending.consoleName);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (consoleQuery.exec() && consoleQuery.next() &&
      now - consoleQuery.value(1).toLongLong() < kConsoleCacheLifetimeSeconds) {
    m_pending.consoleId = consoleQuery.value(0).toInt();
    tryMatchOrFetchGameList();
  } else {
    setStatus(QStringLiteral("refreshing"), QStringLiteral("Looking up RetroAchievements systems"));
    requestConsoleIds();
  }
}

void RetroAchievementsService::tryMatchOrFetchGameList() {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "SELECT game_id FROM retroachievements_hashes WHERE console_id = ? AND md5 = ?"));
  query.addBindValue(m_pending.consoleId);
  query.addBindValue(QString::fromLatin1(m_pending.hash).toLower());
  if (query.exec() && query.next()) {
    requestGameInfo(query.value(0).toLongLong());
    return;
  }
  if (m_pending.gameListRefreshed) {
    fail(QStringLiteral("connected"),
        QStringLiteral("No match found on RetroAchievements for this ROM"));
    return;
  }
  QSqlQuery refreshQuery(m_database);
  refreshQuery.prepare(
      QStringLiteral("SELECT updated_at FROM retroachievements_hash_refresh WHERE console_id = ?"));
  refreshQuery.addBindValue(m_pending.consoleId);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (refreshQuery.exec() && refreshQuery.next() &&
      now - refreshQuery.value(0).toLongLong() < kHashListCacheLifetimeSeconds) {
    fail(QStringLiteral("connected"),
        QStringLiteral("No match found on RetroAchievements for this ROM"));
    return;
  }
  setStatus(QStringLiteral("refreshing"),
           QStringLiteral("Downloading the RetroAchievements hash list for %1")
               .arg(m_pending.consoleName));
  requestGameList(m_pending.consoleId);
}

void RetroAchievementsService::requestConsoleIds() {
  sendRequest(RetroAchievementsApi::consoleIdsUrl(QString::fromLatin1(m_activeApiKey)),
             RequestKind::ConsoleIds);
}

void RetroAchievementsService::requestGameList(int consoleId) {
  sendRequest(RetroAchievementsApi::gameListUrl(QString::fromLatin1(m_activeApiKey), consoleId),
             RequestKind::GameList);
}

void RetroAchievementsService::requestGameInfo(qint64 raGameId) {
  sendRequest(RetroAchievementsApi::gameInfoAndProgressUrl(QString::fromLatin1(m_activeApiKey),
                                                          raGameId, username()),
             RequestKind::GameInfo);
}

void RetroAchievementsService::sendRequest(const QUrl& url, RequestKind kind) {
  QNetworkRequest request(url);
  request.setTransferTimeout(20000);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Omakade/%1").arg(QCoreApplication::applicationVersion()));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  m_responseBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_responseBuffers[reply];
    const qsizetype remaining = kMaximumResponseBytes - buffer.size();
    if (remaining <= 0) {
      reply->setProperty("responseTooLarge", true);
      reply->abort();
      return;
    }
    buffer.append(reply->read(remaining + 1));
    if (buffer.size() > kMaximumResponseBytes) {
      reply->setProperty("responseTooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this,
         [this, reply, kind] { handleReply(reply, kind); });
}

void RetroAchievementsService::handleReply(QNetworkReply* reply, RequestKind kind) {
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QByteArray contents = m_responseBuffers.take(reply);
  if (contents.size() <= kMaximumResponseBytes) {
    contents.append(reply->read(kMaximumResponseBytes + 1 - contents.size()));
  }
  const bool tooLarge =
      reply->property("responseTooLarge").toBool() || contents.size() > kMaximumResponseBytes;
  const RetroAchievementsApiState responseState =
      tooLarge ? RetroAchievementsApiState::RemoteError
              : RetroAchievementsApi::classifyHttpResponse(status,
                                                            reply->error() != QNetworkReply::NoError);
  reply->deleteLater();
  if (responseState != RetroAchievementsApiState::Ready) {
    fail(apiStateName(responseState),
        tooLarge ? QStringLiteral("RetroAchievements returned an unexpectedly large response")
                 : messageForState(responseState));
    return;
  }

  if (kind == RequestKind::ConsoleIds) {
    QVector<RetroAchievementsConsoleRecord> consoles;
    if (!RetroAchievementsApi::parseConsoleIds(contents, &consoles)) {
      fail(QStringLiteral("error"), QStringLiteral("RetroAchievements returned malformed console data"));
      return;
    }
    const int matchedId = RetroAchievementsApi::bestConsoleMatch(consoles, m_pending.consoleName);
    if (matchedId == 0) {
      fail(QStringLiteral("unsupported"),
          QStringLiteral("RetroAchievements does not list a matching system for %1")
              .arg(m_pending.consoleName));
      return;
    }
    QSqlQuery upsert(m_database);
    upsert.prepare(QStringLiteral(
        "INSERT INTO retroachievements_consoles(name, console_id, updated_at) VALUES(?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET console_id = excluded.console_id, updated_at = "
        "excluded.updated_at"));
    upsert.addBindValue(m_pending.consoleName);
    upsert.addBindValue(matchedId);
    upsert.addBindValue(QDateTime::currentSecsSinceEpoch());
    upsert.exec();
    m_pending.consoleId = matchedId;
    tryMatchOrFetchGameList();
  } else if (kind == RequestKind::GameList) {
    QVector<RetroAchievementsHashRecord> games;
    if (!RetroAchievementsApi::parseGameList(contents, &games)) {
      fail(QStringLiteral("error"), QStringLiteral("RetroAchievements returned malformed game data"));
      return;
    }
    if (!m_database.transaction()) {
      fail(QStringLiteral("error"), QStringLiteral("Could not cache the RetroAchievements hash list"));
      return;
    }
    QSqlQuery deleteQuery(m_database);
    deleteQuery.prepare(
        QStringLiteral("DELETE FROM retroachievements_hashes WHERE console_id = ?"));
    deleteQuery.addBindValue(m_pending.consoleId);
    bool okay = deleteQuery.exec();
    QSqlQuery insertQuery(m_database);
    for (const RetroAchievementsHashRecord& game : games) {
      for (const QString& hash : game.md5Hashes) {
        insertQuery.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO retroachievements_hashes(console_id, md5, game_id, title) "
            "VALUES(?, ?, ?, ?)"));
        insertQuery.addBindValue(m_pending.consoleId);
        insertQuery.addBindValue(hash);
        insertQuery.addBindValue(game.gameId);
        insertQuery.addBindValue(game.title);
        okay = okay && insertQuery.exec();
      }
    }
    QSqlQuery refreshQuery(m_database);
    refreshQuery.prepare(QStringLiteral(
        "INSERT INTO retroachievements_hash_refresh(console_id, updated_at) VALUES(?, ?) "
        "ON CONFLICT(console_id) DO UPDATE SET updated_at = excluded.updated_at"));
    refreshQuery.addBindValue(m_pending.consoleId);
    refreshQuery.addBindValue(QDateTime::currentSecsSinceEpoch());
    okay = okay && refreshQuery.exec();
    if (!okay || !m_database.commit()) {
      m_database.rollback();
      fail(QStringLiteral("error"), QStringLiteral("Could not cache the RetroAchievements hash list"));
      return;
    }
    m_pending.gameListRefreshed = true;
    tryMatchOrFetchGameList();
  } else {
    RetroAchievementsProgressResult result;
    QString error;
    const RetroAchievementsApiState state =
        RetroAchievementsApi::parseGameInfoAndProgress(contents, &result, &error);
    if (state != RetroAchievementsApiState::Ready) {
      fail(apiStateName(state), messageForState(state, error));
      return;
    }
    if (!persistAchievements(result)) {
      fail(QStringLiteral("error"), QStringLiteral("Could not cache RetroAchievements data"));
      return;
    }
    const QString gameId = m_pending.gameId;
    m_activeApiKey.fill('\0');
    m_activeApiKey.clear();
    setBusy(false);
    setStatus(QStringLiteral("connected"),
             QStringLiteral("Updated %1 RetroAchievements").arg(result.total));
    emit achievementsUpdated(gameId);
  }
}

bool RetroAchievementsService::persistAchievements(const RetroAchievementsProgressResult& result) {
  if (!m_database.isOpen() || !m_database.transaction()) {
    return false;
  }
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO achievement_summary(app_id, unlocked, total, source, updated_at) VALUES(?, ?, "
      "?, 'retroachievements', ?) ON CONFLICT(app_id) DO UPDATE SET unlocked = excluded.unlocked, "
      "total = excluded.total, source = excluded.source, updated_at = excluded.updated_at"));
  query.addBindValue(m_pending.gameId);
  query.addBindValue(result.unlocked);
  query.addBindValue(result.total);
  query.addBindValue(QDateTime::currentSecsSinceEpoch());
  bool okay = query.exec();
  query.prepare(QStringLiteral("DELETE FROM achievements WHERE app_id = ?"));
  query.addBindValue(m_pending.gameId);
  okay = okay && query.exec();
  for (const RetroAchievementsAchievementRecord& achievement : result.achievements) {
    query.prepare(QStringLiteral(
        "INSERT INTO achievements(app_id, api_name, title, description, icon_url, icon_path, "
        "unlocked, unlock_time, rarity, hidden, current_progress, maximum_progress, source) "
        "VALUES(?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?, ?, 'retroachievements') ON CONFLICT(app_id, "
        "api_name) DO UPDATE SET title = excluded.title, description = excluded.description, "
        "icon_url = excluded.icon_url, unlocked = excluded.unlocked, unlock_time = "
        "excluded.unlock_time, rarity = excluded.rarity, hidden = excluded.hidden, "
        "current_progress = excluded.current_progress, maximum_progress = "
        "excluded.maximum_progress, source = excluded.source"));
    query.addBindValue(m_pending.gameId);
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

void RetroAchievementsService::setBusy(bool busy) {
  if (m_busy == busy) {
    return;
  }
  m_busy = busy;
  emit busyChanged();
  if (!m_busy && !m_pendingAutoRefreshGameId.isEmpty()) {
    const QString gameId = m_pendingAutoRefreshGameId;
    m_pendingAutoRefreshGameId.clear();
    QTimer::singleShot(0, this, [this, gameId] { refreshAchievementsIfStale(gameId); });
  }
}

void RetroAchievementsService::setStatus(const QString& state, const QString& text) {
  m_state = state;
  m_statusText = text;
  emit statusChanged();
}

void RetroAchievementsService::fail(const QString& state, const QString& text) {
  m_activeApiKey.fill('\0');
  m_activeApiKey.clear();
  setBusy(false);
  setStatus(state, text);
}

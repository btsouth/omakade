#include "sources/romm/RommCatalog.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUrlQuery>
#include <QUuid>

namespace {
constexpr qsizetype maxPageBytes = 16 * 1024 * 1024;
constexpr qsizetype maxRefreshBytes = 64 * 1024 * 1024;
bool sameOrigin(const QUrl& a, const QUrl& b) {
  return a.scheme() == b.scheme() && a.host() == b.host() &&
         a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80) &&
         b.userInfo().isEmpty() && !b.hasFragment();
}
QUrl pageUrl(QUrl server, int offset) {
  server.setPath(server.path() + "/api/roms");
  QUrlQuery query;
  query.addQueryItem("limit", "100");
  query.addQueryItem("offset", QString::number(offset));
  query.addQueryItem("order_by", "id");
  query.addQueryItem("order_dir", "asc");
  server.setQuery(query);
  return server;
}
} // namespace

QUrl RommCatalog::serverUrl(const QUrl& input) {
  if (!input.isValid() || (input.scheme() != "https" && input.scheme() != "http") ||
      input.host().isEmpty() || !input.userInfo().isEmpty() || input.hasQuery() ||
      input.hasFragment())
    return {};
  QUrl url = input.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
  if (url.path() == "/")
    url.setPath({});
  return url;
}
bool RommCatalog::validToken(const QByteArray& token) {
  static const QRegularExpression pattern("^rmm_[a-fA-F0-9]{64}$");
  return token.size() == 68 && pattern.match(QString::fromLatin1(token)).hasMatch();
}
QString RommCatalog::cacheKey(const QUrl& server, const QString& root) {
  return QString::fromLatin1(QCryptographicHash::hash(server.toEncoded() + '\n' + root.toUtf8(),
                                                      QCryptographicHash::Sha256)
                                 .toHex());
}
RommCatalog::RommCatalog(const QString& path, QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), m_connection("romm-" + QUuid::createUuid().toString()),
      m_network(network ? network : new QNetworkAccessManager(this)) {
  m_database = QSqlDatabase::addDatabase("QSQLITE", m_connection);
  m_database.setDatabaseName(path);
  m_database.setConnectOptions("QSQLITE_BUSY_TIMEOUT=1000");
  if (m_database.open()) {
    QSqlQuery query(m_database);
    m_valid = query.exec("CREATE TABLE IF NOT EXISTS romm_catalog (scope TEXT NOT NULL, "
                         "id TEXT NOT NULL, record BLOB NOT NULL, PRIMARY KEY(scope,id))");
  }
  m_timeout.setSingleShot(true);
  m_timeout.setInterval(120000);
  connect(&m_timeout, &QTimer::timeout, this,
          [this] { complete("RomM refresh timed out. Previous catalog kept."); });
}
RommCatalog::~RommCatalog() {
  if (m_reply) {
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
  }
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connection);
}
bool RommCatalog::refresh(const QUrl& server, const QString& root, const QByteArray& token) {
  if (m_busy)
    return false;
  m_server = serverUrl(server);
  m_root = QFileInfo(root).canonicalFilePath();
  if (!m_valid || m_server.isEmpty() || !QFileInfo(m_root).isDir() || !validToken(token)) {
    emit finished(false,
                  "Check the RomM server, mounted library, token, and local catalog storage.");
    return false;
  }
  m_token = token;
  m_games.clear();
  m_ids.clear();
  m_bytes = 0;
  m_offset = 0;
  m_pages = 0;
  m_total = -1;
  m_redirects = 0;
  m_busy = true;
  m_timeout.start();
  request(pageUrl(m_server, 0));
  return true;
}
void RommCatalog::cancel() {
  if (m_busy)
    complete("RomM refresh canceled. Previous catalog kept.");
}
void RommCatalog::complete(const QString& error) {
  if (!m_busy)
    return;
  m_busy = false;
  m_timeout.stop();
  if (m_reply) {
    auto* reply = m_reply.data();
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
  m_token.clear();
  m_body.clear();
  m_games.clear();
  m_ids.clear();
  emit finished(error.isEmpty(), error.isEmpty() ? "RomM catalog refreshed." : error);
}
void RommCatalog::request(const QUrl& url) {
  m_body.clear();
  QNetworkRequest httpRequest(url);
  httpRequest.setRawHeader("Authorization", "Bearer " + m_token);
  httpRequest.setRawHeader("Accept", "application/json");
  httpRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                           QNetworkRequest::ManualRedirectPolicy);
  httpRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  httpRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  httpRequest.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
  httpRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                           QNetworkRequest::AlwaysNetwork);
  httpRequest.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  httpRequest.setTransferTimeout(15000);
  auto* reply = m_network->get(httpRequest);
  m_reply = reply;
  reply->setReadBufferSize(maxPageBytes + 1);
  const auto read = [this, reply] {
    if (!m_busy || m_reply != reply)
      return;
    const auto bytes = reply->read(maxPageBytes + 1 - m_body.size());
    m_body += bytes;
    m_bytes += bytes.size();
    if (m_body.size() > maxPageBytes || m_bytes > maxRefreshBytes)
      complete("RomM response exceeded the size limit. Previous catalog kept.");
  };
  connect(reply, &QIODevice::readyRead, this, read);
  connect(reply, &QNetworkReply::finished, this, [this, reply, read] {
    if (!m_busy || m_reply != reply)
      return;
    read();
    if (!m_busy || m_reply != reply)
      return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (status >= 300 && status < 400 && !redirect.isEmpty()) {
      const auto target = reply->url().resolved(redirect);
      if (!target.isValid() || !sameOrigin(m_server, target) || ++m_redirects > 3) {
        complete("RomM returned an unsafe or repeated redirect. Previous catalog kept.");
        return;
      }
      disconnect(reply, nullptr, this, nullptr);
      reply->deleteLater();
      m_reply = nullptr;
      request(target);
      return;
    }
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
      complete(status == 401 || status == 403 ? "RomM authentication failed. Check the token and "
                                                "roms.read scope. Previous catalog kept."
                                              : "RomM request failed. Previous catalog kept.");
      return;
    }
    const auto object = QJsonDocument::fromJson(m_body).object();
    const auto items = object.value("items").toArray();
    const auto page = RommScanner::parsePage(m_body, m_root);
    const auto total = object.value("total");
    if (!page.complete || !object.value("offset").isDouble() ||
        object.value("offset").toInt(-1) != m_offset || ++m_pages > 1000 ||
        (total.isDouble() && m_total >= 0 && total.toInt() != m_total) ||
        (page.hasMore && page.nextOffset <= m_offset)) {
      complete("RomM returned an incomplete or changing catalog. Previous catalog kept.");
      return;
    }
    if (total.isDouble())
      m_total = total.toInt();
    for (const auto& item : items) {
      const auto idValue = item.toObject().value("id");
      const auto id = idValue.toInteger(-1);
      if (id <= 0 || !idValue.isDouble() || idValue.toDouble() != double(id) ||
          m_ids.contains(id) || m_ids.size() >= 100000) {
        complete("RomM returned duplicate or invalid entries. Previous catalog kept.");
        return;
      }
      m_ids.insert(id);
    }
    m_games += page.games;
    disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
    m_reply = nullptr;
    if (page.hasMore) {
      m_offset = page.nextOffset;
      m_redirects = 0;
      request(pageUrl(m_server, m_offset));
    } else if (m_total >= 0 && m_ids.size() != m_total) {
      complete("RomM returned an incomplete catalog. Previous catalog kept.");
    } else {
      complete(save() ? QString{} : "Could not save the RomM catalog. Previous catalog kept.");
    }
  });
}

bool RommCatalog::save() {
  if (QFileInfo(m_root).canonicalFilePath() != m_root || !QFileInfo(m_root).isDir() ||
      !m_database.transaction())
    return false;
  QSqlQuery query(m_database);
  const auto scope = cacheKey(m_server, m_root);
  query.prepare("DELETE FROM romm_catalog WHERE scope=?");
  query.addBindValue(scope);
  bool okay = query.exec();
  for (const auto& game : m_games) {
    if (!okay)
      break;
    QJsonObject record{{"appId", game.appId},
                       {"title", game.title},
                       {"description", game.description},
                       {"contentPath", game.contentPath},
                       {"system", game.system},
                       {"platform", game.platform},
                       {"coverReference", game.coverReference}};
    query.prepare("INSERT INTO romm_catalog(scope,id,record) VALUES(?,?,?)");
    query.addBindValue(scope);
    query.addBindValue(game.appId);
    query.addBindValue(QJsonDocument(record).toJson(QJsonDocument::Compact));
    okay = query.exec();
  }
  if (okay && m_database.commit())
    return true;
  m_database.rollback();
  return false;
}
QVector<RommGameRecord> RommCatalog::cached(const QUrl& server, const QString& root) const {
  QVector<RommGameRecord> result;
  const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
  const auto normalized = serverUrl(server);
  if (!m_valid || normalized.isEmpty() || canonicalRoot.isEmpty())
    return result;
  QSqlQuery query(m_database);
  query.prepare(
      "SELECT record FROM romm_catalog WHERE scope=? ORDER BY CAST(id AS INTEGER) LIMIT 100000");
  query.addBindValue(cacheKey(normalized, canonicalRoot));
  if (!query.exec())
    return result;
  while (query.next()) {
    const auto record = QJsonDocument::fromJson(query.value(0).toByteArray()).object();
    const QFileInfo file(record.value("contentPath").toString());
    if (!file.isFile() || file.isSymLink() ||
        !file.canonicalFilePath().startsWith(canonicalRoot + '/'))
      continue;
    result.append({record.value("appId").toString(), record.value("title").toString(),
                   record.value("description").toString(), file.canonicalFilePath(),
                   record.value("system").toString(), record.value("platform").toString(),
                   record.value("coverReference").toString()});
  }
  return result;
}

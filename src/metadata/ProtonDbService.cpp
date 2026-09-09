#include "metadata/ProtonDbService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

namespace {
constexpr qint64 kWeek = 7 * 86400;
constexpr qint64 kMaxResponse = 64 * 1024;
constexpr qint64 kMaxCache = 4 * 1024 * 1024;
const QStringList kTiers = {"borked", "bronze", "silver", "gold", "platinum", "native", "pending"};
} // namespace

ProtonDbService::ProtonDbService(const QString& cachePath, QObject* parent,
                                 QNetworkAccessManager* network)
    : QObject(parent), m_cachePath(cachePath),
      m_network(network ? network : new QNetworkAccessManager(this)) {
  QFile file(cachePath);
  if (file.open(QIODevice::ReadOnly) && file.size() <= kMaxCache) {
    const auto entries = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = entries.begin(); it != entries.end() && m_cache.size() < 10000; ++it) {
      const auto entry = it.value().toObject();
      const auto parsed = parseSummary(QJsonDocument(entry).toJson());
      const qint64 fetched = entry.value("fetched").toInteger();
      if (validAppId("Steam", it.key()) && !parsed.isEmpty() && fetched > 0 &&
          fetched <= QDateTime::currentSecsSinceEpoch()) {
        auto clean = parsed;
        clean.insert("fetched", fetched);
        m_cache.insert(it.key(), clean);
      }
    }
  }
  m_pace.setSingleShot(true);
  connect(&m_pace, &QTimer::timeout, this, &ProtonDbService::next);
  m_save.setSingleShot(true);
  m_save.setInterval(2000);
  connect(&m_save, &QTimer::timeout, this, &ProtonDbService::save);
}

ProtonDbService::~ProtonDbService() {
  if (m_reply) {
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
  }
  save();
}

bool ProtonDbService::validAppId(const QString& source, const QString& appId) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]{0,9}$"));
  // Steam's non-Steam shortcut IDs set the high bit. Never query them as store apps.
  return source == QStringLiteral("Steam") && digits.match(appId).hasMatch() &&
         appId.toULongLong() < 0x80000000ULL;
}

QJsonObject ProtonDbService::parseSummary(const QByteArray& bytes) {
  if (bytes.size() > kMaxResponse)
    return {};
  const auto doc = QJsonDocument::fromJson(bytes);
  if (!doc.isObject())
    return {};
  const auto object = doc.object();
  const QString tier = object.value("tier").toString();
  const auto total = object.value("total");
  const double count = total.toDouble(-1);
  if (!kTiers.contains(tier) || !total.isDouble() || count < 0 || count > 100000000 ||
      std::floor(count) != count)
    return {};
  return {{"tier", tier}, {"total", static_cast<int>(count)}};
}

void ProtonDbService::setEnabled(bool enabled) {
  if (m_enabled == enabled)
    return;
  m_enabled = enabled;
  if (!enabled) {
    m_queue.clear();
    m_pending.clear();
    if (m_reply)
      m_reply->abort();
  }
  ++m_revision;
  emit changed();
}

QVariantMap ProtonDbService::summary(const QString& source, const QString& appId) const {
  if (!m_enabled || !validAppId(source, appId) || !m_cache.contains(appId))
    return {};
  auto entry = m_cache.value(appId).toObject().toVariantMap();
  const auto fetched = entry.value("fetched").toLongLong();
  entry.insert("stale", QDateTime::currentSecsSinceEpoch() - fetched >= kWeek);
  entry.insert("updated", QDateTime::fromSecsSinceEpoch(fetched).date().toString(Qt::ISODate));
  return entry;
}

void ProtonDbService::request(const QString& source, const QString& appId) {
  if (!m_enabled || !validAppId(source, appId) || m_pending.contains(appId) || m_queue.size() >= 64)
    return;
  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto entry = m_cache.value(appId).toObject();
  const auto ttl = entry.value("tier").toString() == "pending" ? 86400 : kWeek;
  if ((!entry.isEmpty() && now - entry.value("fetched").toInteger() < ttl) ||
      m_retryAfter.value(appId) > now)
    return;
  m_pending.insert(appId);
  m_queue.enqueue(appId);
  if (!m_reply && !m_pace.isActive())
    next();
}

void ProtonDbService::next() {
  if (!m_enabled || m_reply || m_queue.isEmpty())
    return;
  const auto appId = m_queue.dequeue();
  QNetworkRequest request(
      QUrl(QStringLiteral("https://www.protondb.com/api/v1/reports/summaries/%1.json").arg(appId)));
  request.setRawHeader("User-Agent", "Omakade/1.8 (optional ProtonDB badges)");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setTransferTimeout(15000);
  auto* reply = m_network->get(request);
  m_reply = reply;
  reply->setReadBufferSize(kMaxResponse + 1);
  connect(reply, &QNetworkReply::readyRead, this, [reply] {
    if (reply->bytesAvailable() > kMaxResponse)
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, appId] {
    m_reply = nullptr;
    m_pending.remove(appId);
    int delay = 1000;
    if (m_enabled) {
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      const auto now = QDateTime::currentSecsSinceEpoch();
      auto entry = status == 200 && reply->error() == QNetworkReply::NoError
                       ? parseSummary(reply->read(kMaxResponse + 1))
                       : QJsonObject{};
      // No reports is not the same as a game that does not run.
      if (status == 404 && !m_cache.contains(appId))
        entry = {{"tier", "pending"}, {"total", 0}};
      if (!entry.isEmpty()) {
        entry.insert("fetched", now);
        if (m_cache.size() >= 10000 && !m_cache.contains(appId)) {
          QString oldest;
          qint64 oldestTime = now + 1;
          for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            const auto fetched = it.value().toObject().value("fetched").toInteger();
            if (fetched < oldestTime) {
              oldest = it.key();
              oldestTime = fetched;
            }
          }
          m_cache.remove(oldest);
        }
        m_cache.insert(appId, entry);
        m_dirty = true;
        if (!m_save.isActive())
          m_save.start();
        m_retryAfter.remove(appId);
      } else {
        if (m_retryAfter.size() >= 10000)
          m_retryAfter.clear();
        m_retryAfter.insert(appId, now + 3600);
      }
      if (status == 429 || status == 503)
        delay = qBound(60, reply->rawHeader("Retry-After").toInt(), 3600) * 1000;
      m_pace.start(delay);
      ++m_revision;
      emit changed();
    }
    reply->deleteLater();
    if (!m_pace.isActive())
      m_pace.start(delay);
  });
}

void ProtonDbService::save() {
  if (!m_dirty || m_cachePath.isEmpty())
    return;
  if (!QDir().mkpath(QFileInfo(m_cachePath).absolutePath()))
    return;
  QSaveFile file(m_cachePath);
  const auto bytes = QJsonDocument(m_cache).toJson(QJsonDocument::Compact);
  if (bytes.size() <= kMaxCache && file.open(QIODevice::WriteOnly) &&
      file.write(bytes) == bytes.size() && file.commit())
    m_dirty = false;
}

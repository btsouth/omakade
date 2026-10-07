#include "guide/GuideArt.h"

#include <QBuffer>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <memory>

namespace {
constexpr qsizetype maximumBytes = 8 * 1024 * 1024;
bool readable(QImageReader& reader, bool hero) {
  const auto size = reader.size();
  return reader.canRead() && size.width() > 0 && size.height() > 0 &&
         size.width() <= 8192 && size.height() <= 8192 &&
         qint64(size.width()) * size.height() <= 16000000 &&
         (!hero || (size.width() >= 1000 && size.height() >= 300 && size.width() > size.height())) &&
         !reader.read().isNull();
}
// The guide polls every second: decode a cached file once per change, not per poll.
bool cached(const QString& path, bool hero) {
  static QHash<QString, std::pair<QString, bool>> known;
  const QFileInfo file(path);
  if (!file.isFile()) return false;
  const auto stamp = QString::number(file.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(file.size());
  const auto entry = known.constFind(path);
  if (entry != known.cend() && entry->first == stamp) return entry->second;
  const bool valid = GuideArt::validImage(path, hero);
  known.insert(path, {stamp, valid});
  return valid;
}
QString localPath(const QString& art) {
  const QUrl url(art);
  return url.isLocalFile() ? url.toLocalFile() : art;
}
}

QString GuideArt::cacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/omakade/guide-art";
}
QString GuideArt::appId(const QString& source, const QVariantMap& metadata) {
  const auto id = metadata.value("appId").toString();
  static const QRegularExpression digits(QStringLiteral("\\A[0-9]+\\z"));
  return source == "Steam" && digits.match(id).hasMatch() ? id : QString{};
}
bool GuideArt::needsHero(const QString& hero) {
  return hero.isEmpty() || QFileInfo(localPath(hero)).completeBaseName().compare("header", Qt::CaseInsensitive) == 0 ||
         !QFileInfo::exists(localPath(hero));
}
bool GuideArt::validImage(const QString& path, bool hero) {
  if (!QFileInfo::exists(path) || QFileInfo(path).size() > maximumBytes) return false;
  QImageReader reader(path);
  return readable(reader, hero);
}
QVariantMap GuideArt::select(const QString& source, const QVariantMap& metadata, const QString& root) {
  auto result = metadata;
  const auto id = appId(source, metadata);
  if (id.isEmpty()) return result;
  const auto directory = root + '/' + id + '/';
  if (needsHero(metadata.value("heroPath").toString()) && cached(directory + "library_hero.jpg", true))
    result.insert("heroPath", QUrl::fromLocalFile(directory + "library_hero.jpg").toString());
  if (metadata.value("logoPath").toString().isEmpty() && cached(directory + "logo.png", false))
    result.insert("logoPath", QUrl::fromLocalFile(directory + "logo.png").toString());
  return result;
}
void GuideArt::prefetch(const QString& source, const QVariantMap& metadata) {
  const auto id = appId(source, metadata);
  if (id.isEmpty()) return;
  // Keep misses for the entire process, including if a guide service is recreated.
  static QSet<QString> attempted;
  for (const auto& name : {QStringLiteral("library_hero.jpg"), QStringLiteral("logo.png")}) {
    const bool hero = name == "library_hero.jpg";
    if (hero ? !needsHero(metadata.value("heroPath").toString()) : !metadata.value("logoPath").toString().isEmpty()) continue;
    const auto path = cacheRoot() + '/' + id + '/' + name;
    if (cached(path, hero) || attempted.contains(path) || m_pending.contains(path)) continue;
    attempted.insert(path);
    m_pending.insert(path);
    QNetworkRequest request(QUrl(QStringLiteral("https://shared.steamstatic.com/store_item_assets/steam/apps/%1/%2").arg(id, name)));
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = m_network.get(request);
    reply->setReadBufferSize(maximumBytes + 1);
    auto contents = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, contents] {
      contents->append(reply->read(maximumBytes + 1 - contents->size()));
      if (contents->size() > maximumBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, contents, path, id, hero] {
      if (contents->size() <= maximumBytes) contents->append(reply->read(maximumBytes + 1 - contents->size()));
      QBuffer buffer(contents.get());
      buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer);
      bool saved = false;
      if (reply->error() == QNetworkReply::NoError &&
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
          contents->size() <= maximumBytes && readable(reader, hero) && QDir().mkpath(QFileInfo(path).absolutePath())) {
        QSaveFile file(path);
        saved = file.open(QIODevice::WriteOnly) && file.write(*contents) == contents->size() && file.commit();
      }
      m_pending.remove(path);
      reply->deleteLater();
      if (saved) emit ready(id);
    });
  }
}

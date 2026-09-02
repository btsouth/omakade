#include "achievements/AchievementModel.h"

#include "app/AppSettings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {
constexpr qint64 kMaximumIconBytes = 2 * 1024 * 1024;
constexpr int kMaximumConcurrentIconDownloads = 4;

QString localUrl(const QString& path) {
  return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
}
} // namespace

AchievementModel::AchievementModel(const QString& databasePath, AppSettings* settings,
                                   QObject* parent)
    : QAbstractListModel(parent), m_settings(settings),
      m_connectionName(
          QStringLiteral("omakade-achievements-%1").arg(reinterpret_cast<quintptr>(this))) {
  m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_database.setDatabaseName(databasePath);
  m_database.open();
  updateCacheBytes();
}

AchievementModel::~AchievementModel() {
  m_database.close();
  m_database = {};
  QSqlDatabase::removeDatabase(m_connectionName);
}

int AchievementModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_achievements.size());
}

QVariant AchievementModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_achievements.size()) {
    return {};
  }
  const Achievement& achievement = m_achievements.at(index.row());
  switch (role) {
  case ApiNameRole:
    return achievement.apiName;
  case TitleRole:
    return achievement.title;
  case DescriptionRole:
    return achievement.description;
  case IconPathRole:
    return localUrl(achievement.iconPath);
  case UnlockedRole:
    return achievement.unlocked;
  case UnlockTimeRole:
    return achievement.unlockTime;
  case RarityRole:
    return achievement.rarity;
  case HiddenRole:
    return achievement.hidden;
  case CurrentProgressRole:
    return achievement.currentProgress;
  case MaximumProgressRole:
    return achievement.maximumProgress;
  default:
    return {};
  }
}

QHash<int, QByteArray> AchievementModel::roleNames() const {
  return {
      {ApiNameRole, "apiName"},
      {TitleRole, "title"},
      {DescriptionRole, "description"},
      {IconPathRole, "iconPath"},
      {UnlockedRole, "unlocked"},
      {UnlockTimeRole, "unlockTime"},
      {RarityRole, "rarity"},
      {HiddenRole, "hidden"},
      {CurrentProgressRole, "currentProgress"},
      {MaximumProgressRole, "maximumProgress"},
  };
}

QString AchievementModel::appId() const { return m_appId; }

int AchievementModel::unlocked() const { return m_unlocked; }

int AchievementModel::total() const { return m_total; }

int AchievementModel::knownCount() const { return static_cast<int>(m_achievements.size()); }

QString AchievementModel::statusText() const { return m_statusText; }

qint64 AchievementModel::cacheBytes() const { return m_cacheBytes; }

int AchievementModel::sortMode() const { return m_sortMode; }

void AchievementModel::setSortMode(int sortMode) {
  const int normalized = sortMode == 1 ? 1 : 0;
  if (m_sortMode == normalized) {
    return;
  }
  beginResetModel();
  m_sortMode = normalized;
  sortAchievements();
  endResetModel();
  emit sortModeChanged();
}

bool AchievementModel::acceptsIconUrl(const QUrl& url) {
  const QString host = url.host().toLower();
  return url.scheme() == QStringLiteral("https") &&
         (host == QStringLiteral("steamcdn-a.akamaihd.net") ||
          host == QStringLiteral("steamstatic.com") ||
          host.endsWith(QStringLiteral(".steamstatic.com")) ||
          host == QStringLiteral("media.retroachievements.org"));
}

void AchievementModel::load(const QString& appId) {
  while (!m_iconQueue.isEmpty()) {
    const IconRequest request = m_iconQueue.dequeue();
    m_pendingIcons.remove(request.appId + QLatin1Char('/') + request.apiName);
  }
  beginResetModel();
  m_appId = appId;
  m_achievements.clear();
  m_unlocked = 0;
  m_total = 0;
  bool confirmedEmpty = false;

  if (m_database.isOpen()) {
    QSqlQuery summary(m_database);
    summary.prepare(
        QStringLiteral("SELECT unlocked, total, source FROM achievement_summary WHERE app_id = ?"));
    summary.addBindValue(appId);
    if (summary.exec() && summary.next()) {
      m_unlocked = summary.value(0).toInt();
      m_total = summary.value(1).toInt();
      confirmedEmpty = m_total == 0 && summary.value(2).toString() == QStringLiteral("steam-web");
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT api_name, title, description, icon_url, icon_path, unlocked, unlock_time, rarity, "
        "hidden, current_progress, maximum_progress FROM achievements WHERE app_id = ? ORDER BY "
        "unlocked DESC, rarity ASC, title COLLATE NOCASE"));
    query.addBindValue(appId);
    if (query.exec()) {
      while (query.next()) {
        m_achievements.append({
            .apiName = query.value(0).toString(),
            .title = query.value(1).toString(),
            .description = query.value(2).toString(),
            .iconUrl = query.value(3).toString(),
            .iconPath = query.value(4).toString(),
            .unlocked = query.value(5).toBool(),
            .unlockTime = query.value(6).toLongLong(),
            .rarity = query.value(7).toDouble(),
            .hidden = query.value(8).toBool(),
            .currentProgress = query.value(9).toDouble(),
            .maximumProgress = query.value(10).toDouble(),
        });
      }
    }
  }
  sortAchievements();

  if (confirmedEmpty) {
    m_statusText = QStringLiteral("This game has no Steam achievements.");
  } else if (m_total == 0) {
    m_statusText =
        QStringLiteral("No achievement data is cached yet. Use Refresh above to fetch it.");
  } else if (m_achievements.size() < m_total) {
    m_statusText = QStringLiteral("Cached details for %1 of %2 achievements.")
                       .arg(m_achievements.size())
                       .arg(m_total);
  } else {
    m_statusText = QStringLiteral("Complete achievement cache");
  }
  endResetModel();
  emit summaryChanged();
  pruneCache();
  requestMissingIcons();
}

void AchievementModel::clearCache() {
  const QString root = cacheRoot();
  if (!root.endsWith(QStringLiteral("/omakade/achievements"))) {
    return;
  }
  QDir(root).removeRecursively();
  QDir().mkpath(root);
  if (m_database.isOpen()) {
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("UPDATE achievements SET icon_path = ''"));
  }
  for (Achievement& achievement : m_achievements) {
    achievement.iconPath.clear();
  }
  if (!m_achievements.isEmpty()) {
    emit dataChanged(index(0), index(m_achievements.size() - 1), {IconPathRole});
  }
  updateCacheBytes();
}

QString AchievementModel::cacheRoot() const {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/achievements");
}

QString AchievementModel::pathForIcon(const QString& appId, const QString& url) const {
  const QByteArray hash =
      QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();
  return cacheRoot() + QLatin1Char('/') + appId + QLatin1Char('/') + QString::fromLatin1(hash) +
         QStringLiteral(".jpg");
}

void AchievementModel::requestMissingIcons() {
  for (int row = 0; row < m_achievements.size(); ++row) {
    Achievement& achievement = m_achievements[row];
    if (!achievement.iconPath.isEmpty() && QFileInfo::exists(achievement.iconPath)) {
      continue;
    }
    const QString expected = pathForIcon(m_appId, achievement.iconUrl);
    if (QFileInfo::exists(expected)) {
      achievement.iconPath = expected;
      emit dataChanged(index(row), index(row), {IconPathRole});
      continue;
    }
    if (!acceptsIconUrl(QUrl(achievement.iconUrl))) {
      continue;
    }
    const QString key = m_appId + QLatin1Char('/') + achievement.apiName;
    if (!m_pendingIcons.contains(key)) {
      m_pendingIcons.insert(key);
      m_iconQueue.enqueue({m_appId, achievement.apiName});
    }
  }
  startNextIconDownloads();
}

void AchievementModel::startNextIconDownloads() {
  while (m_activeIconDownloads < kMaximumConcurrentIconDownloads && !m_iconQueue.isEmpty()) {
    const IconRequest request = m_iconQueue.dequeue();
    if (request.appId != m_appId) {
      m_pendingIcons.remove(request.appId + QLatin1Char('/') + request.apiName);
      continue;
    }
    int row = -1;
    for (int candidate = 0; candidate < m_achievements.size(); ++candidate) {
      if (m_achievements.at(candidate).apiName == request.apiName) {
        row = candidate;
        break;
      }
    }
    if (row < 0) {
      m_pendingIcons.remove(request.appId + QLatin1Char('/') + request.apiName);
      continue;
    }
    requestIcon(row);
  }
}

void AchievementModel::requestIcon(int row) {
  if (row < 0 || row >= m_achievements.size()) {
    return;
  }
  const QUrl url(m_achievements.at(row).iconUrl);
  if (!acceptsIconUrl(url)) {
    return;
  }
  QNetworkRequest request(url);
  request.setTransferTimeout(10000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  QNetworkReply* reply = m_network.get(request);
  ++m_activeIconDownloads;
  reply->setProperty("achievementAppId", m_appId);
  reply->setProperty("achievementApiName", m_achievements.at(row).apiName);
  m_iconBuffers.insert(reply, {});
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    QByteArray& buffer = m_iconBuffers[reply];
    const qsizetype remaining = kMaximumIconBytes - buffer.size();
    buffer.append(reply->read(remaining + 1));
    if (buffer.size() > kMaximumIconBytes) {
      reply->setProperty("tooLarge", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    const QString appId = reply->property("achievementAppId").toString();
    const QString apiName = reply->property("achievementApiName").toString();
    QByteArray contents = m_iconBuffers.take(reply);
    if (contents.size() <= kMaximumIconBytes) {
      contents.append(reply->read(kMaximumIconBytes + 1 - contents.size()));
    }
    const bool tooLarge =
        reply->property("tooLarge").toBool() || contents.size() > kMaximumIconBytes;
    int row = -1;
    if (appId == m_appId) {
      for (int candidate = 0; candidate < m_achievements.size(); ++candidate) {
        if (m_achievements.at(candidate).apiName == apiName) {
          row = candidate;
          break;
        }
      }
    }
    const bool validRow = row >= 0;
    if (reply->error() == QNetworkReply::NoError && validRow && !contents.isEmpty() &&
        !tooLarge && !QImage::fromData(contents).isNull()) {
      const QString path = pathForIcon(appId, m_achievements.at(row).iconUrl);
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile file(path);
      if (file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() &&
          file.commit()) {
        m_achievements[row].iconPath = path;
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "UPDATE achievements SET icon_path = ? WHERE app_id = ? AND api_name = ?"));
        query.addBindValue(path);
        query.addBindValue(appId);
        query.addBindValue(apiName);
        query.exec();
        emit dataChanged(index(row), index(row), {IconPathRole});
      }
    }
    reply->deleteLater();
    --m_activeIconDownloads;
    m_pendingIcons.remove(appId + QLatin1Char('/') + apiName);
    startNextIconDownloads();
    if (m_activeIconDownloads == 0 && m_iconQueue.isEmpty()) {
      pruneCache();
    }
  });
}

void AchievementModel::sortAchievements() {
  std::sort(m_achievements.begin(), m_achievements.end(), [this](const Achievement& left,
                                                                 const Achievement& right) {
    if (left.unlocked != right.unlocked) {
      return left.unlocked;
    }
    if (m_sortMode == 1 && left.unlocked && left.unlockTime != right.unlockTime) {
      return left.unlockTime > right.unlockTime;
    }
    if (m_sortMode == 0 && left.rarity != right.rarity) {
      return left.rarity < right.rarity;
    }
    return QString::compare(left.title, right.title, Qt::CaseInsensitive) < 0;
  });
}

void AchievementModel::pruneCache() {
  const int limitMb = m_settings == nullptr ? 1024 : m_settings->artworkCacheLimitMb();
  const qint64 limit = static_cast<qint64>(limitMb) * 1024 * 1024;
  struct CachedFile {
    QString path;
    QDateTime modified;
    qint64 size = 0;
  };
  QVector<CachedFile> files;
  qint64 total = 0;
  QDirIterator iterator(cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QFileInfo info(iterator.next());
    files.append({info.absoluteFilePath(), info.lastModified(), info.size()});
    total += info.size();
  }
  std::sort(files.begin(), files.end(), [](const CachedFile& left, const CachedFile& right) {
    return left.modified < right.modified;
  });
  for (const CachedFile& file : files) {
    if (total <= limit) {
      break;
    }
    if (QFile::remove(file.path)) {
      total -= file.size;
    }
  }
  updateCacheBytes();
}

void AchievementModel::updateCacheBytes() {
  qint64 bytes = 0;
  QDirIterator iterator(cacheRoot(), QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    bytes += QFileInfo(iterator.next()).size();
  }
  if (m_cacheBytes != bytes) {
    m_cacheBytes = bytes;
    emit cacheChanged();
  }
}

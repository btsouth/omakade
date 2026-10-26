#pragma once

#include <QAbstractListModel>
#include <QImage>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

class AppSettings;
class QNetworkReply;
class QUrl;

class AchievementModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString appId READ appId NOTIFY summaryChanged)
  Q_PROPERTY(int unlocked READ unlocked NOTIFY summaryChanged)
  Q_PROPERTY(int total READ total NOTIFY summaryChanged)
  Q_PROPERTY(int knownCount READ knownCount NOTIFY summaryChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY summaryChanged)
  Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheChanged)
  Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

public:
  enum Role {
    ApiNameRole = Qt::UserRole + 1,
    TitleRole,
    DescriptionRole,
    IconPathRole,
    UnlockedRole,
    UnlockTimeRole,
    RarityRole,
    HiddenRole,
    CurrentProgressRole,
    MaximumProgressRole,
  };
  Q_ENUM(Role)

  explicit AchievementModel(const QString& databasePath, AppSettings* settings,
                            QObject* parent = nullptr);
  ~AchievementModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QString appId() const;
  [[nodiscard]] int unlocked() const;
  [[nodiscard]] int total() const;
  [[nodiscard]] int knownCount() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] qint64 cacheBytes() const;
  [[nodiscard]] int sortMode() const;
  void setSortMode(int sortMode);

  [[nodiscard]] static bool acceptsIconUrl(const QUrl& url);
  // Wide achievement art, such as Xbox's 16:9 pictures of an icon on a plain backdrop, cut
  // to the square icon a tile shows: the icon without its backdrop, or the middle of a
  // picture that fills the frame. Square art is returned as it is.
  [[nodiscard]] static QImage squareIcon(const QImage& image);

  Q_INVOKABLE void load(const QString& appId);
  Q_INVOKABLE void clearCache();

signals:
  void summaryChanged();
  void cacheChanged();
  void sortModeChanged();

private:
  struct Achievement {
    QString apiName;
    QString title;
    QString description;
    QString iconUrl;
    QString iconPath;
    bool unlocked = false;
    qint64 unlockTime = 0;
    double rarity = 0.0;
    bool hidden = false;
    double currentProgress = 0.0;
    double maximumProgress = 0.0;
  };

  struct IconRequest {
    QString appId;
    QString apiName;
  };

  [[nodiscard]] QString cacheRoot() const;
  [[nodiscard]] QString pathForIcon(const QString& appId, const QString& url) const;
  void requestMissingIcons();
  void startNextIconDownloads();
  void requestIcon(int row);
  void sortAchievements();
  void pruneCache();
  [[nodiscard]] qint64 cacheLimitBytes() const;
  void updateCacheBytes();

  QVector<Achievement> m_achievements;
  QString m_appId;
  QString m_statusText;
  int m_unlocked = 0;
  int m_total = 0;
  qint64 m_cacheBytes = 0;
  int m_sortMode = 0;
  AppSettings* m_settings = nullptr;
  QSqlDatabase m_database;
  QString m_connectionName;
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_iconBuffers;
  QQueue<IconRequest> m_iconQueue;
  QSet<QString> m_pendingIcons;
  int m_activeIconDownloads = 0;
};

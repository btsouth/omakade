#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

class ProtonDbService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
  explicit ProtonDbService(const QString& cachePath, QObject* parent = nullptr,
                           QNetworkAccessManager* network = nullptr);
  ~ProtonDbService() override;
  bool enabled() const { return m_enabled; }
  int revision() const { return m_revision; }
  void setEnabled(bool enabled);
  Q_INVOKABLE void request(const QString& source, const QString& appId);
  Q_INVOKABLE QVariantMap summary(const QString& source, const QString& appId) const;
  static bool validAppId(const QString& source, const QString& appId);
  static QJsonObject parseSummary(const QByteArray& bytes);
signals:
  void changed();

private:
  void next();
  void save();
  QString m_cachePath;
  QNetworkAccessManager* m_network;
  QPointer<QNetworkReply> m_reply;
  QJsonObject m_cache;
  QQueue<QString> m_queue;
  QSet<QString> m_pending;
  QHash<QString, qint64> m_retryAfter;
  QTimer m_pace;
  QTimer m_save;
  bool m_enabled = false;
  bool m_dirty = false;
  int m_revision = 0;
};

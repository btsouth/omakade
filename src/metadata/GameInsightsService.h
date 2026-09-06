#pragma once

#include "metadata/IgdbApi.h"

#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>

class AppSettings;
class QNetworkReply;

struct InsightsSecretResult {
  bool success = false;
  bool found = false;
  QByteArray secret;
  QString error;
};

class GameInsightsService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString clientId READ clientId NOTIFY changed)
  Q_PROPERTY(bool hasClientSecret READ hasClientSecret NOTIFY changed)
  Q_PROPERTY(bool configured READ configured NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
  Q_PROPERTY(int criticScore READ criticScore NOTIFY changed)
  Q_PROPERTY(int criticReviewCount READ criticReviewCount NOTIFY changed)
  Q_PROPERTY(int rushedHours READ rushedHours NOTIFY changed)
  Q_PROPERTY(int normalHours READ normalHours NOTIFY changed)
  Q_PROPERTY(int completeHours READ completeHours NOTIFY changed)
  Q_PROPERTY(int timeSampleCount READ timeSampleCount NOTIFY changed)

public:
  explicit GameInsightsService(const QString& databasePath, AppSettings* settings,
                               QObject* parent = nullptr);
  ~GameInsightsService() override;

  [[nodiscard]] QString clientId() const;
  [[nodiscard]] bool hasClientSecret() const;
  [[nodiscard]] bool configured() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] bool available() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] int criticScore() const;
  [[nodiscard]] int criticReviewCount() const;
  [[nodiscard]] int rushedHours() const;
  [[nodiscard]] int normalHours() const;
  [[nodiscard]] int completeHours() const;
  [[nodiscard]] int timeSampleCount() const;

  Q_INVOKABLE void saveCredentials(const QString& clientId, QString secret);
  Q_INVOKABLE void testConnection();
  Q_INVOKABLE void setClientId(const QString& clientId);
  Q_INVOKABLE void storeClientSecret(QString secret);
  Q_INVOKABLE void removeCredentials();
  Q_INVOKABLE void loadSteam(const QString& appId);
  Q_INVOKABLE void refreshSteam(const QString& appId);

  bool requestCatalog(const QByteArray& query, const QString& endpoint = QStringLiteral("games"));

signals:
  void catalogFinished(const QByteArray& contents, const QString& error);
  void changed();

private:
  enum class SecretAction { Detect, Store, Remove, Lookup };
  enum class RequestKind { Token, Mapping, Game, Time, Catalog };

  void beginSecretOperation(SecretAction action, const QByteArray& value = {});
  void finishSecretOperation();
  void requestToken(QByteArray secret);
  void requestMapping();
  void sendCatalog();
  void requestGame();
  void requestTime();
  void sendRequest(const QNetworkRequest& request, const QByteArray& body, RequestKind kind);
  void finishRequest(QNetworkReply* reply);
  void fail(const QString& message);
  void clearCurrent();
  bool loadCache(const QString& appId);
  bool persist();

  AppSettings* m_settings = nullptr;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<InsightsSecretResult> m_secretWatcher;
  SecretAction m_secretAction = SecretAction::Detect;
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_buffers;
  QString m_appId;
  QString m_refreshAppId;
  QString m_catalogEndpoint;
  QByteArray m_catalogQuery;
  QByteArray m_accessToken;
  qint64 m_accessTokenExpiry = 0;
  IgdbGameInsight m_insight;
  qint64 m_updatedAt = 0;
  bool m_hasClientSecret = false;
  bool m_busy = false;
  QString m_statusText;
};

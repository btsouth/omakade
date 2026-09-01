#pragma once

#include "achievements/SteamAchievementApi.h"
#include "sources/steam/SteamOwnedGames.h"

#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>

class AppSettings;
class QNetworkReply;

struct SteamSecretResult {
  bool success = false;
  bool found = false;
  QByteArray secret;
  QString error;
};

class SteamAccountService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString steamId READ steamId NOTIFY accountChanged)
  Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY accountChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)

public:
  explicit SteamAccountService(const QString& databasePath, AppSettings* settings,
                               QObject* parent = nullptr);
  ~SteamAccountService() override;

  [[nodiscard]] QString steamId() const;
  [[nodiscard]] bool hasApiKey() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString state() const;

  Q_INVOKABLE void setSteamId(const QString& steamId);
  Q_INVOKABLE void storeApiKey(QString apiKey);
  Q_INVOKABLE void removeApiKey();
  Q_INVOKABLE void refreshAchievements(const QString& appId);
  Q_INVOKABLE void refreshAchievementsIfStale(const QString& appId);
  Q_INVOKABLE void refreshOwnedGames();

signals:
  void accountChanged();
  void busyChanged();
  void statusChanged();
  void achievementsUpdated(const QString& appId);
  void ownedGamesUpdated();

private:
  enum class SecretAction { Detect, Store, Remove, LookupForRefresh, LookupForOwned };
  struct ApiRequestState {
    QByteArray player;
    QByteArray schema;
    QByteArray rarity;
    SteamApiState failure = SteamApiState::Ready;
    QString error;
    int pending = 0;
  };

  [[nodiscard]] static QString discoverSteamId();
  void beginSecretOperation(SecretAction action, const QByteArray& value = {});
  void finishSecretOperation();
  void startApiRequests(QByteArray apiKey);
  void startOwnedGamesRequest(QByteArray apiKey);
  void handleOwnedGamesReply(QNetworkReply* reply);
  bool persistOwnedGames(const SteamOwnedGamesResult& result);
  void handleApiReply(QNetworkReply* reply);
  void finishApiRequests();
  bool persistAchievements(const SteamAchievementApiResult& result);
  void setBusy(bool busy);
  void setStatus(const QString& state, const QString& text);

  AppSettings* m_settings = nullptr;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<SteamSecretResult> m_secretWatcher;
  SecretAction m_secretAction = SecretAction::Detect;
  QString m_refreshAppId;
  bool m_hasApiKey = false;
  bool m_busy = false;
  QString m_statusText;
  QString m_state = QStringLiteral("local");
  QString m_pendingAutoRefreshAppId;
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_responseBuffers;
  ApiRequestState m_api;
};

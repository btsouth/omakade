#pragma once

#include "achievements/RetroAchievementsApi.h"
#include "achievements/RetroAchievementsHasher.h"

#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>

class AppSettings;
class QNetworkReply;

struct RetroAchievementsSecretResult {
  bool success = false;
  bool found = false;
  QByteArray secret;
  QString error;
};

// Read-only RetroAchievements integration for RetroArch games, mirroring SteamAccountService: the
// user supplies their own RetroAchievements username + personal Web API key, and this fetches and
// caches (never writes back) their existing unlock state. It never logs into RetroArch, starts a
// play session, or awards achievements itself.
class RetroAchievementsService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString username READ username NOTIFY accountChanged)
  Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY accountChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString state READ state NOTIFY statusChanged)

public:
  explicit RetroAchievementsService(const QString& databasePath, AppSettings* settings,
                                    QObject* parent = nullptr);
  ~RetroAchievementsService() override;

  [[nodiscard]] QString username() const;
  [[nodiscard]] bool hasApiKey() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString state() const;

  Q_INVOKABLE void setUsername(const QString& username);
  Q_INVOKABLE void storeApiKey(QString apiKey);
  Q_INVOKABLE void removeApiKey();
  Q_INVOKABLE void refreshAchievements(const QString& gameId);
  Q_INVOKABLE void refreshAchievementsIfStale(const QString& gameId);

signals:
  void accountChanged();
  void busyChanged();
  void statusChanged();
  void achievementsUpdated(const QString& gameId);

private:
  enum class SecretAction { Detect, Store, Remove, LookupForRefresh };
  enum class RequestKind { ConsoleIds, GameList, GameInfo };

  struct PendingRefresh {
    QString gameId;
    QString contentPath;
    QString consoleName;
    QByteArray hash;
    int consoleId = 0;
    bool gameListRefreshed = false;
  };

  void beginSecretOperation(SecretAction action, const QByteArray& value = {});
  void startDetectOnUsernameChanged();
  void finishSecretOperation();
  void clearCachedAchievements();
  void startRefreshPipeline(QByteArray apiKey);
  void finishHashing();
  void tryMatchOrFetchGameList();
  void requestConsoleIds();
  void requestGameList(int consoleId);
  void requestGameInfo(qint64 raGameId);
  void sendRequest(const QUrl& url, RequestKind kind);
  void handleReply(QNetworkReply* reply, RequestKind kind);
  bool persistAchievements(const RetroAchievementsProgressResult& result);
  bool reportBusy();
  void setBusy(bool busy);
  void setStatus(const QString& state, const QString& text);
  void fail(const QString& state, const QString& text);

  AppSettings* m_settings = nullptr;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<RetroAchievementsSecretResult> m_secretWatcher;
  QFutureWatcher<std::optional<QByteArray>> m_hashWatcher;
  SecretAction m_secretAction = SecretAction::Detect;
  PendingRefresh m_pending;
  QByteArray m_activeApiKey;
  QString m_pendingAutoRefreshGameId;
  bool m_hasApiKey = false;
  bool m_busy = false;
  QString m_statusText;
  QString m_state = QStringLiteral("local");
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_responseBuffers;
};

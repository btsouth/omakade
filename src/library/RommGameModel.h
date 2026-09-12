#pragma once
#include "sources/romm/RommCatalog.h"
#include "sources/romm/RommCredentials.h"
#include <QAbstractListModel>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
class AppSettings;
class PlaySessionStore;
class RommGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(bool hasToken READ hasToken NOTIFY statusChanged)
public:
  using Credentials = std::function<RommCredentialResult(const QUrl&, const QByteArray&, bool)>;
  RommGameModel(const QString& cachePath, AppSettings* settings,
                PlaySessionStore* sessions = nullptr, QObject* parent = nullptr,
                QNetworkAccessManager* network = nullptr, Credentials credentials = {});
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  bool scanning() const { return m_loading || m_catalog.busy(); }
  QString statusText() const { return m_status; }
  QString errorText() const { return m_error; }
  QStringList detectedPaths() const;
  bool hasToken() const { return m_hasToken; }
  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool connectServer(const QString& server, const QString& root, const QString& token);
  Q_INVOKABLE void storeToken(const QString& token);
  Q_INVOKABLE void disconnectServer(bool forgetToken = false);
  void configure();
signals:
  void statusChanged();

private:
  void credentials(const QByteArray& token, bool store, bool refreshAfter);
  void reload();
  bool available(const QString& path) const;
  AppSettings* m_settings;
  PlaySessionStore* m_sessions;
  RommCatalog m_catalog;
  Credentials m_credentials;
  QVector<RommGameRecord> m_games;
  QUrl m_server;
  QString m_root, m_status, m_error;
  struct CredentialQueue {
    std::mutex mutex;
    std::atomic<quint64> generation{0};
  };
  std::shared_ptr<CredentialQueue> m_credentialQueue = std::make_shared<CredentialQueue>();
  quint64 m_generation = 0;
  bool m_enabled = false, m_loading = false, m_hasToken = false;
};

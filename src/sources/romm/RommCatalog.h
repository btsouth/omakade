#pragma once
#include "sources/romm/RommScanner.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSqlDatabase>
#include <QTimer>
#include <QUrl>

// Read-only remote catalog. Only a complete refresh replaces the local SQLite snapshot.
// Tokens are held in memory for a refresh and never written to the cache.
class RommCatalog final : public QObject {
  Q_OBJECT
public:
  explicit RommCatalog(const QString& databasePath, QObject* parent = nullptr,
                       QNetworkAccessManager* network = nullptr);
  ~RommCatalog() override;
  // How the token travels. Plain HTTP is accepted without asking only for loopback and
  // local network hosts; any other HTTP host needs the user's confirmation first.
  enum class Transport { Invalid, Https, LocalHttp, PublicHttp };
  static Transport transport(const QUrl& server);
  static QUrl serverUrl(const QUrl& url);
  static bool validToken(const QByteArray& token);
  bool refresh(const QUrl& server, const QString& libraryRoot, const QByteArray& token,
               bool allowPublicHttp = false);
  void cancel();
  bool busy() const { return m_busy; }
  QVector<RommGameRecord> cached(const QUrl& server, const QString& libraryRoot) const;

signals:
  void finished(bool success, const QString& message);

private:
  void request(const QUrl& url);
  void complete(const QString& error = {});
  bool save();
  static QString cacheKey(const QUrl& server, const QString& root);
  QSqlDatabase m_database;
  QString m_connection;
  QNetworkAccessManager* m_network;
  QPointer<QNetworkReply> m_reply;
  QTimer m_timeout;
  QUrl m_server;
  QString m_root;
  QByteArray m_token;
  QByteArray m_body;
  QVector<RommGameRecord> m_games;
  QSet<qint64> m_ids;
  qsizetype m_bytes = 0;
  int m_offset = 0;
  int m_pages = 0;
  int m_redirects = 0;
  int m_total = -1;
  bool m_valid = false;
  bool m_busy = false;
};

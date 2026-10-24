#pragma once

#include "sources/xodus/XodusScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QSqlDatabase>

class AppSettings;
class QNetworkReply;

class XodusGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool xodusDetected READ xodusDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)

public:
  explicit XodusGameModel(const QString& omakadeDatabasePath, AppSettings* settings = nullptr,
                          QObject* parent = nullptr);
  ~XodusGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool xodusDetected() const { return m_xodusDetected; }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] QString errorText() const { return m_errorText; }
  [[nodiscard]] QStringList detectedPaths() const { return m_detectedPaths; }
  [[nodiscard]] qint64 lastScan() const { return m_lastScan; }
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] static QString artworkCacheRoot();

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  void refreshFromRoots(const QStringList& roots, const QString& artworkCache = {});
  // Tests turn artwork requests off so a scan never reaches the network.
  void setArtworkDownloadsEnabled(bool enabled) { m_downloadArtwork = enabled; }

signals:
  void statusChanged();

private:
  struct Game {
    XodusGameRecord xodus;
    // From the store catalog: a portrait poster for the cover and a wide hero.
    QString posterPath;
    QString heroPath;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const XodusScanResult& result);
  bool setFlag(int row, const char* column, bool Game::* flag, int role);
  struct ArtRequest {
    QString gameId;
    QUrl url;
    bool hero = false;
  };
  void requestMissingArtwork();
  void requestCatalog(const QStringList& storeIds);
  void queueArtwork(const QString& gameId, const QUrl& url, bool hero);
  void startNextArtworkDownloads();
  void downloadArtwork(const ArtRequest& request);
  void applyArtwork(const QString& gameId, const QString& path, bool hero);
  void pruneArtworkCache();
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  AppSettings* m_settings = nullptr;
  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<XodusScanResult> m_scanWatcher;
  QNetworkAccessManager m_network;
  QQueue<ArtRequest> m_artQueue;
  QHash<QNetworkReply*, QByteArray> m_artBuffers;
  QSet<QString> m_pendingArt;
  QSet<QString> m_failedArt;
  QSet<QString> m_catalogAsked;
  int m_activeArtDownloads = 0;
  bool m_downloadArtwork = true;
  bool m_scanning = false;
  bool m_xodusDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
};

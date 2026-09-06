#pragma once

#include "sources/steam/SteamScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QSqlDatabase>
#include <QTimer>

class AppSettings;
class QNetworkReply;

class SteamGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
  Q_PROPERTY(bool steamDetected READ steamDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(int artworkCount READ artworkCount NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)
  Q_PROPERTY(QString databasePath READ databasePath CONSTANT)

public:
  explicit SteamGameModel(const QString& databasePath = {}, AppSettings* settings = nullptr,
                          QObject* parent = nullptr);
  ~SteamGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] bool scanning() const;
  [[nodiscard]] bool steamDetected() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] int artworkCount() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;
  [[nodiscard]] QString databasePath() const;

  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void reloadAchievementSummary(const QString& appId);
  Q_INVOKABLE void reloadOwnedGames();
  Q_INVOKABLE void requestCover(const QString& appId);
  void refreshFromRoots(const QStringList& roots);

signals:
  void scanningChanged();
  void statusChanged();

private:
  struct Game {
    SteamGameRecord steam;
    bool favorite = false;
    bool hidden = false;
    int achievementsUnlocked = 0;
    int achievementsTotal = 0;
    bool installed = true;
    QColor accentStart;
    QColor accentEnd;
  };

  [[nodiscard]] static QString defaultDatabasePath();
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const SteamScanResult& result);
  void reportScan(const SteamScanResult& result);
  void rebuildWatchPaths(const SteamScanResult& result);
  void setStatus(const QString& status, const QString& error = {});
  void requestMissingCovers();
  void requestCoverForGame(const Game& game);
  void startNextCoverDownloads();
  void downloadCover(const QString& appId, int attempt);
  void applyCover(const QString& appId, const QString& path);
  // Cover paths are written to the database in one batch shortly after they arrive.
  void flushCoverWrites();
  QHash<QString, QString> m_pendingCoverWrites;
  QTimer m_coverWriteTimer;
  void pruneCoverCache();

  struct CoverRequest {
    QString appId;
    int attempt = 0;
  };

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_databasePath;
  QFutureWatcher<SteamScanResult> m_scanWatcher;
  QFileSystemWatcher m_fileWatcher;
  QTimer m_rescanTimer;
  bool m_scanning = false;
  bool m_rescanPending = false;
  bool m_explicitRefresh = false;
  bool m_steamDetected = false;
  SteamScanResult m_appliedScan;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
  AppSettings* m_settings = nullptr;
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_coverBuffers;
  QQueue<CoverRequest> m_coverQueue;
  QSet<QString> m_pendingCovers;
  QSet<QString> m_failedCovers;
  int m_activeCoverDownloads = 0;
};

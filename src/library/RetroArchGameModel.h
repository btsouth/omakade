#pragma once

#include "sources/retroarch/RetroArchScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QHash>
#include <QTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QSqlDatabase>

class AppSettings;
class QNetworkReply;

class RetroArchGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool retroArchDetected READ retroArchDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)

public:
  explicit RetroArchGameModel(const QString& databasePath, AppSettings* settings = nullptr,
                              QObject* parent = nullptr);
  ~RetroArchGameModel() override;
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool retroArchDetected() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;
  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void requestCover(const QString& appId);
  [[nodiscard]] bool scanning() const { return m_scanning; }
  Q_INVOKABLE void reloadAchievementSummary(const QString& gameId);
  void clearAchievementSummaries();
  void setConfiguredRomFolders(const QStringList& encoded);
  void refreshFromRoots(const QStringList& roots);
  void refreshFromSources(const QStringList& retroArchRoots, const QStringList& encodedFolders);
  [[nodiscard]] static QString libretroCoverCachePath(const QString& gameId);
  [[nodiscard]] static QString libretroCoverUrl(const QString& playlist, const QString& label);
  [[nodiscard]] static QStringList coverLabelCandidates(const QString& title,
                                                        const QString& fileBase);

signals:
  void statusChanged();

private:
  struct Game {
    RetroArchGameRecord retroArch;
    bool favorite = false;
    bool hidden = false;
    int achievementsUnlocked = 0;
    int achievementsTotal = 0;
    QColor accentStart;
    QColor accentEnd;
  };
  struct CoverRequest {
    QString gameId;
    int attempt = 0;
  };
  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const RetroArchScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});
  void requestCoverForGame(const Game& game);
  void startNextCoverDownloads();
  void downloadCover(const QString& gameId, int attempt);
  void applyCover(const QString& gameId, const QString& path);
  // Cover paths are written to the database in one batch shortly after they arrive.
  void flushCoverWrites();
  QHash<QString, QString> m_pendingCoverWrites;
  QTimer m_coverWriteTimer;
  void pruneCoverCache();
  [[nodiscard]] QStringList coverLabels(const Game& game) const;

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  AppSettings* m_settings = nullptr;
  bool m_retroArchDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
  QFutureWatcher<RetroArchScanResult> m_scanWatcher;
  bool m_scanning = false;
  QStringList m_configuredRomFolders;
  QNetworkAccessManager m_network;
  QHash<QNetworkReply*, QByteArray> m_coverBuffers;
  QQueue<CoverRequest> m_coverQueue;
  QSet<QString> m_pendingCovers;
  QSet<QString> m_failedCovers;
  int m_activeCoverDownloads = 0;
};

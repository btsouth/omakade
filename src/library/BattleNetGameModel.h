#pragma once

#include "sources/battlenet/BattleNetScanner.h"

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

class BattleNetGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool battleNetDetected READ battleNetDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit BattleNetGameModel(const QString& omakadeDatabasePath, AppSettings* settings = nullptr,
                              QObject* parent = nullptr);
  ~BattleNetGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool battleNetDetected() const;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void requestCover(const QString& appId);
  Q_INVOKABLE void refresh();
  void refreshFromPrefixes(const QStringList& prefixes);

signals:
  void statusChanged();

private:
  struct Game {
    BattleNetGameRecord battlenet;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  struct CoverRequest {
    QString gameId;
    QString productId;
    bool hero = false;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const BattleNetScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});
  void requestMissingCovers();
  void requestCoverForGame(const Game& game);
  void startNextCoverDownloads();
  void downloadArtwork(const QString& gameId, const QString& productId, bool hero);
  void applyArtwork(const QString& gameId, const QString& path, bool hero);
  void pruneCoverCache();

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  QFutureWatcher<BattleNetScanResult> m_scanWatcher;
  bool m_battleNetDetected = false;
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

#pragma once

#include "sources/dolphin/DolphinScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QHash>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QSet>
#include <QSqlDatabase>

class DolphinGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool dolphinDetected READ dolphinDetected NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit DolphinGameModel(const QString& omakadeDatabasePath, QObject* parent = nullptr);
  ~DolphinGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool dolphinDetected() const;
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void requestCover(const QString& appId);
  void refreshFromRoots(const QStringList& roots, const QStringList& folders = {});
  [[nodiscard]] static QString gameTdbCoverUrl(const QString& discId);
  [[nodiscard]] static QString coverCachePath(const QString& discId);

signals:
  void statusChanged();

private:
  struct Game {
    DolphinGameRecord dolphin;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const DolphinScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  void applyCover(const QString& gameId, const QString& path);
  // Cover paths are written to the database in one batch shortly after they arrive.
  void flushCoverWrites();
  QHash<QString, QString> m_pendingCoverWrites;
  QTimer m_coverWriteTimer;
  QNetworkAccessManager m_network;
  QSet<QString> m_pendingCovers;
  QSet<QString> m_failedCovers;
  QFutureWatcher<DolphinScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_dolphinDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
};

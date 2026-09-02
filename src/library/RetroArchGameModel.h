#pragma once

#include "sources/retroarch/RetroArchScanner.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

class RetroArchGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool retroArchDetected READ retroArchDetected NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit RetroArchGameModel(const QString& databasePath, QObject* parent = nullptr);
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
  Q_INVOKABLE void reloadAchievementSummary(const QString& gameId);
  void refreshFromRoots(const QStringList& roots);

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
  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const RetroArchScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  bool m_retroArchDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
  QFutureWatcher<RetroArchScanResult> m_scanWatcher;
};

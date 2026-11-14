#pragma once

#include "sources/lichii/LichiiScanner.h"
#include "tracking/PlaySessionStore.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

class LichiiGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool lichiiDetected READ lichiiDetected NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  explicit LichiiGameModel(const QString& omakadeDatabasePath,
                           PlaySessionStore* playSessions = nullptr, QObject* parent = nullptr);
  ~LichiiGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool lichiiDetected() const;
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString errorText() const;
  [[nodiscard]] QStringList detectedPaths() const;
  [[nodiscard]] qint64 lastScan() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  void refreshFromInstalls(const QVector<LichiiInstall>& installs);

signals:
  void statusChanged();

private:
  struct Game {
    LichiiGameRecord lichii;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void applyScan(const LichiiScanResult& result);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});

  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  PlaySessionStore* m_playSessions = nullptr;
  QFutureWatcher<LichiiScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_lichiiDetected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
};

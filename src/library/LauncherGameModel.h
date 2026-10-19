#pragma once

#include "sources/LauncherGame.h"
#include "tracking/PlaySessionStore.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QSqlDatabase>

#include <functional>

// Games a source starts with a command it found: Kyty games and installed native ports. The
// last scan is cached, so the library shows them before the next scan finishes.
class LauncherGameModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool detected READ detected NOTIFY statusChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY statusChanged)
  Q_PROPERTY(int count READ count NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
  Q_PROPERTY(QStringList detectedPaths READ detectedPaths NOTIFY statusChanged)
  Q_PROPERTY(qint64 lastScan READ lastScan NOTIFY statusChanged)

public:
  struct Definition {
    // The library source name, such as "Kyty".
    QString source;
    // A short key for the cache table and source state, such as "kyty".
    QString key;
    QString description;
    std::function<LauncherScanResult()> scan;
  };

  LauncherGameModel(Definition definition, const QString& omakadeDatabasePath,
                    PlaySessionStore* playSessions = nullptr, QObject* parent = nullptr);
  ~LauncherGameModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] bool detected() const { return m_detected; }
  [[nodiscard]] bool scanning() const { return m_scanning; }
  [[nodiscard]] int count() const { return static_cast<int>(m_games.size()); }
  [[nodiscard]] QString statusText() const { return m_statusText; }
  [[nodiscard]] QString errorText() const { return m_errorText; }
  [[nodiscard]] QStringList detectedPaths() const { return m_detectedPaths; }
  [[nodiscard]] qint64 lastScan() const { return m_lastScan; }
  [[nodiscard]] const QString& source() const { return m_definition.source; }
  // The game ids of the current library, such as the installed port ids.
  [[nodiscard]] QStringList gameIds() const;

  Q_INVOKABLE void toggleFavorite(int row);
  Q_INVOKABLE void toggleHidden(int row);
  Q_INVOKABLE void refresh();
  void applyScan(const LauncherScanResult& result);

  // The launch target a game carries, in the manual game format GameLauncher validates.
  [[nodiscard]] static QString launchTarget(const LauncherGameRecord& game);

signals:
  void statusChanged();

private:
  struct Game {
    LauncherGameRecord record;
    bool favorite = false;
    bool hidden = false;
    QColor accentStart;
    QColor accentEnd;
  };

  bool openDatabase(const QString& path);
  bool ensureSchema();
  void loadDatabase();
  void loadSourceState();
  void setFlag(int row, const char* column, bool Game::*flag, int role);
  [[nodiscard]] QVariant valueForRole(const Game& game, int role) const;
  void setStatus(const QString& status, const QString& error = {});
  [[nodiscard]] QString table() const { return m_definition.key + QStringLiteral("_games"); }

  Definition m_definition;
  QVector<Game> m_games;
  QSqlDatabase m_database;
  QString m_connectionName;
  PlaySessionStore* m_playSessions = nullptr;
  QFutureWatcher<LauncherScanResult> m_scanWatcher;
  bool m_scanning = false;
  bool m_detected = false;
  QString m_statusText;
  QString m_errorText;
  QStringList m_detectedPaths;
  qint64 m_lastScan = 0;
};

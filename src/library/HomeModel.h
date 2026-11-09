#pragma once
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QSet>
#include <QVector>
class UnifiedGameModel;
// Home's shelves: recently played, the Up next queue, suggestions and shortcuts.
//
// Rows are read from the library on the GUI thread, and only the rows that changed. The
// shelves are then built from that snapshot on a worker thread. A change that cannot move a
// game between shelves (a new cover, a playtime label) is patched into the shelves already on
// screen instead of rebuilding them. Opening Home for the first time, an explicit refresh and
// every queue action still build in place, so callers read the result straight away.
class HomeModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive)
  Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
  Q_PROPERTY(QVariantList queue READ queue NOTIFY changed)
  Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY changed)
  Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY changed)
  Q_PROPERTY(int gameCount READ gameCount NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  // One library row as Home reads it. rank holds just the fields that decide which shelf a
  // game sits on and where, so a change elsewhere is known not to need a rebuild.
  struct CachedGame {
    QVariantMap game;
    QStringList identities;
    QVariantList rank;
  };
  struct Shelves {
    quint64 generation = 0;
    QVariantList recent, queue, suggestions, shortcuts;
    int gameCount = 0;
  };

  HomeModel(UnifiedGameModel* games, const QString& databasePath, QObject* parent = nullptr);
  ~HomeModel() override;
  bool active() const { return m_active; }
  void setActive(bool value);
  QVariantList recent() const { return m_recent; }
  QVariantList queue() const { return m_queue; }
  QVariantList suggestions() const { return m_suggestions; }
  QVariantList shortcuts() const { return m_shortcuts; }
  int gameCount() const { return m_gameCount; }
  QString error() const { return m_error; }
  Q_INVOKABLE bool enqueue(const QString& source, const QString& runner, const QString& appId);
  Q_INVOKABLE bool remove(const QString& key);
  Q_INVOKABLE bool move(const QString& key, int direction);
  Q_INVOKABLE void refresh();

  // How many times the shelves have been built, on either thread. For the tests and the trace.
  int rebuilds() const { return m_rebuilds; }
  // True while a background build is queued or running.
  bool pending() const { return m_refreshPending || m_inFlight; }

  // Builds every shelf from the library rows and the saved queue. Touches no shared state, so
  // it runs on any thread.
  static Shelves build(const QVector<CachedGame>& games, const QVariantList& saved);

signals:
  void changed();

private:
  static QString keyFor(const QVariantMap& game);
  static QHash<QString, QVariantMap> identityMap(const QVector<CachedGame>& games);
  QHash<QString, QVariantMap> gamesByIdentity() const;
  // Rereads the rows marked dirty. Returns true when a row's rank changed or the whole cache
  // was rebuilt; rows that changed only in what they show are added to patched.
  bool updateCache(QSet<int>* patched = nullptr) const;
  QVariantList stored(bool* okay = nullptr) const;
  bool write(const QVariantList& rows);
  void scheduleRefresh();
  void backgroundRefresh();
  void refreshCached();
  void apply(const Shelves& shelves);
  bool patchShelves(const QSet<int>& rows);
  mutable QVector<CachedGame> m_gameCache;
  mutable QSet<int> m_dirtyRows;
  mutable bool m_cacheInvalid = true;
  UnifiedGameModel* m_games;
  QSqlDatabase m_database;
  QString m_connection, m_error;
  QVariantList m_recent, m_queue, m_suggestions, m_shortcuts;
  int m_gameCount = 0;
  bool m_refreshPending = false;
  bool m_active = false;
  bool m_hasShelves = false;
  // A rank changed (or the cache was rebuilt) since the shelves were last built.
  mutable bool m_needsBuild = true;
  bool m_inFlight = false;
  bool m_rerun = false;
  quint64 m_generation = 0;
  int m_rebuilds = 0;
  QFutureWatcher<Shelves> m_watcher;
};

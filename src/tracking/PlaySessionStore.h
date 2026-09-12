#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <QSqlDatabase>

class QTimer;

// Aggregates the sessions recorded by omakade-sessiond and merges them with the
// playtime each source imports from its own emulator. The displayed total is
// max(imported, baseline + tracked). On first observation the baseline excludes
// already recorded time, conservatively treating it as included in the import.
// Existing baselines are preserved. Later gaps in tracking can make the import
// win until observed time catches up; this is not exact overlap reconciliation.
class PlaySessionStore final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
  Q_PROPERTY(bool recorderRunning READ recorderRunning NOTIFY recorderStatusChanged)
  Q_PROPERTY(bool storageAvailable READ storageAvailable CONSTANT)
  Q_PROPERTY(int revision READ revision NOTIFY totalsChanged)

public:
  explicit PlaySessionStore(const QString& databasePath, QObject* parent = nullptr);

  ~PlaySessionStore() override;

  [[nodiscard]] bool enabled() const;
  void setEnabled(bool value);
  bool recorderRunning() const { return m_recorderRunning; }
  bool storageAvailable() const { return m_valid; }
  int revision() const { return m_revision; }
  Q_INVOKABLE void refreshRecorderStatus();
  Q_INVOKABLE QVariantList historyForPaths(const QStringList& gamePaths, int limit = 8) const;
  static bool recorderOwnsDatabase(const QString& databasePath);
  // A negative import means this source has no imported playtime counter.
  static QString provenance(const PlaySessionStore* store, const QString& gamePath,
                            qint64 importedSeconds);

  // Models report the playtime their emulator imports so the first sighting is
  // remembered. Later sightings are ignored by design.
  void captureBaseline(const QString& gamePath, qint64 importedSeconds);

  [[nodiscard]] qint64 displaySeconds(const QString& gamePath, qint64 importedSeconds) const;
  [[nodiscard]] qint64 sessionLastPlayed(const QString& gamePath) const;

  [[nodiscard]] static qint64 merge(qint64 importedSeconds, qint64 baselineSeconds,
                                    qint64 trackedSeconds);

  // Model helpers: they read through the store when present and fall back to the
  // imported values otherwise, so sources built without a store behave exactly
  // as they did before session tracking existed.
  [[nodiscard]] static qint64 displayedSeconds(const PlaySessionStore* store,
                                               const QString& gamePath, qint64 importedSeconds);
  [[nodiscard]] static qint64 displayedLastPlayed(const PlaySessionStore* store,
                                                  const QString& gamePath,
                                                  qint64 importedLastPlayed);

signals:
  void enabledChanged();
  void recorderStatusChanged();
  void totalsChanged();

private:
  void refresh();

  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_databasePath;
  bool m_recorderRunning = false;
  bool m_enabled = true;
  bool m_valid = false;
  int m_revision = 0;
  QHash<QString, qint64> m_trackedSeconds;
  QHash<QString, qint64> m_baselines;
  QHash<QString, qint64> m_lastPlayed;
  QTimer* m_refreshTimer = nullptr;
};

#pragma once

#include "tracking/SessionDatabase.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <QSqlDatabase>

class QTimer;

// Aggregates the sessions recorded by omakade-sessiond and merges them with the
// playtime each source imports from its own emulator. Recorded time can never lower
// a total, and play recorded since the emulator's counter was last observed is added,
// because that counter cannot already include it. The counter is not observed while a
// session is open, since its unflushed time is not yet recorded. An import that
// arrives for the first time is captured as a baseline that conservatively excludes
// the recorded time already present, and existing baselines are preserved because
// historical overlap cannot be inferred reliably.
class PlaySessionStore final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
  Q_PROPERTY(bool recorderRunning READ recorderRunning NOTIFY recorderStatusChanged)
  Q_PROPERTY(bool storageAvailable READ storageAvailable CONSTANT)
  Q_PROPERTY(int revision READ revision NOTIFY totalsChanged)
  // Bumped by every accepted history deletion, so a view bound to the history
  // refreshes even when the deleted row held no seconds and the totals did not move.
  Q_PROPERTY(int historyRevision READ historyRevision NOTIFY historyChanged)
  // Live sessions from the recorder, refreshed continuously so the Now Playing
  // view can show what is running right now and offer to stop it.
  Q_PROPERTY(QVariantList nowPlaying READ nowPlaying NOTIFY nowPlayingChanged)

public:
  explicit PlaySessionStore(const QString& databasePath, QObject* parent = nullptr);

  ~PlaySessionStore() override;

  [[nodiscard]] bool enabled() const;
  void setEnabled(bool value);
  bool recorderRunning() const { return m_recorderRunning; }
  bool storageAvailable() const { return m_valid; }
  int revision() const { return m_revision; }
  int historyRevision() const { return m_historyRevision; }
  Q_INVOKABLE void refreshRecorderStatus();
  Q_INVOKABLE QVariantList historyForPaths(const QStringList& gamePaths, int limit = 8) const;

  // Safe deletion for the per-game history view. A deletion is only carried out
  // for one closed session that still belongs to one of the given paths, so a
  // stale key can never remove another game's session and a session the
  // recorder is still tracking is never touched. Imported playtime and captured
  // baselines stay as they are: forgetting recorded time can only lower the
  // displayed total toward what the emulator itself reports.
  Q_INVOKABLE bool deleteSession(const QString& sessionKey, const QStringList& gamePaths);
  // Removes every closed session of these paths. Returns how many rows went, or
  // -1 when the deletion failed.
  Q_INVOKABLE int deleteHistoryForPaths(const QStringList& gamePaths);

  // Sessions that are running right now, each verified against its recorded
  // process so a closed game never lingers in the list.
  [[nodiscard]] QVariantList nowPlaying() const { return m_nowPlaying; }
  Q_INVOKABLE void refreshNowPlaying();
  // Asks the recorded process to exit, then reports it as stopping until the
  // session closes. forceStopSession is the escalation for a game that ignores
  // the polite request. Both verify the recorded process identity first.
  Q_INVOKABLE bool stopSession(qint64 pid, qint64 procStart);
  Q_INVOKABLE bool forceStopSession(qint64 pid, qint64 procStart);
  static bool recorderOwnsDatabase(const QString& databasePath);
  // A negative import means this source has no imported playtime counter.
  static QString provenance(const PlaySessionStore* store, const QString& gamePath,
                            qint64 importedSeconds);

  // Models report the playtime their emulator imports on every scan. The first
  // sighting is remembered as the baseline; a later sighting that differs moves the
  // recorded-time watermark. Re-reporting an unchanged figure does nothing.
  void observeImportedPlaytime(const QString& gamePath, qint64 importedSeconds);

  [[nodiscard]] qint64 displaySeconds(const QString& gamePath, qint64 importedSeconds) const;
  [[nodiscard]] qint64 sessionLastPlayed(const QString& gamePath) const;

  // Merges an imported count with recorded sessions. A negative import means the
  // source has no counter of its own, so recorded time is the whole total.
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
  void historyChanged();
  void nowPlayingChanged();

private:
  struct StopAttempt {
    qint64 procStart = -1;
    qint64 deadline = 0;
  };

  void refresh();
  [[nodiscard]] bool trackedSessionOpen(qint64 pid, qint64 procStart);

  QSqlDatabase m_database;
  QString m_connectionName;
  QString m_databasePath;
  bool m_recorderRunning = false;
  bool m_enabled = true;
  bool m_valid = false;
  int m_revision = 0;
  int m_historyRevision = 0;
  QHash<QString, qint64> m_trackedSeconds;
  QHash<QString, qint64> m_baselines;
  QHash<QString, SessionDatabase::ImportWatermark> m_watermarks;
  QHash<QString, qint64> m_lastPlayed;
  QVariantList m_nowPlaying;
  QHash<QString, StopAttempt> m_pendingStops;
  QTimer* m_refreshTimer = nullptr;
  QTimer* m_nowPlayingTimer = nullptr;
};

#pragma once

#include "tracking/GameStop.h"
#include "tracking/GameStopper.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

// What one game's stop did, ready for the interface.
struct GameStopOutcome {
  bool okay = false;
  QString message;
  QVariantList lines;
};

// The user-facing half of stopping a game (issue #53, slice 4). It turns a
// library row into the identity the attribution layer wants, says what a stop
// would close before anything is signalled, and applies it off the interface
// thread, because the wine and flatpak levers block while their tool runs.
class GameStopService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList runningGames READ runningGames NOTIFY liveGamesChanged)
  Q_PROPERTY(bool scanning READ scanning NOTIFY liveGamesChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)

public:
  // Every library row as a map. Injected so this stays independent of which
  // model is in front, and so tests can supply rows of their own.
  using RowsProvider = std::function<QVariantList()>;

  explicit GameStopService(QObject* parent = nullptr);
  ~GameStopService() override;

  void setRowsProvider(RowsProvider provider);
  void setProfiles(const ProcessProfileSet& profiles);
  void setGuards(const GameStop::Guards& guards);
  // Replaces the levers. The service does not take ownership.
  void setSignalSink(GameStop::SignalSink* sink);
  // The wait between the graceful signal and the forced one, in milliseconds.
  // Zero makes the stop immediate, which is what the tests use.
  void setGracePeriodMs(int milliseconds);
  // Snapshots and liveness for tests. Both default to procfs.
  void setSnapshotProvider(std::function<QVector<ProcessSnapshot>()> provider);
  void setLiveness(GameStop::Stopper::LivenessFn liveness);

  [[nodiscard]] bool busy() const { return m_busy; }
  [[nodiscard]] QString message() const { return m_message; }
  [[nodiscard]] QVariantList lines() const { return m_lines; }

  // The identity behind a library row. Public because this mapping is the part
  // that can be wrong, and it is asserted per source in the tests.
  [[nodiscard]] static GameStop::GameIdentity identityFor(const QVariantMap& game);

  // What stopping this game would signal, one sentence per target. Empty when
  // nothing is attributable, in which case notesFor says what is missing.
  Q_INVOKABLE QVariantList preview(const QVariantMap& game) const;
  Q_INVOKABLE QStringList notesFor(const QVariantMap& game) const;
  // Games with something to stop right now, each as {title, source, appId,
  // installation, lines}, for the global action and its confirmation.
  Q_INVOKABLE QVariantList liveGames() const;
  [[nodiscard]] QVariantList runningGames() const { return m_runningGames; }
  [[nodiscard]] bool scanning() const { return m_scanning; }
  Q_INVOKABLE void refreshLiveGames(bool afterCurrent = false);
  // Stop only the identities shown in the confirmation. The worker revalidates
  // their process identities before sending any signal.
  Q_INVOKABLE bool stopListedGames(const QVariantList& games);
  // Lines for an identity that has something to stop, or empty.
  [[nodiscard]] QVariantList linesFor(const GameStop::GameIdentity& game) const;

  Q_INVOKABLE bool stop(const QVariantMap& game);
  Q_INVOKABLE bool stopAll();
  // Cancels remaining work and prevents forced escalation.
  Q_INVOKABLE void cancel();

signals:
  void changed();
  void liveGamesChanged();
  // A stop is about to signal these games, each a map of source and appId.
  void stopping(const QVariantList& games);
  // okay is false when nothing was signalled or a lever failed.
  void finished(bool okay, const QString& message, const QVariantList& lines);

private:
  void beginStop(const QVector<GameStop::GameIdentity>& games);
  void publish(const GameStopOutcome& outcome);

  std::shared_ptr<std::atomic_bool> m_discoveryCancel = std::make_shared<std::atomic_bool>(false);
  QVariantList m_runningGames;
  bool m_scanning = false;
  bool m_refreshAgain = false;
  RowsProvider m_rows;
  std::function<QVector<ProcessSnapshot>()> m_snapshot;
  GameStop::Stopper::LivenessFn m_liveness;
  ProcessProfileSet m_profiles;
  GameStop::Guards m_guards;
  GameStop::SignalSink* m_sink = nullptr;
  std::shared_ptr<GameStop::SystemSignalSink> m_ownSink;
  std::shared_ptr<std::atomic_bool> m_cancel;
  int m_gracePeriodMs = GameStop::Stopper::gracePeriodMs();
  bool m_busy = false;
  QString m_message;
  QVariantList m_lines;
};

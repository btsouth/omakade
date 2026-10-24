#include "library/GameStopService.h"

#include "sources/steam/SteamScanner.h"
#include "tracking/ProcFs.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

namespace {

// The flatpak application each source runs inside. These are the same ids the
// launcher itself starts, taken from GameLauncher so a stop closes the app the
// launch opened. Sources whose id is not a constant are absent on purpose: a
// wrong app id would close something else, and an absent one only means the
// flatpak lever is not offered.
QString flatpakAppIdForSource(const QString& source) {
  static const QHash<QString, QString> ids = {
      {QStringLiteral("retroarch"), QStringLiteral("org.libretro.RetroArch")},
      {QStringLiteral("pcsx2"), QStringLiteral("net.pcsx2.PCSX2")},
      {QStringLiteral("dolphin"), QStringLiteral("org.DolphinEmu.dolphin-emu")},
      {QStringLiteral("cemu"), QStringLiteral("info.cemu.Cemu")},
      {QStringLiteral("ryujinx"), QStringLiteral("io.github.ryubing.Ryujinx")},
      {QStringLiteral("shadps4"), QStringLiteral("net.shadps4.shadPS4")},
      {QStringLiteral("steam"), QStringLiteral("com.valvesoftware.Steam")},
      {QStringLiteral("lutris"), QStringLiteral("net.lutris.Lutris")},
      {QStringLiteral("heroic"), QStringLiteral("com.heroicgameslauncher.hgl")},
      {QStringLiteral("faugus"), QStringLiteral("io.github.Faugus.faugus-launcher")},
      {QStringLiteral("battle.net"), QStringLiteral("com.usebottles.bottles")},
  };
  return ids.value(source.trimmed().toLower());
}

bool isEmulatorSource(const QString& source) {
  static const QSet<QString> sources = {
      QStringLiteral("retroarch"), QStringLiteral("pcsx2"),   QStringLiteral("ryujinx"),
      QStringLiteral("shadps4"),   QStringLiteral("cemu"),    QStringLiteral("xenia"),
      QStringLiteral("dolphin"),   QStringLiteral("retro"),   QStringLiteral("romm"),
  };
  return sources.contains(source.trimmed().toLower());
}

// A flatpak application id and a runner name are different things: the runner
// role holds an app id for the emulators that store one, and a wine runner like
// "wine" or "proton" for the launchers. Only an id is accepted as one.
bool looksLikeFlatpakAppId(const QString& value) {
  const QString trimmed = value.trimmed();
  return trimmed.contains(QLatin1Char('.')) && !trimmed.contains(QLatin1Char('/')) &&
         !trimmed.contains(QLatin1Char(' '));
}

QString mapString(const QVariantMap& game, const QString& key) {
  return game.value(key).toString();
}

} // namespace

GameStopService::GameStopService(QObject* parent) : QObject(parent) {
  m_snapshot = [] { return ProcFs::listProcesses(true); };
  m_liveness = [](qint64 pid, qint64 procStart) { return ProcFs::processAlive(pid, procStart); };
  m_ownSink = std::make_shared<GameStop::SystemSignalSink>();
  m_sink = m_ownSink.get();
}

GameStopService::~GameStopService() {
  m_discoveryCancel->store(true);
}

void GameStopService::setRowsProvider(RowsProvider provider) { m_rows = std::move(provider); }

void GameStopService::setProfiles(const ProcessProfileSet& profiles) { m_profiles = profiles; }

void GameStopService::setGuards(const GameStop::Guards& guards) { m_guards = guards; }

void GameStopService::setSignalSink(GameStop::SignalSink* sink) {
  m_sink = sink != nullptr ? sink : m_ownSink.get();
}

void GameStopService::setGracePeriodMs(int milliseconds) {
  m_gracePeriodMs = qMax(0, milliseconds);
}

void GameStopService::setSnapshotProvider(std::function<QVector<ProcessSnapshot>()> provider) {
  if (provider) {
    m_snapshot = std::move(provider);
  }
}

void GameStopService::setLiveness(GameStop::Stopper::LivenessFn liveness) {
  if (liveness) {
    m_liveness = std::move(liveness);
  }
}

GameStop::GameIdentity GameStopService::identityFor(const QVariantMap& game) {
  GameStop::GameIdentity identity;
  const QString source = mapString(game, QStringLiteral("source"));
  identity.title = mapString(game, QStringLiteral("title"));
  identity.source = source;
  identity.appId = mapString(game, QStringLiteral("appId"));
  identity.installPath = mapString(game, QStringLiteral("installPath"));
  identity.runner = mapString(game, QStringLiteral("runner"));
  identity.flatpak = game.value(QStringLiteral("flatpak")).toBool();
  const QString launchTarget = mapString(game, QStringLiteral("launchTarget"));

  if (isEmulatorSource(source)) {
    identity.emulator = source;
    // The content path is what the recorder matches an emulator session on, and
    // the sources store it in InstallPath. A couple of them repeat it in
    // LaunchTarget; both are offered so the match does not depend on which.
    for (const QString& candidate : {identity.installPath, launchTarget}) {
      if (!candidate.isEmpty() && !identity.gamePaths.contains(candidate)) {
        identity.gamePaths.append(candidate);
      }
    }
  } else if (source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0 ||
             source.compare(QStringLiteral("Xodus Nexus"), Qt::CaseInsensitive) == 0) {
    // For Battle.net and Xodus Nexus rows the launch target is the Wine prefix, which is
    // the rung that answers the reported complaint.
    if (!launchTarget.isEmpty()) {
      identity.winePrefixes.append(launchTarget);
    }
  } else if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0) {
    // Derive the Proton prefix from Steam's own layout rather than guessing it.
    const QString prefix = SteamScanner::protonPrefix(identity.installPath, identity.appId);
    if (!prefix.isEmpty()) {
      identity.winePrefixes.append(prefix);
    }
  }

  if (identity.flatpak) {
    identity.flatpakAppId = looksLikeFlatpakAppId(identity.runner)
                                ? identity.runner.trimmed()
                                : flatpakAppIdForSource(source);
  }
  return identity;
}

QVariantList GameStopService::linesFor(const GameStop::GameIdentity& game) const {
  const GameStop::Plan plan = GameStop::plan(game, m_snapshot(), m_profiles, m_guards);
  QVariantList lines;
  for (const QString& line : GameStop::describe(plan)) {
    lines.append(line);
  }
  return lines;
}

QVariantList GameStopService::preview(const QVariantMap& game) const {
  return linesFor(identityFor(game));
}

QStringList GameStopService::notesFor(const QVariantMap& game) const {
  const GameStop::Plan plan = GameStop::plan(identityFor(game), m_snapshot(), m_profiles, m_guards);
  return plan.notes;
}

namespace {
QVariantList findLiveGames(const QVariantList& rows, const QVector<ProcessSnapshot>& processes,
                          const ProcessProfileSet& profiles, const GameStop::Guards& guards,
                          const std::shared_ptr<std::atomic_bool>& cancelled = {}) {
  QVariantList games;
  QSet<QString> seen;
  QSet<QString> shownScopes;
  const auto emulatorMatches = ProcessMatcher::match(processes, profiles);
  for (const QVariant& entry : rows) {
    if (cancelled && cancelled->load()) return {};
    const QVariantMap row = entry.toMap();
    const GameStop::GameIdentity identity = GameStopService::identityFor(row);
    const QString key = identity.source + QLatin1Char('\n') + identity.appId;
    if (seen.contains(key)) {
      continue;
    }
    const GameStop::Plan plan = GameStop::plan(identity, processes, profiles, guards, &emulatorMatches);
    if (plan.isEmpty()) {
      continue;
    }
    bool newTarget = false;
    for (const auto& target : plan.targets) {
      const QString scopeKey = target.kind == GameStop::TargetKind::WinePrefix ? "wine:" + target.prefix
                                : target.kind == GameStop::TargetKind::FlatpakApp ? "flatpak:" + target.appId
                                : QStringLiteral("pid:%1:%2").arg(target.pid).arg(target.procStart);
      newTarget |= !shownScopes.contains(scopeKey);
      shownScopes.insert(scopeKey);
    }
    if (!newTarget) continue;
    seen.insert(key);
    // The whole row is carried through, not just the display fields, so the
    // caller can hand it straight back to stop() and get the same identity.
    QVariantMap game = row;
    QVariantList lines;
    for (const QString& line : GameStop::describe(plan)) {
      lines.append(line);
    }
    game.insert(QStringLiteral("lines"), lines);
    games.append(game);
  }
  return games;
}
} // namespace

QVariantList GameStopService::liveGames() const {
  return m_rows ? findLiveGames(m_rows(), m_snapshot(), m_profiles, m_guards) : QVariantList{};
}

void GameStopService::refreshLiveGames(bool afterCurrent) {
  if (m_scanning) {
    m_refreshAgain = m_refreshAgain || afterCurrent;
    return;
  }
  // Model access stays on its owning thread. Procfs and attribution run on a
  // worker with value copies, never the live model or this service.
  const QVariantList rows = m_rows ? m_rows() : QVariantList{};
  const auto snapshot = m_snapshot;
  const auto profiles = m_profiles;
  const auto guards = m_guards;
  const auto cancelled = m_discoveryCancel;
  m_scanning = true;
  emit liveGamesChanged();
  auto* watcher = new QFutureWatcher<QVariantList>(this);
  connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    m_scanning = false;
    if (m_refreshAgain) {
      m_refreshAgain = false;
      refreshLiveGames();
      return; // Never publish a scan superseded by a post-stop refresh.
    }
    m_runningGames = result;
    emit liveGamesChanged();
  });
  watcher->setFuture(QtConcurrent::run([rows, snapshot, profiles, guards, cancelled] {
    if (cancelled->load()) return QVariantList{};
    return findLiveGames(rows, snapshot(), profiles, guards, cancelled);
  }));
}

bool GameStopService::stopListedGames(const QVariantList& games) {
  if (m_busy || games.isEmpty()) return false;
  QVector<GameStop::GameIdentity> identities;
  for (const auto& game : games) identities.append(identityFor(game.toMap()));
  beginStop(identities);
  return true;
}

bool GameStopService::stop(const QVariantMap& game) {
  if (m_busy) {
    return false;
  }
  const GameStop::GameIdentity identity = identityFor(game);
  if (GameStop::plan(identity, m_snapshot(), m_profiles, m_guards).isEmpty()) {
    return false;
  }
  beginStop(QVector<GameStop::GameIdentity>{identity});
  return true;
}

bool GameStopService::stopAll() {
  if (m_busy) {
    return false;
  }
  QVector<GameStop::GameIdentity> identities;
  for (const QVariant& entry : liveGames()) {
    identities.append(identityFor(entry.toMap()));
  }
  if (identities.isEmpty()) {
    return false;
  }
  beginStop(identities);
  return true;
}

void GameStopService::cancel() {
  if (m_cancel) {
    m_cancel->store(true);
  }
}

void GameStopService::beginStop(const QVector<GameStop::GameIdentity>& games) {
  m_busy = true;
  m_message.clear();
  m_lines.clear();
  m_cancel = std::make_shared<std::atomic_bool>(false);
  emit changed();

  // Everything the worker needs is copied, so the worker never reads a member
  // while the interface thread could be writing one.
  const auto snapshot = m_snapshot;
  const auto liveness = m_liveness;
  const ProcessProfileSet profiles = m_profiles;
  const GameStop::Guards guards = m_guards;
  const auto ownedSink = m_ownSink;
  GameStop::SignalSink* sink = m_sink;
  const int gracePeriodMs = m_gracePeriodMs;
  const std::shared_ptr<std::atomic_bool> cancel = m_cancel;

  auto* watcher = new QFutureWatcher<GameStopOutcome>(this);
  connect(watcher, &QFutureWatcher<GameStopOutcome>::finished, this, [this, watcher] {
    const GameStopOutcome outcome = watcher->result();
    watcher->deleteLater();
    publish(outcome);
  });
  watcher->setFuture(QtConcurrent::run(
      [games, snapshot, liveness, profiles, guards, sink, ownedSink, gracePeriodMs, cancel] {
        GameStopOutcome outcome;
        bool signalledAnything = false;
        bool failed = false;
        QSet<QString> handledTargets;

        for (const GameStop::GameIdentity& game : games) {
          if (cancel->load()) break;
          GameStop::Plan plan = GameStop::plan(game, snapshot(), profiles, guards);
          for (qsizetype i = plan.targets.size(); i-- > 0;) {
            const auto& target = plan.targets.at(i);
            const QString key = target.kind == GameStop::TargetKind::WinePrefix
                                    ? "wine:" + target.prefix
                                    : target.kind == GameStop::TargetKind::FlatpakApp
                                          ? "flatpak:" + target.appId
                                          : QStringLiteral("pid:%1:%2").arg(target.pid).arg(target.procStart);
            if (handledTargets.contains(key)) plan.targets.removeAt(i);
            else handledTargets.insert(key);
          }
          if (plan.isEmpty()) {
            continue;
          }
          GameStop::Stopper stopper(sink, guards, liveness);
          const GameStop::StopReport graceful = stopper.begin(plan);
          signalledAnything = signalledAnything || graceful.signalled() > 0;
          failed = failed || graceful.failed() > 0;

          GameStop::StopReport forced;
          if (stopper.pending() && !cancel->load()) {
            for (int waited = 0; waited < gracePeriodMs && !cancel->load(); waited += 250) {
              QThread::msleep(static_cast<unsigned long>(qMin(250, gracePeriodMs - waited)));
            }
            if (!cancel->load()) {
              forced = stopper.escalate();
              signalledAnything = signalledAnything || forced.signalled() > 0;
              failed = failed || forced.failed() > 0;
            }
          }

          const auto targetRunning = [&liveness](const GameStop::Target& target) {
            if (target.kind == GameStop::TargetKind::TerminateProcess &&
                liveness(target.pid, target.procStart)) return true;
            for (const auto& member : target.witnesses) {
              if (liveness(member.pid, member.procStart)) return true;
            }
            return false;
          };
          // kill(2) reports delivery, not process exit. Proton's last prefix
          // member can still be visible immediately after the forced signal.
          // Let it settle on this worker thread before declaring failure.
          if (forced.signalled() > 0) {
            for (int waited = 0; waited < 2000 && !cancel->load(); waited += 100) {
              bool running = false;
              for (const GameStop::Target& target : plan.targets) running |= targetRunning(target);
              if (!running) break;
              QThread::msleep(100);
            }
          }

          if (!game.title.isEmpty()) {
            outcome.lines.append(game.title);
          }
          for (const QString& line : graceful.lines()) {
            outcome.lines.append(line);
          }
          for (const QString& line : forced.lines()) {
            outcome.lines.append(line);
          }
          // What the levers reported is not the same as the game being gone.
          // Re-read the process table and say plainly what survived.
          for (const GameStop::Target& target : plan.targets) {
            const bool running = targetRunning(target);
            if (running) {
              outcome.lines.append(QStringLiteral("%1 is still running.").arg(target.label));
              failed = true;
            } else {
              outcome.lines.append(QStringLiteral("%1 closed.").arg(target.label));
            }
          }
        }

        if (cancel->load()) {
          outcome.okay = false;
          outcome.message = QStringLiteral("Stop cancelled. Some close requests may already have been sent.");
          return outcome;
        }
        outcome.okay = signalledAnything && !failed;
        if (games.size() > 1) {
          outcome.message = outcome.okay
                                ? QStringLiteral("Stop completed. See the results below.")
                                : QStringLiteral("Some processes could not be stopped.");
        } else {
          outcome.message = outcome.okay ? QStringLiteral("The game was stopped.")
                            : signalledAnything ? QStringLiteral("Some processes could not be stopped.")
                                               : QStringLiteral("Nothing could be stopped.");
        }
        return outcome;
      }));
}

void GameStopService::publish(const GameStopOutcome& outcome) {
  m_busy = false;
  m_message = outcome.message;
  m_lines = outcome.lines;
  emit changed();
  emit finished(outcome.okay, outcome.message, outcome.lines);
}

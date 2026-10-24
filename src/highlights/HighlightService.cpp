#include "highlights/HighlightService.h"

#include "tracking/ProcFs.h"
#include "tracking/SessionDatabase.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>

namespace {
constexpr int kTickMs = 5000;
constexpr int kMaxFailures = 3;
constexpr int kRetryBaseMs = 5000;
// A save the recorder never answered, or one cut short when sessiond ended mid-trim, leaves
// its file here; anything this old is not coming back.
constexpr qint64 kStaleRecordingSecs = 3600;
// A recorder that ran this long before failing had worked: its failure starts a fresh count.
constexpr qint64 kHealthyRunMs = 60000;
// Ending a clip less than this late is not worth rewriting the file for.
constexpr qint64 kMinTrimMs = 1000;
} // namespace

HighlightService::HighlightService(Environment environment, QObject* parent)
    : QObject(parent), m_environment(std::move(environment)) {
  m_settings = HighlightSettings::load(m_environment.configPath);
  m_achievements = std::make_unique<SteamAchievements>(m_environment.steamStatsDirectories);
  connect(m_achievements.get(), &SteamAchievements::unlocked, this, &HighlightService::achievementUnlocked);
  connect(&m_buffer, &ReplayBuffer::stateChanged, this, &HighlightService::bufferStateChanged);
  m_tick.setInterval(kTickMs);
  connect(&m_tick, &QTimer::timeout, this, &HighlightService::reconcile);
  m_tick.start();
  m_retry.setSingleShot(true);
  connect(&m_retry, &QTimer::timeout, this, &HighlightService::reconcile);
  recoverLeftovers();
}

void HighlightService::recoverLeftovers() {
  QDirIterator leftovers(recordingDirectory(), QDir::Files | QDir::Hidden);
  const QDateTime stale = QDateTime::currentDateTime().addSecs(-kStaleRecordingSecs);
  const QString recovered = m_environment.root + QStringLiteral("/Recovered");
  while (leftovers.hasNext()) {
    const QFileInfo file(leftovers.next());
    if (file.lastModified() >= stale) continue;
    // A half-written trim is useless; a whole recording may be a clip the recorder saved after
    // all, so it is kept where it can be found rather than deleted.
    if (file.fileName().startsWith(QLatin1Char('.')) || file.size() <= 0) {
      QFile::remove(file.absoluteFilePath());
      continue;
    }
    if (!QDir().mkpath(recovered)) return;
    QString target = recovered + QLatin1Char('/') + file.fileName();
    for (int copy = 2; QFileInfo::exists(target); ++copy)
      target = recovered + QStringLiteral("/%1 (%2).%3").arg(file.completeBaseName()).arg(copy).arg(file.suffix());
    QFile::rename(file.absoluteFilePath(), target);
  }
}

HighlightService::~HighlightService() {
  m_tick.stop();
  m_retry.stop();
  m_buffer.stop();
  if (m_database.isValid()) {
    m_database.close();
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

HighlightService::Environment HighlightService::defaultEnvironment() {
  Environment environment;
  environment.configPath = SessionDatabase::defaultConfigPath();
  environment.root = HighlightSettings::folder();
  environment.recorder = ReplayBuffer::findRecorder();
  environment.alive = [](qint64 pid, qint64 procStart) { return ProcFs::processAlive(pid, procStart); };
  environment.notify = [](const QString& title, const QString& body, const QString& image) {
    // Omarchy's own sender styles it like the rest of the desktop; notify-send elsewhere.
    const QString omarchy = QStandardPaths::findExecutable(QStringLiteral("omarchy-notification-send"));
    QStringList arguments;
    if (!omarchy.isEmpty()) {
      if (!image.isEmpty()) arguments << QStringLiteral("--image") << image;
      QProcess::startDetached(omarchy, arguments << title << body);
      return;
    }
    const QString notifySend = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
    if (notifySend.isEmpty()) return;
    arguments << QStringLiteral("--app-name=Omakade");
    if (!image.isEmpty()) arguments << QStringLiteral("--icon=") + image;
    QProcess::startDetached(notifySend, arguments << title << body);
  };
  environment.steamStatsDirectories = SteamAchievements::defaultDirectories();
  environment.openDatabase = [](QSqlDatabase& database) {
    return SessionDatabase::open(database, SessionDatabase::defaultDatabasePath(),
                                 QStringLiteral("omakade-highlights-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
  };
  return environment;
}

HighlightService::Game HighlightService::gameFrom(const QVariantMap& session, const QVariantMap& metadata,
                                                  const QString& output) {
  Game game;
  if (session.isEmpty()) return game;
  game.pid = session.value(QStringLiteral("pid")).toLongLong();
  game.procStart = session.value(QStringLiteral("procStart")).toLongLong();
  game.source = session.value(QStringLiteral("source")).toString();
  game.key = session.value(QStringLiteral("path")).toString();
  game.title = metadata.value(QStringLiteral("title")).toString().trimmed();
  game.fallbackTitle = session.value(QStringLiteral("name")).toString().trimmed();
  game.monitor = output;
  if (game.pid <= 0) return {};
  return game;
}

void HighlightService::follow(const Game& game) {
  if (game.pid > 0) {
    if (m_game.pid <= 0 || !game.sameProcess(m_game)) {
      m_game = game;
      m_libraryTitle.clear();
      m_libraryTitleRead = false;
      m_failures = 0;
      m_failureReported = false;
      m_retry.stop();
    } else {
      // The same game: keep what this snapshot knows, and follow it to another output.
      if (!game.title.isEmpty()) m_game.title = game.title;
      if (!game.fallbackTitle.isEmpty()) m_game.fallbackTitle = game.fallbackTitle;
      if (!game.monitor.isEmpty()) m_game.monitor = game.monitor;
    }
  }
  m_inView = game.pid > 0 && game.sameProcess(m_game);
  reconcile();
}

QString HighlightService::clipTitle() {
  // Read from the library only when a clip needs it, never on the guide's way to a snapshot.
  if (m_game.title.isEmpty() && !m_libraryTitleRead && m_game.source != QStringLiteral("Steam")) {
    m_libraryTitleRead = true;
    m_libraryTitle = libraryTitle(m_game.key);
  }
  return displayTitle();
}

QString HighlightService::displayTitle() const {
  // The library's metadata first, then its scan of the game's content, then whatever the
  // process or window called it.
  if (!m_game.title.isEmpty()) return m_game.title;
  if (!m_libraryTitle.isEmpty()) return m_libraryTitle;
  if (!m_game.fallbackTitle.isEmpty()) return m_game.fallbackTitle;
  return QStringLiteral("Game");
}

bool HighlightService::openDatabase() {
  if (m_database.isValid() && m_database.isOpen()) return true;
  m_database = QSqlDatabase();
  if (!m_connectionName.isEmpty()) QSqlDatabase::removeDatabase(m_connectionName);
  m_connectionName.clear();
  if (!m_environment.openDatabase || !m_environment.openDatabase(m_database)) return false;
  m_connectionName = m_database.connectionName();
  return true;
}

QString HighlightService::libraryTitle(const QString& gamePath) {
  if (gamePath.isEmpty() || !openDatabase()) return {};
  const qint64 token = SessionTitleIndex::cacheChangeToken(m_database);
  if (token != m_titlesToken && m_titles.refresh(m_database)) m_titlesToken = token;
  return m_titles.titleForPath(gamePath);
}

void HighlightService::reconcile() {
  const int clipSeconds = m_settings.clipSeconds;
  m_settings = HighlightSettings::load(m_environment.configPath);
  // The guide card names the clip length.
  if (m_settings.clipSeconds != clipSeconds) emit statusChanged();
  if (m_game.pid > 0 && !(m_environment.alive && m_environment.alive(m_game.pid, m_game.procStart))) {
    m_game = {};
    m_failures = 0;
    m_failureReported = false;
    m_retry.stop();
  }
  const bool steamClips = m_settings.enabled && m_settings.achievementClips && m_game.pid > 0 &&
                          m_game.source == QStringLiteral("Steam");
  m_achievements->watch(steamClips ? m_game.key : QString());
  if (!m_settings.enabled || m_game.pid <= 0 || m_game.monitor.isEmpty()) {
    m_pendingAchievements.clear();
    stopBuffer();
    return;
  }
  const auto state = m_buffer.state();
  // A buffer belongs to one game on one output: another game must not inherit its footage.
  const bool current = m_bufferGame.sameProcess(m_game) && m_buffer.options().monitor == m_game.monitor &&
                       m_bufferSeconds == HighlightSettings::bufferSeconds(m_settings);
  if ((state == ReplayBuffer::State::Running || state == ReplayBuffer::State::Starting) && current) return;
  if (state == ReplayBuffer::State::Failed && current && (m_retry.isActive() || m_failures >= kMaxFailures)) return;
  startBuffer();
}

void HighlightService::startBuffer() {
  ReplayBuffer::Options options;
  options.program = m_environment.recorder;
  options.monitor = m_game.monitor;
  options.directory = recordingDirectory();
  options.socketPath = ReplayBuffer::newSocketPath();
  options.bufferSeconds = HighlightSettings::bufferSeconds(m_settings);
  m_bufferSeconds = options.bufferSeconds;
  m_bufferGame = m_game;
  m_buffer.start(options);
}

void HighlightService::stopBuffer() {
  m_retry.stop();
  if (m_buffer.state() == ReplayBuffer::State::Stopped) return;
  // Stopping also clears a failure: it was about the game it happened with.
  m_buffer.stop();
}

void HighlightService::bufferStateChanged(ReplayBuffer::State state) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 ranMs = m_runningSinceMs > 0 ? nowMs - m_runningSinceMs : 0;
  m_runningSinceMs = state == ReplayBuffer::State::Running ? nowMs : 0;
  if (state == ReplayBuffer::State::Failed && m_settings.enabled && m_game.pid > 0) {
    // A long game can lose its recorder now and then, such as when a display sleeps; only
    // failures in a row give up.
    if (ranMs >= kHealthyRunMs) {
      m_failures = 0;
      m_failureReported = false;
    }
    ++m_failures;
    qWarning("omakade-sessiond: highlight recorder failed (%d of %d): %s", m_failures, kMaxFailures,
             qPrintable(m_buffer.lastError()));
    if (m_failures < kMaxFailures) {
      m_retry.start(kRetryBaseMs * m_failures);
    } else if (!m_failureReported) {
      m_failureReported = true;
      if (m_environment.notify)
        m_environment.notify(QStringLiteral("Highlights stopped"), m_buffer.lastError(), {});
    }
  }
  emit statusChanged();
}

void HighlightService::clip(const QString& trigger, const QString& label,
                            std::function<void(const ClipLibrary::Clip&, const QString&)> done, qint64 trimEndMs) {
  const QString reason = unavailableReason();
  if (!reason.isEmpty()) {
    if (m_environment.notify) m_environment.notify(QStringLiteral("Clip not saved"), reason, {});
    if (done) done({}, reason);
    return;
  }
  // What the buffer holds from before the moment to end on: since it started, and no more
  // than its length.
  const qint64 trimMs = qMax<qint64>(0, trimEndMs);
  const qint64 heldMs = qMin<qint64>(m_runningSinceMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_runningSinceMs
                                                         : qint64(m_bufferSeconds) * 1000,
                                     qint64(m_bufferSeconds) * 1000) - trimMs;
  if (trimMs > 0 && heldMs < kMinTrimMs) {
    const QString tooLate = QStringLiteral("Nothing from before the guide opened is left to clip");
    if (m_environment.notify) m_environment.notify(QStringLiteral("Clip not saved"), tooLate, {});
    if (done) done({}, tooLate);
    return;
  }
  // The guide card shows the save in progress and its outcome, since Game Mode silences
  // notifications.
  ++m_saving;
  emit statusChanged();
  done = [this, done](const ClipLibrary::Clip& saved, const QString& error) {
    --m_saving;
    if (saved.path.isEmpty()) {
      ++m_failedCount;
      m_lastError = error;
    } else {
      ++m_savedCount;
    }
    emit statusChanged();
    if (done) done(saved, error);
  };

  ClipLibrary::Clip clip;
  clip.source = m_game.source;
  clip.gameKey = m_game.key;
  clip.title = clipTitle();
  clip.createdAt = m_environment.now();
  clip.seconds = m_settings.clipSeconds;
  clip.trigger = trigger;
  clip.label = label;
  const double trim = trimMs / 1000.0;
  const int requested = qMin(clip.seconds + int(std::ceil(trim)), qMax(1, m_bufferSeconds));
  m_buffer.save(requested, [this, clip, done, trim](const QString& saved, const QString& error) {
    if (saved.isEmpty()) {
      if (m_environment.notify) m_environment.notify(QStringLiteral("Clip not saved"), error, {});
      if (done) done({}, error);
      return;
    }
    // The recorder names replays by the second, so a save right after this one could write
    // over it while it is trimmed. The next save starts only once this returns.
    const QFileInfo info(saved);
    QString path = recordingDirectory() + QStringLiteral("/clip-") + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                   QLatin1Char('.') + (info.suffix().isEmpty() ? QStringLiteral("mp4") : info.suffix());
    if (!QFile::rename(saved, path)) path = saved;
    if (trim > 0) {
      // A clip that cannot be trimmed is still worth keeping, guide frames and all.
      ClipLibrary::trimEnd(path, trim, this, [this, path, clip, done](bool) { measure(path, clip, done); });
      return;
    }
    measure(path, clip, done);
  });
}

void HighlightService::measure(const QString& path, ClipLibrary::Clip clip,
                               const std::function<void(const ClipLibrary::Clip&, const QString&)>& done) {
  // A buffer started moments ago holds less than the clip length.
  ClipLibrary::duration(path, this, [this, path, clip, done](double seconds) mutable {
    if (seconds > 0) clip.seconds = qMax(1, int(std::lround(seconds)));
    store(path, clip, done);
  });
}

void HighlightService::store(const QString& path, const ClipLibrary::Clip& clip,
                             const std::function<void(const ClipLibrary::Clip&, const QString&)>& done) {
  openDatabase();
  QString fileError;
  const ClipLibrary::Clip filed = ClipLibrary::file(m_database, path, m_environment.root, clip, &fileError);
  if (filed.path.isEmpty()) {
    // The recording stays where the recorder wrote it, inside the clip folder.
    if (m_environment.notify) m_environment.notify(QStringLiteral("Clip not saved"), fileError, {});
    if (done) done({}, fileError);
    return;
  }
  ClipLibrary::makeThumbnail(filed.path, this, [this, filed, done, fileError](const QString& image) {
    QString body = filed.title + QStringLiteral(" · %1 s").arg(filed.seconds);
    if (!filed.label.isEmpty()) body = filed.label + QStringLiteral("\n") + body;
    if (filed.id == 0) body += QStringLiteral("\nNot listed in Omakade: ") + fileError;
    if (m_environment.notify) m_environment.notify(QStringLiteral("Clip saved"), body, image);
    if (filed.id != 0) emit clipSaved(filed);
    if (done) done(filed, filed.id == 0 ? fileError : QString());
  });
}

void HighlightService::achievementUnlocked(const QString& name, qint64 time) {
  // An unlock while the game is out of sight, such as one for time played, would clip
  // whatever else is on screen.
  if (!m_inView) return;
  // Unlocks a few seconds apart are one moment, and one clip names them all. It ends a little
  // after the last, and still has to reach back to the first.
  for (PendingAchievement& pending : m_pendingAchievements) {
    if (!pending.game.sameProcess(m_game) || qAbs(time - pending.last) > kAchievementTailSeconds * 2 ||
        qMax(time, pending.last) - pending.first > m_settings.clipSeconds - kAchievementTailSeconds * 2)
      continue;
    pending.names.append(name);
    pending.first = qMin(pending.first, time);
    if (time > pending.last) {
      pending.last = time;
      scheduleAchievementClip(pending);
    }
    return;
  }
  m_pendingAchievements.append({m_nextAchievementId++, time, time, {name}, m_game, 0});
  scheduleAchievementClip(m_pendingAchievements.last());
}

void HighlightService::scheduleAchievementClip(PendingAchievement& pending) {
  // Wait for the moment after the last unlock, then keep a clip ending there. A later unlock
  // joining the moment moves the end, and the earlier timer finds it moved and does nothing.
  const qint64 endMs = (pending.last + kAchievementTailSeconds) * 1000;
  pending.endMs = endMs;
  const qint64 delay = qMax<qint64>(0, endMs - QDateTime::currentMSecsSinceEpoch());
  QTimer::singleShot(delay, this, [this, id = pending.id, endMs] {
    for (qsizetype index = 0; index < m_pendingAchievements.size(); ++index) {
      if (m_pendingAchievements.at(index).id != id) continue;
      if (m_pendingAchievements.at(index).endMs != endMs) return;
      const PendingAchievement pending = m_pendingAchievements.takeAt(index);
      // The game ended, another took its place or it went out of sight: nothing of it to clip.
      if (!pending.game.sameProcess(m_game) || !running() || !m_inView) return;
      QStringList names;
      for (const QString& name : pending.names)
        if (!names.contains(name)) names.append(name);
      // Without Steam's schema every unlock has the same generic name, so count them instead.
      const QString label = names.size() == 1 && pending.names.size() > 1
                                ? QStringLiteral("%1 achievements unlocked").arg(pending.names.size())
                                : names.join(QStringLiteral(", "));
      // A timer is always a little late; only lateness worth cutting is cut.
      const qint64 lateMs = QDateTime::currentMSecsSinceEpoch() - endMs;
      clip(QStringLiteral("achievement"), label, {}, lateMs >= kMinTrimMs ? lateMs : 0);
      return;
    }
  });
}

QString HighlightService::unavailableReason() const {
  const auto settings = HighlightSettings::load(m_environment.configPath);
  if (!settings.enabled) return QStringLiteral("Highlights are off. Turn them on in Omakade's settings.");
  if (m_game.pid <= 0) return QStringLiteral("No game is running");
  if (m_buffer.state() == ReplayBuffer::State::Failed)
    return m_buffer.lastError().isEmpty() ? QStringLiteral("The recorder is not running") : m_buffer.lastError();
  if (m_buffer.state() != ReplayBuffer::State::Running && m_buffer.state() != ReplayBuffer::State::Starting)
    return QStringLiteral("The recorder is not running");
  return {};
}

QJsonObject HighlightService::guideStatus() const {
  if (!m_settings.enabled || m_game.pid <= 0 || m_buffer.state() != ReplayBuffer::State::Running) return {};
  QJsonObject status{{QStringLiteral("seconds"), m_settings.clipSeconds},
                     {QStringLiteral("saving"), m_saving > 0},
                     {QStringLiteral("saved"), m_savedCount},
                     {QStringLiteral("failed"), m_failedCount}};
  if (!m_lastError.isEmpty()) status.insert(QStringLiteral("error"), m_lastError);
  return status;
}

QString HighlightService::recordingDirectory() const { return m_environment.root + QStringLiteral("/.recording"); }

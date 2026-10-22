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
// A save the recorder never answered leaves its file here; anything this old is not coming back.
constexpr qint64 kStaleRecordingSecs = 3600;
} // namespace

HighlightService::HighlightService(Environment environment, QObject* parent)
    : QObject(parent), m_environment(std::move(environment)) {
  m_settings = HighlightSettings::load(m_environment.configPath);
  connect(&m_buffer, &ReplayBuffer::stateChanged, this, &HighlightService::bufferStateChanged);
  m_tick.setInterval(kTickMs);
  connect(&m_tick, &QTimer::timeout, this, &HighlightService::reconcile);
  m_tick.start();
  m_retry.setSingleShot(true);
  connect(&m_retry, &QTimer::timeout, this, &HighlightService::reconcile);
  QDirIterator leftovers(recordingDirectory(), QDir::Files);
  const QDateTime stale = QDateTime::currentDateTime().addSecs(-kStaleRecordingSecs);
  while (leftovers.hasNext()) {
    const QFileInfo file(leftovers.next());
    if (file.lastModified() < stale) QFile::remove(file.absoluteFilePath());
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
      m_libraryTitle = game.source == QStringLiteral("Steam") ? QString() : libraryTitle(game.key);
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
  reconcile();
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
  m_settings = HighlightSettings::load(m_environment.configPath);
  if (m_game.pid > 0 && !(m_environment.alive && m_environment.alive(m_game.pid, m_game.procStart))) {
    m_game = {};
    m_failures = 0;
    m_failureReported = false;
    m_retry.stop();
  }
  if (!m_settings.enabled || m_game.pid <= 0 || m_game.monitor.isEmpty()) {
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
  if (state == ReplayBuffer::State::Failed && m_settings.enabled && m_game.pid > 0) {
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

  ClipLibrary::Clip clip;
  clip.source = m_game.source;
  clip.gameKey = m_game.key;
  clip.title = displayTitle();
  clip.createdAt = m_environment.now();
  clip.seconds = m_settings.clipSeconds;
  clip.trigger = trigger;
  clip.label = label;
  const double trim = qMax<qint64>(0, trimEndMs) / 1000.0;
  const int extra = int(std::ceil(trim));
  m_buffer.save(clip.seconds + extra, [this, clip, done, trim](const QString& path, const QString& error) {
    if (path.isEmpty()) {
      if (m_environment.notify) m_environment.notify(QStringLiteral("Clip not saved"), error, {});
      if (done) done({}, error);
      return;
    }
    if (trim > 0) {
      // A clip that cannot be trimmed is still worth keeping, guide frames and all.
      ClipLibrary::trimEnd(path, trim, this, [this, path, clip, done](bool) { store(path, clip, done); });
      return;
    }
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
  return QJsonObject{{QStringLiteral("seconds"), m_settings.clipSeconds}};
}

QString HighlightService::recordingDirectory() const { return m_environment.root + QStringLiteral("/.recording"); }

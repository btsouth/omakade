#include "ask/AskService.h"

#include "tracking/ProcFs.h"
#include "tracking/SessionDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
// What the agent prints is a few kilobytes; a runaway one is cut off well before this.
constexpr qsizetype kOutputLimit = 4 * 1024 * 1024;
constexpr qsizetype kErrorLimit = 8 * 1024;
// A picture of a game scaled for the agent is well under this.
constexpr qint64 kImageLimit = 16 * 1024 * 1024;

void makePrivate(const QString& path) {
  QDir().mkpath(path);
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

// The agent and anything it started, as one process group: the agent's own pid.
void signalGroup(qint64 group, int signal) {
  if (group > 0) ::kill(pid_t(-group), signal);
}

QString lastLine(const QByteArray& text) {
  const auto lines = QString::fromUtf8(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (auto it = lines.crbegin(); it != lines.crend(); ++it)
    if (!it->trimmed().isEmpty()) return it->simplified();
  return {};
}

QString shortened(const QString& text) {
  return text.size() <= 140 ? text : text.left(139) + QChar(0x2026);
}
} // namespace

AskService::AskService(Environment environment, QObject* parent)
    : QObject(parent), m_environment(std::move(environment)) {
  m_deadline.setSingleShot(true);
  connect(&m_deadline, &QTimer::timeout, this, [this] {
    finish({}, QStringLiteral("%1 didn't answer within %2 seconds.").arg(m_agent.name).arg(m_environment.timeoutMs / 1000));
  });
  // Pictures left by an earlier run are never asked about again.
  makePrivate(m_environment.folder);
  QDir shots(shotFolder());
  if (shots.exists()) shots.removeRecursively();
  makePrivate(shotFolder());
  reload();
}

AskService::~AskService() {
  reset();
  // Agents that answered and were still exiting go with the service.
  for (auto* process : findChildren<QProcess*>()) {
    if (process->state() == QProcess::NotRunning) continue;
    signalGroup(process->processId(), SIGKILL);
    process->waitForFinished(1000);
  }
}

AskService::Environment AskService::defaultEnvironment() {
  Environment environment;
  environment.home = QDir::homePath();
  environment.configPath = SessionDatabase::defaultConfigPath();
  auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty()) runtime = QDir::tempPath();
  environment.folder = runtime + QStringLiteral("/omakade/ask");
  environment.process = QProcessEnvironment::systemEnvironment();
  environment.alive = [](qint64 pid, qint64 procStart) { return ProcFs::processAlive(pid, procStart); };
  return environment;
}

AskService::Game AskService::gameFrom(const QVariantMap& session, const QVariantMap& metadata) {
  Game game;
  if (session.isEmpty()) return game;
  game.pid = session.value(QStringLiteral("pid")).toLongLong();
  game.procStart = session.value(QStringLiteral("procStart")).toLongLong();
  game.title = metadata.value(QStringLiteral("title")).toString().trimmed();
  if (game.title.isEmpty()) game.title = session.value(QStringLiteral("name")).toString().trimmed();
  for (const auto& key : {QStringLiteral("platform"), QStringLiteral("system")}) {
    const auto value = metadata.value(key);
    if (value.typeId() == QMetaType::QString && !value.toString().trimmed().isEmpty()) {
      game.platform = value.toString().trimmed();
      break;
    }
  }
  if (game.pid <= 0) return {};
  return game;
}

QString AskService::shotFolder() const { return m_environment.folder + QStringLiteral("/shots"); }

void AskService::follow(const Game& game) {
  const bool before = available();
  const auto agent = m_agent;
  reload();
  bool changed = before != available() || agent != m_agent;
  if (game.pid > 0 && (m_game.pid <= 0 || !game.sameProcess(m_game))) {
    // An answer is about the game it was asked in.
    reset();
    m_game = game;
    changed = true;
  } else if (game.pid > 0) {
    if (!game.title.isEmpty()) m_game.title = game.title;
    if (!game.platform.isEmpty()) m_game.platform = game.platform;
  } else if (m_game.pid > 0 && m_environment.alive && !m_environment.alive(m_game.pid, m_game.procStart)) {
    // A snapshot without the game may only mean it is out of sight; once it has ended,
    // nothing about it is kept.
    reset();
    m_game = {};
    changed = true;
  }
  if (changed) emit statusChanged();
}

void AskService::reload() {
  m_enabled = AskAgent::enabledIn(m_environment.configPath);
  const auto agent = AskAgent::defaultAgent(m_environment.home);
  // An answer, and a question in flight, belong to the agent and setting they were asked with.
  if (agent != m_agent || !m_enabled) reset();
  m_agent = agent;
}

bool AskService::available() const { return m_enabled && m_agent.supported; }

bool AskService::ask(const QString& mode, const QString& image) {
  const bool lookup = mode == QStringLiteral("lookup");
  const auto discard = [this, &image] {
    const auto path = acceptedImage(image);
    if (!path.isEmpty() && path != m_image) QFile::remove(path);
  };
  const bool wasAvailable = available();
  reload();
  if (wasAvailable != available()) emit statusChanged();
  if ((!lookup && mode != QStringLiteral("next")) || !available() || m_game.pid <= 0 ||
      m_state == QStringLiteral("thinking")) {
    discard();
    return false;
  }
  QString earlier;
  if (lookup) {
    if (!m_agent.canLookUp || m_state != QStringLiteral("answer") || m_mode != AskAgent::Mode::Next ||
        m_image.isEmpty() || !QFileInfo::exists(m_image)) return false;
    earlier = m_text;
  } else {
    const auto picture = acceptedImage(image);
    if (picture.isEmpty()) return false;
    // The new picture replaces the last, and any the guide took but never asked about.
    const auto entries = QDir(shotFolder()).entryInfoList(QDir::Files | QDir::System | QDir::Hidden);
    for (const auto& entry : entries)
      if (entry.canonicalFilePath() != picture) QFile::remove(entry.absoluteFilePath());
    m_image = picture;
  }

  m_mode = lookup ? AskAgent::Mode::Lookup : AskAgent::Mode::Next;
  m_state = QStringLiteral("thinking");
  m_text.clear();
  m_started = m_environment.now();
  m_at = 0;
  m_output.clear();
  m_errors.clear();
  emit statusChanged();

  const auto path = program();
  if (path.isEmpty()) {
    finish({}, QStringLiteral("%1 isn't installed.").arg(m_agent.name));
    return true;
  }
  const auto work = m_environment.folder + QStringLiteral("/work");
  makePrivate(work);
  const auto command = AskAgent::command(m_agent, path, work, {m_game.title, m_game.platform, m_mode, m_image, earlier});
  if (command.program.isEmpty()) {
    finish({}, QStringLiteral("The picture of the game could not be read."));
    return true;
  }

  auto* process = new QProcess(this);
  m_process = process;
  process->setProcessEnvironment(m_environment.process);
  process->setWorkingDirectory(work);
  process->setProgram(command.program);
  process->setArguments(command.arguments);
  process->setChildProcessModifier([] {
    // Its own group, so it goes with everything it started; and never outlives Omakade.
    ::setpgid(0, 0);
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
  });
  connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
    if (process != m_process) { process->readAllStandardOutput(); return; }
    m_output += process->readAllStandardOutput();
    if (m_output.size() > kOutputLimit) { finish({}, QStringLiteral("%1 said too much.").arg(m_agent.name)); return; }
    const auto reply = AskAgent::read(m_agent, m_output);
    if (reply.done) finish(reply, {});
  });
  connect(process, &QProcess::readyReadStandardError, this, [this, process] {
    const auto text = process->readAllStandardError();
    if (process != m_process) return;
    m_errors += text;
    if (m_errors.size() > kErrorLimit) m_errors = m_errors.right(kErrorLimit);
  });
  connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
    if (process == m_process) {
      m_output += process->readAllStandardOutput();
      m_errors += process->readAllStandardError();
      const auto error = lastLine(m_errors);
      QString fallback;
      if (status == QProcess::CrashExit) fallback = QStringLiteral("%1 stopped before it answered.").arg(m_agent.name);
      else if (code != 0 && !error.isEmpty()) fallback = QStringLiteral("%1 couldn't answer: %2").arg(m_agent.name, shortened(error));
      finish(AskAgent::read(m_agent, m_output), fallback);
    }
    process->deleteLater();
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) return;
    if (process == m_process) finish({}, QStringLiteral("%1 couldn't start.").arg(m_agent.name));
    process->deleteLater();
  });
  // An agent with nothing to read gets no input at all, rather than a pipe it waits on.
  if (command.input.isEmpty()) process->setStandardInputFile(QProcess::nullDevice());
  process->start();
  // A program that could not start has already been answered for.
  if (m_process != process) return true;
  if (!command.input.isEmpty()) {
    process->write(command.input);
    process->closeWriteChannel();
  }
  m_deadline.start(m_environment.timeoutMs);
  return true;
}

void AskService::finish(const AskAgent::Reply& reply, const QString& fallbackError) {
  if (m_state != QStringLiteral("thinking")) return;
  m_deadline.stop();
  // An agent that answered may finish exiting on its own; one that failed is stopped.
  stopProcess(reply.answer.isEmpty());
  if (!reply.answer.isEmpty()) {
    m_state = QStringLiteral("answer");
    m_text = reply.answer;
    m_at = m_environment.now();
  } else {
    m_state = QStringLiteral("failed");
    m_text = !reply.error.isEmpty() ? QStringLiteral("%1 couldn't answer: %2").arg(m_agent.name, shortened(reply.error))
             : !fallbackError.isEmpty() ? fallbackError
                                        : QStringLiteral("%1 didn't answer.").arg(m_agent.name);
  }
  emit statusChanged();
}

void AskService::stopProcess(bool now) {
  QProcess* process = m_process;
  m_process = nullptr;
  if (!process || process->state() == QProcess::NotRunning) return;
  const auto group = process->processId();
  const int grace = now ? 0 : m_environment.exitGraceMs;
  signalLater(group, SIGTERM, grace);
  signalLater(group, SIGKILL, grace + 1000);
}

void AskService::signalLater(qint64 group, int signal, int delayMs) {
  if (group <= 0) return;
  if (delayMs <= 0) { signalGroup(group, signal); return; }
  // Only while the group still has members: a group that is gone has nothing to stop.
  QTimer::singleShot(delayMs, this, [group, signal] { if (::kill(pid_t(-group), 0) == 0) signalGroup(group, signal); });
}

void AskService::reset() {
  m_deadline.stop();
  if (m_process) {
    QProcess* process = m_process;
    m_process = nullptr;
    if (process->state() != QProcess::NotRunning) {
      signalGroup(process->processId(), SIGKILL);
      process->waitForFinished(1000);
    }
    process->deleteLater();
  }
  dropImage();
  m_state = QStringLiteral("ready");
  m_mode = AskAgent::Mode::Next;
  m_text.clear();
  m_started = m_at = 0;
}

void AskService::dropImage() {
  const auto entries = QDir(shotFolder()).entryInfoList(QDir::Files | QDir::System | QDir::Hidden);
  for (const auto& entry : entries) QFile::remove(entry.absoluteFilePath());
  m_image.clear();
}

QString AskService::acceptedImage(const QString& path) const {
  if (path.isEmpty()) return {};
  const QFileInfo info(path);
  if (info.isSymLink() || !info.isFile() || info.size() <= 0 || info.size() > kImageLimit) return {};
  if (info.absoluteDir().canonicalPath() != QDir(shotFolder()).canonicalPath()) return {};
  return info.canonicalFilePath();
}

QString AskService::program() const {
  const auto path = m_environment.process.value(QStringLiteral("PATH"));
  const auto folders = path.split(QLatin1Char(':'), Qt::SkipEmptyParts);
  return QStandardPaths::findExecutable(m_agent.id, folders);
}

QJsonObject AskService::guideStatus() const {
  if (!available() || m_game.pid <= 0) return {};
  QJsonObject status{{QStringLiteral("agent"), m_agent.name},
                     {QStringLiteral("state"), m_state},
                     {QStringLiteral("mode"), m_mode == AskAgent::Mode::Lookup ? QStringLiteral("lookup") : QStringLiteral("next")},
                     {QStringLiteral("shots"), shotFolder()},
                     {QStringLiteral("lookup"), m_agent.canLookUp && m_state == QStringLiteral("answer") &&
                                                    m_mode == AskAgent::Mode::Next && !m_image.isEmpty()}};
  if (!m_text.isEmpty()) status.insert(QStringLiteral("text"), m_text);
  if (m_started > 0) status.insert(QStringLiteral("started"), m_started);
  if (m_at > 0) status.insert(QStringLiteral("at"), m_at);
  return status;
}

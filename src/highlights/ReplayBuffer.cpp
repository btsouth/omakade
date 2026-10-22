#include "highlights/ReplayBuffer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>

#include <atomic>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
constexpr qsizetype kOutputLimit = 4096;
constexpr int kStopGraceMs = 3000;

QString socketDirectory() {
  QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty()) runtime = QDir::tempPath();
  return runtime + QStringLiteral("/omakade");
}

QString socketPrefix() { return socketDirectory() + QStringLiteral("/highlights-"); }
} // namespace

ReplayBuffer::ReplayBuffer(QObject* parent) : QObject(parent) {
  m_startPoll.setInterval(100);
  connect(&m_startPoll, &QTimer::timeout, this, &ReplayBuffer::checkStarted);
}

ReplayBuffer::~ReplayBuffer() {
  m_startPoll.stop();
  finishSaves(QStringLiteral("The recorder stopped"));
  if (m_process != nullptr) {
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
      m_process->terminate();
      if (!m_process->waitForFinished(kStopGraceMs)) {
        m_process->kill();
        m_process->waitForFinished(1000);
      }
    }
    delete m_process;
  }
  // Recorders still on their way out after stop() go with this object, not on their own.
  for (QProcess* stopping : findChildren<QProcess*>(Qt::FindDirectChildrenOnly)) {
    stopping->disconnect();
    if (stopping->state() != QProcess::NotRunning && !stopping->waitForFinished(kStopGraceMs)) {
      stopping->kill();
      stopping->waitForFinished(1000);
    }
  }
}

QStringList ReplayBuffer::arguments(const Options& options) {
  return {QStringLiteral("-w"), options.monitor,
          QStringLiteral("-c"), QStringLiteral("mp4"),
          QStringLiteral("-f"), QStringLiteral("60"),
          QStringLiteral("-fm"), QStringLiteral("cfr"),
          QStringLiteral("-k"), QStringLiteral("h264"),
          QStringLiteral("-q"), QStringLiteral("very_high"),
          QStringLiteral("-a"), QStringLiteral("default_output"),
          QStringLiteral("-ac"), QStringLiteral("aac"),
          QStringLiteral("-cursor"), QStringLiteral("no"),
          QStringLiteral("-fallback-cpu-encoding"), QStringLiteral("yes"),
          QStringLiteral("-r"), QString::number(options.bufferSeconds),
          QStringLiteral("-replay-storage"), QStringLiteral("ram"),
          QStringLiteral("-restart-replay-on-save"), QStringLiteral("no"),
          QStringLiteral("-ipc"), options.socketPath,
          QStringLiteral("-v"), QStringLiteral("no"),
          QStringLiteral("-o"), options.directory};
}

QString ReplayBuffer::findRecorder() {
  return QStandardPaths::findExecutable(QStringLiteral("gpu-screen-recorder"));
}

QString ReplayBuffer::newSocketPath() {
  static std::atomic_int counter{0};
  return socketPrefix() + QStringLiteral("%1-%2.sock").arg(QCoreApplication::applicationPid()).arg(++counter);
}

int ReplayBuffer::reapOrphans() {
  const QString prefix = socketPrefix();
  int reaped = 0;
  const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& entry : entries) {
    bool numeric = false;
    const qint64 pid = entry.toLongLong(&numeric);
    if (!numeric || pid == QCoreApplication::applicationPid()) continue;
    if (QFileInfo(QStringLiteral("/proc/") + entry).ownerId() != uint(::getuid())) continue;
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(entry));
    if (!cmdline.open(QIODevice::ReadOnly)) continue;
    const QList<QByteArray> arguments = cmdline.readAll().split('\0');
    if (arguments.isEmpty() || QFileInfo(QString::fromLocal8Bit(arguments.first())).fileName() != QStringLiteral("gpu-screen-recorder"))
      continue;
    const qsizetype ipc = arguments.indexOf("-ipc");
    if (ipc < 0 || ipc + 1 >= arguments.size()) continue;
    const QString socket = QString::fromLocal8Bit(arguments.at(ipc + 1));
    // Only this daemon's own recorders: another user's or another tool's are left alone.
    const QString owner = socket.mid(prefix.size()).section(QLatin1Char('-'), 0, 0);
    if (!socket.startsWith(prefix) || owner == QString::number(QCoreApplication::applicationPid())) continue;
    if (::kill(pid_t(pid), SIGTERM) == 0) ++reaped;
  }
  return reaped;
}

void ReplayBuffer::start(const Options& options) {
  if (m_process != nullptr) stop();
  m_options = options;
  m_output.clear();
  if (options.program.isEmpty()) { setState(State::Failed, QStringLiteral("gpu-screen-recorder is not installed")); return; }
  if (options.monitor.isEmpty()) { setState(State::Failed, QStringLiteral("The game's display is not known")); return; }
  if (!QDir().mkpath(options.directory)) { setState(State::Failed, QStringLiteral("The clip folder could not be created")); return; }
  if (!QDir().mkpath(QFileInfo(options.socketPath).absolutePath())) {
    setState(State::Failed, QStringLiteral("The recorder's socket folder could not be created"));
    return;
  }
  QFile::remove(options.socketPath);

  auto* process = new QProcess(this);
  m_process = process;
  process->setProcessChannelMode(QProcess::MergedChannels);
  process->setChildProcessModifier([] {
    // Exit with the daemon, however it ends, rather than record with nobody to save.
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
  });
  connect(process, &QProcess::readyRead, this, [this, process] {
    m_output += process->readAll();
    if (m_output.size() > kOutputLimit) m_output = m_output.right(kOutputLimit);
  });
  connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
    if (process != m_process) return;
    m_startPoll.stop();
    m_process = nullptr;
    process->deleteLater();
    finishSaves(QStringLiteral("The recorder stopped"));
    const QString tail = outputTail();
    const QString reason = status == QProcess::CrashExit
                               ? QStringLiteral("The recorder crashed")
                               : QStringLiteral("The recorder exited with status %1").arg(code);
    setState(State::Failed, tail.isEmpty() ? reason : reason + QStringLiteral(": ") + tail);
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (process != m_process || error != QProcess::FailedToStart) return;
    m_startPoll.stop();
    m_process = nullptr;
    process->deleteLater();
    setState(State::Failed, QStringLiteral("gpu-screen-recorder could not be started"));
  });
  setState(State::Starting);
  m_startDeadline = QDateTime::currentMSecsSinceEpoch() + startTimeoutMs;
  process->start(options.program, arguments(options));
  m_startPoll.start();
}

void ReplayBuffer::checkStarted() {
  if (m_process == nullptr || m_state != State::Starting) { m_startPoll.stop(); return; }
  if (QFileInfo::exists(m_options.socketPath)) {
    m_startPoll.stop();
    setState(State::Running);
    runNextSave();
    return;
  }
  if (QDateTime::currentMSecsSinceEpoch() < m_startDeadline) return;
  m_startPoll.stop();
  const QString tail = outputTail();
  stop();
  setState(State::Failed, tail.isEmpty() ? QStringLiteral("The recorder did not start")
                                         : QStringLiteral("The recorder did not start: ") + tail);
}

void ReplayBuffer::stop() {
  m_startPoll.stop();
  finishSaves(QStringLiteral("The recorder stopped"));
  if (m_process != nullptr) {
    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
      process->deleteLater();
    } else {
      // Let it exit on its own time; a recorder that ignores the request is killed.
      connect(process, &QProcess::finished, process, &QObject::deleteLater);
      QTimer::singleShot(kStopGraceMs, process, [process] {
        if (process->state() != QProcess::NotRunning) process->kill();
      });
      process->terminate();
    }
  }
  setState(State::Stopped);
}

void ReplayBuffer::save(int seconds, SaveDone done) {
  if (m_state != State::Running && m_state != State::Starting) {
    if (done) done({}, m_error.isEmpty() ? QStringLiteral("The replay buffer is not running") : m_error);
    return;
  }
  m_saves.enqueue({qMax(1, seconds), std::move(done)});
  runNextSave();
}

void ReplayBuffer::runNextSave() {
  if (m_saving || m_saves.isEmpty() || m_state != State::Running) return;
  m_saving = true;
  const PendingSave pending = m_saves.head();
  const int id = m_nextId++;
  auto* socket = new QLocalSocket(this);
  auto buffer = std::make_shared<QByteArray>();
  m_saveSocket = socket;
  const auto complete = [this, socket](const QString& path, const QString& error) {
    // A stop already answered this save and may have started another.
    if (socket != m_saveSocket) return;
    m_saveSocket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    m_saving = false;
    if (m_saves.isEmpty()) return;
    PendingSave done = m_saves.dequeue();
    if (done.done) done.done(path, error);
    runNextSave();
  };
  connect(socket, &QLocalSocket::connected, this, [socket, id, pending] {
    const QJsonObject request{{QStringLiteral("id"), id},
                              {QStringLiteral("name"), QStringLiteral("save-replay")},
                              {QStringLiteral("data"), QJsonObject{{QStringLiteral("seconds"), pending.seconds}}}};
    socket->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
  });
  connect(socket, &QLocalSocket::readyRead, this, [socket, buffer, id, complete] {
    buffer->append(socket->readAll());
    while (buffer->contains('\n')) {
      const QByteArray line = buffer->left(buffer->indexOf('\n'));
      buffer->remove(0, line.size() + 1);
      const QJsonObject reply = QJsonDocument::fromJson(line).object();
      if (reply.value(QStringLiteral("id")).toInt() != id) continue;
      const QString data = reply.value(QStringLiteral("data")).toString();
      if (reply.value(QStringLiteral("result")).toString() == QStringLiteral("ok") && !data.isEmpty())
        complete(data, {});
      else
        complete({}, data.isEmpty() ? QStringLiteral("The recorder could not save the replay") : data);
      return;
    }
    if (buffer->size() > 65536) complete({}, QStringLiteral("The recorder sent an unreadable reply"));
  });
  connect(socket, &QLocalSocket::errorOccurred, this, [complete, socket](QLocalSocket::LocalSocketError) {
    complete({}, QStringLiteral("The recorder could not be reached: ") + socket->errorString());
  });
  connect(socket, &QLocalSocket::disconnected, this, [complete] {
    complete({}, QStringLiteral("The recorder closed the connection before saving"));
  });
  QTimer::singleShot(saveTimeoutMs, socket, [complete] {
    complete({}, QStringLiteral("The recorder took too long to save"));
  });
  socket->connectToServer(m_options.socketPath);
}

void ReplayBuffer::finishSaves(const QString& error) {
  m_saving = false;
  if (QLocalSocket* socket = m_saveSocket) {
    m_saveSocket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
  }
  QQueue<PendingSave> saves;
  saves.swap(m_saves);
  for (const PendingSave& pending : saves)
    if (pending.done) pending.done({}, error);
}

void ReplayBuffer::setState(State state, const QString& error) {
  if (state == State::Failed) m_error = error;
  else if (state == State::Running) m_error.clear();
  if (m_state == state) return;
  m_state = state;
  emit stateChanged(state);
}

QString ReplayBuffer::outputTail() const {
  const QStringList lines = QString::fromLocal8Bit(m_output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  return lines.isEmpty() ? QString() : lines.last().trimmed().left(240);
}

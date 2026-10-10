#include "guide/GuidePause.h"
#include "tracking/ProcFs.h"

#include <QDir>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <cerrno>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
struct Process { qint64 pid = 0, parent = 0, start = -1; char state = 0; };
Process process(qint64 pid) {
  QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
  if (!file.open(QIODevice::ReadOnly) || QFileInfo(file).ownerId() != ::getuid()) return {};
  const auto stat = file.readAll();
  const auto fields = stat.mid(stat.lastIndexOf(')') + 2).simplified().split(' ');
  if (fields.size() <= 19) return {};
  return {pid, fields[1].toLongLong(), fields[19].toLongLong(), fields[0][0]};
}
bool signalFd(int fd, int signal) {
  return ::syscall(SYS_pidfd_send_signal, fd, signal, nullptr, 0) == 0;
}
} // namespace

GuidePause::~GuidePause() { resume(); }

bool GuidePause::stop(qint64 pid, qint64 start, QString* error, const std::function<bool(const QJsonObject&)>& pin) {
  resume();
  if (pid <= 1 || (pid == ::getpid() || pid == ::getppid()) || start <= 0 || !ProcFs::processAlive(pid, start)) {
    if (error) *error = "The game process identity is no longer valid.";
    return false;
  }
  const auto add = [this, error, &pin](qint64 target, qint64 identity) {
    const int fd = ::syscall(SYS_pidfd_open, target, 0);
    const auto current = process(target);
    // Already stopped processes are not ours to resume.
    if (fd < 0 || current.start != identity || current.state == 'T' || current.state == 't' ||
        current.state == 'Z' || (pin && !pin(QJsonObject{{"pid", target}, {"start", identity}})) || !signalFd(fd, SIGSTOP)) {
      if (fd >= 0) ::close(fd);
      if (error) *error = "The game process tree could not be paused safely.";
      return false;
    }
    m_stopped.append({target, identity, fd});
    return true;
  };
  if (!add(pid, start)) return false;
  QSet<qint64> owned{pid};
  // Stop parents first, wait for the kernel's stopped state, then discover children.
  // A stopped parent cannot fork children between the final scan and the overlay opening.
  for (int round = 0; round < 32; ++round) {
    bool settled = true;
    for (const auto& stopped : m_stopped) {
      const auto current = process(stopped.pid);
      if (current.start == stopped.start && current.state != 'T' && current.state != 't') settled = false;
    }
    if (!settled) { ::usleep(2000); continue; }
    bool added = false;
    for (const auto& entry : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      bool numeric = false;
      const auto target = entry.toLongLong(&numeric);
      if (!numeric || owned.contains(target)) continue;
      const auto child = process(target);
      if (child.pid <= 1 || !owned.contains(child.parent)) continue;
      if (!add(child.pid, child.start)) { resume(); return false; }
      owned.insert(child.pid);
      added = true;
    }
    if (!added) return true;
  }
  if (error) *error = "The game process tree did not settle before the pause deadline.";
  resume();
  return false;
}

void GuidePause::resume() {
  for (auto it = m_stopped.crbegin(); it != m_stopped.crend(); ++it) {
    signalFd(it->fd, SIGCONT);
    ::close(it->fd);
  }
  m_stopped.clear();
}

QJsonArray GuidePause::identities() const {
  QJsonArray result;
  for (const auto& e : m_stopped) result.append(QJsonObject{{"pid", e.pid}, {"start", e.start}});
  return result;
}

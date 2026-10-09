#include "guide/GuideActions.h"
#include "tracking/ProcFs.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace GuideActions {
QString mangoConfig(const QString& socket, const QString& level, int limit) {
  QString config = "no_display\ncontrol=" + socket + "\nfps_limit=" + QString::number(qMax(0, limit)) + '\n';
  if (level == "full") config += "full\n";
  else {
    config += "fps\ncpu_stats=0\ngpu_stats=0\n";
    config += level == "frametime" ? "frame_timing=1\n" : "frame_timing=0\n";
  }
  return config;
}
QProcessEnvironment mangoEnvironment(QProcessEnvironment base, bool installed, const QString& config) {
  if (installed && !config.isEmpty()) {
    base.insert("MANGOHUD", "1");
    base.insert("MANGOHUD_CONFIGFILE", config);
    base.insert("MANGOHUD_CONFIG", "read_cfg,no_display");
    base.remove("MANGOHUD_FPS_LIMIT");
  }
  return base;
}
Tree::~Tree() { clear(); }
void Tree::clear() { for (const auto& e : m_entries) ::close(e.fd); m_entries.clear(); }
bool Tree::adopt(const QJsonArray& identities) {
  std::vector<Entry> next;
  const auto refuse = [&next] { for (const auto& e : next) ::close(e.fd); return false; };
  for (const auto& value : identities) {
    const auto e = value.toObject();
    const auto pid = e.value("pid").toInteger(), start = e.value("start").toInteger();
    if (pid <= 1 || pid == ::getpid() || start <= 0) return refuse();
    const int fd = ::syscall(SYS_pidfd_open, pid, 0);
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (fd < 0 || QFileInfo(stat).ownerId() != ::getuid() || !ProcFs::processAlive(pid, start)) {
      if (fd >= 0) ::close(fd);
      return refuse();
    }
    next.push_back({pid, start, fd});
  }
  if (next.empty()) return false;
  // A failed extension must retain the already pinned recovery tree.
  clear(); m_entries = std::move(next);
  return true;
}
bool Tree::pin(qint64 pid, qint64 start) {
  if (!ProcFs::processAlive(pid, start)) return false;
  QJsonArray ids{QJsonObject{{"pid", pid}, {"start", start}}};
  QSet<qint64> selected{pid};
  // Keep pinned descendants even if they were reparented during graceful quit.
  for (const auto& entry : m_entries) {
    if (selected.contains(entry.pid) || !ProcFs::processAlive(entry.pid, entry.start)) continue;
    selected.insert(entry.pid);
    ids.append(QJsonObject{{"pid", entry.pid}, {"start", entry.start}});
  }
  const auto processes = ProcFs::listProcesses();
  for (int pass = 0; pass < 32; ++pass) {
    bool added = false;
    for (const auto& process : processes) {
      QFile stat(QStringLiteral("/proc/%1/stat").arg(process.pid));
      if (!stat.open(QIODevice::ReadOnly)) continue;
      const auto bytes = stat.readAll();
      const auto fields = bytes.mid(bytes.lastIndexOf(')') + 2).simplified().split(' ');
      if (fields.size() < 2 || selected.contains(process.pid) || !selected.contains(fields[1].toLongLong())) continue;
      selected.insert(process.pid); added = true;
      ids.append(QJsonObject{{"pid", process.pid}, {"start", process.procStart}});
    }
    if (!added) break;
  }
  return adopt(ids);
}
QJsonArray Tree::identities() const {
  QJsonArray ids;
  for (const auto& e : m_entries) ids.append(QJsonObject{{"pid", e.pid}, {"start", e.start}});
  return ids;
}
void Tree::signal(int number, qint64 exceptPid) {
  for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) if (it->pid != exceptPid) ::syscall(SYS_pidfd_send_signal, it->fd, number, nullptr, 0);
}
bool Tree::alive() const {
  for (const auto& e : m_entries) if (::syscall(SYS_pidfd_send_signal, e.fd, 0, nullptr, 0) == 0) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(e.pid));
    if (stat.open(QIODevice::ReadOnly)) { const auto bytes = stat.readAll(); if (bytes.mid(bytes.lastIndexOf(')') + 2, 1) != "Z") return true; }
  }
  return false;
}
}

#include "guide/GuideActions.h"
#include "tracking/ProcFs.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>
#include <sys/stat.h>
#include <fcntl.h>
#include <csignal>
#include <sys/syscall.h>
#include <unistd.h>

namespace GuideActions {
// Discover only this game's explicit telemetry, once in the snapshot worker.
// MangoHud's control socket has no statistics reply. Its configured CSV log does.
QVariantMap performanceSource(qint64 pid, qint64 start) {
  if (!ProcFs::processAlive(pid, start)) return {};
  QFile environment(QStringLiteral("/proc/%1/environ").arg(pid));
  QString config;
  if (environment.open(QIODevice::ReadOnly)) for (const auto& entry : environment.read(1024 * 1024).split('\0'))
    if (entry.startsWith("MANGOHUD_CONFIGFILE=")) config = QString::fromLocal8Bit(entry.mid(20));
  QFile settings(config);
  if (!config.isEmpty() && settings.open(QIODevice::ReadOnly)) {
    const auto text = QString::fromUtf8(settings.read(64 * 1024));
    const auto value = [&text](const QString& key) {
      return QRegularExpression("(?m)^\\s*" + key + "\\s*=\\s*([^#\\r\\n]+)").match(text).captured(1).trimmed();
    };
    const auto prefix = value("output_file"), folder = value("output_folder");
    // A per-game output_file is necessary to avoid borrowing another game's log.
    if (!prefix.isEmpty() && !folder.isEmpty()) return {{"kind", "mangohud"}, {"folder", folder}, {"prefix", prefix}};
  }
  // Gamescope documents --stats-path (-T); walk only this process's ancestry.
  for (int depth = 0; pid > 1 && depth < 8; ++depth) {
    QFile command(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!command.open(QIODevice::ReadOnly)) break;
    const auto args = command.read(64 * 1024).split('\0');
    if (!args.isEmpty() && QFileInfo(QString::fromLocal8Bit(args.first())).fileName() == "gamescope") {
      for (int i = 1; i < args.size(); ++i) {
        if ((args[i] == "--stats-path" || args[i] == "-T") && i + 1 < args.size())
          return {{"kind", "gamescope"}, {"path", QString::fromLocal8Bit(args[i + 1])}};
        if (args[i].startsWith("--stats-path=")) return {{"kind", "gamescope"}, {"path", QString::fromLocal8Bit(args[i].mid(13))}};
      }
    }
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) break;
    const auto raw = stat.readAll(), fields = raw.mid(raw.lastIndexOf(')') + 2).simplified();
    const auto parts = fields.split(' '); if (parts.size() < 2) break;
    pid = parts[1].toLongLong();
  }
  return {};
}

QJsonObject performance(const QVariantMap& source) {
  QString path = source.value("path").toString();
  const bool mango = source.value("kind") == "mangohud";
  if (mango) {
    QDir folder(source.value("folder").toString());
    const auto prefix = QFileInfo(source.value("prefix").toString()).fileName();
    // Bound directory lookup; do not search the user's capture or log tree.
    const auto files = folder.entryInfoList({prefix + "*.csv"}, QDir::Files, QDir::Time);
    if (files.isEmpty() || files.size() > 128) return {};
    for (const auto& file : files) if (!file.fileName().endsWith("_summary.csv")) { path = file.filePath(); break; }
  }
  if (path.isEmpty()) return {};
  // Never block on a stats FIFO or consume an existing reader's stream.
  const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return {};
  struct stat info{};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::getuid() ||
      QDateTime::currentSecsSinceEpoch() - info.st_mtime > 5) { ::close(fd); return {}; }
  QFile file; if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); return {}; }
  const auto head = file.read(4096);
  file.seek(qMax<qint64>(0, file.size() - 4096));
  auto tail = file.read(4096); if (!tail.endsWith('\n')) tail = tail.left(tail.lastIndexOf('\n') + 1);
  QJsonObject result;
  const auto reading = [&result](const QString& key, const QByteArray& text) {
    bool ok = false; const auto number = text.trimmed().toDouble(&ok);
    if (ok && std::isfinite(number) && number > 0 && number < 100000) result.insert(key, number);
  };
  if (mango) {
    QList<QByteArray> header;
    for (const auto& line : head.split('\n')) if (line.startsWith("fps,frametime,")) { header = line.trimmed().split(','); break; }
    const auto lines = tail.trimmed().split('\n');
    if (!header.isEmpty() && !lines.isEmpty()) {
      const auto row = lines.last().split(',');
      if (row.size() == header.size()) { reading("fps", row[0]); reading("frametime", row[1]); }
    }
  } else if (source.value("kind") == "gamescope") {
    for (const auto& line : tail.split('\n')) if (line.startsWith("fps=")) reading("fps", line.mid(4));
    // Gamescope's stats stream does not report frame time; leave it absent.
  }
  return result;
}

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

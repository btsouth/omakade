#include "tracking/ProcFs.h"

#include <QDir>
#include <QFile>
#include <csignal>
#include <unistd.h>

namespace {
// Parses the fields after the command name from /proc/<pid>/stat: state first,
// start time the twentieth, exactly like the launcher's own process tracking.
qint64 statStartTime(QFile& stat, char* state) {
  if (!stat.open(QIODevice::ReadOnly)) {
    return -1;
  }
  const QByteArray contents = stat.readAll();
  const qsizetype commEnd = contents.lastIndexOf(')');
  if (commEnd < 0) {
    return -1;
  }
  const QList<QByteArray> fields = contents.mid(commEnd + 2).simplified().split(' ');
  if (fields.size() < 20) {
    return -1;
  }
  *state = fields.at(0).isEmpty() ? '?' : fields.at(0).at(0);
  bool okay = false;
  const qint64 startTime = fields.at(19).toLongLong(&okay);
  return okay ? startTime : -1;
}

// The target of /proc/<pid>/exe. The kernel appends " (deleted)" once the file
// behind a running process is gone, which would defeat a path comparison
// against a live install folder, so it is stripped.
QString executablePath(const QString& base) {
  const QString target = QFile::symLinkTarget(base + QStringLiteral("/exe"));
  const QString deleted = QStringLiteral(" (deleted)");
  return target.endsWith(deleted) ? target.chopped(deleted.size()) : target;
}
} // namespace

namespace ProcFs {

bool isWindowsProgram(const QString& argument) {
  return argument.size() > 3 && argument.at(0).isLetter() && argument.at(1) == QLatin1Char(':') &&
         argument.at(2) == QLatin1Char('\\');
}

QVector<ProcessSnapshot> listProcesses(bool includeScopes) {
  QVector<ProcessSnapshot> processes;
  QDir procDir(QStringLiteral("/proc"));
  const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  processes.reserve(entries.size());
  for (const QString& entry : entries) {
    bool numeric = false;
    const qint64 pid = entry.toLongLong(&numeric);
    if (!numeric || pid <= 0) {
      continue;
    }
    const QString base = QStringLiteral("/proc/%1").arg(pid);
    if (QFileInfo(base).ownerId() != static_cast<uint>(geteuid()))
      continue;
    QFile stat(base + QStringLiteral("/stat"));
    char state = '?';
    const qint64 procStart = statStartTime(stat, &state);
    if (procStart < 0 || state == 'Z' || state == 'X') {
      continue;
    }
    QFile cmdline(base + QStringLiteral("/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QList<QByteArray> rawArguments = cmdline.readAll().split('\0');
    QStringList arguments;
    for (const QByteArray& argument : rawArguments) {
      if (!argument.isEmpty()) {
        arguments.append(QString::fromLocal8Bit(argument));
      }
    }
    if (arguments.isEmpty()) {
      continue;
    }
    ProcessSnapshot snapshot{.pid = pid,
                             .procStart = procStart,
                             .comm = QFileInfo(arguments.first()).fileName(),
                             .arguments = arguments,
                             .exePath = executablePath(base),
                             .winePrefix = {},
                             .flatpakAppId = {},
                             .steamAppId = {}};
    // Wine programs keep their prefix even without scopes: it is what tells one
    // launcher's games apart when they all run under the same loader.
    if (includeScopes || isWindowsProgram(arguments.first())) {
      QFile environment(base + QStringLiteral("/environ"));
      if (environment.open(QIODevice::ReadOnly)) {
        // Retain only these two scope identifiers. Never log or expose the environment.
        for (const QByteArray& field : environment.read(1024 * 1024).split('\0')) {
          if (field.startsWith("WINEPREFIX=")) snapshot.winePrefix = QString::fromLocal8Bit(field.mid(11));
          if (field.startsWith("FLATPAK_ID=")) snapshot.flatpakAppId = QString::fromLocal8Bit(field.mid(11));
          if (field.startsWith("SteamAppId="))
            snapshot.steamAppId = QString::fromLocal8Bit(field.mid(11));
        }
      }
    }
    // Attribute only a consistent process identity across the procfs reads.
    if (!processAlive(pid, procStart)) continue;
    processes.append(snapshot);
  }
  return processes;
}

bool processAlive(qint64 pid, qint64 procStart) {
  if (pid <= 0 || procStart < 0) {
    return false;
  }
  QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
  char state = '?';
  const qint64 current = statStartTime(stat, &state);
  return current == procStart && state != 'Z' && state != 'X';
}

bool processRunning(qint64 pid) {
  if (pid <= 0) {
    return false;
  }
  const QString base = QStringLiteral("/proc/%1").arg(pid);
  if (QFileInfo(base).ownerId() != static_cast<uint>(geteuid())) {
    return false;
  }
  QFile stat(base + QStringLiteral("/stat"));
  char state = '?';
  return statStartTime(stat, &state) >= 0 && state != 'Z' && state != 'X';
}

bool sendSignal(qint64 pid, int signal) {
  if (pid <= 0 || signal <= 0) {
    return false;
  }
  return ::kill(static_cast<pid_t>(pid), signal) == 0;
}

} // namespace ProcFs

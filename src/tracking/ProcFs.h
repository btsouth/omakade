#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// A point-in-time snapshot of one process from procfs, enough to decide whether
// it is an emulator and which game it was started with.
struct ProcessSnapshot {
  qint64 pid = 0;
  // procfs stat field 22, the value GameLauncher also uses to tell a reused pid
  // apart from the process it was tracking.
  qint64 procStart = -1;
  QString comm;
  QStringList arguments;
  // The executable behind /proc/<pid>/exe, resolved. Empty when the link cannot
  // be read. A Wine process reports the wine loader here, not the Windows
  // executable it runs, so this catches native games and launchers.
  QString exePath;
  // Read for every Windows program, and for any process with scopes included.
  QString winePrefix;
  QString flatpakAppId;
  // Steam's exact app identity, read from the process environment when present.
  QString steamAppId;
  // The parent's pid from procfs, so a launcher's process tree can be followed, such as
  // the windows of the game Steam's reaper started. 0 when unknown.
  qint64 parentPid = 0;
};

namespace ProcFs {

// True for a command line Wine reports for a Windows program, such as
// "Z:\games\Game.exe". Wine's loader, wineserver and shell wrappers run under a
// Unix path instead.
[[nodiscard]] bool isWindowsProgram(const QString& argument);

// Lists user-space processes. Kernel threads have an empty cmdline and are skipped.
[[nodiscard]] QVector<ProcessSnapshot> listProcesses(bool includeScopes = false);

[[nodiscard]] bool processAlive(qint64 pid, qint64 procStart);

// The pid and every descendant of it in one snapshot, followed through parentPid. A pid that
// is not in the snapshot still gives itself, so a caller can ask about it either way.
[[nodiscard]] QSet<qint64> processTree(const QVector<ProcessSnapshot>& processes, qint64 pid);

// True when a process with this pid exists, belongs to this user, and is not a
// zombie. This is deliberately weaker than processAlive: it cannot tell a reused
// pid apart from the original process, so it must never authorize a signal, only
// decide whether a session recorded without a process identity is still running.
[[nodiscard]] bool processRunning(qint64 pid);

// Delivers a signal to a process. Returns false when the process is gone or the
// signal could not be sent, so callers never report a stop that did not happen.
[[nodiscard]] bool sendSignal(qint64 pid, int signal);

} // namespace ProcFs

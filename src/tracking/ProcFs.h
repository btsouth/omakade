#pragma once

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
};

namespace ProcFs {

// Lists user-space processes. Kernel threads have an empty cmdline and are skipped.
[[nodiscard]] QVector<ProcessSnapshot> listProcesses();

[[nodiscard]] bool processAlive(qint64 pid, qint64 procStart);

// True when a process with this pid exists, belongs to this user, and is not a
// zombie. This is deliberately weaker than processAlive: it cannot tell a reused
// pid apart from the original process, so it must never authorize a signal, only
// decide whether a session recorded without a process identity is still running.
[[nodiscard]] bool processRunning(qint64 pid);

// Delivers a signal to a process. Returns false when the process is gone or the
// signal could not be sent, so callers never report a stop that did not happen.
[[nodiscard]] bool sendSignal(qint64 pid, int signal);

} // namespace ProcFs

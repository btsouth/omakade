#include "tracking/SessionStopper.h"

#include <csignal>

namespace {

// Never signal init, and never signal a process we cannot identify: procStart is
// the procfs start time captured when the session was recorded, so a negative
// value means the row cannot be trusted to still point at the recorded process.
constexpr qint64 kSmallestStoppablePid = 2;

SessionStopper::Result deliver(qint64 pid, qint64 procStart, int signal,
                               const SessionStopper::AliveFn& alive,
                               const SessionStopper::SignalFn& send) {
  if (pid < kSmallestStoppablePid || procStart < 0) {
    return SessionStopper::Result::Refused;
  }
  if (!alive || !alive(pid, procStart)) {
    return SessionStopper::Result::NotRunning;
  }
  if (!send || !send(pid, signal)) {
    return SessionStopper::Result::NotRunning;
  }
  return SessionStopper::Result::Signalled;
}

} // namespace

SessionStopper::Result SessionStopper::terminate(qint64 pid, qint64 procStart,
                                                 const AliveFn& alive, const SignalFn& send) {
  return deliver(pid, procStart, SIGTERM, alive, send);
}

SessionStopper::Result SessionStopper::forceKill(qint64 pid, qint64 procStart,
                                                 const AliveFn& alive, const SignalFn& send) {
  return deliver(pid, procStart, SIGKILL, alive, send);
}

QString SessionStopper::describe(Result result) {
  switch (result) {
  case Result::Signalled:
    return QStringLiteral("Stop requested");
  case Result::NotRunning:
    return QStringLiteral("That game is no longer running");
  case Result::Refused:
    return QStringLiteral("This session cannot be stopped from here");
  }
  return {};
}

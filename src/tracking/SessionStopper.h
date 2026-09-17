#pragma once

#include <QString>

#include <functional>

// Decides whether a recorded session's process may be stopped, and delivers the
// signal. Both steps verify the process identity first: a session row stores the
// pid together with the procfs start time it was recorded with, and a stop is
// only sent when that exact process is still running. A reused pid can never be
// signalled by mistake, and a session without a verified identity is refused
// rather than guessed at.
namespace SessionStopper {

enum class Result {
  Signalled,  // the signal was delivered
  NotRunning, // the recorded process is already gone
  Refused,    // the session has no trustworthy process identity
};

using AliveFn = std::function<bool(qint64 pid, qint64 procStart)>;
using SignalFn = std::function<bool(qint64 pid, int signal)>;

// Graceful stop: SIGTERM, the request a game can act on before exiting.
[[nodiscard]] Result terminate(qint64 pid, qint64 procStart, const AliveFn& alive,
                               const SignalFn& send);

// Force stop: SIGKILL, used only after a graceful stop was ignored.
[[nodiscard]] Result forceKill(qint64 pid, qint64 procStart, const AliveFn& alive,
                               const SignalFn& send);

[[nodiscard]] QString describe(Result result);

} // namespace SessionStopper

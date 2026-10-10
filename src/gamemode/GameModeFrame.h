#pragma once

#include <QSemaphore>
#include <atomic>

// Shared by the worker and GUI frame callback. A missing callback is advisory,
// never an entry failure: visible placement already has a committed buffer.
class GameModeFrameRequest {
public:
  static constexpr int timeoutMs = 200;
  bool pending() const { return !m_finished.load(); }
  void complete() {
    if (!m_finished.exchange(true)) m_ready.release();
  }
  bool wait() {
    const bool ready = m_ready.tryAcquire(1, timeoutMs);
    m_finished = true;
    return ready;
  }
private:
  QSemaphore m_ready;
  std::atomic<bool> m_finished{false};
};

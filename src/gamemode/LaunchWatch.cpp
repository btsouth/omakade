#include "gamemode/LaunchWatch.h"

#include <QCoreApplication>
#include <QtConcurrent>

LaunchWatch::LaunchWatch(GameModeCompositor* compositor, LaunchHold::ProcessLookup lookup,
                         QObject* parent)
    : QObject(parent), m_compositor(compositor),
      m_lookup(lookup ? std::move(lookup) : LaunchHold::ProcessLookup(&LaunchHold::readProcess)) {
  m_poll.setInterval(500);
  connect(&m_poll, &QTimer::timeout, this, &LaunchWatch::scan);
  // Long enough for a large game's first start, which compiles its shaders before it
  // shows anything.
  m_timeout.setInterval(3 * 60 * 1000);
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    if (!m_active) return;
    stop();
    emit timedOut();
  });
  connect(&m_scanWatcher, &QFutureWatcher<Scan>::finished, this, &LaunchWatch::finishScan);
}

LaunchWatch::~LaunchWatch() { m_scanWatcher.waitForFinished(); }

bool LaunchWatch::begin(const QVariantMap& game) {
  if (m_active) stop();
  m_scanWatcher.waitForFinished();
  QVector<GameModeHeldWindow> windows;
  if (!m_compositor || !m_compositor->available() || !m_compositor->openWindows(&windows, nullptr))
    return false;
  m_ignored.clear();
  for (const auto& window : std::as_const(windows)) m_ignored.append(window.address);
  ++m_generation;
  m_target = LaunchHold::targetFor(game);
  m_active = true;
  m_poll.start();
  m_timeout.start();
  emit changed();
  return true;
}

void LaunchWatch::cancel() {
  if (m_active) stop();
}

void LaunchWatch::stop() {
  m_poll.stop();
  m_timeout.stop();
  m_active = false;
  ++m_generation;
  emit changed();
}

void LaunchWatch::scan() {
  if (!m_active || m_scanWatcher.isRunning()) return;
  m_scanWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, target = m_target, lookup = m_lookup, ignored = m_ignored,
       generation = m_generation, owner = QCoreApplication::applicationPid()] {
        Scan scan;
        scan.generation = generation;
        QVector<GameModeHeldWindow> windows;
        scan.ok = compositor->openWindows(&windows, nullptr);
        for (const auto& window : std::as_const(windows)) {
          if (ignored.contains(window.address) || window.pid == owner) continue;
          if (LaunchHold::isGameWindow(window, target, lookup)) {
            scan.shown = true;
            break;
          }
          // Nexus's own window, such as a sign-in it asks for, is something to look at
          // too. Wine's other windows, of this game's helpers or another prefix, and
          // unrelated apps opening meanwhile are not.
          const auto process = lookup(window.pid);
          if (process && process->winePrefix.isEmpty() &&
              window.windowClass == QStringLiteral("xodus-nexus")) {
            scan.shown = true;
            break;
          }
        }
        return scan;
      }));
}

void LaunchWatch::finishScan() {
  const Scan scan = m_scanWatcher.result();
  if (!m_active || !scan.ok || scan.generation != m_generation || !scan.shown) return;
  stop();
  emit shown();
}

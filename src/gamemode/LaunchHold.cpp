#include "gamemode/LaunchHold.h"

#include "launch/GameLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent>
#include <unistd.h>

namespace {
// Deep enough for Steam's reaper, Proton and Wine, shallow enough to stop on a loop.
constexpr int kMaxAncestors = 32;

bool underPath(QString candidate, const QString& path, bool folder) {
  if (candidate.isEmpty()) return false;
  // Wine and Proton are often given the game as a Windows path on the Z: drive.
  if (candidate.size() > 2 && candidate.at(1) == QLatin1Char(':') &&
      candidate.at(0).toLower() == QLatin1Char('z') && candidate.at(2) == QLatin1Char('\\'))
    candidate = candidate.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/'));
  const QString clean = QDir::cleanPath(candidate);
  return folder ? clean.startsWith(path + QLatin1Char('/')) : clean == path;
}
} // namespace

LaunchHold::LaunchHold(GameModeCompositor* compositor, QString workspace, ProcessLookup lookup,
                       QObject* parent)
    : QObject(parent), m_compositor(compositor), m_workspace(std::move(workspace)),
      m_lookup(lookup ? std::move(lookup) : ProcessLookup(&LaunchHold::readProcess)) {
  m_poll.setInterval(400);
  connect(&m_poll, &QTimer::timeout, this, &LaunchHold::scan);
  // Long enough for a first-run update or a slow launcher on a handheld.
  m_timeout.setInterval(90 * 1000);
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] { release({}); });
  connect(&m_scanWatcher, &QFutureWatcher<Scan>::finished, this, &LaunchHold::finishScan);
}

LaunchHold::~LaunchHold() {
  m_scanWatcher.waitForFinished();
  m_actionWatcher.waitForFinished();
  // Never leave the rule on: new windows on the session workspace would keep vanishing.
  if (m_active && m_compositor) m_compositor->setLaunchHold({});
}

QVariantList LaunchHold::windows() const {
  QVariantList list;
  for (const auto& window : m_windows) {
    list.append(QVariantMap{{"address", window.address},
                            {"windowClass", window.windowClass},
                            {"title", window.title}});
  }
  return list;
}

LaunchHoldTarget LaunchHold::targetFor(const QVariantMap& game) {
  const QString source = game.value(QStringLiteral("source")).toString();
  LaunchHoldTarget target;
  target.title = game.value(QStringLiteral("title")).toString();
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0)
    target.steamAppId = game.value(QStringLiteral("appId")).toString();
  target.installPath = game.value(QStringLiteral("installPath")).toString();
  target.directProcess = source.compare(QStringLiteral("Manual"), Qt::CaseInsensitive) == 0 ||
                         GameLauncher::isEmulatorSourceName(source);
  return target;
}

std::optional<LaunchHoldProcess> LaunchHold::readProcess(qint64 pid) {
  const QString base = QStringLiteral("/proc/%1").arg(pid);
  if (pid <= 0 || QFileInfo(base).ownerId() != static_cast<uint>(geteuid())) return std::nullopt;
  QFile stat(base + QStringLiteral("/stat"));
  if (!stat.open(QIODevice::ReadOnly)) return std::nullopt;
  const QByteArray contents = stat.readAll();
  const qsizetype commEnd = contents.lastIndexOf(')');
  const QList<QByteArray> fields =
      commEnd < 0 ? QList<QByteArray>{} : contents.mid(commEnd + 2).simplified().split(' ');
  if (fields.size() < 2) return std::nullopt;
  LaunchHoldProcess process;
  process.parent = fields.at(1).toLongLong();
  process.exePath = QFile::symLinkTarget(base + QStringLiteral("/exe"));
  QFile cmdline(base + QStringLiteral("/cmdline"));
  if (cmdline.open(QIODevice::ReadOnly)) {
    for (const QByteArray& argument : cmdline.readAll().split('\0'))
      if (!argument.isEmpty()) process.arguments.append(QString::fromLocal8Bit(argument));
  }
  QFile environment(base + QStringLiteral("/environ"));
  if (environment.open(QIODevice::ReadOnly)) {
    // Only Steam's app identity is kept. Never log or expose the environment.
    for (const QByteArray& field : environment.read(1024 * 1024).split('\0'))
      if (field.startsWith("SteamAppId=")) process.steamAppId = QString::fromLocal8Bit(field.mid(11));
  }
  return process;
}

bool LaunchHold::isGameWindow(const GameModeHeldWindow& window, const LaunchHoldTarget& target,
                              const ProcessLookup& lookup) {
  const bool steam = !target.steamAppId.isEmpty() && target.steamAppId != QStringLiteral("0");
  // Proton names a game's windows after its app. A publisher launcher started for the
  // same app shares the name, which only ends the hold on a window that wants an answer.
  if (steam && window.windowClass == QStringLiteral("steam_app_") + target.steamAppId) return true;
  const QFileInfo install(target.installPath);
  const bool hasInstall = !target.installPath.isEmpty() && install.exists();
  const QString installPath = QDir::cleanPath(install.absoluteFilePath());
  const bool folder = install.isDir();
  qint64 pid = window.pid;
  for (int depth = 0; pid > 1 && depth < kMaxAncestors; ++depth) {
    if (target.directProcess && target.pid > 0 && pid == target.pid) return true;
    const auto process = lookup ? lookup(pid) : std::nullopt;
    if (!process) return false;
    if (steam && process->steamAppId == target.steamAppId) return true;
    if (hasInstall) {
      if (underPath(process->exePath, installPath, folder)) return true;
      for (const QString& argument : process->arguments)
        if (underPath(argument, installPath, folder)) return true;
    }
    pid = process->parent;
  }
  return false;
}

bool LaunchHold::begin(const QVariantMap& game) {
  if (m_active) release({});
  m_scanWatcher.waitForFinished();
  m_actionWatcher.waitForFinished();
  if (!m_compositor || !m_compositor->available() || !m_compositor->setLaunchHold(m_workspace))
    return false;
  m_target = targetFor(game);
  m_windows.clear();
  m_presented.clear();
  m_active = true;
  m_poll.start();
  m_timeout.start();
  emit changed();
  return true;
}

void LaunchHold::preview(const QString& title, const QVector<GameModeHeldWindow>& windows) {
  m_target = {};
  m_target.title = title;
  m_windows = windows;
  m_active = true;
  emit changed();
}

void LaunchHold::launched(qint64 pid) {
  if (m_active) m_target.pid = pid;
}

void LaunchHold::cancel() {
  if (m_active) release({});
}

void LaunchHold::scan() {
  if (!m_active || m_scanWatcher.isRunning() || m_actionWatcher.isRunning()) return;
  m_scanWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, target = m_target, lookup = m_lookup] {
        Scan scan;
        scan.ok = compositor->heldWindows(&scan.windows, nullptr);
        for (const auto& window : std::as_const(scan.windows)) {
          if (isGameWindow(window, target, lookup)) {
            scan.game = window.address;
            break;
          }
        }
        return scan;
      }));
}

void LaunchHold::finishScan() {
  if (!m_active) return;
  const Scan scan = m_scanWatcher.result();
  if (!scan.ok) return;
  if (!scan.game.isEmpty()) {
    release(scan.game);
    return;
  }
  setWindows(scan.windows);
}

void LaunchHold::setWindows(const QVector<GameModeHeldWindow>& windows) {
  // Keep the order windows arrived in, so the list does not jump while it is read.
  QVector<GameModeHeldWindow> ordered;
  for (const auto& known : std::as_const(m_windows)) {
    const auto found = std::find_if(windows.cbegin(), windows.cend(),
                                    [&](const auto& window) { return window.address == known.address; });
    if (found != windows.cend()) ordered.append(*found);
  }
  for (const auto& window : windows) {
    if (std::none_of(ordered.cbegin(), ordered.cend(),
                     [&](const auto& known) { return known.address == window.address; }))
      ordered.append(window);
  }
  const bool same = ordered.size() == m_windows.size() &&
                    std::equal(ordered.cbegin(), ordered.cend(), m_windows.cbegin(),
                               [](const auto& a, const auto& b) {
                                 return a.address == b.address && a.title == b.title;
                               });
  if (same) return;
  m_windows = ordered;
  emit changed();
}

void LaunchHold::release(const QString& game) {
  m_poll.stop();
  m_timeout.stop();
  m_active = false;
  m_windows.clear();
  m_presented.clear();
  emit changed();
  m_scanWatcher.waitForFinished();
  m_actionWatcher.waitForFinished();
  if (!m_compositor) {
    emit released();
    return;
  }
  const qint64 owner = QCoreApplication::applicationPid();
  // Stop holding first, then return what was held, so nothing opens out of sight after.
  m_actionWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, workspace = m_workspace, game, owner] {
        compositor->setLaunchHold({});
        QVector<GameModeHeldWindow> held;
        if (compositor->heldWindows(&held, nullptr)) {
          for (const auto& window : std::as_const(held))
            if (window.address != game) compositor->releaseWindow(window.address, workspace);
        }
        if (game.isEmpty()) return;
        const GameModeWindow root = compositor->windowForPid(owner);
        if (root.valid()) compositor->setWindowMode(root.address, 0, 0);
        compositor->releaseWindow(game, workspace);
        compositor->focusWindow(game);
      }));
  if (game.isEmpty()) emit released();
  else emit gameShown();
}

void LaunchHold::present(const QString& address) {
  if (!m_active || !m_compositor || m_actionWatcher.isRunning() ||
      std::none_of(m_windows.cbegin(), m_windows.cend(),
                   [&](const auto& window) { return window.address == address; }))
    return;
  m_presented = address;
  emit changed();
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, workspace = m_workspace, address, owner] {
        const GameModeWindow root = compositor->windowForPid(owner);
        int width = 1440;
        int height = 810;
        for (const auto& output : compositor->outputs()) {
          if (output.name == root.output && output.width > 0 && output.height > 0) {
            width = output.width * 3 / 4;
            height = output.height * 3 / 4;
          }
        }
        // A floating window is covered by a fullscreen library on the same workspace.
        if (root.valid()) compositor->setWindowMode(root.address, 0, 0);
        compositor->presentWindow(address, workspace, width, height);
      }));
}

void LaunchHold::returnToLibrary() {
  if (m_presented.isEmpty() || !m_compositor) return;
  m_presented.clear();
  emit changed();
  m_actionWatcher.waitForFinished();
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run([compositor = m_compositor, owner] {
    const GameModeWindow root = compositor->windowForPid(owner);
    if (!root.valid()) return;
    compositor->setWindowMode(root.address, 2, 2);
    compositor->focusWindow(root.address);
  }));
}

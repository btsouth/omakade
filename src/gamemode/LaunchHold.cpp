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

// Symlinks resolved where the path exists, so a library reached through a link, such as
// Flatpak Steam's, matches the path Omakade found it by.
QString canonical(const QString& path) {
  const QString resolved = QFileInfo(path).canonicalFilePath();
  return resolved.isEmpty() ? QDir::cleanPath(path) : resolved;
}

// The host path behind a Wine path, by the prefix's drive links, or Wine's defaults when
// it has none: Z: is the host's root and C: the prefix's drive_c, where Battle.net and
// other prefix installs live. Empty when it cannot be told.
QString hostPath(const QString& windowsPath, const QString& winePrefix) {
  const QChar drive = windowsPath.at(0).toLower();
  QString root;
  const QFileInfo link(winePrefix + QStringLiteral("/dosdevices/") + drive + QLatin1Char(':'));
  if (!winePrefix.isEmpty() && link.isSymLink()) root = link.symLinkTarget();
  else if (drive == QLatin1Char('z')) root = QStringLiteral("/");
  else if (drive == QLatin1Char('c') && !winePrefix.isEmpty()) root = winePrefix + QStringLiteral("/drive_c");
  if (root.isEmpty()) return {};
  // The drive on its own too, as a path Wine names in another case does not exist here.
  return canonical(canonical(root) + windowsPath.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/')));
}

bool windowsPath(const QString& path) {
  return path.size() > 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':') &&
         path.at(2) == QLatin1Char('\\');
}

// Wine paths are matched without case, as Windows programs name them either way.
bool under(const QString& candidate, const QString& path, bool folder,
           Qt::CaseSensitivity sensitivity = Qt::CaseSensitive) {
  if (candidate.isEmpty()) return false;
  return folder ? candidate.startsWith(path + QLatin1Char('/'), sensitivity)
                : candidate.compare(path, sensitivity) == 0;
}

// Where a process's own program is, against the game's install. Wine shows a process
// its Windows program as the first argument. Wine's and Proton's own helpers under
// C:\windows, and native programs elsewhere such as Steam or a shell, decide nothing.
enum class Place { Unknown, Inside, Outside };
Place placeOf(const LaunchHoldProcess& process, const QString& installPath, bool folder) {
  const QString program = process.arguments.value(0);
  if (windowsPath(program) && program.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
      !program.startsWith(QStringLiteral("C:\\windows\\"), Qt::CaseInsensitive)) {
    if (under(hostPath(program, process.winePrefix), installPath, folder, Qt::CaseInsensitive))
      return Place::Inside;
    // Only a program installed in the prefix itself, as publisher launchers are, is
    // surely another program. Proton shows the game's own folder through other drives,
    // whose paths may not match the one Omakade knows.
    if (program.at(0).toLower() == QLatin1Char('c') && !process.winePrefix.isEmpty())
      return Place::Outside;
    return Place::Unknown;
  }
  if (under(process.exePath, installPath, folder)) return Place::Inside;
  for (const QString& argument : process.arguments) {
    if (windowsPath(argument)) {
      if (under(hostPath(argument, process.winePrefix), installPath, folder, Qt::CaseInsensitive))
        return Place::Inside;
    } else if (argument.startsWith(QLatin1Char('/')) && under(canonical(argument), installPath, folder)) {
      return Place::Inside;
    }
  }
  return Place::Unknown;
}
} // namespace

LaunchHold::LaunchHold(GameModeCompositor* compositor, QString workspace, ProcessLookup lookup,
                       QObject* parent)
    : QObject(parent), m_compositor(compositor), m_workspace(std::move(workspace)),
      m_lookup(lookup ? std::move(lookup) : ProcessLookup(&LaunchHold::readProcess)) {
  m_poll.setInterval(400);
  connect(&m_poll, &QTimer::timeout, this, &LaunchHold::scan);
  // Counted from the last window a launcher opened, so a slow first-run update on a
  // handheld keeps the hold while it is still working.
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
    // Only Steam's app identity and the Wine prefix are kept. Never log or expose the
    // environment.
    for (const QByteArray& field : environment.read(1024 * 1024).split('\0')) {
      if (field.startsWith("SteamAppId=")) process.steamAppId = QString::fromLocal8Bit(field.mid(11));
      if (field.startsWith("WINEPREFIX=")) process.winePrefix = QString::fromLocal8Bit(field.mid(11));
    }
  }
  return process;
}

bool LaunchHold::isGameWindow(const GameModeHeldWindow& window, const LaunchHoldTarget& target,
                              const ProcessLookup& lookup) {
  const bool steam = !target.steamAppId.isEmpty() && target.steamAppId != QStringLiteral("0");
  const QFileInfo install(target.installPath);
  const bool hasInstall = !target.installPath.isEmpty() && install.exists();
  const QString installPath = canonical(install.absoluteFilePath());
  const bool folder = install.isDir();
  bool steamProcess = false;
  qint64 pid = window.pid;
  for (int depth = 0; pid > 1 && depth < kMaxAncestors; ++depth) {
    if (target.directProcess && target.pid > 0 && pid == target.pid) return true;
    const auto process = lookup ? lookup(pid) : std::nullopt;
    if (!process) break;
    // The nearest program that can be placed decides. A publisher's launcher, such as
    // Rockstar's or EA's, is a program of its own outside the game's folder even when
    // the game started it, so it stays held while it asks for a sign-in.
    if (hasInstall) {
      const Place place = placeOf(*process, installPath, folder);
      if (place == Place::Inside) return true;
      if (place == Place::Outside) return false;
    }
    if (steam && process->steamAppId == target.steamAppId) steamProcess = true;
    pid = process->parent;
  }
  // Nothing placed it: Steam's own word for the app is all there is. Proton names a
  // game's windows after its app, and Steam marks the processes it starts for it.
  return steam && (steamProcess || window.windowClass == QStringLiteral("steam_app_") + target.steamAppId);
}

bool LaunchHold::begin(const QVariantMap& game) {
  if (m_active) release({});
  m_scanWatcher.waitForFinished();
  m_actionWatcher.waitForFinished();
  if (!m_compositor || !m_compositor->available() || !m_compositor->setLaunchHold(m_workspace))
    return false;
  // This hold covers every window, Steam's included, and ends when the game opens.
  m_compositor->setSteamHold({});
  // What earlier launches left held, if it is still held; a closed window's address
  // can come back on a new one.
  m_ignored.clear();
  QVector<GameModeHeldWindow> held;
  if (!m_kept.isEmpty() && m_compositor->heldWindows(&held, nullptr))
    for (const auto& window : std::as_const(held))
      if (m_kept.contains(window.address)) m_ignored.append(window.address);
  m_kept = m_ignored;
  ++m_generation;
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

void LaunchHold::abandon() {
  // Game Mode puts every held window back as it leaves or parks.
  m_kept.clear();
  if (m_active) stop();
}

void LaunchHold::stop() {
  m_poll.stop();
  m_timeout.stop();
  m_active = false;
  m_kept.clear();
  m_windows.clear();
  m_presented.clear();
  ++m_generation;
  emit changed();
  emit released();
}

void LaunchHold::scan() {
  if (!m_active || m_scanWatcher.isRunning() || m_actionWatcher.isRunning()) return;
  m_scanWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, target = m_target, lookup = m_lookup, ignored = m_ignored,
       generation = m_generation] {
        Scan scan;
        scan.generation = generation;
        scan.ok = compositor->heldWindows(&scan.windows, nullptr);
        scan.windows.removeIf([&](const auto& window) { return ignored.contains(window.address); });
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
  if (!scan.ok || scan.generation != m_generation) return;
  if (!scan.game.isEmpty()) {
    // Record what opened alongside the game, so it is kept out of the next launch.
    setWindows(scan.windows);
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
  // A new window means the launcher is still working: give it the full time again.
  if (std::any_of(ordered.cbegin(), ordered.cend(), [&](const auto& window) {
        return std::none_of(m_windows.cbegin(), m_windows.cend(),
                            [&](const auto& known) { return known.address == window.address; });
      }))
    m_timeout.start();
  m_windows = ordered;
  emit changed();
}

void LaunchHold::release(const QString& game) {
  m_poll.stop();
  m_timeout.stop();
  m_active = false;
  if (!game.isEmpty()) {
    for (const auto& window : std::as_const(m_windows))
      if (window.address != game && !m_kept.contains(window.address)) m_kept.append(window.address);
  }
  m_windows.clear();
  m_presented.clear();
  ++m_generation;
  emit changed();
  m_scanWatcher.waitForFinished();
  m_actionWatcher.waitForFinished();
  if (!m_compositor) {
    emit released();
    return;
  }
  const qint64 owner = QCoreApplication::applicationPid();
  // Stop holding first, so nothing opens out of sight after.
  m_actionWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, workspace = m_workspace, game, owner, ignored = m_ignored] {
        compositor->setLaunchHold({});
        if (game.isEmpty()) {
          // No game: this launch's windows come back, as if there had been no hold.
          QVector<GameModeHeldWindow> held;
          if (compositor->heldWindows(&held, nullptr))
            for (const auto& window : std::as_const(held))
              if (!ignored.contains(window.address))
                compositor->releaseWindow(window.address, workspace);
          return;
        }
        // The launcher's windows stay out of sight until Game Mode ends. The game takes
        // over fullscreen from the library, as any window opening in Game Mode does.
        const GameModeWindow root = compositor->windowForPid(owner);
        if (root.valid()) compositor->setWindowMode(root.address, 0, 0);
        compositor->releaseWindow(game, workspace);
        compositor->setWindowMode(game, 2, 2);
        compositor->focusWindow(game);
      }));
  if (game.isEmpty()) emit released();
  else emit gameShown();
}

void LaunchHold::present(const QString& address) {
  if (!m_active || m_actionWatcher.isRunning() ||
      std::none_of(m_windows.cbegin(), m_windows.cend(),
                   [&](const auto& window) { return window.address == address; }))
    return;
  // One window at a time: one already shown goes back with the others first.
  const QString previous = m_presented == address ? QString{} : m_presented;
  m_presented = address;
  emit changed();
  // A preview only records the choice.
  if (!m_compositor) return;
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, workspace = m_workspace, address, owner, previous] {
        if (!previous.isEmpty()) compositor->holdWindow(previous);
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
  if (m_presented.isEmpty()) return;
  const QString presented = m_presented;
  m_presented.clear();
  emit changed();
  if (!m_compositor) return;
  m_actionWatcher.waitForFinished();
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run([compositor = m_compositor, owner, presented] {
    // A sign-in left half done can be opened again from the list. A closed window is gone.
    compositor->holdWindow(presented);
    const GameModeWindow root = compositor->windowForPid(owner);
    if (!root.valid()) return;
    compositor->setWindowMode(root.address, 2, 2);
    compositor->focusWindow(root.address);
  }));
}

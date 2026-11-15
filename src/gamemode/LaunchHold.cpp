#include "gamemode/LaunchHold.h"

#include "launch/GameLauncher.h"
#include "launch/SteamLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
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

// Setup a game ships in its own folder, such as Steam's redistributables or EA's
// installer, runs before the game and is not it.
bool setupFolder(const QString& name) {
  for (const char* folder : {"_CommonRedist", "CommonRedist", "__Installer", "Redist", "_Redist"})
    if (name.compare(QLatin1String(folder), Qt::CaseInsensitive) == 0) return true;
  return false;
}

enum class Place { Unknown, Inside, Outside };
Place placeIn(const QString& candidate, const QString& installPath, bool folder,
              Qt::CaseSensitivity sensitivity = Qt::CaseSensitive) {
  if (!under(candidate, installPath, folder, sensitivity)) return Place::Unknown;
  if (folder && setupFolder(candidate.mid(installPath.size() + 1).section(QLatin1Char('/'), 0, 0)))
    return Place::Outside;
  return Place::Inside;
}

// Steam runs a game's first-time setup through its install script evaluator, under the
// game's own app id: reaper SteamLaunch AppId=N Install=1 -- ... iscriptevaluator.exe.
bool steamSetup(const LaunchHoldProcess& process) {
  const QStringList& arguments = process.arguments;
  if (arguments.value(1) == QStringLiteral("SteamLaunch")) {
    for (qsizetype index = 2; index < arguments.size() && arguments.at(index) != QStringLiteral("--"); ++index)
      if (arguments.at(index) == QStringLiteral("Install=1")) return true;
  }
  return std::any_of(arguments.cbegin(), arguments.cend(), [](const QString& argument) {
    return argument.endsWith(QStringLiteral("iscriptevaluator.exe"), Qt::CaseInsensitive);
  });
}

// Wine's and Proton's own programs, such as explorer.exe or Proton's steam.exe.
bool wineSystemProgram(const LaunchHoldProcess& process) {
  return process.arguments.value(0).startsWith(QStringLiteral("C:\\windows\\"), Qt::CaseInsensitive);
}

// Where a process's own program is, against the game's install. Wine shows a process
// its Windows program as the first argument. Wine's and Proton's own helpers under
// C:\windows, and native programs elsewhere such as Steam or a shell, decide nothing.
Place placeOf(const LaunchHoldProcess& process, const QString& installPath, bool folder) {
  const QString program = process.arguments.value(0);
  if (windowsPath(program) && program.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
      !wineSystemProgram(process)) {
    const Place place = placeIn(hostPath(program, process.winePrefix), installPath, folder, Qt::CaseInsensitive);
    if (place != Place::Unknown) return place;
    // Only a program installed in the prefix itself, as publisher launchers are, is
    // surely another program. Proton shows the game's own folder through other drives,
    // whose paths may not match the one Omakade knows.
    if (program.at(0).toLower() == QLatin1Char('c') && !process.winePrefix.isEmpty())
      return Place::Outside;
    return Place::Unknown;
  }
  if (const Place place = placeIn(process.exePath, installPath, folder); place != Place::Unknown)
    return place;
  for (const QString& argument : process.arguments) {
    Place place = Place::Unknown;
    if (windowsPath(argument))
      place = placeIn(hostPath(argument, process.winePrefix), installPath, folder, Qt::CaseInsensitive);
    else if (argument.startsWith(QLatin1Char('/')))
      place = placeIn(canonical(argument), installPath, folder);
    if (place != Place::Unknown) return place;
  }
  return Place::Unknown;
}

// Where Steam keeps its logs and settings: native, then Flatpak.
QStringList steamRoots() {
  const QString home = QDir::homePath();
  return {home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam")};
}

// The end of a file, where Steam's latest launches are.
QByteArray tail(const QString& path, qint64 size) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  if (file.size() > size) file.seek(file.size() - size);
  return file.read(size);
}

// Pointing at the library focuses it again. A busy compositor can miss one request, and
// a library left without it would keep its window in front on the desktop, so a second
// try follows a moment later.
void followMouseAgain(GameModeCompositor* compositor, qint64 owner) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt > 0) QThread::msleep(100);
    const GameModeWindow root = compositor->windowForPid(owner);
    if (root.valid() && compositor->setFollowMouse(root.address, true)) return;
  }
}
} // namespace

LaunchHold::LaunchHold(GameModeCompositor* compositor, QString workspace, ProcessLookup lookup,
                       QObject* parent)
    : QObject(parent), m_compositor(compositor), m_workspace(std::move(workspace)),
      m_lookup(lookup ? std::move(lookup) : ProcessLookup(&LaunchHold::readProcess)),
      m_steamLaunchCheck(&LaunchHold::steamLaunchRunning),
      m_steamPromptCheck(&LaunchHold::readSteamPrompt),
      m_steamRelaunch([](const QString& appId) { SteamLauncher::openUrl(SteamLauncher::launchUrl(appId)); }) {
  m_poll.setInterval(400);
  connect(&m_poll, &QTimer::timeout, this, &LaunchHold::scan);
  // Gives up only when nothing is listed or shown, counted from the last window that
  // opened or closed. Listed windows wait for the game or for B, however long a first-run
  // update or Steam's shader processing takes on a handheld.
  m_timeout.setInterval(90 * 1000);
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    if (m_windows.isEmpty() && m_presented.isEmpty()) release({}, Ending::Cancelled);
  });
  // Long enough for a game that restarts itself through Steam to come back.
  m_closedGrace.setInterval(10 * 1000);
  m_closedGrace.setSingleShot(true);
  connect(&m_closedGrace, &QTimer::timeout, this, [this] {
    // Someone answering a shown window is still working on this launch.
    if (m_active && m_presented.isEmpty()) release({}, Ending::GameClosed);
  });
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
  target.directProcess = GameLauncher::isDirectSourceName(source) ||
                         GameLauncher::isEmulatorSourceName(source);
  // Xodus Nexus starts a game from a copy of its folder, so only the prefix places it.
  const QString prefix = game.value(QStringLiteral("launchTarget")).toString();
  if (source == QStringLiteral("Xodus Nexus") && !prefix.isEmpty())
    target.winePrefix = canonical(prefix);
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

bool LaunchHold::isSteamLaunch(const QStringList& arguments, const QString& appId) {
  if (appId.isEmpty() || arguments.size() < 3 || arguments.at(1) != QStringLiteral("SteamLaunch") ||
      QFileInfo(arguments.at(0)).fileName() != QStringLiteral("reaper"))
    return false;
  bool found = false;
  for (qsizetype index = 2; index < arguments.size() && arguments.at(index) != QStringLiteral("--"); ++index) {
    if (arguments.at(index) == QStringLiteral("Install=1")) return false;
    if (arguments.at(index) == QStringLiteral("AppId=") + appId) found = true;
  }
  return found;
}

bool LaunchHold::steamLaunchRunning(const QString& appId) {
  const QStringList processes = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& name : processes) {
    if (!name.front().isDigit()) continue;
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(name));
    if (!cmdline.open(QIODevice::ReadOnly)) continue;
    // reaper's own options come first, so the start of the line is enough.
    QStringList arguments;
    for (const QByteArray& argument : cmdline.read(4096).split('\0'))
      if (!argument.isEmpty()) arguments.append(QString::fromLocal8Bit(argument));
    if (isSteamLaunch(arguments, appId)) return true;
  }
  return false;
}

QString LaunchHold::steamPromptIn(const QByteArray& log, const QString& appId,
                                  const QDateTime& since) {
  if (appId.isEmpty()) return {};
  // [2026-11-14 20:38:03] GameAction [AppID 632360, ActionID 1] : LaunchApp waiting for user
  // response to ShowEula ""
  static const QRegularExpression line(
      QStringLiteral(R"(^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\] GameAction \[AppID (\d+), ActionID (\d+)\] : (.*)$)"));
  static const QRegularExpression waiting(QStringLiteral(R"(waiting for user response to (\w+))"));
  QString action;
  QDateTime began;
  QString task;
  for (const QByteArray& raw : log.split('\n')) {
    if (!raw.contains("GameAction")) continue;
    const QRegularExpressionMatch match = line.match(QString::fromUtf8(raw).trimmed());
    if (!match.hasMatch() || match.captured(2) != appId) continue;
    // Each launch is a new action; a launch left waiting never says it was given up.
    if (match.captured(3) != action) {
      action = match.captured(3);
      began = QDateTime::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd hh:mm:ss"));
      task.clear();
    }
    const QString event = match.captured(4);
    if (const QRegularExpressionMatch wait = waiting.match(event); wait.hasMatch())
      task = wait.captured(1);
    else
      task.clear();
  }
  if (task.isEmpty() || !began.isValid() || began < since) return {};
  if (task == QStringLiteral("ShowEula")) return QStringLiteral("eula");
  if (task == QStringLiteral("ProcessingShaderCache")) return QStringLiteral("shaders");
  // Steam answers these itself as it goes.
  if (task == QStringLiteral("CreatingProcess") || task == QStringLiteral("ShowInterstitials")) return {};
  return QStringLiteral("answer");
}

QString LaunchHold::eulaRecordIn(const QByteArray& localConfig, const QString& appId) {
  if (appId.isEmpty()) return {};
  const QByteArray key = '"' + appId.toUtf8() + "_eula_";
  QStringList lines;
  for (const QByteArray& raw : localConfig.split('\n'))
    if (raw.contains(key)) lines.append(QString::fromUtf8(raw.simplified()));
  return lines.join(QLatin1Char('\n'));
}

SteamPromptState LaunchHold::readSteamPrompt(const QString& appId, const QDateTime& since) {
  SteamPromptState state;
  QString root;
  QDateTime newest;
  for (const QString& candidate : steamRoots()) {
    const QFileInfo log(candidate + QStringLiteral("/logs/console_log.txt"));
    if (log.exists() && (root.isEmpty() || log.lastModified() > newest)) {
      root = candidate;
      newest = log.lastModified();
    }
  }
  if (root.isEmpty()) return state;
  state.prompt = steamPromptIn(tail(root + QStringLiteral("/logs/console_log.txt"), 256 * 1024), appId, since);
  if (state.prompt != QStringLiteral("eula")) return state;
  QStringList records;
  const QDir users(root + QStringLiteral("/userdata"));
  for (const QString& user : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    QFile config(users.filePath(user + QStringLiteral("/config/localconfig.vdf")));
    if (config.open(QIODevice::ReadOnly)) records.append(eulaRecordIn(config.readAll(), appId));
  }
  state.eulaRecord = records.join(QLatin1Char('\n'));
  return state;
}

bool LaunchHold::isGameWindow(const GameModeHeldWindow& window, const LaunchHoldTarget& target,
                              const ProcessLookup& lookup) {
  const bool steam = !target.steamAppId.isEmpty() && target.steamAppId != QStringLiteral("0");
  const QFileInfo install(target.installPath);
  const bool hasInstall = !target.installPath.isEmpty() && install.exists();
  const QString installPath = canonical(install.absoluteFilePath());
  const bool folder = install.isDir();
  // Setup Steam runs for the game, such as its redistributables on a first launch, is
  // held wherever its program is.
  if (steam && lookup) {
    qint64 pid = window.pid;
    for (int depth = 0; pid > 1 && depth < kMaxAncestors; ++depth) {
      const auto process = lookup(pid);
      if (!process) break;
      if (steamSetup(*process)) return false;
      pid = process->parent;
    }
  }
  bool steamProcess = false;
  qint64 pid = window.pid;
  for (int depth = 0; pid > 1 && depth < kMaxAncestors; ++depth) {
    if (target.directProcess && target.pid > 0 && pid == target.pid) return true;
    const auto process = lookup ? lookup(pid) : std::nullopt;
    if (!process) break;
    // A window of Wine's own, such as the untitled one explorer.exe opens under Proton
    // while Rockstar's launcher starts, carries the game's Steam name but is not the game.
    if (depth == 0 && wineSystemProgram(*process)) return false;
    if (!target.winePrefix.isEmpty() && !process->winePrefix.isEmpty() &&
        canonical(process->winePrefix) == target.winePrefix)
      return true;
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
  if (m_active) release({}, Ending::Cancelled);
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
  m_order.clear();
  m_steamLaunchSeen = false;
  m_presented.clear();
  m_steamPrompt.clear();
  // Steam's log counts whole seconds.
  m_begun = QDateTime::currentDateTime().addSecs(-2);
  m_eulaBaseline.clear();
  m_eulaAsked = false;
  m_eulaAccepted.invalidate();
  m_relaunched = false;
  m_active = true;
  m_poll.start();
  m_timeout.start();
  emit changed();
  return true;
}

void LaunchHold::preview(const QString& title, const QVector<GameModeHeldWindow>& windows,
                         const QString& steamPrompt) {
  m_target = {};
  m_target.title = title;
  m_windows = windows;
  m_steamPrompt = steamPrompt;
  m_active = true;
  emit changed();
}

void LaunchHold::launched(qint64 pid) {
  if (m_active) m_target.pid = pid;
}

void LaunchHold::cancel() {
  if (m_active) release({}, Ending::Cancelled);
}

void LaunchHold::abandon() {
  // Game Mode puts every held window back as it leaves or parks.
  m_kept.clear();
  if (m_active) stop();
}

void LaunchHold::stop() {
  m_poll.stop();
  m_timeout.stop();
  m_closedGrace.stop();
  m_active = false;
  m_kept.clear();
  m_windows.clear();
  // Pointing at the library focuses it again, wherever Game Mode puts it.
  if (!m_presented.isEmpty() && m_compositor) {
    m_actionWatcher.waitForFinished();
    const qint64 owner = QCoreApplication::applicationPid();
    m_actionWatcher.setFuture(
        QtConcurrent::run([compositor = m_compositor, owner] { followMouseAgain(compositor, owner); }));
  }
  m_presented.clear();
  m_steamPrompt.clear();
  ++m_generation;
  emit changed();
  emit released();
}

void LaunchHold::scan() {
  if (!m_active || m_scanWatcher.isRunning() || m_actionWatcher.isRunning()) return;
  m_scanWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, target = m_target, lookup = m_lookup, ignored = m_ignored,
       generation = m_generation, steamLaunch = m_steamLaunchCheck,
       steamPrompt = m_steamPromptCheck, begun = m_begun] {
        Scan scan;
        scan.generation = generation;
        if (!target.steamAppId.isEmpty() && steamLaunch)
          scan.steamLaunchRunning = steamLaunch(target.steamAppId);
        if (!target.steamAppId.isEmpty() && steamPrompt)
          scan.steam = steamPrompt(target.steamAppId, begun);
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
    release(scan.game, Ending::GameShown);
    return;
  }
  setWindows(scan.windows);
  followSteamPrompt(scan.steam);
  // A game that dies before it opens a window would otherwise leave Steam's window listed
  // and this screen waiting for good. While Steam waits on its EULA or shaders, its launch
  // is not over, and its window must not be left out of sight. Any other wait may be a
  // step of Steam's own that never says it ended, so the grace still runs for it.
  if (scan.steamLaunchRunning) {
    m_steamLaunchSeen = true;
    m_closedGrace.stop();
  } else if (scan.steam.prompt == QStringLiteral("eula") ||
             scan.steam.prompt == QStringLiteral("shaders")) {
    m_closedGrace.stop();
  } else if (m_steamLaunchSeen && !m_closedGrace.isActive()) {
    m_closedGrace.start();
  }
}

void LaunchHold::followSteamPrompt(const SteamPromptState& steam) {
  if (steam.prompt != m_steamPrompt) {
    m_steamPrompt = steam.prompt;
    emit changed();
  }
  if (steam.prompt != QStringLiteral("eula")) {
    m_eulaAsked = false;
    m_eulaAccepted.invalidate();
    return;
  }
  if (!m_eulaAsked) {
    m_eulaAsked = true;
    m_eulaBaseline = steam.eulaRecord;
    return;
  }
  // Accepted, and Steam still waits: once its own time to carry on has passed, start the
  // game again. Steam asks no more once the EULA is accepted.
  if (m_relaunched || steam.eulaRecord == m_eulaBaseline) {
    m_eulaAccepted.invalidate();
    return;
  }
  if (!m_eulaAccepted.isValid()) {
    m_eulaAccepted.start();
    return;
  }
  if (m_eulaAccepted.elapsed() < m_relaunchDelay) return;
  m_relaunched = true;
  m_eulaAccepted.invalidate();
  m_timeout.start();
  if (m_steamRelaunch) m_steamRelaunch(m_target.steamAppId);
}

void LaunchHold::setWindows(const QVector<GameModeHeldWindow>& windows) {
  // Keep the order windows first arrived in, so the list does not jump while it is read,
  // and a window that was shown goes back to its own row.
  QVector<GameModeHeldWindow> ordered = windows;
  for (const auto& window : windows)
    if (!m_order.contains(window.address)) m_order.append(window.address);
  std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
    return m_order.indexOf(a.address) < m_order.indexOf(b.address);
  });
  const bool same = ordered.size() == m_windows.size() &&
                    std::equal(ordered.cbegin(), ordered.cend(), m_windows.cbegin(),
                               [](const auto& a, const auto& b) {
                                 return a.address == b.address && a.title == b.title;
                               });
  if (same) {
    // A window can grow after it maps, as Steam's dialogs do. Its size is not listed, so
    // only a shown window needs it, and nothing is redrawn.
    m_windows = ordered;
    return;
  }
  // A new window means the launcher is still working: give it the full time again. So
  // does the last one closing, as the time only runs out with nothing listed.
  if (ordered.isEmpty() || std::any_of(ordered.cbegin(), ordered.cend(), [&](const auto& window) {
        return std::none_of(m_windows.cbegin(), m_windows.cend(),
                            [&](const auto& known) { return known.address == window.address; });
      }))
    m_timeout.start();
  m_windows = ordered;
  emit changed();
}

void LaunchHold::release(const QString& game, Ending ending) {
  m_poll.stop();
  m_timeout.stop();
  m_closedGrace.stop();
  m_active = false;
  // A window still shown when the game opens goes back with the others, or it would sit
  // over Game Mode when the game ends.
  const QString presented = ending == Ending::GameShown && m_presented != game ? m_presented : QString{};
  const bool wasPresented = !m_presented.isEmpty();
  if (ending != Ending::Cancelled) {
    for (const auto& window : std::as_const(m_windows))
      if (window.address != game && !m_kept.contains(window.address)) m_kept.append(window.address);
    if (!presented.isEmpty() && !m_kept.contains(presented)) m_kept.append(presented);
  }
  m_windows.clear();
  m_presented.clear();
  m_steamPrompt.clear();
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
      [compositor = m_compositor, workspace = m_workspace, game, owner, ignored = m_ignored,
       ending, presented, wasPresented] {
        compositor->setLaunchHold({});
        if (wasPresented) followMouseAgain(compositor, owner);
        // The game is gone, so the library is what's left in front. Its launcher's windows
        // stay out of sight, as they would have after it played.
        if (ending == Ending::GameClosed) return;
        if (ending == Ending::Cancelled) {
          // No game: this launch's windows come back, as if there had been no hold.
          QVector<GameModeHeldWindow> held;
          if (compositor->heldWindows(&held, nullptr))
            for (const auto& window : std::as_const(held))
              if (!ignored.contains(window.address))
                compositor->releaseWindow(window.address, workspace);
          return;
        }
        if (!presented.isEmpty()) compositor->holdWindow(presented);
        // The launcher's windows stay out of sight until Game Mode ends. The game takes
        // over fullscreen from the library, as any window opening in Game Mode does.
        const GameModeWindow root = compositor->windowForPid(owner);
        if (root.valid()) compositor->setWindowMode(root.address, 0, 0);
        compositor->releaseWindow(game, workspace);
        compositor->setWindowMode(game, 2, 2);
        compositor->focusWindow(game);
      }));
  if (ending == Ending::GameShown) {
    emit gameShown();
    return;
  }
  emit released();
  if (ending == Ending::GameClosed) emit gameClosed();
}

void LaunchHold::present(const QString& address) {
  if (!m_active || m_actionWatcher.isRunning() ||
      std::none_of(m_windows.cbegin(), m_windows.cend(),
                   [&](const auto& window) { return window.address == address; }))
    return;
  // One window at a time: one already shown goes back with the others first.
  const QString previous = m_presented == address ? QString{} : m_presented;
  // A window is never made smaller than it is, so a dialog such as Steam's EULA keeps its
  // buttons in sight.
  const auto shown = std::find_if(m_windows.cbegin(), m_windows.cend(),
                                  [&](const auto& window) { return window.address == address; });
  const int ownWidth = shown->width;
  const int ownHeight = shown->height;
  m_presented = address;
  emit changed();
  // A preview only records the choice.
  if (!m_compositor) return;
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, workspace = m_workspace, address, owner, previous, ownWidth,
       ownHeight] {
        if (!previous.isEmpty()) compositor->holdWindow(previous);
        const GameModeWindow root = compositor->windowForPid(owner);
        int width = 1440;
        int height = 810;
        for (const auto& output : compositor->outputs()) {
          if (output.name == root.output && output.width > 0 && output.height > 0) {
            const double scale = output.scale > 0 ? output.scale : 1.0;
            const int screenWidth = qRound(output.width / scale);
            const int screenHeight = qRound(output.height / scale);
            width = qBound(screenWidth * 3 / 4, ownWidth, screenWidth * 9 / 10);
            height = qBound(screenHeight * 3 / 4, ownHeight, screenHeight * 9 / 10);
          }
        }
        // A floating window is covered by a fullscreen library on the same workspace. A
        // pointer that leaves the window for the library leaves it in front.
        if (root.valid()) {
          compositor->setWindowMode(root.address, 0, 0);
          compositor->setFollowMouse(root.address, false);
        }
        compositor->presentWindow(address, workspace, width, height);
      }));
}

void LaunchHold::returnToLibrary() {
  if (m_presented.isEmpty()) return;
  const QString presented = m_presented;
  m_presented.clear();
  // A sign-in can take any time; the hold's own time starts again from here.
  if (m_active) m_timeout.start();
  m_closedGrace.stop();
  emit changed();
  if (!m_compositor) return;
  m_actionWatcher.waitForFinished();
  const qint64 owner = QCoreApplication::applicationPid();
  m_actionWatcher.setFuture(QtConcurrent::run([compositor = m_compositor, owner, presented] {
    // A sign-in left half done can be opened again from the list. A closed window is gone.
    compositor->holdWindow(presented);
    followMouseAgain(compositor, owner);
    const GameModeWindow root = compositor->windowForPid(owner);
    if (!root.valid()) return;
    compositor->setWindowMode(root.address, 2, 2);
    compositor->focusWindow(root.address);
  }));
}

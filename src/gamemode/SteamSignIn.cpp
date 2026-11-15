#include "gamemode/SteamSignIn.h"

#include "gamemode/GameModeController.h"
#include "launch/LaunchCommand.h"
#include "launch/SteamLauncher.h"
#include "sources/FlatpakInstall.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>

namespace {
// Steam's name for the window in English, from #Login_RefreshSignIn.
const QString kEnglishTitle = QStringLiteral("Refresh Sign In");

// Native Steam first, then Flatpak Steam, as SteamLauncher looks for them.
QStringList steamRoots() {
  const QString home = QDir::homePath();
  return {home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
          home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam")};
}

QByteArray readFile(const QString& path, qint64 tailSize = -1) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  if (tailSize > 0 && file.size() > tailSize) file.seek(file.size() - tailSize);
  return tailSize > 0 ? file.read(tailSize) : file.readAll();
}

// The root whose file at `relative` Steam wrote last.
QString newestRoot(const QString& relative) {
  QString root;
  QDateTime newest;
  for (const QString& candidate : steamRoots()) {
    const QFileInfo file(candidate + relative);
    if (file.exists() && (root.isEmpty() || file.lastModified() > newest)) {
      root = candidate;
      newest = file.lastModified();
    }
  }
  return root;
}

bool waitFor(const std::function<bool()>& done, int milliseconds, int step) {
  QElapsedTimer timer;
  timer.start();
  while (!done()) {
    if (timer.elapsed() >= milliseconds) return false;
    QThread::msleep(step);
  }
  return true;
}

// Asks the running Steam to quit, waits for it to be gone and starts it again minimized.
// A Steam that cannot sign in on its own then shows its window, where someone can.
SteamSignIn::Restart restartClient() {
  const QDateTime since = QDateTime::currentDateTime().addSecs(-1);
  const QString steam = QStandardPaths::findExecutable(QStringLiteral("steam"));
  QList<LaunchCommand> commands;
  if (!steam.isEmpty()) commands.append({steam, {QStringLiteral("-shutdown")}});
  if (flatpakAppInstalled(QStringLiteral("com.valvesoftware.Steam")))
    commands.append({QStringLiteral("flatpak"),
                     {QStringLiteral("run"), QStringLiteral("com.valvesoftware.Steam"),
                      QStringLiteral("-shutdown")}});
  if (SteamLauncher::clientRunning()) {
    for (const LaunchCommand& command : std::as_const(commands))
      if (QProcess::startDetached(command.program, command.arguments)) break;
    if (!waitFor([] { return !SteamLauncher::clientRunning(); }, 60 * 1000, 500))
      return SteamSignIn::Restart::StillRunning;
  }
  const bool started = SteamLauncher::startInBackground();
  const bool signedIn = started && waitFor(
      [since] {
        const QString root = newestRoot(QStringLiteral("/logs/connection_log.txt"));
        return !root.isEmpty() &&
               SteamSignIn::signedInSince(
                   readFile(root + QStringLiteral("/logs/connection_log.txt"), 64 * 1024), since);
      },
      90 * 1000, 1000);
  if (signedIn) return SteamSignIn::Restart::SignedIn;
  SteamLauncher::openUrl(QUrl(QStringLiteral("steam://open/main")));
  return SteamSignIn::Restart::NotSignedIn;
}
} // namespace

SteamSignIn::SteamSignIn(GameModeCompositor* compositor, QObject* parent)
    : QObject(parent), m_compositor(compositor), m_restarter(&restartClient) {
  m_poll.setInterval(2000);
  connect(&m_poll, &QTimer::timeout, this, &SteamSignIn::scan);
  // Hyprland reports a window before Steam has finished titling it, so a look waits a
  // moment after each event.
  m_soon.setInterval(300);
  m_soon.setSingleShot(true);
  connect(&m_soon, &QTimer::timeout, this, &SteamSignIn::scan);
  connect(&m_scanWatcher, &QFutureWatcher<Scan>::finished, this, &SteamSignIn::finishScan);
  connect(&m_restartWatcher, &QFutureWatcher<Restart>::finished, this,
          &SteamSignIn::finishRestart);
  connect(&m_events, &QLocalSocket::readyRead, this, [this] {
    m_eventBuffer.append(m_events.readAll());
    bool relevant = false;
    qsizetype end = 0;
    while ((end = m_eventBuffer.indexOf('\n')) >= 0) {
      const QByteArray line = m_eventBuffer.left(end);
      m_eventBuffer.remove(0, end + 1);
      relevant = relevant || line.startsWith("openwindow>>") || line.startsWith("closewindow>>") ||
                 line.startsWith("windowtitle>>") || line.startsWith("windowtitlev2>>");
    }
    if (relevant && m_watching) m_soon.start();
  });
  // Without Hyprland's events, looking every poll interval still finds the window.
  connect(&m_events, &QLocalSocket::disconnected, this, [this] {
    if (m_watching) m_poll.start();
  });
  connect(&m_events, &QLocalSocket::errorOccurred, this, [this] {
    if (m_watching && m_events.state() != QLocalSocket::ConnectedState) m_poll.start();
  });
}

SteamSignIn::~SteamSignIn() {
  m_scanWatcher.waitForFinished();
  m_restartWatcher.waitForFinished();
  m_action.waitForFinished();
}

void SteamSignIn::setWatching(bool watching) {
  if (m_watching == watching) return;
  m_watching = watching;
  ++m_generation;
  if (!watching) {
    m_poll.stop();
    m_soon.stop();
    m_events.abort();
    m_eventBuffer.clear();
    if (!m_window.address.isEmpty()) {
      m_window = {};
      emit changed();
    }
    return;
  }
  if (!m_compositor) return;
  listen();
  scan();
}

void SteamSignIn::listen() {
  const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  const QString instance = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
  const QString socket = runtime + QStringLiteral("/hypr/") + instance + QStringLiteral("/.socket2.sock");
  if (!m_hyprlandEvents || runtime.isEmpty() || instance.isEmpty() || !QFileInfo::exists(socket)) {
    m_poll.start();
    return;
  }
  m_events.connectToServer(socket, QIODevice::ReadOnly);
  if (!m_events.waitForConnected(500)) {
    m_events.abort();
    m_poll.start();
  }
}

void SteamSignIn::scan() {
  if (!m_watching || !m_compositor) return;
  if (m_scanWatcher.isRunning()) {
    m_rescan = true;
    return;
  }
  m_rescan = false;
  m_scanWatcher.setFuture(QtConcurrent::run(
      [compositor = m_compositor, titles = m_titles, generation = m_generation] {
        Scan scan;
        scan.generation = generation;
        scan.titles = titles.isEmpty() ? readTitles() : titles;
        QVector<GameModeHeldWindow> windows;
        scan.ok = compositor->openWindows(&windows, nullptr);
        for (const auto& window : std::as_const(windows)) {
          if (!isRefreshSignIn(window, scan.titles)) continue;
          scan.window = window;
          break;
        }
        return scan;
      }));
}

void SteamSignIn::finishScan() {
  const Scan result = m_scanWatcher.result();
  if (m_titles.isEmpty()) m_titles = result.titles;
  if (result.generation == m_generation && result.ok) {
    const bool fresh = !result.window.address.isEmpty() && result.window.address != m_window.address;
    const bool same = result.window.address == m_window.address;
    m_window = result.window;
    if (!same) emit changed();
    if (fresh && !m_restarting) emit found();
  }
  if (m_rescan) scan();
}

void SteamSignIn::restartSteam() {
  if (m_restarting || !m_restarter) return;
  m_restarting = true;
  emit changed();
  m_restartWatcher.setFuture(QtConcurrent::run([restarter = m_restarter] { return restarter(); }));
}

void SteamSignIn::finishRestart() {
  const Restart result = m_restartWatcher.result();
  m_restarting = false;
  emit changed();
  // Steam would not quit, so its window is still the way in.
  if (result == Restart::StillRunning) showWindow();
  emit restarted(result == Restart::SignedIn);
}

void SteamSignIn::showWindow() {
  if (!m_compositor || m_window.address.isEmpty()) return;
  // The window's own size, or the size Steam gives it.
  const int width = m_window.width > 0 ? m_window.width : 705;
  const int height = m_window.height > 0 ? m_window.height : 470;
  m_action.waitForFinished();
  m_action = QtConcurrent::run([compositor = m_compositor, address = m_window.address, width, height] {
    if (!compositor->presentWindow(address, GameModeController::workspace(), width, height))
      compositor->focusWindow(address);
  });
}

QStringList SteamSignIn::readTitles() {
  QStringList titles{kEnglishTitle};
  const QString home = QDir::homePath();
  for (const QString& registry :
       {home + QStringLiteral("/.steam/registry.vdf"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.steam/registry.vdf")}) {
    const QString language = languageIn(readFile(registry));
    if (language.isEmpty() || language == QStringLiteral("english")) continue;
    const QString relative =
        QStringLiteral("/steamui/localization/shared_%1-json.js").arg(language);
    const QString root = newestRoot(relative);
    if (root.isEmpty()) continue;
    const QString title = titleIn(readFile(root + relative));
    if (!title.isEmpty() && !titles.contains(title)) titles.append(title);
  }
  return titles;
}

bool SteamSignIn::isRefreshSignIn(const GameModeHeldWindow& window, const QStringList& titles) {
  return window.windowClass.compare(QStringLiteral("steam"), Qt::CaseInsensitive) == 0 &&
         titles.contains(window.title.trimmed());
}

QString SteamSignIn::languageIn(const QByteArray& registry) {
  static const QRegularExpression language(QStringLiteral("\"language\"\\s+\"([a-z]+)\""),
                                           QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = language.match(QString::fromUtf8(registry));
  return match.hasMatch() ? match.captured(1).toLower() : QString{};
}

QString SteamSignIn::titleIn(const QByteArray& localization) {
  const QByteArray key = "\"Login_RefreshSignIn\":\"";
  const qsizetype start = localization.indexOf(key);
  if (start < 0) return {};
  // The strings sit in a JavaScript template literal, so text outside ASCII is written
  // as \xNN or \uNNNN.
  QString title;
  for (qsizetype at = start + key.size(); at < localization.size(); ++at) {
    const char c = localization.at(at);
    if (c == '"') return title.trimmed();
    if (c != '\\') {
      // Bytes outside ASCII are UTF-8; gather the whole character before decoding it.
      qsizetype end = at + 1;
      while ((static_cast<unsigned char>(c) & 0x80) != 0 && end < localization.size() &&
             (static_cast<unsigned char>(localization.at(end)) & 0xc0) == 0x80)
        ++end;
      title += QString::fromUtf8(localization.mid(at, end - at));
      at = end - 1;
      continue;
    }
    if (at + 1 >= localization.size()) break;
    const char kind = localization.at(at + 1);
    const int digits = kind == 'x' ? 2 : kind == 'u' ? 4 : 0;
    if (digits == 0) {
      title += QLatin1Char(kind);
      ++at;
      continue;
    }
    bool valid = false;
    const uint code = localization.mid(at + 2, digits).toUInt(&valid, 16);
    if (!valid) return {};
    title += QChar(static_cast<char16_t>(code));
    at += 1 + digits;
  }
  return {};
}

bool SteamSignIn::signedInSince(const QByteArray& connectionLog, const QDateTime& since) {
  // [2026-11-14 22:08:20] [Logged On, 4, 7] [U:1:N] RecvMsgClientLogOnResponse() : processing complete
  static const QRegularExpression stamp(QStringLiteral("^\\[(\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d)\\]"));
  const QList<QByteArray> lines = connectionLog.split('\n');
  for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
    if (!line->contains("RecvMsgClientLogOnResponse() : processing complete") ||
        !line->contains("[Logged On")) {
      continue;
    }
    const QRegularExpressionMatch match = stamp.match(QString::fromUtf8(*line));
    if (!match.hasMatch()) continue;
    const QDateTime at =
        QDateTime::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return at.isValid() && at >= since;
  }
  return false;
}

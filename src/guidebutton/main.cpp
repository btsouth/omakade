// omakade-guide-button: toggles Game Mode when a controller's Guide or Home button is pressed.
// Settings starts and stops it as a user service. It reads controllers passively and asks
// Hyprland to run the same command the keyboard shortcut runs.

#include "guidebutton/GuideListener.h"
#include "guide/GuideClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <unistd.h>
#include <chrono>

namespace {
constexpr int kHyprctlTimeoutMs = 5000;
// EX_CONFIG, which the service unit lists as not worth restarting.
constexpr int kNoHyprland = 78;

// Hyprland ignores key bindings while a lock screen is up, so the button does too: it should
// not rearrange the desktop or switch audio behind a locked screen.
bool screenLocked() {
  static const QStringList lockers{QStringLiteral("hyprlock"), QStringLiteral("swaylock"),
                                   QStringLiteral("gtklock"), QStringLiteral("waylock")};
  const QString uid = QString::number(::getuid());
  const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs);
  for (const QString& pid : pids) {
    if (pid.isEmpty() || !pid.at(0).isDigit()) {
      continue;
    }
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly) ||
        !lockers.contains(QString::fromUtf8(comm.readAll()).trimmed())) {
      continue;
    }
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
      continue;
    }
    for (const QByteArray& line : status.readAll().split('\n')) {
      if (line.startsWith("Uid:") &&
          QString::fromUtf8(line.mid(4)).simplified().section(QLatin1Char(' '), 0, 0) == uid) {
        return true;
      }
    }
  }
  return false;
}

QString luaString(const QString& text) {
  QString quoted = text;
  quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  quoted.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
  return QLatin1Char('"') + quoted + QLatin1Char('"');
}

// Hyprland starts the command, exactly as it does for the keyboard shortcut. A cold start
// becomes the full Omakade window, which must belong to the desktop session rather than to
// this service, or stopping the service would close it.
void toggleGameMode(const QString& command, QObject* parent) {
  if (screenLocked()) {
    qInfo("Ignored while the screen is locked");
    return;
  }
  const QString hyprctl = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
  if (hyprctl.isEmpty() || qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
    qWarning("Hyprland is not reachable from this service, so Game Mode cannot be toggled");
    return;
  }
  auto* process = new QProcess(parent);
  process->setProcessChannelMode(QProcess::MergedChannels);
  QObject::connect(process, &QProcess::finished, process,
                   [process](int exitCode, QProcess::ExitStatus status) {
                     if (status != QProcess::NormalExit || exitCode != 0) {
                       qWarning().noquote()
                           << "hyprctl could not run the Game Mode command:"
                           << QString::fromUtf8(process->readAll()).trimmed();
                     }
                     process->deleteLater();
                   });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) {
                       qWarning("hyprctl failed to start");
                       process->deleteLater();
                     }
                   });
  QTimer::singleShot(kHyprctlTimeoutMs, process, [process] { process->kill(); });
  process->start(hyprctl,
                 {QStringLiteral("eval"),
                  QStringLiteral("hl.exec_cmd(%1)").arg(luaString(command))});
}
} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("omakade-guide-button"));
  QCoreApplication::setApplicationVersion(QStringLiteral(OMAKADE_VERSION));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Toggles Omakade Game Mode with a controller's Guide or Home button."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption list(QStringLiteral("list"),
                                QStringLiteral("List the controllers that would be watched."));
  // For tests: fake device and sysfs trees, and a command other than the installed Omakade.
  QCommandLineOption devDir(QStringLiteral("dev-dir"), {}, QStringLiteral("path"),
                            QStringLiteral("/dev/input"));
  QCommandLineOption sysDir(QStringLiteral("sys-dir"), {}, QStringLiteral("path"),
                            QStringLiteral("/sys/class/input"));
  QCommandLineOption command(QStringLiteral("command"), {}, QStringLiteral("command"),
                             QStringLiteral("omakade --game-mode-toggle"));
  for (QCommandLineOption* hidden : {&devDir, &sysDir, &command}) {
    hidden->setFlags(QCommandLineOption::HiddenFromHelp);
  }
  parser.addOptions({list, devDir, sysDir, command});
  parser.process(application);

  if (parser.isSet(list)) {
    QTextStream out(stdout);
    const QList<GuideListener::Controller> controllers =
        GuideListener::scan(parser.value(devDir), parser.value(sysDir));
    if (controllers.isEmpty()) {
      out << "No controllers with a Guide or Home button are connected.\n";
    }
    for (const GuideListener::Controller& controller : controllers) {
      const QString path = parser.value(devDir) + QLatin1Char('/') + controller.node;
      out << controller.node << '\t' << controller.name
          << (controller.virtualDevice ? "\tvirtual" : "")
          << (::access(QFile::encodeName(path).constData(), R_OK) == 0 ? "" : "\tno read access")
          << '\n';
    }
    return EXIT_SUCCESS;
  }

  // Every press goes through Hyprland. Without it the service fails, rather than running and
  // dropping presses, so Settings can show that it is not working. The exit status tells
  // systemd not to restart it: the environment will not appear by retrying.
  if (qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() ||
      QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty()) {
    qCritical("Hyprland is not reachable from this service, so Game Mode cannot be toggled. "
              "Start the service from a Hyprland session.");
    return kNoHyprland;
  }

  GuideListener listener(parser.value(devDir), parser.value(sysDir));
  const QString toggleCommand = parser.value(command);
  QObject::connect(&listener, &GuideListener::preparing, &application, [&application](const QString& node, const QString&) {
    qInfo("Guide timing: Home down mono_ns=%lld node=%s", qint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()), qPrintable(node));
    // Preserve release/chord safety while preparing the cached game snapshot on down.
    GuideClient::request({{"action", "prepare"}}, &application);
  });
  QObject::connect(&listener, &GuideListener::pressed, &application,
                   [&application, toggleCommand](const QString& node, const QString& name) {
    qInfo().noquote() << QStringLiteral("Guide pressed on %1 (%2)").arg(node, name);
    const auto now = qint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    qInfo("Guide timing: Home release mono_ns=%lld node=%s", now, qPrintable(node));
    GuideClient::request({{"action", "shortcut"}, {"node", node}, {"requestNs", QString::number(now)}}, &application,
                        [&application, toggleCommand, node](const QString& result, const QJsonObject&) {
      if (result == "fallback")
        toggleGameMode(QString(toggleCommand).replace("--game-mode-toggle", "--game-mode-fallback").replace("--guide-toggle", "--game-mode-fallback") + " --guide-device " + node, &application);
      else if (result != "handled" && result != "locked") qWarning("Resident guide unavailable; Home did not open the library");
    });
  });
  listener.start();
  return application.exec();
}

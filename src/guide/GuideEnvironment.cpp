#include "guide/GuideEnvironment.h"
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLocalSocket>

QProcessEnvironment GuideEnvironment::resolve(const QProcessEnvironment& inherited) {
  auto environment = inherited;
  QProcess manager;
  manager.setProcessEnvironment(inherited);
  manager.start(QStandardPaths::findExecutable("systemctl", inherited.value("PATH").split(':')), {"--user", "show-environment"});
  if (!manager.waitForFinished(300)) { manager.kill(); manager.waitForFinished(); }
  const auto output = manager.exitStatus() == QProcess::NormalExit && manager.exitCode() == 0 ? manager.readAllStandardOutput() : QByteArray{};
  for (const auto& line : output.split('\n')) {
    for (const auto& key : {QByteArray("HYPRLAND_INSTANCE_SIGNATURE"), QByteArray("WAYLAND_DISPLAY")}) {
      if (!line.startsWith(key + '=')) continue;
      auto value = line.mid(key.size() + 1);
      // These names have no shell metacharacters. Reject shell-quoted or malformed
      // manager output rather than evaluating it.
      if (value.isEmpty() || value.contains('\'') || value.contains('"') || value.contains(' ') || value.contains('/')) continue;
      environment.insert(QString::fromLatin1(key), QString::fromUtf8(value));
    }
  }
  const auto runtime = environment.value("XDG_RUNTIME_DIR");
  const auto socket = runtime + "/hypr/" + environment.value("HYPRLAND_INSTANCE_SIGNATURE") + "/.socket2.sock";
  const auto reachable = [](const QString& path) {
    QLocalSocket probe; probe.connectToServer(path);
    return probe.waitForConnected(80);
  };
  if (!reachable(socket)) {
    // A compositor can restart before it imports the new manager environment.
    // Select the newest live-looking instance in this private user runtime.
    for (const auto& instance : QDir(runtime + "/hypr").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
      if (!reachable(instance.filePath() + "/.socket2.sock")) continue;
      environment.insert("HYPRLAND_INSTANCE_SIGNATURE", instance.fileName());
      for (const auto& display : QDir(runtime).entryList({"wayland-*"}, QDir::System, QDir::Time)) {
        if (display.endsWith(".lock")) continue;
        environment.insert("WAYLAND_DISPLAY", display); break;
      }
      break;
    }
  }
  return environment;
}

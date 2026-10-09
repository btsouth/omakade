#include "guide/GuideEnvironment.h"
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QFile>
#include "tracking/ProcFs.h"

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
  const auto reachable = [](const QString& path) {
    QLocalSocket probe; probe.connectToServer(path);
    return probe.waitForConnected(80);
  };
  struct Instance { QString signature, display; };
  QList<Instance> live;
  for (const auto& instance : QDir(runtime + "/hypr").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QFile lock(instance.filePath() + "/hyprland.lock");
    if (!lock.open(QIODevice::ReadOnly)) continue;
    const auto pid = lock.readLine().trimmed().toLongLong();
    const auto display = QString::fromUtf8(lock.readLine().trimmed());
    if (!ProcFs::processRunning(pid) || display.isEmpty() || display.contains('/') ||
        !QFileInfo::exists(runtime + '/' + display) || !reachable(instance.filePath() + "/.socket2.sock")) continue;
    live.append({instance.fileName(), display});
  }
  const auto signature = environment.value("HYPRLAND_INSTANCE_SIGNATURE");
  const auto display = environment.value("WAYLAND_DISPLAY");
  QList<Instance> matches;
  for (const auto& instance : live)
    if (instance.signature == signature && (display.isEmpty() || display == instance.display)) matches.append(instance);
  if (matches.isEmpty() && !display.isEmpty())
    for (const auto& instance : live) if (instance.display == display) matches.append(instance);
  if (matches.isEmpty() && live.size() == 1) matches = live;
  environment.remove("HYPRLAND_INSTANCE_SIGNATURE"); environment.remove("WAYLAND_DISPLAY");
  if (matches.size() == 1) {
    environment.insert("HYPRLAND_INSTANCE_SIGNATURE", matches.first().signature);
    environment.insert("WAYLAND_DISPLAY", matches.first().display);
  }
  return environment;
}

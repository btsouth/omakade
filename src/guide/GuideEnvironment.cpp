#include "guide/GuideEnvironment.h"
#include <QProcess>

QProcessEnvironment GuideEnvironment::resolve(const QProcessEnvironment& inherited) {
  auto environment = inherited;
  if (!environment.value("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() &&
      !environment.value("WAYLAND_DISPLAY").isEmpty()) return environment;
  QProcess manager;
  manager.setProcessEnvironment(inherited);
  manager.start("systemctl", {"--user", "show-environment"});
  if (!manager.waitForFinished(300)) { manager.kill(); manager.waitForFinished(); return environment; }
  if (manager.exitStatus() != QProcess::NormalExit || manager.exitCode() != 0) return environment;
  for (const auto& line : manager.readAllStandardOutput().split('\n')) {
    for (const auto& key : {QByteArray("HYPRLAND_INSTANCE_SIGNATURE"), QByteArray("WAYLAND_DISPLAY")}) {
      if (!line.startsWith(key + '=')) continue;
      auto value = line.mid(key.size() + 1);
      // These names have no shell metacharacters. Reject shell-quoted or malformed
      // manager output rather than evaluating it.
      if (value.isEmpty() || value.contains('\'') || value.contains('"') || value.contains(' ') || value.contains('/')) continue;
      environment.insert(QString::fromLatin1(key), QString::fromUtf8(value));
    }
  }
  return environment;
}

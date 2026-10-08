#include "guide/GuidePlugin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace GuidePlugin {
namespace {

QString shellReply(const Paths& paths, const QStringList& arguments) {
  const QString program = QStandardPaths::findExecutable(paths.shellProgram);
  if (program.isEmpty()) return {};
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(2500)) {
    process.kill();
    process.waitForFinished(500);
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

}  // namespace

Paths defaultPaths(const QString& configRoot, const QString& stateRoot, const QString& applicationDir) {
  Paths paths;
  paths.pluginsDir = configRoot + QStringLiteral("/omarchy/plugins");
  paths.shellConfig = configRoot + QStringLiteral("/omarchy/shell.json");
  paths.bundledDir = QDir::cleanPath(applicationDir + QStringLiteral("/../share/omakade/omarchy-plugin"));
  paths.markerPath = stateRoot + QStringLiteral("/omakade/guide-plugin-enabled");
  return paths;
}

bool usable(const Paths& paths) {
  if (!QFileInfo::exists(paths.pluginsDir + QLatin1Char('/') + kId + QStringLiteral("/manifest.json"))) return false;
  QFile file(paths.shellConfig);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const auto config = QJsonDocument::fromJson(file.readAll()).object();
  for (const auto& entry : config.value("plugins").toArray())
    if (entry.toObject().value("id").toString() == QLatin1String(kId)) return true;
  return false;
}

bool ensure(const Paths& paths) {
  const QString link = paths.pluginsDir + QLatin1Char('/') + kId;
  // lstat semantics: a dangling link or any other occupant belongs to the user.
  if (!QFileInfo(link).isSymLink() && !QFileInfo::exists(link) &&
      QFileInfo::exists(paths.bundledDir + QStringLiteral("/manifest.json"))) {
    QDir().mkpath(paths.pluginsDir);
    if (!QFile::link(paths.bundledDir, link)) qWarning("Guide: could not link %s", qPrintable(link));
  }
  if (!QFileInfo::exists(link + QStringLiteral("/manifest.json"))) return false;
  if (QFileInfo::exists(paths.markerPath)) return usable(paths);

  // Enabled once. After that, `omarchy plugin disable omakade.guide` stays disabled.
  bool enabled = usable(paths);
  if (!enabled) {
    // The shell answers "unknown" until its rescan has seen the new link, which it does
    // in the background after rescanPlugins returns.
    shellReply(paths, {"shell", "rescanPlugins"});
    for (int attempt = 0; attempt < 10 && !enabled; ++attempt) {
      if (attempt > 0) QThread::msleep(200);
      const QString reply = shellReply(paths, {"shell", "enablePlugin", QString::fromLatin1(kId), "{}"});
      if (reply != QLatin1String("unknown") && reply != QLatin1String("ok")) break;
      enabled = reply == QLatin1String("ok");
    }
  }
  if (enabled) {
    QDir().mkpath(QFileInfo(paths.markerPath).absolutePath());
    QFile marker(paths.markerPath);
    if (marker.open(QIODevice::WriteOnly)) marker.write("1\n");
  }
  return enabled;
}

}  // namespace GuidePlugin

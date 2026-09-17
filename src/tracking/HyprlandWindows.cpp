#include "tracking/HyprlandWindows.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QtGlobal>

namespace {
// A stalled compositor must never block the poll loop, so the query is bounded.
constexpr int kQueryTimeoutMs = 1500;
} // namespace

namespace HyprlandWindows {

QVector<Window> parse(const QByteArray& json, QString* error) {
  QVector<Window> windows;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    if (error != nullptr) {
      *error = parseError.errorString();
    }
    return windows;
  }
  for (const QJsonValue& value : document.array()) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject client = value.toObject();
    Window window;
    window.title = client.value(QLatin1String("title")).toString();
    window.address = client.value(QLatin1String("address")).toString();
    window.pid = client.value(QLatin1String("pid")).toVariant().toLongLong();
    if (window.title.isEmpty() || window.pid <= 0) {
      continue;
    }
    windows.append(window);
  }
  return windows;
}

bool available() {
  return !QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty() &&
         !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty();
}

QVector<Window> list() {
  if (!available()) {
    return {};
  }
  QProcess process;
  process.start(QStandardPaths::findExecutable(QStringLiteral("hyprctl")),
                {QStringLiteral("clients"), QStringLiteral("-j")});
  if (!process.waitForStarted(kQueryTimeoutMs)) {
    return {};
  }
  if (!process.waitForFinished(kQueryTimeoutMs) || process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    process.kill();
    process.waitForFinished(kQueryTimeoutMs);
    return {};
  }
  return parse(process.readAllStandardOutput());
}

QString titleForPid(const QVector<Window>& windows, qint64 pid) {
  if (pid <= 0) {
    return {};
  }
  for (const Window& window : windows) {
    if (window.pid == pid) {
      return window.title;
    }
  }
  return {};
}

} // namespace HyprlandWindows

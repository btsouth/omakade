#include "highlights/HighlightSettings.h"

#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cstdlib>

namespace HighlightSettings {

Values parse(const QString& contents) {
  Values values;
  const auto flag = [&contents](const QString& key, bool fallback) {
    const QRegularExpression pattern(
        QStringLiteral("(?m)^%1\\s*=\\s*(true|false)\\s*$").arg(key));
    const auto match = pattern.match(contents);
    return match.hasMatch() ? match.captured(1) == QStringLiteral("true") : fallback;
  };
  values.enabled = flag(QStringLiteral("highlights"), values.enabled);
  values.achievementClips = flag(QStringLiteral("highlight_achievements"), values.achievementClips);
  static const QRegularExpression seconds(QStringLiteral("(?m)^highlight_seconds\\s*=\\s*([0-9]{1,6})\\s*$"));
  const auto match = seconds.match(contents);
  if (match.hasMatch()) values.clipSeconds = normalizedSeconds(match.captured(1).toInt());
  return values;
}

Values load(const QString& configPath) {
  QFile file(configPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return parse(QString::fromUtf8(file.read(1024 * 1024)));
}

int normalizedSeconds(int seconds) {
  int best = kClipLengths[0];
  for (const int length : kClipLengths)
    if (std::abs(length - seconds) < std::abs(best - seconds)) best = length;
  return best;
}

QString folder() {
  QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  if (videos.isEmpty()) videos = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Videos");
  return videos + QStringLiteral("/Omakade");
}

int bufferSeconds(const Values& values) { return qMax(120, values.clipSeconds + 30); }

} // namespace HighlightSettings

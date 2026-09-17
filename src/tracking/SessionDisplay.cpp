#include "tracking/SessionDisplay.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// Entry points that say nothing about the game itself. When one of these is the
// launched file, the folder around it carries the name instead.
bool isGenericEntryPoint(const QString& baseName) {
  static const QStringList names = {
      QStringLiteral("default"), QStringLiteral("eboot"),   QStringLiteral("boot"),
      QStringLiteral("game"),    QStringLiteral("disc"),    QStringLiteral("auto"),
      QStringLiteral("main"),    QStringLiteral("start"),   QStringLiteral("run"),
      QStringLiteral("xenia"),   QStringLiteral("executable"),
  };
  return names.contains(baseName.toLower());
}

QString tidy(const QString& raw) {
  QString name = raw.simplified();
  name.replace(QLatin1Char('_'), QLatin1Char(' '));
  name = name.simplified();
  return name;
}

} // namespace

QString SessionDisplay::titleForGamePath(const QString& gamePath) {
  const QString trimmed = gamePath.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("Unknown game");
  }
  const QFileInfo info(trimmed);
  const QString baseName = info.completeBaseName();
  const QString folder = info.dir().dirName();
  const QString candidate = isGenericEntryPoint(baseName) && !folder.isEmpty() ? folder : baseName;
  const QString title = tidy(candidate);
  return title.isEmpty() ? QStringLiteral("Unknown game") : title;
}

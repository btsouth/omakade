#include "tracking/ProcessMatcher.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {
bool binaryMatches(const QString& candidate, const QStringList& binaries) {
  for (const QString& binary : binaries) {
    if (candidate.compare(binary, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

QString romPathFromArguments(const QStringList& arguments, const QSet<QString>& romExtensions) {
  for (qsizetype index = 1; index < arguments.size(); ++index) {
    const QString& argument = arguments.at(index);
    const qsizetype dot = argument.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot + 1 >= argument.size()) {
      continue;
    }
    if (romExtensions.contains(argument.mid(dot + 1).toLower())) {
      return argument;
    }
  }
  return {};
}
} // namespace

namespace ProcessMatcher {

ProcessProfileSet load(const QString& path, QString* error) {
  ProcessProfileSet set;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not read %1").arg(path);
    }
    return set;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
    }
    return set;
  }
  const QJsonObject root = document.object();
  const QJsonArray extensions = root.value(QLatin1String("romExtensions")).toArray();
  for (const QJsonValue& value : extensions) {
    const QString extension = value.toString().toLower();
    if (!extension.isEmpty()) {
      set.romExtensions.insert(extension);
    }
  }
  const QJsonArray emulators = root.value(QLatin1String("emulators")).toArray();
  for (const QJsonValue& value : emulators) {
    const QJsonObject entry = value.toObject();
    SessionProcessProfile profile;
    profile.name = entry.value(QLatin1String("name")).toString();
    const QJsonArray binaries = entry.value(QLatin1String("binaries")).toArray();
    for (const QJsonValue& binary : binaries) {
      const QString name = binary.toString();
      if (!name.isEmpty()) {
        profile.binaries.append(name);
      }
    }
    profile.rescanSource = entry.value(QLatin1String("rescanSource")).toString();
    if (!profile.name.isEmpty() && !profile.binaries.isEmpty()) {
      set.emulators.append(profile);
    }
  }
  return set;
}

QVector<SessionMatch> match(const QVector<ProcessSnapshot>& processes,
                            const ProcessProfileSet& profiles) {
  QVector<SessionMatch> matches;
  for (const ProcessSnapshot& process : processes) {
    if (process.procStart < 0) {
      continue;
    }
    for (const SessionProcessProfile& profile : profiles.emulators) {
      if (!binaryMatches(process.comm, profile.binaries)) {
        continue;
      }
      const QString gamePath = romPathFromArguments(process.arguments, profiles.romExtensions);
      if (gamePath.isEmpty()) {
        break;
      }
      matches.append({.pid = process.pid,
                      .procStart = process.procStart,
                      .emulator = profile.name,
                      .rescanSource = profile.rescanSource,
                      .gamePath = gamePath});
      break;
    }
  }
  return matches;
}

bool matchCameFromWindowTitle(const SessionMatch& match) {
  return match.procStart < 0;
}

QVector<SessionMatch> matchWithWindowTitles(
    const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
    const std::function<QString(qint64)>& windowTitleForPid, const TitleResolver& resolve) {
  QVector<SessionMatch> matches = match(processes, profiles);
  if (!windowTitleForPid || !resolve) {
    return matches;
  }
  for (const ProcessSnapshot& process : processes) {
    const bool alreadyMatched =
        std::any_of(matches.cbegin(), matches.cend(), [&process](const SessionMatch& match) {
          return match.pid == process.pid && match.procStart == process.procStart;
        });
    if (alreadyMatched) {
      continue;
    }
    const QString title = windowTitleForPid(process.pid);
    if (title.isEmpty()) {
      continue;
    }
    for (const SessionProcessProfile& profile : profiles.emulators) {
      if (!binaryMatches(process.comm, profile.binaries)) {
        continue;
      }
      const QString gamePath = resolve(title, profile.name);
      if (gamePath.isEmpty()) {
        continue;
      }
      // A title match carries no process identity: the resolved path is the
      // proof, and procStart stays negative so the recorder can tell the two
      // kinds of match apart.
      matches.append({.pid = process.pid,
                      .procStart = -1,
                      .emulator = profile.name,
                      .rescanSource = profile.rescanSource,
                      .gamePath = gamePath});
      break;
    }
  }
  return matches;
}

} // namespace ProcessMatcher

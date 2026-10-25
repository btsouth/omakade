#include "tracking/ProcessMatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

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

bool processMatches(const ProcessSnapshot& process, const SessionProcessProfile& profile) {
  if (binaryMatches(process.comm, profile.binaries) ||
      binaryMatches(QFileInfo(process.exePath).fileName(), profile.binaries) ||
      binaryMatches(QFileInfo(process.arguments.value(0)).fileName(), profile.binaries)) {
    return true;
  }
  for (const QString& configured : profile.executablePaths) {
    const QFileInfo configuredInfo(configured);
    const QString configuredPath = configuredInfo.canonicalFilePath().isEmpty()
                                       ? configuredInfo.absoluteFilePath()
                                       : configuredInfo.canonicalFilePath();
    const auto matchesPath = [&configuredPath](const QString& candidate) {
      if (candidate.isEmpty()) {
        return false;
      }
      const QFileInfo info(candidate);
      const QString path =
          info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
      return path == configuredPath;
    };
    if (matchesPath(process.exePath) ||
        (!process.arguments.isEmpty() && matchesPath(process.arguments.constFirst()))) {
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
    const QJsonArray executablePaths = entry.value(QLatin1String("executablePaths")).toArray();
    for (const QJsonValue& path : executablePaths) {
      const QString value = path.toString();
      if (!value.isEmpty()) {
        profile.executablePaths.append(value);
      }
    }
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
      if (!processMatches(process, profile)) {
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
  return matchWithAttribution(processes, profiles, windowTitleForPid, resolve,
                              AttributionResolver{});
}

QVector<SessionMatch> matchWithAttribution(
    const QVector<ProcessSnapshot>& processes, const ProcessProfileSet& profiles,
    const std::function<QString(qint64)>& windowTitleForPid, const TitleResolver& resolveTitle,
    const AttributionResolver& resolveAttribution) {
  QVector<SessionMatch> matches = match(processes, profiles);
  if (processes.isEmpty() || (!windowTitleForPid && !resolveAttribution)) {
    return matches;
  }
  // Re-evaluated as matches are added, so a weaker kind of evidence is never offered a
  // process that a stronger kind has already attributed.
  const auto alreadyMatched = [&matches](const ProcessSnapshot& process) {
    return std::any_of(matches.cbegin(), matches.cend(), [&process](const SessionMatch& match) {
      return match.pid == process.pid && match.procStart == process.procStart;
    });
  };
  if (resolveAttribution) {
    for (const ProcessSnapshot& process : processes) {
      if (alreadyMatched(process)) {
        continue;
      }
      for (const SessionProcessProfile& profile : profiles.emulators) {
        if (!processMatches(process, profile)) {
          continue;
        }
        const AttributionAdapter::Result attributed =
            resolveAttribution(process.pid, process.procStart, profile.name);
        if (attributed.gamePath.isEmpty()) {
          continue;
        }
        // The adapter read a live process's own record, so the match carries that process
        // identity and the recorder treats it as verified rather than guessed.
        matches.append({.pid = process.pid,
                        .procStart = process.procStart,
                        .emulator = profile.name,
                        .rescanSource = profile.rescanSource,
                        .gamePath = attributed.gamePath});
        break;
      }
    }
  }
  if (!windowTitleForPid || !resolveTitle) {
    return matches;
  }
  for (const ProcessSnapshot& process : processes) {
    if (alreadyMatched(process)) {
      continue;
    }
    const QString title = windowTitleForPid(process.pid);
    if (title.isEmpty()) {
      continue;
    }
    for (const SessionProcessProfile& profile : profiles.emulators) {
      if (!processMatches(process, profile)) {
        continue;
      }
      const QString gamePath = resolveTitle(title, profile.name);
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

QVector<SessionMatch> matchWinePrefixes(const QVector<ProcessSnapshot>& processes,
                                        const QHash<QString, QString>& games,
                                        const QString& source) {
  QHash<QString, const ProcessSnapshot*> oldest;
  QHash<QString, QString> canonical;
  for (const ProcessSnapshot& process : processes) {
    if (process.winePrefix.isEmpty() || process.procStart < 0) continue;
    // Wine reports a Windows program's own command line. Its loader, wineserver and
    // the runner script carry the prefix too, but run under a Unix path.
    const QString program = process.arguments.value(0);
    if (!ProcFs::isWindowsProgram(program) ||
        program.startsWith(QStringLiteral("C:\\windows\\"), Qt::CaseInsensitive))
      continue;
    auto prefix = canonical.find(process.winePrefix);
    if (prefix == canonical.end()) {
      const QString resolved = QFileInfo(process.winePrefix).canonicalFilePath();
      prefix = canonical.insert(process.winePrefix,
                                resolved.isEmpty() ? QDir::cleanPath(process.winePrefix) : resolved);
    }
    if (!games.contains(*prefix)) continue;
    const ProcessSnapshot*& current = oldest[*prefix];
    if (current == nullptr || process.procStart < current->procStart ||
        (process.procStart == current->procStart && process.pid < current->pid))
      current = &process;
  }
  QVector<SessionMatch> matches;
  for (auto it = oldest.cbegin(); it != oldest.cend(); ++it)
    matches.append({.pid = it.value()->pid, .procStart = it.value()->procStart,
                    .emulator = source, .rescanSource = {}, .gamePath = games.value(it.key()),
                    .handsOver = true});
  return matches;
}

QString profilesPath() {
  const QString userPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                           QStringLiteral("/omakade/sessiond-profiles.json");
  if (QFileInfo::exists(userPath)) {
    return userPath;
  }
  // A side-by-side local installation carries the same profiles as the system
  // package. Resolve them beside that binary instead of using another version.
  const QString installedPath = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("../share/omakade/sessiond-profiles.json"));
  if (QFileInfo::exists(installedPath)) {
    return QDir::cleanPath(installedPath);
  }
  return QStringLiteral(OMAKADE_SESSIOND_PROFILES);
}

} // namespace ProcessMatcher

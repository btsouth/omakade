#include "sources/ryujinx/RyujinxScanner.h"

#include "artwork/SwitchTitleReader.h"

#include <QDateTime>
#include <QDir>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>

namespace {
constexpr qint64 kMaximumJsonBytes = 16 * 1024 * 1024;
constexpr qint64 kMaximumPlaytimeSeconds = 1'000'000LL * 24 * 60 * 60;

const QStringList& gameExtensions() {
  static const QStringList extensions = {QStringLiteral(".xci"), QStringLiteral(".nsp"),
                                         QStringLiteral(".nro")};
  return extensions;
}

bool isGameFile(const QString& fileName) {
  for (const QString& extension : gameExtensions()) {
    if (fileName.endsWith(extension, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

bool validTitleId(const QString& value) {
  static const QRegularExpression valid(QStringLiteral("^[0-9A-Fa-f]{16}$"));
  return valid.match(value).hasMatch();
}

bool isUpdateTitleId(const QString& value) {
  return validTitleId(value) && value.endsWith(QStringLiteral("800"), Qt::CaseInsensitive);
}

qint64 playtimeSeconds(const QJsonValue& value) {
  if (value.isDouble()) {
    const double seconds = value.toDouble();
    return seconds > 0 && seconds <= static_cast<double>(kMaximumPlaytimeSeconds)
               ? static_cast<qint64>(seconds)
               : 0;
  }
  if (!value.isString()) {
    return 0;
  }
  // .NET TimeSpan's invariant form is [days.]HH:MM:SS[.fraction].
  static const QRegularExpression timeSpan(
      QStringLiteral("^(?:(\\d{1,6})\\.)?(\\d{1,2}):([0-5]\\d):([0-5]\\d)(?:\\.\\d{1,7})?$"));
  const QRegularExpressionMatch match = timeSpan.match(value.toString().trimmed());
  if (!match.hasMatch()) {
    return 0;
  }
  const qint64 days = match.captured(1).isEmpty() ? 0 : match.captured(1).toLongLong();
  const qint64 hours = match.captured(2).toLongLong();
  if (hours > 23) {
    return 0;
  }
  return days * 24 * 60 * 60 + hours * 60 * 60 + match.captured(3).toLongLong() * 60 +
         match.captured(4).toLongLong();
}

QString normalizedPackagePath(QString path, const QString& root) {
  path = path.trimmed();
  if (path.startsWith(QStringLiteral("~/"))) {
    path.replace(0, 1, QDir::homePath());
  }
  if (path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QFileInfo(path).isAbsolute() ? path : QDir(root).absoluteFilePath(path));
}

void collectConfiguredPackagePaths(const QJsonValue& value, const QString& root,
                                   QSet<QString>* paths) {
  if (value.isString()) {
    const QString path = normalizedPackagePath(value.toString(), root);
    if (!path.isEmpty()) {
      paths->insert(path);
    }
    return;
  }
  if (value.isArray()) {
    for (const QJsonValue& child : value.toArray()) {
      collectConfiguredPackagePaths(child, root, paths);
    }
    return;
  }
  if (!value.isObject()) {
    return;
  }
  const QJsonObject object = value.toObject();
  for (const QString& key : {QStringLiteral("path"), QStringLiteral("paths"),
                             QStringLiteral("selected")}) {
    if (object.contains(key)) {
      collectConfiguredPackagePaths(object.value(key), root, paths);
    }
  }
}

// Strips update/DLC markers: "Game [0100...][v0].nsp" -> "0100...".
QString titleIdFromFilename(const QString& fileName) {
  static const QRegularExpression expression(
      QStringLiteral("\\[\\s*(0100[0-9A-Fa-f]{12})\\s*\\]"));
  const QRegularExpressionMatch match = expression.match(fileName);
  return match.hasMatch() ? match.captured(1).toUpper() : QString{};
}

bool isAddOnFileName(const QString& fileName) {
  static const QRegularExpression addOn(
      QStringLiteral("(?:^|[\\s_\\-\\[\\(\\.])(dlc|update|upd|patch)(?:$|[\\s_\\-\\]\\)\\.\\[])"),
      QRegularExpression::CaseInsensitiveOption);
  return addOn.match(fileName).hasMatch();
}

bool hasVersionSuffix(const QString& fileName) {
  static const QRegularExpression version(
      QStringLiteral("(?:\\[\\s*v\\d+\\s*\\]|\\bv\\d+(?:\\.\\d+)+\\b)"),
      QRegularExpression::CaseInsensitiveOption);
  return version.match(QFileInfo(fileName).completeBaseName()).hasMatch();
}

QString cleanGameName(const QString& fileName) {
  QString name = QFileInfo(fileName).completeBaseName();
  name.remove(QRegularExpression(QStringLiteral("\\[\\s*[0-9A-Fa-f]{16}\\s*\\]")));
  name.remove(QRegularExpression(QStringLiteral("\\[\\s*v\\d+\\s*\\]")));
  static const QRegularExpression bracketedDlc(QStringLiteral("\\[\\s*dlc[^\\]]*\\]"),
                                               QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression parenthesizedDlc(QStringLiteral("\\(\\s*dlc[^)]*\\)"),
                                                   QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression versionSuffix(
      QStringLiteral("\\s+v\\d+(?:\\.\\d+)+\\s*$"), QRegularExpression::CaseInsensitiveOption);
  name.remove(bracketedDlc);
  name.remove(parenthesizedDlc);
  name.remove(versionSuffix);
  return name.trimmed();
}

QString normalizedTitle(QString title) {
  static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
  title = title.trimmed().toCaseFolded();
  title.remove(separators);
  return title;
}

bool isAddOnDirectory(const QString& path) {
  static const QSet<QString> names = {
      QStringLiteral("dlc"), QStringLiteral("dlcs"), QStringLiteral("update"),
      QStringLiteral("updates"), QStringLiteral("patch"), QStringLiteral("patches"),
      QStringLiteral("dlcsupdates"), QStringLiteral("dlcupdates")};
  const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    if (names.contains(normalizedTitle(part))) {
      return true;
    }
  }
  return false;
}

QString firstExistingImage(const QStringList& candidates) {
  for (const QString& candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

QStringList imageVariants(const QString& stem) {
  QStringList paths;
  for (const QString& extension :
       {QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
        QStringLiteral(".webp")}) {
    paths.append(stem + extension);
  }
  return paths;
}

// Local artwork only: sidecar images next to the ROM, then Ryujinx's
// games/<titleId>/covers and games/<titleId> icon files.
QString coverFor(const QString& root, const QString& titleId, const QString& romPath) {
  QStringList candidates;
  if (!romPath.isEmpty()) {
    const QFileInfo rom(romPath);
    candidates += imageVariants(rom.absolutePath() + QLatin1Char('/') + rom.completeBaseName());
    const QString cleaned = cleanGameName(rom.fileName());
    if (!cleaned.isEmpty() && cleaned != rom.completeBaseName()) {
      candidates += imageVariants(rom.absolutePath() + QLatin1Char('/') + cleaned);
    }
    for (const QString& stem :
         {QStringLiteral("cover"), QStringLiteral("box"), QStringLiteral("game"),
          QStringLiteral("folder")}) {
      candidates += imageVariants(rom.absolutePath() + QLatin1Char('/') + stem);
    }
  }
  if (!titleId.isEmpty()) {
    QStringList gameDirs = {root + QStringLiteral("/games/") + titleId};
    const QString folded = titleId.toLower();
    if (folded != titleId) {
      gameDirs.append(root + QStringLiteral("/games/") + folded);
    }
    for (const QString& gameDir : gameDirs) {
      const QDir covers(gameDir + QStringLiteral("/covers"));
      for (const QString& stem :
           {QStringLiteral("box"), QStringLiteral("icon"), QStringLiteral("default"),
            QStringLiteral("banner")}) {
        candidates += imageVariants(covers.filePath(stem));
      }
      candidates += imageVariants(gameDir + QStringLiteral("/icon"));
      candidates += imageVariants(gameDir + QStringLiteral("/cover"));
    }
  }
  return firstExistingImage(candidates);
}

// Reads only the PFS0 table. Ticket/cert names start with the 16-hex title id.
QString titleIdFromNsp(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 16) {
    return {};
  }
  const QByteArray header = file.read(16);
  if (!header.startsWith("PFS0")) {
    return {};
  }
  const auto fileCount = qFromLittleEndian<quint32>(header.constData() + 4);
  const auto stringSize = qFromLittleEndian<quint32>(header.constData() + 8);
  if (fileCount == 0 || fileCount > 512 || stringSize == 0 || stringSize > 1024 * 1024) {
    return {};
  }
  const qint64 tableBytes = static_cast<qint64>(fileCount) * 24;
  if (file.size() < 16 + tableBytes + stringSize || !file.seek(16 + tableBytes)) {
    return {};
  }
  const QByteArray strings = file.read(stringSize);
  if (strings.size() != static_cast<int>(stringSize)) {
    return {};
  }
  static const QRegularExpression ticket(QStringLiteral("^(0100[0-9A-Fa-f]{12})"));
  const QStringList names = QString::fromLatin1(strings).split(QChar(QChar::Null), Qt::SkipEmptyParts);
  for (const QString& name : names) {
    if (!name.endsWith(QStringLiteral(".tik"), Qt::CaseInsensitive) &&
        !name.endsWith(QStringLiteral(".cert"), Qt::CaseInsensitive)) {
      continue;
    }
    const QRegularExpressionMatch match = ticket.match(name);
    if (match.hasMatch()) {
      return match.captured(1).toUpper();
    }
  }
  return {};
}
} // namespace

QStringList RyujinxScanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      home + QStringLiteral("/.config/Ryujinx"),
      home + QStringLiteral("/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx"),
      home + QStringLiteral("/.var/app/org.ryujinx.Ryujinx/config/Ryujinx"),
  };
  candidates.removeDuplicates();

  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/Config.json")).isFile()) {
      roots.append(root);
    }
  }
  return roots;
}

RyujinxScanResult RyujinxScanner::scan(const QStringList& roots) {
  RyujinxScanResult result;
  QSet<QString> seenPaths;
  QSet<QString> seenGameIds;
  for (const QString& root : roots) {
    const QString configPath = root + QStringLiteral("/Config.json");
    QFile configFile(configPath);
    if (!configFile.exists()) {
      continue;
    }
    result.roots.append(root);
    if (!configFile.open(QIODevice::ReadOnly) || configFile.size() > kMaximumJsonBytes) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(configPath));
      continue;
    }
    QJsonParseError parseError;
    const QJsonDocument configDocument =
        QJsonDocument::fromJson(configFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !configDocument.isObject()) {
      result.incomplete = true;
      result.warnings.append(
          QStringLiteral("Could not parse %1: %2").arg(configPath, parseError.errorString()));
      continue;
    }
    const QJsonObject config = configDocument.object();

    QStringList gameDirectories;
    const QJsonArray configuredDirectories = config.value(QStringLiteral("game_dirs")).toArray();
    for (const QJsonValue& value : configuredDirectories) {
      const QString directory = value.toString().trimmed();
      if (!directory.isEmpty()) {
        gameDirectories.append(directory);
      }
    }
    const bool flatpak = root.contains(QStringLiteral("/.var/app/io.github.ryubing.Ryujinx/")) ||
                         root.contains(QStringLiteral("/.var/app/org.ryujinx.Ryujinx/"));
    const QString flatpakAppId =
        root.contains(QStringLiteral("/.var/app/org.ryujinx.Ryujinx/"))
            ? QStringLiteral("org.ryujinx.Ryujinx")
        : flatpak ? QStringLiteral("io.github.ryubing.Ryujinx")
                  : QStringLiteral("");

    // Real installs: games/<titleId>/gui is a directory containing metadata.json
    // ("title", "timespan_played", "last_played_utc", plus the legacy
    // "time_played" and "last_played" keys).
    QHash<QString, QString> displayTitles;
    QHash<QString, qint64> playtimeFor;
    QHash<QString, qint64> lastPlayedFor;
    QSet<QString> configuredAddOnPaths;
    QHash<QString, int> indexByCleanTitle;
    const QDir gamesDirectory(root + QStringLiteral("/games"));
    const QFileInfoList titleDirectories =
        gamesDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : titleDirectories) {
      const QString titleId = entry.fileName().toUpper();
      if (!validTitleId(titleId)) {
        continue;
      }
      for (const QString& fileName : {QStringLiteral("updates.json"),
                                      QStringLiteral("dlc.json")}) {
        QFile packageConfig(entry.filePath() + QLatin1Char('/') + fileName);
        if (!packageConfig.open(QIODevice::ReadOnly) ||
            packageConfig.size() > kMaximumJsonBytes) {
          continue;
        }
        const QJsonDocument packageDocument =
            QJsonDocument::fromJson(packageConfig.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError) {
          collectConfiguredPackagePaths(packageDocument.isArray()
                                            ? QJsonValue(packageDocument.array())
                                            : QJsonValue(packageDocument.object()),
                                        root, &configuredAddOnPaths);
        }
      }
      const QString guiPath = entry.filePath() + QStringLiteral("/gui");
      QFile metadataFile(guiPath + QStringLiteral("/metadata.json"));
      if (metadataFile.open(QIODevice::ReadOnly) &&
          metadataFile.size() <= kMaximumJsonBytes) {
        const QJsonObject metadata =
            QJsonDocument::fromJson(metadataFile.readAll(), &parseError).object();
        if (parseError.error == QJsonParseError::NoError) {
          const QString customTitle = metadata.value(QStringLiteral("title")).toString().trimmed();
          if (!customTitle.isEmpty()) {
            displayTitles.insert(titleId, customTitle);
          }
          // Newer fields: timespan_played (.NET TimeSpan) and last_played_utc (ISO-8601).
          qint64 seconds = playtimeSeconds(metadata.value(QStringLiteral("timespan_played")));
          QString lastPlayedIso = metadata.value(QStringLiteral("last_played_utc")).toString();
          // Legacy fallbacks for older installs.
          if (seconds <= 0) {
            seconds = playtimeSeconds(metadata.value(QStringLiteral("time_played")));
          }
          if (lastPlayedIso.isEmpty()) {
            lastPlayedIso = metadata.value(QStringLiteral("last_played")).toString();
          }
          if (seconds > 0) {
            playtimeFor.insert(titleId, seconds);
          }
          if (!lastPlayedIso.isEmpty()) {
            const QDateTime parsed = QDateTime::fromString(lastPlayedIso, Qt::ISODate);
            if (parsed.isValid()) {
              lastPlayedFor.insert(titleId, parsed.toSecsSinceEpoch());
            }
          }
        }
      }
      // Legacy fallback: gui as a plain file with TitleName (older Ryujinx layouts).
      QFile guiFile(guiPath);
      if (!metadataFile.exists() && guiFile.open(QIODevice::ReadOnly) &&
          guiFile.size() <= kMaximumJsonBytes) {
        const QJsonObject gui =
            QJsonDocument::fromJson(guiFile.readAll(), &parseError).object();
        if (parseError.error == QJsonParseError::NoError) {
          const QString customTitle = gui.value(QStringLiteral("TitleName")).toString().trimmed();
          if (!customTitle.isEmpty()) {
            displayTitles.insert(titleId, customTitle);
          }
        }
      }
      QFile timeFile(entry.filePath() + QStringLiteral("/time_played"));
      if (timeFile.open(QIODevice::ReadOnly) && timeFile.size() <= kMaximumJsonBytes) {
        const QJsonObject times =
            QJsonDocument::fromJson(timeFile.readAll(), &parseError).object();
        if (parseError.error == QJsonParseError::NoError) {
          const qint64 seconds =
              static_cast<qint64>(times.value(QStringLiteral("playtime")).toDouble(0));
          const QString lastPlayedIso = times.value(QStringLiteral("last_played")).toString();
          if (seconds > 0 && !playtimeFor.contains(titleId)) {
            playtimeFor.insert(titleId, seconds);
          }
          if (!lastPlayedIso.isEmpty() && !lastPlayedFor.contains(titleId)) {
            const QDateTime parsed = QDateTime::fromString(lastPlayedIso, Qt::ISODate);
            if (parsed.isValid()) {
              lastPlayedFor.insert(titleId, parsed.toSecsSinceEpoch());
            }
          }
        }
      }
    }

    if (gameDirectories.isEmpty()) {
      // Ryujinx shows an empty game list without configured directories.
      continue;
    }

    for (const QString& configuredDirectory : gameDirectories) {
      QString expanded = configuredDirectory;
      if (expanded.startsWith(QStringLiteral("~/"))) {
        expanded.replace(0, 1, QDir::homePath());
      }
      const QFileInfo directoryInfo(expanded);
      if (isAddOnDirectory(expanded)) {
        continue;
      }
      if (!directoryInfo.isDir() || !directoryInfo.isReadable()) {
        // A missing or unreadable directory must not silently empty the library;
        // flag it so the model keeps its cached games instead of wiping them.
        result.incomplete = true;
        result.warnings.append(
            QStringLiteral("Game directory is unavailable: %1").arg(expanded));
        continue;
      }
      QDirIterator romIterator(expanded, QDir::Files, QDirIterator::Subdirectories);
      while (romIterator.hasNext()) {
        const QString filePath = QDir::cleanPath(romIterator.next());
        const QString fileName = QFileInfo(filePath).fileName();
        if (!isGameFile(fileName) || seenPaths.contains(filePath) ||
            configuredAddOnPaths.contains(filePath) || isAddOnFileName(fileName) ||
            isAddOnDirectory(filePath)) {
          continue;
        }
        QString titleId = titleIdFromFilename(fileName);
        if (titleId.isEmpty()) {
          const QFileInfo parentInfo(filePath);
          const QString parentName = parentInfo.dir().dirName();
          if (validTitleId(parentName)) {
            titleId = parentName.toUpper();
          }
        }
        if (titleId.isEmpty() && fileName.endsWith(QStringLiteral(".nsp"), Qt::CaseInsensitive)) {
          titleId = titleIdFromNsp(filePath);
        }
        if (titleId.isEmpty()) {
          QStringList needles = {normalizedTitle(cleanGameName(fileName))};
          QDir parent = QFileInfo(filePath).absoluteDir();
          for (int depth = 0; depth < 4; ++depth) {
            const QString folder = normalizedTitle(parent.dirName());
            if (folder.size() >= 8) {
              needles.append(folder);
            }
            if (validTitleId(parent.dirName())) {
              titleId = parent.dirName().toUpper();
              break;
            }
            if (!parent.cdUp()) {
              break;
            }
          }
          if (titleId.isEmpty()) {
            needles.removeDuplicates();
            needles.removeAll(QString{});
            for (const QString& needle : needles) {
              for (auto it = displayTitles.cbegin(); it != displayTitles.cend(); ++it) {
                if (normalizedTitle(it.value()) == needle) {
                  titleId = it.key();
                  break;
                }
              }
              if (!titleId.isEmpty()) {
                break;
              }
            }
          }
        }
        // The dump itself is the best source: its control data carries the icon,
        // the display name, and the title id. Reading it needs the console keys
        // Ryujinx already has; without them the older heuristics stay in charge.
        QString embeddedIcon;
        QString embeddedTitle;
        if (SwitchTitleReader::keysAvailable()) {
          if (titleId.isEmpty() || !displayTitles.contains(titleId)) {
            const SwitchTitleInfo info = SwitchTitleReader::read(filePath);
            if (titleId.isEmpty() && validTitleId(info.titleId)) {
              titleId = info.titleId.toUpper();
            }
            embeddedTitle = info.title.simplified();
          }
          const QString cacheKey = titleId.isEmpty()
                                       ? QString::fromLatin1(QCryptographicHash::hash(
                                                                 filePath.toUtf8(), QCryptographicHash::Sha256)
                                                                 .toHex()
                                                                 .left(32))
                                       : titleId;
          embeddedIcon = SwitchTitleReader::cachedIcon(filePath, cacheKey, SwitchTitleReader::defaultCacheRoot());
        }
        if (fileName.endsWith(QStringLiteral(".nsp"), Qt::CaseInsensitive) &&
            isUpdateTitleId(titleId)) {
          continue;
        }
        QString title = cleanGameName(fileName);
        if (!titleId.isEmpty() && displayTitles.contains(titleId)) {
          title = displayTitles.value(titleId);
        } else if (!embeddedTitle.isEmpty()) {
          title = embeddedTitle;
        }
        if (title.isEmpty()) {
          title = titleId;
        }
        if (title.isEmpty()) {
          continue;
        }
        const QString recordKey =
            titleId.isEmpty() ? QStringLiteral("path:%1").arg(filePath) : titleId;
        if (seenGameIds.contains(recordKey)) {
          // Same title id already imported from another root; keep first.
          continue;
        }
        const QString cleanKey = normalizedTitle(title);
        const int existingIndex = indexByCleanTitle.value(cleanKey, -1);
        if (existingIndex >= 0 && existingIndex < result.games.size()) {
          const bool existingVersioned = hasVersionSuffix(result.games.at(existingIndex).path);
          const bool currentVersioned = hasVersionSuffix(fileName);
          if (currentVersioned && !existingVersioned) {
            continue;
          }
          if (!currentVersioned && existingVersioned) {
            const RyujinxGameRecord previous = result.games.at(existingIndex);
            seenPaths.remove(previous.path);
            seenGameIds.remove(previous.gameId);
            result.games.removeAt(existingIndex);
            for (auto it = indexByCleanTitle.begin(); it != indexByCleanTitle.end(); ++it) {
              if (it.value() > existingIndex) {
                it.value() -= 1;
              }
            }
          } else {
            continue;
          }
        }
        result.games.append(RyujinxGameRecord{
            .gameId = recordKey,
            .titleId = titleId,
            .title = title,
            .path = filePath,
            .coverPath = embeddedIcon.isEmpty() ? coverFor(root, titleId, filePath) : embeddedIcon,
            .playtimeSeconds = playtimeFor.value(titleId, 0),
            .lastPlayed = lastPlayedFor.value(titleId, 0),
            .flatpak = flatpak,
            .flatpakAppId = flatpakAppId});
        seenPaths.insert(filePath);
        seenGameIds.insert(recordKey);
        indexByCleanTitle.insert(cleanKey, result.games.size() - 1);
      }
    }
  }
  return result;
}

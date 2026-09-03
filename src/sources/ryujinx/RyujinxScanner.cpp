#include "sources/ryujinx/RyujinxScanner.h"

#include <QDateTime>
#include <QDir>
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

namespace {
constexpr qint64 kMaximumJsonBytes = 16 * 1024 * 1024;

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

// Strips update/DLC markers: "Game [0100...][v0].nsp" -> "0100...".
QString titleIdFromFilename(const QString& fileName) {
  static const QRegularExpression expression(
      QStringLiteral("\\[\\s*(0100[0-9A-Fa-f]{12})\\s*\\]"));
  const QRegularExpressionMatch match = expression.match(fileName);
  return match.hasMatch() ? match.captured(1).toUpper() : QString{};
}

QString cleanGameName(const QString& fileName) {
  QString name = QFileInfo(fileName).completeBaseName();
  name.remove(QRegularExpression(QStringLiteral("\\[\\s*[0-9A-Fa-f]{16}\\s*\\]")));
  name.remove(QRegularExpression(QStringLiteral("\\[\\s*v\\d+\\s*\\]")));
  return name.trimmed();
}

// Ryujinx covers: <root>/games/<titleId>/covers/{default,box,banner,icon}.jpg|png
QString coverFor(const QString& root, const QString& titleId) {
  const QDir base(root + QStringLiteral("/games/") + titleId + QStringLiteral("/covers"));
  for (const QString& stem :
       {QStringLiteral("box"), QStringLiteral("default"), QStringLiteral("banner")}) {
    for (const QString& extension :
         {QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png")}) {
      const QString candidate = base.filePath(stem + extension);
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
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
    const bool flatpak = root.contains(QStringLiteral("/.var/app/io.github.ryubing.Ryujinx/"));

    // Real installs: games/<titleId>/gui is a directory containing metadata.json
    // ("title", "timespan_played", "last_played_utc", plus the legacy
    // "time_played" and "last_played" keys).
    QHash<QString, QString> displayTitles;
    QHash<QString, qint64> playtimeFor;
    QHash<QString, qint64> lastPlayedFor;
    const QDir gamesDirectory(root + QStringLiteral("/games"));
    const QFileInfoList titleDirectories =
        gamesDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : titleDirectories) {
      const QString titleId = entry.fileName().toUpper();
      if (!validTitleId(titleId)) {
        continue;
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
          // Newer fields: timespan_played (seconds) and last_played_utc (ISO-8601).
          qint64 seconds =
              static_cast<qint64>(metadata.value(QStringLiteral("timespan_played")).toDouble(0));
          QString lastPlayedIso = metadata.value(QStringLiteral("last_played_utc")).toString();
          // Legacy fallbacks for older installs.
          if (seconds <= 0) {
            seconds = static_cast<qint64>(metadata.value(QStringLiteral("time_played")).toDouble(0));
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
      if (!metadataFile.exists() && guiFile.open(QIODevice::ReadOnly)) {
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
      if (timeFile.open(QIODevice::ReadOnly)) {
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
        const QString filePath = romIterator.next();
        const QString fileName = QFileInfo(filePath).fileName();
        if (!isGameFile(fileName) || seenPaths.contains(filePath)) {
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
        QString title = cleanGameName(fileName);
        if (!titleId.isEmpty() && displayTitles.contains(titleId)) {
          title = displayTitles.value(titleId);
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
        result.games.append(RyujinxGameRecord{
            .gameId = recordKey,
            .titleId = titleId,
            .title = title,
            .path = filePath,
            .coverPath = titleId.isEmpty() ? QString{} : coverFor(root, titleId),
            .playtimeSeconds = playtimeFor.value(titleId, 0),
            .lastPlayed = lastPlayedFor.value(titleId, 0),
            .flatpak = flatpak});
        seenPaths.insert(filePath);
        seenGameIds.insert(recordKey);
      }
    }
  }
  return result;
}

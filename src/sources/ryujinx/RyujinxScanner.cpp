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

    // Per-title metadata lives in games/<titleId>/gui ("TitleName") and
    // time_played ("playtime" seconds, "last_played" ISO-8601).
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
      QFile guiFile(entry.filePath() + QStringLiteral("/gui"));
      if (guiFile.open(QIODevice::ReadOnly)) {
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
    }

    if (gameDirectories.isEmpty()) {
      // Ryujinx shows an empty game list without configured directories.
      continue;
    }

    QSet<QString> seenPaths;
    for (const QString& configuredDirectory : gameDirectories) {
      QString expanded = configuredDirectory;
      if (expanded.startsWith(QStringLiteral("~/"))) {
        expanded.replace(0, 1, QDir::homePath());
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
        result.games.append(RyujinxGameRecord{
            .gameId = titleId.isEmpty() ? QStringLiteral("path:%1").arg(filePath) : titleId,
            .titleId = titleId,
            .title = title,
            .path = filePath,
            .coverPath = titleId.isEmpty() ? QString{} : coverFor(root, titleId),
            .playtimeSeconds = playtimeFor.value(titleId, 0),
            .lastPlayed = lastPlayedFor.value(titleId, 0),
            .flatpak = flatpak});
        seenPaths.insert(filePath);
      }
    }
  }
  return result;
}

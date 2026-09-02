#include "sources/retroarch/RetroArchScanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <limits>

namespace {
constexpr qint64 kMaximumJsonBytes = 16LL * 1024 * 1024;
constexpr qint64 kMaximumRuntimeBytes = 1LL * 1024 * 1024;
constexpr int kMaximumGames = 100000;

struct Paths {
  QString playlists;
  QString thumbnails;
  QString runtimeLogs;
  QStringList coreInfoDirectories;
};

const QString kFlatpakMarker = QStringLiteral("/.var/app/org.libretro.RetroArch/");

// The RetroArch Flatpak writes its own sandbox paths (/var/config, /var/data) into
// retroarch.cfg. Map them back to the host so playlists, thumbnails, and logs resolve.
QString hostPath(QString path, const QString& root) {
  const qsizetype marker = root.indexOf(kFlatpakMarker);
  if (marker < 0 || path.isEmpty()) {
    return path;
  }
  const QString appDirectory = root.left(marker + kFlatpakMarker.size() - 1);
  if (path.startsWith(QStringLiteral("/var/config/"))) {
    path.replace(0, 11, appDirectory + QStringLiteral("/config"));
  } else if (path.startsWith(QStringLiteral("/var/data/"))) {
    path.replace(0, 9, appDirectory + QStringLiteral("/data"));
  }
  return path;
}

QString expandedPath(QString path) {
  path = path.trimmed();
  if (path.startsWith(QStringLiteral("~/"))) {
    path.replace(0, 1, QDir::homePath());
  } else if (path.startsWith(QStringLiteral("$HOME/"))) {
    path.replace(0, 5, QDir::homePath());
  }
  return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

QString configValue(const QString& contents, const QString& key) {
  const QRegularExpression expression(
      QStringLiteral("(?m)^\\s*%1\\s*=\\s*\"([^\"\\r\\n]*)\"\\s*(?:#.*)?$")
          .arg(QRegularExpression::escape(key)));
  return expandedPath(expression.match(contents).captured(1).left(4096));
}

Paths pathsFor(const QString& root) {
  Paths paths{.playlists = root + QStringLiteral("/playlists"),
              .thumbnails = root + QStringLiteral("/thumbnails"),
              .runtimeLogs = {},
              .coreInfoDirectories = {}};
  QFile config(root + QStringLiteral("/retroarch.cfg"));
  if (config.open(QIODevice::ReadOnly | QIODevice::Text) && config.size() <= kMaximumJsonBytes) {
    const QString contents = QString::fromUtf8(config.readAll());
    const QString playlists =
        hostPath(configValue(contents, QStringLiteral("playlist_directory")), root);
    const QString thumbnails =
        hostPath(configValue(contents, QStringLiteral("thumbnails_directory")), root);
    paths.runtimeLogs =
        hostPath(configValue(contents, QStringLiteral("runtime_log_directory")), root);
    const QString info =
        hostPath(configValue(contents, QStringLiteral("libretro_info_path")), root);
    if (!playlists.isEmpty()) {
      paths.playlists = playlists;
    }
    if (!thumbnails.isEmpty()) {
      paths.thumbnails = thumbnails;
    }
    if (!info.isEmpty()) {
      paths.coreInfoDirectories.append(info);
    }
  }
  if (paths.runtimeLogs.isEmpty()) {
    paths.runtimeLogs = paths.playlists + QStringLiteral("/logs");
  }
  paths.coreInfoDirectories.append(root + QStringLiteral("/cores"));
  paths.coreInfoDirectories.append(QStringLiteral("/usr/share/libretro/info"));
  return paths;
}

// RetroArch names runtime log folders after the short core name from the core's .info file,
// while playlists store the long display name. Resolve every candidate we can.
QStringList coreNameCandidates(const Paths& paths, const QString& corePath, const QString& coreName,
                               QHash<QString, QString>* infoCache) {
  QStringList candidates;
  const auto add = [&candidates](const QString& value) {
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !candidates.contains(trimmed)) {
      candidates.append(trimmed);
    }
  };
  const QString coreBase = QFileInfo(corePath).completeBaseName();
  if (!coreBase.isEmpty()) {
    if (!infoCache->contains(coreBase)) {
      QString resolved;
      static const QRegularExpression coreNameLine(
          QStringLiteral("(?m)^\\s*corename\\s*=\\s*\"([^\"\\r\\n]+)\""));
      for (const QString& directory : paths.coreInfoDirectories) {
        QFile info(directory + QLatin1Char('/') + coreBase + QStringLiteral(".info"));
        if (!info.open(QIODevice::ReadOnly | QIODevice::Text) ||
            info.size() > kMaximumRuntimeBytes) {
          continue;
        }
        resolved = coreNameLine.match(QString::fromUtf8(info.readAll())).captured(1);
        if (!resolved.isEmpty()) {
          break;
        }
      }
      infoCache->insert(coreBase, resolved);
    }
    add(infoCache->value(coreBase));
  }
  static const QRegularExpression parenthesized(QStringLiteral("\\(([^()]+)\\)\\s*$"));
  add(parenthesized.match(coreName).captured(1));
  add(coreName);
  return candidates;
}

QString sanitizedThumbnailName(QString name) {
  static const QRegularExpression invalid(QStringLiteral("[&*/:`<>?\\\\|]"));
  return name.replace(invalid, QStringLiteral("_")).trimmed();
}

QString shortenedLabel(QString label) {
  static const QRegularExpression suffix(QStringLiteral("\\s*(?:\\([^)]*\\)|\\[[^]]*\\])\\s*$"));
  while (suffix.match(label).hasMatch()) {
    label.remove(suffix).replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  }
  return label.trimmed();
}

QString artworkPath(const QString& thumbnails, const QString& playlist, const QString& directory,
                    const QStringList& names) {
  for (const QString& rawName : names) {
    const QString name = sanitizedThumbnailName(rawName);
    if (name.isEmpty()) {
      continue;
    }
    const QString base = thumbnails + QLatin1Char('/') + playlist + QLatin1Char('/') + directory +
                         QLatin1Char('/') + name;
    for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                     QStringLiteral(".jpeg"), QStringLiteral(".webp")}) {
      if (QFileInfo(base + extension).isFile()) {
        return base + extension;
      }
    }
  }
  return {};
}

QString runtimeFileName(const QString& contentPath) {
  // Archived content is stored as archive.zip#inner.rom and RetroArch logs the inner name.
  const qsizetype archive = contentPath.lastIndexOf(QLatin1Char('#'));
  const QString fileName =
      QFileInfo(archive >= 0 ? contentPath.mid(archive + 1) : contentPath).fileName();
  const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
  return (dot > 0 ? fileName.left(dot) : fileName) + QStringLiteral(".lrtl");
}

void readRuntime(const Paths& paths, const QString& contentPath, const QStringList& coreNames,
                 qint64* seconds, qint64* lastPlayed) {
  const QString fileName = runtimeFileName(contentPath);
  QStringList candidates;
  for (const QString& coreName : coreNames) {
    candidates.append(paths.runtimeLogs + QLatin1Char('/') + coreName + QLatin1Char('/') +
                      fileName);
  }
  candidates.append(paths.runtimeLogs + QLatin1Char('/') + fileName);
  for (const QString& path : candidates) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumRuntimeBytes) {
      continue;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      continue;
    }
    const QJsonObject runtime = document.object();
    const QStringList parts = runtime.value(QStringLiteral("runtime")).toString().split(':');
    if (parts.size() == 3) {
      bool hoursOkay = false;
      bool minutesOkay = false;
      bool secondsOkay = false;
      const qint64 hours = parts.at(0).toLongLong(&hoursOkay);
      const qint64 minutes = parts.at(1).toLongLong(&minutesOkay);
      const qint64 remainder = parts.at(2).toLongLong(&secondsOkay);
      if (hoursOkay && minutesOkay && secondsOkay && hours >= 0 &&
          hours <= (std::numeric_limits<qint64>::max() - 3599) / 3600 && minutes >= 0 &&
          minutes < 60 && remainder >= 0 && remainder < 60) {
        *seconds = hours * 3600 + minutes * 60 + remainder;
      }
    }
    const QDateTime date =
        QDateTime::fromString(runtime.value(QStringLiteral("last_played")).toString(),
                              QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (date.isValid()) {
      *lastPlayed = date.toSecsSinceEpoch();
    }
    return;
  }
}

bool ignoredPlaylist(const QString& name) {
  static const QSet<QString> ignored = {
      QStringLiteral("content_history"), QStringLiteral("content_favorites"),
      QStringLiteral("content_music_history"), QStringLiteral("content_video_history"),
      QStringLiteral("content_image_history")};
  return ignored.contains(name.toLower());
}

QString identityFor(const QString& contentPath) {
  return QString::fromLatin1(
      QCryptographicHash::hash(contentPath.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace

QStringList RetroArchScanner::discoverRoots() {
  const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString home = QDir::homePath();
  QStringList candidates = {
      config + QStringLiteral("/retroarch"),
      home + QStringLiteral("/.var/app/org.libretro.RetroArch/config/retroarch")};
  candidates.removeDuplicates();
  QStringList roots;
  for (const QString& root : candidates) {
    const Paths paths = pathsFor(root);
    if (QFileInfo(root + QStringLiteral("/retroarch.cfg")).isFile() ||
        QDir(paths.playlists).exists()) {
      roots.append(root);
    }
  }
  return roots;
}

RetroArchScanResult RetroArchScanner::scan(const QStringList& roots) {
  RetroArchScanResult result;
  QSet<QString> contentPaths;
  QHash<QString, QString> infoCache;
  for (const QString& root : roots) {
    const Paths paths = pathsFor(root);
    QDir playlistDirectory(paths.playlists);
    if (!playlistDirectory.exists()) {
      continue;
    }
    result.roots.append(root);
    const bool flatpak = root.contains(kFlatpakMarker);
    const QFileInfoList playlists = playlistDirectory.entryInfoList(
        {QStringLiteral("*.lpl")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& playlistInfo : playlists) {
      const QString playlistName = playlistInfo.completeBaseName();
      if (ignoredPlaylist(playlistName)) {
        continue;
      }
      QFile file(playlistInfo.absoluteFilePath());
      if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumJsonBytes) {
        result.incomplete = true;
        result.warnings.append(
            QStringLiteral("Could not read %1").arg(playlistInfo.absoluteFilePath()));
        continue;
      }
      QJsonParseError error;
      const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
      if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.incomplete = true;
        result.warnings.append(QStringLiteral("Could not parse %1: %2")
                                   .arg(playlistInfo.absoluteFilePath(), error.errorString()));
        continue;
      }
      const QJsonObject playlist = document.object();
      const QString defaultCorePath =
          playlist.value(QStringLiteral("default_core_path")).toString().left(4096);
      const QString defaultCoreName =
          playlist.value(QStringLiteral("default_core_name")).toString().left(512);
      for (const QJsonValue value : playlist.value(QStringLiteral("items")).toArray()) {
        if (result.games.size() >= kMaximumGames) {
          result.incomplete = true;
          result.warnings.append(
              QStringLiteral("RetroArch library exceeds %1 games").arg(kMaximumGames));
          return result;
        }
        const QJsonObject item = value.toObject();
        const QString contentPath =
            hostPath(expandedPath(item.value(QStringLiteral("path")).toString().left(4096)), root);
        const QString title = item.value(QStringLiteral("label")).toString().trimmed().left(512);
        if (contentPath.isEmpty() || title.isEmpty() || contentPaths.contains(contentPath)) {
          continue;
        }
        QString corePath = item.value(QStringLiteral("core_path")).toString().trimmed().left(4096);
        QString coreName = item.value(QStringLiteral("core_name")).toString().trimmed().left(512);
        if (corePath.isEmpty() || corePath == QStringLiteral("DETECT")) {
          corePath = defaultCorePath;
          if (coreName.isEmpty() || coreName == QStringLiteral("DETECT")) {
            coreName = defaultCoreName;
          }
        }
        if (corePath == QStringLiteral("DETECT")) {
          corePath.clear();
        }
        if (coreName == QStringLiteral("DETECT")) {
          coreName.clear();
        }
        QString database = item.value(QStringLiteral("db_name")).toString().trimmed().left(512);
        if (database.endsWith(QStringLiteral(".lpl"), Qt::CaseInsensitive)) {
          database.chop(4);
        }
        const QString artworkPlaylist = database.isEmpty() ? playlistName : database;
        const QFileInfo contentInfo(contentPath);
        const QStringList artworkNames = {contentInfo.completeBaseName(), title,
                                          shortenedLabel(title)};
        qint64 playtimeSeconds = 0;
        qint64 lastPlayed = 0;
        readRuntime(paths, contentPath, coreNameCandidates(paths, corePath, coreName, &infoCache),
                    &playtimeSeconds, &lastPlayed);
        result.games.append(
            {.gameId = identityFor(contentPath),
             .title = title,
             .contentPath = contentPath,
             .corePath = expandedPath(corePath),
             .coreName = coreName,
             .coverPath = artworkPath(paths.thumbnails, artworkPlaylist,
                                      QStringLiteral("Named_Boxarts"), artworkNames),
             .heroPath = artworkPath(paths.thumbnails, artworkPlaylist,
                                     QStringLiteral("Named_Snaps"), artworkNames),
             .system = database.isEmpty() ? playlistName : database,
             .playtimeSeconds = playtimeSeconds,
             .lastPlayed = lastPlayed,
             .flatpak = flatpak});
        contentPaths.insert(contentPath);
      }
    }
  }
  return result;
}

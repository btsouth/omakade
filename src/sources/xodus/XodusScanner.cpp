#include "sources/xodus/XodusScanner.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Nexus keeps each cover inline as a data URL, so the file grows with the library.
constexpr qint64 kMaximumStateBytes = 256 * 1024 * 1024;
constexpr qint64 kMaximumCoverBytes = 16 * 1024 * 1024;
constexpr qint64 kMaximumScriptBytes = 64 * 1024;

QString profileFromConfig(const QString& config) {
  return QDir::cleanPath(config + QStringLiteral("/xodus-nexus"));
}

// A group named after this user with nobody else in it, as distributions that give each
// user a private group (and a umask of 002) create.
bool ownPrivateGroup(gid_t gid) {
  const passwd* user = ::getpwuid(::getuid());
  const group* entry = ::getgrgid(gid);
  return user != nullptr && entry != nullptr && qstrcmp(user->pw_name, entry->gr_name) == 0 &&
         (entry->gr_mem == nullptr || entry->gr_mem[0] == nullptr);
}

// Owned by this user and writable by nobody else, so what it says is what Nexus wrote.
bool privateEntry(const QString& path, bool directory) {
  struct stat info{};
  if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
    return false;
  }
  const bool kind = directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
  const bool groupWritable = (info.st_mode & S_IWGRP) != 0 && !ownPrivateGroup(info.st_gid);
  return kind && info.st_uid == ::getuid() && (info.st_mode & S_IWOTH) == 0 && !groupWritable;
}

QString coverExtension(const QString& mime) {
  if (mime == QStringLiteral("image/png"))
    return QStringLiteral(".png");
  if (mime == QStringLiteral("image/jpeg") || mime == QStringLiteral("image/jpg"))
    return QStringLiteral(".jpg");
  if (mime == QStringLiteral("image/webp"))
    return QStringLiteral(".webp");
  return {};
}

// Writes the inline cover to the cache, keeping an identical file untouched.
QString cachedCover(const QString& cache, const QString& gameId, const QString& dataUrl,
                    QStringList* warnings) {
  static const QRegularExpression header(QStringLiteral("^data:(image/[a-z]+);base64,"));
  const QRegularExpressionMatch match = header.match(dataUrl.left(64));
  if (cache.isEmpty() || !match.hasMatch()) {
    return {};
  }
  const QString extension = coverExtension(match.captured(1));
  if (extension.isEmpty()) {
    return {};
  }
  // Base64 is a third larger than the image, so an oversized cover is skipped undecoded.
  if (dataUrl.size() - match.capturedLength() > kMaximumCoverBytes / 3 * 4 + 4) {
    warnings->append(QStringLiteral("The cover stored for %1 is too large").arg(gameId));
    return {};
  }
  const auto decoded = QByteArray::fromBase64Encoding(
      dataUrl.mid(match.capturedLength()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  const QByteArray bytes = decoded ? *decoded : QByteArray{};
  if (bytes.isEmpty() || bytes.size() > kMaximumCoverBytes || QImage::fromData(bytes).isNull()) {
    warnings->append(QStringLiteral("The cover stored for %1 could not be read").arg(gameId));
    return {};
  }
  const QString path = cache + QLatin1Char('/') + gameId + QStringLiteral("-cover") + extension;
  QFile existing(path);
  if (existing.size() == bytes.size() && existing.open(QIODevice::ReadOnly) &&
      QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256) ==
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)) {
    return path;
  }
  existing.close();
  QDir().mkpath(cache);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    warnings->append(QStringLiteral("Could not cache the cover for %1").arg(gameId));
    return {};
  }
  return path;
}

QString prefixFor(const QString& dataRoot, const QString& gameId) {
  const QString proton =
      dataRoot + QStringLiteral("/compatdata/") + gameId + QStringLiteral("/pfx");
  if (QFileInfo(proton).isDir()) {
    return QDir::cleanPath(proton);
  }
  const QString legacy = dataRoot + QStringLiteral("/prefixes/") + gameId;
  return QFileInfo(legacy).isDir() ? QDir::cleanPath(legacy) : QString{};
}
} // namespace

bool XodusScanner::validGameId(const QString& gameId) {
  static const QRegularExpression valid(QStringLiteral("^[a-f0-9]{16}$"));
  return valid.match(gameId).hasMatch();
}

QUrl XodusScanner::safeArtworkUrl(const QString& value) {
  const QUrl url(value, QUrl::StrictMode);
  if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() ||
      !url.userInfo().isEmpty()) {
    return {};
  }
  return url;
}

bool XodusScanner::validStoreId(const QString& storeId) {
  static const QRegularExpression valid(QStringLiteral("^[0-9A-Z]{12}$"));
  return valid.match(storeId).hasMatch();
}

QUrl XodusScanner::catalogUrl(const QStringList& storeIds) {
  QStringList ids;
  for (const QString& id : storeIds) {
    if (validStoreId(id) && !ids.contains(id) && ids.size() < 20) {
      ids.append(id);
    }
  }
  if (ids.isEmpty()) {
    return {};
  }
  return QUrl(QStringLiteral("https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=%1"
                             "&market=US&languages=en-US")
                  .arg(ids.join(QLatin1Char(','))));
}

QHash<QString, XodusStoreArt> XodusScanner::parseCatalog(const QByteArray& json) {
  QHash<QString, XodusStoreArt> art;
  // Store image URLs take a width, which keeps downloads near the size shown.
  const auto image = [](const QString& uri, int width) -> QUrl {
    QUrl url =
        safeArtworkUrl(uri.startsWith(QStringLiteral("//")) ? QStringLiteral("https:") + uri : uri);
    if (url.isEmpty() || url.host() != QStringLiteral("store-images.s-microsoft.com")) {
      return {};
    }
    url.setQuery(QStringLiteral("w=%1").arg(width));
    return url;
  };
  const QJsonArray products =
      QJsonDocument::fromJson(json).object().value(QStringLiteral("Products")).toArray();
  for (const QJsonValue& value : products) {
    const QJsonObject product = value.toObject();
    const QString storeId = product.value(QStringLiteral("ProductId")).toString();
    const QJsonArray localized = product.value(QStringLiteral("LocalizedProperties")).toArray();
    if (!validStoreId(storeId) || localized.isEmpty()) {
      continue;
    }
    XodusStoreArt entry;
    QUrl titledHero;
    for (const QJsonValue& item :
         localized.first().toObject().value(QStringLiteral("Images")).toArray()) {
      const QJsonObject picture = item.toObject();
      const QString purpose = picture.value(QStringLiteral("ImagePurpose")).toString();
      const QString uri = picture.value(QStringLiteral("Uri")).toString();
      if (purpose == QStringLiteral("Poster") && entry.poster.isEmpty()) {
        entry.poster = image(uri, 600);
      } else if (purpose == QStringLiteral("SuperHeroArt") && entry.hero.isEmpty()) {
        entry.hero = image(uri, 1920);
      } else if (purpose == QStringLiteral("TitledHeroArt") && titledHero.isEmpty()) {
        titledHero = image(uri, 1920);
      }
    }
    if (entry.hero.isEmpty()) {
      entry.hero = titledHero;
    }
    if (!entry.poster.isEmpty() || !entry.hero.isEmpty()) {
      art.insert(storeId, entry);
    }
  }
  return art;
}

QString XodusScanner::launchScript(const QString& givenRoot, bool game) {
  if (givenRoot.isEmpty() || !QDir::isAbsolutePath(givenRoot)) {
    return {};
  }
  // The profile itself may be a link, as dotfile managers make; below it nothing may be.
  const QString profileRoot = QFileInfo(givenRoot).canonicalFilePath();
  if (profileRoot.isEmpty()) {
    return {};
  }
  const QString directory = QDir::cleanPath(profileRoot + QStringLiteral("/shortcuts"));
  const QString script =
      directory + (game ? QStringLiteral("/launch-game") : QStringLiteral("/launch-nexus"));
  if (!privateEntry(profileRoot, true) || !privateEntry(directory, true) ||
      !privateEntry(script, false) || !QFileInfo(script).isExecutable()) {
    return {};
  }
  QFile file(script);
  if (file.size() > kMaximumScriptBytes || !file.open(QIODevice::ReadOnly)) {
    return {};
  }
  // Nexus marks the scripts it writes; anything else in that place is not run.
  const QList<QByteArray> lines = file.readAll().split('\n');
  if (lines.size() < 3 || lines.at(0) != QByteArrayLiteral("#!/bin/sh") ||
      !lines.at(1).startsWith(QByteArrayLiteral("# X-Xodus-Nexus-Profile="))) {
    return {};
  }
  return script;
}

QStringList XodusScanner::discoverRoots() {
  QStringList candidates;
  const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (!config.isEmpty()) {
    candidates.append(profileFromConfig(config));
  }
  candidates.append(profileFromConfig(QDir::homePath() + QStringLiteral("/.config")));
  // Nexus uses one profile, the first of these; launching uses the same one.
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/launcher-state.json")).isFile()) {
      return {root};
    }
  }
  return {};
}

XodusScanResult XodusScanner::scan(const QStringList& roots, const QString& artworkCache) {
  XodusScanResult result;
  QSet<QString> gameIds;
  for (const QString& root : roots) {
    const QString statePath = root + QStringLiteral("/launcher-state.json");
    QFile file(statePath);
    if (!file.exists()) {
      continue;
    }
    result.roots.append(root);
    if (file.size() > kMaximumStateBytes || !file.open(QIODevice::ReadOnly)) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(statePath));
      continue;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value(QStringLiteral("games")).isArray()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not parse %1: %2")
                                 .arg(statePath, error.error != QJsonParseError::NoError
                                                     ? error.errorString()
                                                     : QStringLiteral("no game list")));
      continue;
    }
    const QJsonObject state = document.object();
    const QString runtimeRoot = state.value(QStringLiteral("settings"))
                                    .toObject()
                                    .value(QStringLiteral("runtimeRoot"))
                                    .toString();
    const QString dataRoot =
        QDir::isAbsolutePath(runtimeRoot)
            ? QFileInfo(QDir::cleanPath(runtimeRoot)).absolutePath()
            : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                  QStringLiteral("/xodus-nexus");
    for (const QJsonValue& value : state.value(QStringLiteral("games")).toArray()) {
      const QJsonObject game = value.toObject();
      const QString gameId = game.value(QStringLiteral("id")).toString();
      const QString title = game.value(QStringLiteral("title")).toString().trimmed();
      const QString path = game.value(QStringLiteral("path")).toString();
      // Nexus starts only ready games from a shortcut; anything else is mid-install.
      if (!validGameId(gameId) || title.isEmpty() || !QDir::isAbsolutePath(path) ||
          game.value(QStringLiteral("installState")).toString() != QStringLiteral("ready") ||
          gameIds.contains(gameId)) {
        continue;
      }
      result.games.append(
          {.gameId = gameId,
           .title = title,
           .publisher = game.value(QStringLiteral("publisher")).toString().trimmed(),
           .storeId = game.value(QStringLiteral("storeId")).toString().trimmed(),
           .installPath = QDir::cleanPath(path),
           .executable = game.value(QStringLiteral("executable")).toString(),
           .winePrefix = prefixFor(dataRoot, gameId),
           .coverPath =
               cachedCover(artworkCache, gameId,
                           game.value(QStringLiteral("coverDataUrl")).toString(), &result.warnings),
           .heroUrl = safeArtworkUrl(game.value(QStringLiteral("heroUrl")).toString()),
           .antiCheat = game.value(QStringLiteral("antiCheat")).isObject()});
      gameIds.insert(gameId);
    }
  }
  return result;
}

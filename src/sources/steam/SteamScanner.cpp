#include "sources/steam/SteamScanner.h"

#include "sources/steam/ValveKeyValues.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr qint64 kMaximumAchievementCacheBytes = 16LL * 1024 * 1024;

struct Activity {
  qint64 lastPlayed = 0;
  int playtimeMinutes = 0;
};

struct AchievementCache {
  int unlocked = 0;
  int total = 0;
  QVector<SteamAchievementRecord> achievements;
};

QString cleanPath(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Steam gives every library the shape <library>/steamapps, so a game's install
// path says which library it belongs to.
QString libraryFromInstallPath(const QString& installPath) {
  const QString path = cleanPath(installPath);
  const qsizetype at = path.indexOf(QStringLiteral("/steamapps/"));
  return at > 0 ? path.left(at) : QString{};
}

bool isSteamAppId(const QString& appId) {
  // Anchored at both ends so a trailing newline or any other trailing character
  // cannot slip through into the path segment.
  static const QRegularExpression digits(QStringLiteral("\\A[0-9]+\\z"));
  return digits.match(appId.trimmed()).hasMatch();
}

QString firstMatchingFile(const QString& directory, const QStringList& filters) {
  const QDir dir(directory);
  for (const QString& filter : filters) {
    const QStringList matches = dir.entryList({filter}, QDir::Files, QDir::Name);
    if (!matches.isEmpty()) {
      return dir.absoluteFilePath(matches.first());
    }
  }
  return {};
}

QString firstMatchingFileRecursively(const QString& directory, const QString& filename) {
  QDirIterator iterator(directory, {filename}, QDir::Files, QDirIterator::Subdirectories);
  return iterator.hasNext() ? iterator.next() : QString{};
}

const ValveKeyValues* descend(const ValveKeyValues& root, const QStringList& path) {
  const ValveKeyValues* current = &root;
  for (const QString& part : path) {
    current = current->object(part);
    if (current == nullptr) {
      return nullptr;
    }
  }
  return current;
}

QHash<QString, Activity> readActivity(const QStringList& roots) {
  QHash<QString, Activity> result;
  for (const QString& root : roots) {
    QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString& user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      ValveKeyValues values;
      if (!ValveKeyValuesParser::parseFile(
              userdata.absoluteFilePath(user + QStringLiteral("/config/localconfig.vdf")),
              &values)) {
        continue;
      }
      const ValveKeyValues* apps = descend(
          values, {QStringLiteral("UserLocalConfigStore"), QStringLiteral("Software"),
                   QStringLiteral("Valve"), QStringLiteral("Steam"), QStringLiteral("apps")});
      if (apps == nullptr) {
        continue;
      }
      for (auto iterator = apps->objects.cbegin(); iterator != apps->objects.cend(); ++iterator) {
        bool numeric = false;
        iterator.key().toULongLong(&numeric);
        if (!numeric) {
          continue;
        }
        Activity& activity = result[iterator.key()];
        activity.lastPlayed = qMax(
            activity.lastPlayed, iterator.value().value(QStringLiteral("LastPlayed")).toLongLong());
        activity.playtimeMinutes = qMax(activity.playtimeMinutes,
                                        iterator.value().value(QStringLiteral("Playtime")).toInt());
      }
    }
  }
  return result;
}

void mergeAchievementArray(const QJsonArray& source, bool hidden,
                           QVector<SteamAchievementRecord>* destination, QSet<QString>* seen) {
  for (const QJsonValue& value : source) {
    const QJsonObject object = value.toObject();
    const QString apiName = object.value(QStringLiteral("strID")).toString();
    if (apiName.isEmpty() || seen->contains(apiName)) {
      continue;
    }
    seen->insert(apiName);
    destination->append({
        .apiName = apiName,
        .title = object.value(QStringLiteral("strName")).toString(),
        .description = object.value(QStringLiteral("strDescription")).toString(),
        .iconUrl = object.value(QStringLiteral("strImage")).toString(),
        .unlocked = object.value(QStringLiteral("bAchieved")).toBool(),
        .unlockTime = object.value(QStringLiteral("rtUnlocked")).toInteger(),
        .rarity = object.value(QStringLiteral("flAchieved")).toDouble(),
        .hidden = hidden || object.value(QStringLiteral("bHidden")).toBool(),
        .currentProgress = object.value(QStringLiteral("flCurrentProgress")).toDouble(),
        .maximumProgress = object.value(QStringLiteral("flMaxProgress")).toDouble(),
    });
  }
}

AchievementCache parseAchievementFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumAchievementCacheBytes) {
    return {};
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (!document.isArray()) {
    return {};
  }
  for (const QJsonValue& entryValue : document.array()) {
    const QJsonArray entry = entryValue.toArray();
    if (entry.size() != 2 || entry.at(0).toString() != QStringLiteral("achievements")) {
      continue;
    }
    const QJsonObject data = entry.at(1).toObject().value(QStringLiteral("data")).toObject();
    AchievementCache cache;
    cache.unlocked = data.value(QStringLiteral("nAchieved")).toInt();
    cache.total = data.value(QStringLiteral("nTotal")).toInt();
    QSet<QString> seen;
    mergeAchievementArray(data.value(QStringLiteral("vecHighlight")).toArray(), false,
                          &cache.achievements, &seen);
    mergeAchievementArray(data.value(QStringLiteral("vecUnachieved")).toArray(), false,
                          &cache.achievements, &seen);
    mergeAchievementArray(data.value(QStringLiteral("vecAchievedHidden")).toArray(), true,
                          &cache.achievements, &seen);
    return cache;
  }
  return {};
}

QHash<QString, AchievementCache> readAchievementCaches(const QStringList& roots) {
  QHash<QString, AchievementCache> result;
  for (const QString& root : roots) {
    QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString& user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString cachePath =
          userdata.absoluteFilePath(user + QStringLiteral("/config/librarycache"));
      QDir cacheDirectory(cachePath);
      for (const QString& filename :
           cacheDirectory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString appId = QFileInfo(filename).completeBaseName();
        bool numeric = false;
        appId.toULongLong(&numeric);
        if (!numeric || filename == QStringLiteral("achievement_progress.json")) {
          continue;
        }
        const AchievementCache cache =
            parseAchievementFile(cacheDirectory.absoluteFilePath(filename));
        AchievementCache& current = result[appId];
        if (cache.achievements.size() > current.achievements.size()) {
          current.achievements = cache.achievements;
        }
        current.unlocked = qMax(current.unlocked, cache.unlocked);
        current.total = qMax(current.total, cache.total);
      }

      QFile progressFile(
          cacheDirectory.absoluteFilePath(QStringLiteral("achievement_progress.json")));
      if (!progressFile.open(QIODevice::ReadOnly) ||
          progressFile.size() > kMaximumAchievementCacheBytes) {
        continue;
      }
      const QJsonArray map = QJsonDocument::fromJson(progressFile.readAll())
                                 .object()
                                 .value(QStringLiteral("mapCache"))
                                 .toArray();
      for (const QJsonValue& entryValue : map) {
        const QJsonArray entry = entryValue.toArray();
        if (entry.size() != 2) {
          continue;
        }
        const QString appId = QString::number(entry.at(0).toInteger());
        const QJsonObject summary = entry.at(1).toObject();
        AchievementCache& current = result[appId];
        current.unlocked =
            qMax(current.unlocked, summary.value(QStringLiteral("unlocked")).toInt());
        current.total = qMax(current.total, summary.value(QStringLiteral("total")).toInt());
      }
    }
  }
  return result;
}

void resolveArtwork(SteamGameRecord* game, const QStringList& steamRoots,
                    QHash<QString, QStringList>* gridListings);

QString unquotePath(const QString& value) {
  const QString trimmed = value.trimmed();
  if (trimmed.size() >= 2 && trimmed.front() == QLatin1Char('"') &&
      trimmed.back() == QLatin1Char('"')) {
    return trimmed.mid(1, trimmed.size() - 2);
  }
  return trimmed;
}

void importNonSteamShortcuts(const QStringList& steamRoots, const QHash<QString, Activity>& activity,
                             QHash<QString, QStringList>* gridListings, QSet<QString>* importedIds,
                             SteamScanResult* result) {
  for (const QString& root : steamRoots) {
    QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString& user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString shortcutsPath =
          userdata.absoluteFilePath(user + QStringLiteral("/config/shortcuts.vdf"));
      if (!QFileInfo::exists(shortcutsPath)) {
        continue;
      }
      ValveKeyValues parsed;
      QString error;
      if (!ValveKeyValuesParser::parseBinaryFile(shortcutsPath, &parsed, &error)) {
        result->warnings.append(
            QStringLiteral("Could not read %1: %2").arg(shortcutsPath, error));
        result->unreadableManifests.append(shortcutsPath);
        continue;
      }
      const ValveKeyValues* shortcuts = parsed.object(QStringLiteral("shortcuts"));
      if (shortcuts == nullptr) {
        continue;
      }
      for (auto iterator = shortcuts->objects.cbegin(); iterator != shortcuts->objects.cend();
           ++iterator) {
        const ValveKeyValues& entry = iterator.value();
        const QString appId = entry.value(QStringLiteral("appid"));
        const QString name = entry.value(QStringLiteral("AppName")).trimmed();
        bool numeric = false;
        appId.toULongLong(&numeric);
        if (!numeric || appId == QStringLiteral("0") || name.isEmpty() ||
            SteamScanner::isToolTitle(name) || importedIds->contains(appId)) {
          continue;
        }

        const QString exe = unquotePath(entry.value(QStringLiteral("Exe")));
        QString startDir = unquotePath(entry.value(QStringLiteral("StartDir")));
        if (startDir.isEmpty() && !exe.isEmpty()) {
          startDir = QFileInfo(exe).absolutePath();
        }
        const QString icon = unquotePath(entry.value(QStringLiteral("icon")));
        const Activity gameActivity = activity.value(appId);
        SteamGameRecord game{
            .appId = appId,
            .title = name,
            .installDirectory = startDir,
            .libraryPath = cleanPath(root),
            .manifestPath = shortcutsPath,
            .coverPath = {},
            .heroPath = {},
            .logoPath = {},
            .lastPlayed = qMax(gameActivity.lastPlayed, entry.value(QStringLiteral("LastPlayTime"))
                                                            .toLongLong()),
            .playtimeMinutes = gameActivity.playtimeMinutes,
            .achievementsUnlocked = 0,
            .achievementsTotal = 0,
            .achievements = {},
        };
        resolveArtwork(&game, steamRoots, gridListings);
        if (game.coverPath.isEmpty() && !icon.isEmpty() && QFileInfo::exists(icon)) {
          game.coverPath = icon;
        }
        result->games.append(game);
        importedIds->insert(appId);
      }
    }
  }
}

QStringList libraryPaths(const QString& steamRoot, QStringList* warnings, bool* incomplete) {
  QStringList paths{steamRoot};
  QString libraryFile = steamRoot + QStringLiteral("/config/libraryfolders.vdf");
  if (!QFileInfo::exists(libraryFile)) {
    libraryFile = steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf");
  }

  ValveKeyValues root;
  QString error;
  if (!ValveKeyValuesParser::parseFile(libraryFile, &root, &error)) {
    warnings->append(QStringLiteral("Could not read %1: %2").arg(libraryFile, error));
    *incomplete = true;
    return paths;
  }
  const ValveKeyValues* folders = root.object(QStringLiteral("libraryfolders"));
  if (folders == nullptr) {
    folders = &root;
  }
  for (auto iterator = folders->values.cbegin(); iterator != folders->values.cend(); ++iterator) {
    bool numeric = false;
    iterator.key().toInt(&numeric);
    if (numeric && !iterator.value().isEmpty()) {
      paths.append(cleanPath(iterator.value()));
    }
  }
  for (auto iterator = folders->objects.cbegin(); iterator != folders->objects.cend(); ++iterator) {
    bool numeric = false;
    iterator.key().toInt(&numeric);
    const QString path = iterator.value().value(QStringLiteral("path"));
    if (numeric && !path.isEmpty()) {
      paths.append(cleanPath(path));
    }
  }
  paths.removeDuplicates();
  return paths;
}

// Custom artwork folders can hold thousands of files, so each grid directory is listed once
// per scan and matched in memory instead of three times per game.
QString firstCachedMatch(QHash<QString, QStringList>* listings, const QString& directory,
                         const QString& appId, const QString& filter) {
  if (!listings->contains(directory)) {
    listings->insert(directory, QDir(directory).entryList(QDir::Files, QDir::Name));
  }
  for (const QString& name : listings->value(directory)) {
    if (name.startsWith(appId) && QDir::match(filter, name)) {
      return QDir(directory).absoluteFilePath(name);
    }
  }
  return {};
}

void resolveArtwork(SteamGameRecord* game, const QStringList& steamRoots,
                    QHash<QString, QStringList>* gridListings) {
  for (const QString& root : steamRoots) {
    QDir userdata(root + QStringLiteral("/userdata"));
    for (const QString& user : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString grid = userdata.absoluteFilePath(user + QStringLiteral("/config/grid"));
      if (game->coverPath.isEmpty()) {
        game->coverPath =
            firstCachedMatch(gridListings, grid, game->appId, game->appId + QStringLiteral("p.*"));
      }
      if (game->heroPath.isEmpty()) {
        game->heroPath = firstCachedMatch(gridListings, grid, game->appId,
                                          game->appId + QStringLiteral("_hero.*"));
      }
      if (game->logoPath.isEmpty()) {
        game->logoPath = firstCachedMatch(gridListings, grid, game->appId,
                                          game->appId + QStringLiteral("_logo.*"));
      }
    }
  }
  for (const QString& root : steamRoots) {
    const QString cache = root + QStringLiteral("/appcache/librarycache/") + game->appId;
    if (game->coverPath.isEmpty()) {
      game->coverPath = firstMatchingFile(cache, {QStringLiteral("library_600x900.*")});
    }
    if (game->coverPath.isEmpty()) {
      game->coverPath = firstMatchingFileRecursively(cache, QStringLiteral("library_capsule.jpg"));
    }
    if (game->heroPath.isEmpty()) {
      game->heroPath =
          firstMatchingFile(cache, {QStringLiteral("library_hero.*"), QStringLiteral("header.*")});
    }
    if (game->heroPath.isEmpty()) {
      game->heroPath = firstMatchingFileRecursively(cache, QStringLiteral("library_hero.jpg"));
    }
    if (game->logoPath.isEmpty()) {
      game->logoPath = firstMatchingFile(cache, {QStringLiteral("logo.*")});
    }
    if (game->logoPath.isEmpty()) {
      game->logoPath = firstMatchingFileRecursively(cache, QStringLiteral("logo.png"));
    }
  }
}
} // namespace

bool SteamScanner::isToolTitle(const QString& name) {
  const QString normalized = name.trimmed().toLower();
  return normalized.startsWith(QStringLiteral("proton ")) ||
         normalized.startsWith(QStringLiteral("steam linux runtime")) ||
         normalized.startsWith(QStringLiteral("steamworks common redistributables")) ||
         normalized.startsWith(QStringLiteral("steam runtime"));
}

QStringList SteamScanner::discoverSteamRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      home + QStringLiteral("/.local/share/Steam"),
      home + QStringLiteral("/.steam/steam"),
      home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam"),
  };
  QStringList roots;
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate + QStringLiteral("/steamapps"))) {
      const QString canonical = QFileInfo(candidate).canonicalFilePath();
      roots.append(canonical.isEmpty() ? cleanPath(candidate) : canonical);
    }
  }
  roots.removeDuplicates();
  return roots;
}

SteamScanResult SteamScanner::scan(const QStringList& steamRoots) {
  SteamScanResult result;
  result.steamRoots = steamRoots;
  const QHash<QString, Activity> activity = readActivity(steamRoots);
  const QHash<QString, AchievementCache> achievementCaches = readAchievementCaches(steamRoots);
  QSet<QString> importedIds;
  QHash<QString, QStringList> gridListings;

  for (const QString& steamRoot : steamRoots) {
    const QStringList discoveredLibraries =
        libraryPaths(steamRoot, &result.warnings, &result.incomplete);
    result.libraryPaths.append(discoveredLibraries);
    for (const QString& library : discoveredLibraries) {
      QDir steamapps(library + QStringLiteral("/steamapps"));
      if (!steamapps.exists() || !QFileInfo(steamapps.path()).isReadable()) {
        // An unplugged drive or a stale entry left in libraryfolders.vdf. Warn and keep its
        // cached games; Settings can forget a library that is gone for good.
        result.warnings.append(QStringLiteral("Steam library is unavailable, skipping: %1").arg(library));
        result.unavailableLibraries.append(cleanPath(library));
        continue;
      }
      const QStringList manifests =
          steamapps.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files, QDir::Name);
      for (const QString& filename : manifests) {
        ValveKeyValues parsed;
        QString error;
        const QString manifest = steamapps.absoluteFilePath(filename);
        if (!ValveKeyValuesParser::parseFile(manifest, &parsed, &error)) {
          result.warnings.append(QStringLiteral("Could not read %1: %2").arg(manifest, error));
          result.unreadableManifests.append(manifest);
          continue;
        }
        const ValveKeyValues* app = parsed.object(QStringLiteral("AppState"));
        if (app == nullptr) {
          result.warnings.append(QStringLiteral("Missing AppState in %1").arg(manifest));
          result.unreadableManifests.append(manifest);
          continue;
        }
        const QString appId = app->value(QStringLiteral("appid"));
        const QString name = app->value(QStringLiteral("name")).trimmed();
        const int stateFlags = app->value(QStringLiteral("StateFlags")).toInt();
        if (appId.isEmpty() || name.isEmpty() || (stateFlags & 4) == 0 || isToolTitle(name) ||
            importedIds.contains(appId)) {
          continue;
        }

        const Activity gameActivity = activity.value(appId);
        const AchievementCache achievementCache = achievementCaches.value(appId);
        SteamGameRecord game{
            .appId = appId,
            .title = name,
            .installDirectory = app->value(QStringLiteral("installdir")),
            .libraryPath = cleanPath(library),
            .manifestPath = manifest,
            .coverPath = {},
            .heroPath = {},
            .logoPath = {},
            .lastPlayed = gameActivity.lastPlayed,
            .playtimeMinutes = gameActivity.playtimeMinutes,
            .achievementsUnlocked = achievementCache.unlocked,
            .achievementsTotal = achievementCache.total,
            .achievements = achievementCache.achievements,
        };
        resolveArtwork(&game, steamRoots, &gridListings);
        result.games.append(game);
        importedIds.insert(appId);
      }
    }
  }

  importNonSteamShortcuts(steamRoots, activity, &gridListings, &importedIds, &result);

  result.libraryPaths.removeDuplicates();
  result.unavailableLibraries.removeDuplicates();

  std::sort(result.games.begin(), result.games.end(), [](const auto& left, const auto& right) {
    return left.title.localeAwareCompare(right.title) < 0;
  });
  return result;
}

QString SteamScanner::protonPrefixPath(const QString& installPath, const QString& appId) {
  const QString library = libraryFromInstallPath(installPath);
  // A manifest value can carry surrounding whitespace; trim before the check so
  // the segment that reaches the path is only ever digits.
  const QString id = appId.trimmed();
  if (library.isEmpty() || !isSteamAppId(id)) {
    return {};
  }
  return library + QStringLiteral("/steamapps/compatdata/") + id + QStringLiteral("/pfx");
}

QString SteamScanner::protonPrefix(const QString& installPath, const QString& appId) {
  const QString prefix = protonPrefixPath(installPath, appId);
  return !prefix.isEmpty() && QFileInfo(prefix).isDir() ? prefix : QString{};
}

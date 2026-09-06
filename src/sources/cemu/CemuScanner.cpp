#include "sources/cemu/CemuScanner.h"

#include "artwork/TgaImage.h"
#include "artwork/ZArchiveReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace {
constexpr qint64 kMaximumSettingsBytes = 4 * 1024 * 1024;
constexpr qint64 kMaximumMetaBytes = 256 * 1024;

const QStringList& packageExtensions() {
  static const QStringList extensions = {QStringLiteral(".wud"), QStringLiteral(".wux"),
                                         QStringLiteral(".wua"), QStringLiteral(".iso"),
                                         QStringLiteral(".rpx")};
  return extensions;
}

bool isPackageFile(const QString& fileName) {
  for (const QString& extension : packageExtensions()) {
    if (fileName.endsWith(extension, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

QString expandPath(QString path) {
  path = path.trimmed();
  if (path.startsWith(QStringLiteral("~/"))) {
    path.replace(0, 1, QDir::homePath());
  }
  return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

QString cleanTitle(const QString& fileName) {
  static const QRegularExpression bracketed(QStringLiteral("\\[.*?\\]"));
  static const QRegularExpression parenthesized(QStringLiteral("\\(.*?\\)"));
  QString name = QFileInfo(fileName).completeBaseName();
  name.remove(bracketed);
  name.remove(parenthesized);
  return name.simplified();
}

bool isAddOnTitleId(const QString& titleId) {
  return titleId.startsWith(QStringLiteral("0005000E"), Qt::CaseInsensitive) ||
         titleId.startsWith(QStringLiteral("0005000C"), Qt::CaseInsensitive);
}

QString xmlText(const QString& contents, const QString& tag) {
  static QHash<QString, QRegularExpression> expressions;
  auto found = expressions.find(tag);
  if (found == expressions.end()) {
    found = expressions.insert(tag, QRegularExpression(QStringLiteral("<%1(?:\\s[^>]*)?>([^<]*)</%1>").arg(tag),
                                                       QRegularExpression::CaseInsensitiveOption));
  }
  const QRegularExpressionMatch match = found->match(contents);
  return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}

struct TitleMeta {
  QString titleId;
  QString title;
};

struct CachedPackage {
  QString titleId;
  QString name;
};

TitleMeta readTitleMeta(const QString& path) {
  TitleMeta meta;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumMetaBytes) {
    return meta;
  }
  const QString contents = QString::fromUtf8(file.readAll());
  meta.titleId = xmlText(contents, QStringLiteral("title_id")).toUpper();
  meta.title = xmlText(contents, QStringLiteral("longname_en"));
  if (meta.title.isEmpty()) {
    meta.title = xmlText(contents, QStringLiteral("shortname_en"));
  }
  if (meta.title.isEmpty()) {
    meta.title = xmlText(contents, QStringLiteral("longname_ja"));
  }
  return meta;
}

QString coverForTitle(const QString& titleDirectory, const QString& packagePath,
                      const QString& titleId = {}, const QString& root = {}) {
  QStringList candidates;
  const auto addVariants = [&](const QString& stem) {
    for (const QString& extension :
         {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
      candidates.append(stem + extension);
    }
  };
  if (!titleDirectory.isEmpty()) {
    addVariants(titleDirectory + QStringLiteral("/meta/iconTex"));
    addVariants(titleDirectory + QStringLiteral("/meta/bootTvTex"));
    addVariants(titleDirectory + QStringLiteral("/cover"));
    addVariants(titleDirectory + QStringLiteral("/icon"));
  }
  if (!packagePath.isEmpty()) {
    const QFileInfo package(packagePath);
    addVariants(package.absolutePath() + QLatin1Char('/') + package.completeBaseName());
    addVariants(package.absolutePath() + QStringLiteral("/cover"));
  }
  if (!titleId.isEmpty() && titleId.size() >= 16 && !root.isEmpty()) {
    const QString high = titleId.left(8).toLower();
    const QString low = titleId.right(8).toLower();
    addVariants(root + QStringLiteral("/mlc01/usr/title/%1/%2/meta/iconTex").arg(high, low));
    addVariants(root + QStringLiteral("/mlc01/usr/save/%1/%2/meta/iconTex").arg(high, low));
  }
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

QStringList gamePathsFromSettings(const QString& contents) {
  QStringList paths;
  QXmlStreamReader xml(contents);
  bool inGamePaths = false;
  while (!xml.atEnd() && !xml.hasError()) {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (token == QXmlStreamReader::StartElement) {
      const QStringView name = xml.name();
      if (name.compare(QStringLiteral("GamePaths"), Qt::CaseInsensitive) == 0) {
        inGamePaths = true;
      } else if (inGamePaths && (name.compare(QStringLiteral("string"), Qt::CaseInsensitive) == 0 ||
                                 name.compare(QStringLiteral("GamePath"), Qt::CaseInsensitive) ==
                                     0 ||
                                 name.compare(QStringLiteral("Entry"), Qt::CaseInsensitive) == 0)) {
        const QString path = expandPath(xml.readElementText().trimmed());
        if (!path.isEmpty()) {
          paths.append(path);
        }
      }
    } else if (token == QXmlStreamReader::EndElement &&
               xml.name().compare(QStringLiteral("GamePaths"), Qt::CaseInsensitive) == 0) {
      inGamePaths = false;
    }
  }
  paths.removeDuplicates();
  return paths;
}

// A .wua archive holds one folder per title, named <titleId>_v<version>. The
// base game's folder carries meta/iconTex.tga and meta/meta.xml, which are the
// icon and name the console itself shows.
struct ArchiveMeta {
  QString titleId;
  QString title;
  QString iconPath;
};

QString wiiUCacheRoot() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
         QStringLiteral("/omakade/covers/wiiu");
}

ArchiveMeta readArchiveMeta(const QString& archivePath) {
  ArchiveMeta meta;
  static const QRegularExpression folder(QStringLiteral("^([0-9A-Fa-f]{16})_v\\d+$"));
  auto reader = ZArchiveReader::open(archivePath);
  if (!reader) {
    return meta;
  }
  QString baseFolder;
  QString firstFolder;
  for (const QString& name : reader->list(QString{})) {
    const QRegularExpressionMatch match = folder.match(name);
    if (!match.hasMatch() || !reader->isDirectory(name)) {
      continue;
    }
    const QString id = match.captured(1).toUpper();
    if (firstFolder.isEmpty()) {
      firstFolder = name;
    }
    if (!isAddOnTitleId(id)) {
      baseFolder = name;
      meta.titleId = id;
      break;
    }
  }
  if (baseFolder.isEmpty()) {
    return meta;
  }
  const QByteArray metaXml = reader->readFile(baseFolder + QStringLiteral("/meta/meta.xml"), kMaximumMetaBytes);
  if (!metaXml.isEmpty()) {
    const QString contents = QString::fromUtf8(metaXml);
    QString name = xmlText(contents, QStringLiteral("longname_en"));
    if (name.isEmpty()) {
      name = xmlText(contents, QStringLiteral("shortname_en"));
    }
    meta.title = name.split(QLatin1Char('\n'), Qt::SkipEmptyParts).join(QStringLiteral(" - ")).simplified();
  }
  const QString cached = wiiUCacheRoot() + QLatin1Char('/') + meta.titleId + QStringLiteral(".png");
  if (QFileInfo::exists(cached)) {
    meta.iconPath = cached;
    return meta;
  }
  const QImage icon = decodeTgaImage(reader->readFile(baseFolder + QStringLiteral("/meta/iconTex.tga"), 8 * 1024 * 1024));
  if (icon.isNull()) {
    return meta;
  }
  QDir().mkpath(wiiUCacheRoot());
  QSaveFile file(cached);
  if (file.open(QIODevice::WriteOnly) && icon.save(&file, "PNG") && file.commit()) {
    meta.iconPath = cached;
  }
  return meta;
}

bool preferCachedTitle(const CachedPackage& existing, const QString& incomingId) {
  if (existing.titleId.isEmpty()) {
    return true;
  }
  return isAddOnTitleId(existing.titleId) && !isAddOnTitleId(incomingId);
}

QHash<QString, CachedPackage> loadTitleListCache(const QString& root) {
  QHash<QString, CachedPackage> byPath;
  QFile file(root + QStringLiteral("/title_list_cache.xml"));
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumSettingsBytes) {
    return byPath;
  }
  QXmlStreamReader xml(&file);
  while (!xml.atEnd() && !xml.hasError()) {
    if (xml.readNext() != QXmlStreamReader::StartElement ||
        xml.name().compare(QStringLiteral("title"), Qt::CaseInsensitive) != 0) {
      continue;
    }
    const QString titleId =
        xml.attributes().value(QStringLiteral("titleId")).toString().trimmed().toUpper();
    QString name;
    QString packagePath;
    while (!(xml.atEnd() || xml.hasError() ||
             (xml.isEndElement() &&
              xml.name().compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0))) {
      if (xml.readNext() == QXmlStreamReader::StartElement) {
        if (xml.name().compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0) {
          name = xml.readElementText().trimmed();
        } else if (xml.name().compare(QStringLiteral("path"), Qt::CaseInsensitive) == 0) {
          packagePath = expandPath(xml.readElementText());
        }
      }
    }
    if (packagePath.isEmpty()) {
      continue;
    }
    const CachedPackage incoming{.titleId = titleId, .name = name};
    const auto existing = byPath.constFind(packagePath);
    if (existing == byPath.cend() || preferCachedTitle(*existing, titleId)) {
      byPath.insert(packagePath, incoming);
    }
  }
  return byPath;
}

void importPackage(const QString& filePath, bool flatpak, const CachedPackage& cached,
                   const QString& root, QSet<QString>* seenPaths, QSet<QString>* seenIds,
                   CemuScanResult* result) {
  const QString cleaned = QDir::cleanPath(filePath);
  if (seenPaths->contains(cleaned)) {
    return;
  }
  QString titleId = cached.titleId;
  if (isAddOnTitleId(titleId)) {
    titleId.clear();
  }
  QString title = cached.name;
  ArchiveMeta archive;
  if (cleaned.endsWith(QStringLiteral(".wua"), Qt::CaseInsensitive)) {
    archive = readArchiveMeta(cleaned);
    if (titleId.isEmpty() && !archive.titleId.isEmpty()) {
      titleId = archive.titleId;
    }
    if (title.isEmpty()) {
      title = archive.title;
    }
  }
  if (title.isEmpty()) {
    title = cleanTitle(QFileInfo(cleaned).fileName());
  }
  if (title.isEmpty()) {
    return;
  }
  const QString gameId =
      titleId.isEmpty() ? QStringLiteral("path:%1").arg(cleaned) : titleId;
  if (seenIds->contains(gameId)) {
    return;
  }
  result->games.append(CemuGameRecord{.gameId = gameId,
                                      .titleId = titleId,
                                      .title = title,
                                      .path = cleaned,
                                      .coverPath = archive.iconPath.isEmpty()
                                                       ? coverForTitle({}, cleaned, titleId, root)
                                                       : archive.iconPath,
                                      .flatpak = flatpak});
  seenPaths->insert(cleaned);
  seenIds->insert(gameId);
}

void importTitleDirectory(const QString& directory, bool flatpak, QSet<QString>* seenPaths,
                          QSet<QString>* seenIds, CemuScanResult* result) {
  const QString cleaned = QDir::cleanPath(directory);
  const QDir dir(cleaned);
  const QString metaPath = dir.filePath(QStringLiteral("meta/meta.xml"));
  const QString appPath = dir.filePath(QStringLiteral("code/app.xml"));
  TitleMeta meta = readTitleMeta(metaPath);
  if (meta.titleId.isEmpty()) {
    meta = readTitleMeta(appPath);
  }
  if (isAddOnTitleId(meta.titleId)) {
    return;
  }
  QString launchPath;
  const QFileInfoList rpxFiles =
      QDir(dir.filePath(QStringLiteral("code")))
          .entryInfoList({QStringLiteral("*.rpx")}, QDir::Files);
  if (!rpxFiles.isEmpty()) {
    launchPath = rpxFiles.constFirst().absoluteFilePath();
  } else {
    launchPath = cleaned;
  }
  if (seenPaths->contains(QDir::cleanPath(launchPath))) {
    return;
  }
  QString title = meta.title;
  if (title.isEmpty()) {
    title = dir.dirName();
  }
  if (title.isEmpty()) {
    return;
  }
  const QString gameId =
      meta.titleId.isEmpty() ? QStringLiteral("path:%1").arg(launchPath) : meta.titleId;
  if (seenIds->contains(gameId)) {
    return;
  }
  result->games.append(CemuGameRecord{.gameId = gameId,
                                      .titleId = meta.titleId,
                                      .title = title,
                                      .path = launchPath,
                                      .coverPath = coverForTitle(cleaned, launchPath, meta.titleId, {}),
                                      .flatpak = flatpak});
  seenPaths->insert(QDir::cleanPath(launchPath));
  seenIds->insert(gameId);
}

bool looksLikeTitleDirectory(const QDir& directory) {
  return QFileInfo(directory.filePath(QStringLiteral("code"))).isDir() &&
         (QFileInfo(directory.filePath(QStringLiteral("meta/meta.xml"))).isFile() ||
          QFileInfo(directory.filePath(QStringLiteral("code/app.xml"))).isFile());
}

CachedPackage cachedPackageFor(const QHash<QString, CachedPackage>& cache,
                               const QString& filePath) {
  const QString cleaned = QDir::cleanPath(filePath);
  const auto exact = cache.constFind(cleaned);
  if (exact != cache.cend()) {
    return *exact;
  }
  const QString canonical = QFileInfo(cleaned).canonicalFilePath();
  if (!canonical.isEmpty()) {
    const auto matched = cache.constFind(canonical);
    if (matched != cache.cend()) {
      return *matched;
    }
  }
  return {};
}

void collectFromGamePath(const QString& gamePath, bool flatpak,
                         const QHash<QString, CachedPackage>& cache, const QString& root,
                         QSet<QString>* seenPaths, QSet<QString>* seenIds,
                         CemuScanResult* result) {
  const QFileInfo info(gamePath);
  if (info.isFile() && isPackageFile(info.fileName())) {
    importPackage(info.absoluteFilePath(), flatpak, cachedPackageFor(cache, info.absoluteFilePath()),
                  root, seenPaths, seenIds, result);
    return;
  }
  if (!info.isDir()) {
    return;
  }
  const QDir directory(info.absoluteFilePath());
  if (looksLikeTitleDirectory(directory)) {
    importTitleDirectory(info.absoluteFilePath(), flatpak, seenPaths, seenIds, result);
    return;
  }
  const QFileInfoList entries =
      directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& entry : entries) {
    if (entry.isFile() && isPackageFile(entry.fileName())) {
      importPackage(entry.absoluteFilePath(), flatpak,
                    cachedPackageFor(cache, entry.absoluteFilePath()), root, seenPaths, seenIds,
                    result);
    } else if (entry.isDir() && looksLikeTitleDirectory(QDir(entry.absoluteFilePath()))) {
      importTitleDirectory(entry.absoluteFilePath(), flatpak, seenPaths, seenIds, result);
    }
  }
}
} // namespace

QStringList CemuScanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      home + QStringLiteral("/.config/Cemu"),
      home + QStringLiteral("/.local/share/Cemu"),
      home + QStringLiteral("/.var/app/info.cemu.Cemu/config/Cemu"),
      home + QStringLiteral("/.var/app/info.cemu.Cemu/data/Cemu"),
  };
  candidates.removeDuplicates();

  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/settings.xml")).isFile()) {
      roots.append(root);
    }
  }
  return roots;
}

CemuScanResult CemuScanner::scan(const QStringList& roots) {
  CemuScanResult result;
  QSet<QString> seenPaths;
  QSet<QString> seenIds;
  for (const QString& root : roots) {
    const QString settingsPath = root + QStringLiteral("/settings.xml");
    QFile settingsFile(settingsPath);
    if (!settingsFile.exists()) {
      continue;
    }
    result.roots.append(root);
    if (!settingsFile.open(QIODevice::ReadOnly) || settingsFile.size() > kMaximumSettingsBytes) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(settingsPath));
      continue;
    }
    const QString contents = QString::fromUtf8(settingsFile.readAll());
    const bool flatpak = root.contains(QStringLiteral("/.var/app/info.cemu.Cemu/"));
    const QHash<QString, CachedPackage> cache = loadTitleListCache(root);
    const QStringList gamePaths = gamePathsFromSettings(contents);
    if (gamePaths.isEmpty()) {
      continue;
    }
    for (const QString& gamePath : gamePaths) {
      const QFileInfo info(gamePath);
      if ((!info.isDir() || !info.isReadable()) && !info.isFile()) {
        result.incomplete = true;
        result.warnings.append(QStringLiteral("Game directory is unavailable: %1").arg(gamePath));
        continue;
      }
      collectFromGamePath(gamePath, flatpak, cache, root, &seenPaths, &seenIds, &result);
    }
  }
  return result;
}

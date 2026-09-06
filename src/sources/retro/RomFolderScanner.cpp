#include "sources/retro/RomFolderScanner.h"

#include "library/ConsoleCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {
QString identityFor(const QString& contentPath) {
  return QString::fromLatin1(
      QCryptographicHash::hash(contentPath.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString displayTitle(const QString& fileName) {
  static const QRegularExpression parenthesized(QStringLiteral("\\([^)]*\\)"));
  static const QRegularExpression bracketed(QStringLiteral("\\[[^\\]]*\\]"));
  QString title = QFileInfo(fileName).completeBaseName();
  title.remove(parenthesized);
  title.remove(bracketed);
  return title.simplified();
}

QString sidecarCover(const QString& romPath) {
  const QFileInfo rom(romPath);
  const QString stem = rom.absolutePath() + QLatin1Char('/') + rom.completeBaseName();
  for (const QString& extension :
       {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

bool hasExtension(const QString& fileName, const QStringList& extensions) {
  const QString suffix = QFileInfo(fileName).suffix();
  for (const QString& extension : extensions) {
    if (suffix.compare(extension, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}
} // namespace

QString RomFolderScanner::canonicalPath(const QString& path) {
  const QString cleaned = QDir::cleanPath(path);
  const QString canonical = QFileInfo(cleaned).canonicalFilePath();
  return canonical.isEmpty() ? cleaned : canonical;
}

QString RomFolderScanner::encode(const QString& path, const QString& system) {
  return QDir::cleanPath(path) + QLatin1Char('|') + system;
}

QVector<RomFolder> RomFolderScanner::parseEncoded(const QStringList& encoded) {
  QVector<RomFolder> folders;
  QSet<QString> seen;
  for (const QString& entry : encoded) {
    const QString trimmed = entry.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    const qsizetype split = trimmed.lastIndexOf(QLatin1Char('|'));
    QString path = split >= 0 ? trimmed.left(split) : trimmed;
    QString system = split >= 0 ? trimmed.mid(split + 1) : QString{};
    path = QDir::cleanPath(path);
    if (path.startsWith(QStringLiteral("~/"))) {
      path.replace(0, 1, QDir::homePath());
    }
    if (system.isEmpty()) {
      system = ConsoleCatalog::idFor(QFileInfo(path).fileName());
    } else {
      system = ConsoleCatalog::idFor(system);
    }
    const QString key = canonicalPath(path) + QLatin1Char('|') + system;
    if (path.isEmpty() || system.isEmpty() || seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    folders.append({.path = path, .system = system});
  }
  return folders;
}

QVector<RomFolder> RomFolderScanner::discoverAutoFolders() {
  QStringList encoded;
  const QString home = QDir::homePath();
  const QStringList roots = {
      home + QStringLiteral("/Emulation/roms"),
      home + QStringLiteral("/Emulation/Games"),
      QStringLiteral("/data/Emulation/roms"),
      QStringLiteral("/data/Emulation/Games"),
  };
  for (const QString& root : roots) {
    const QDir directory(root);
    if (!directory.exists()) {
      continue;
    }
    const QFileInfoList entries = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
      const ConsoleDefinition* console = ConsoleCatalog::find(entry.fileName());
      if (console == nullptr || console->dedicatedSource) {
        continue;
      }
      bool folderMatch = entry.fileName().compare(console->id, Qt::CaseInsensitive) == 0;
      for (const QString& folderName : console->folderNames) {
        folderMatch = folderMatch ||
                      entry.fileName().compare(folderName, Qt::CaseInsensitive) == 0;
      }
      if (!folderMatch) {
        continue;
      }
      encoded.append(encode(entry.absoluteFilePath(), console->id));
    }
  }
  return parseEncoded(encoded);
}

RetroArchScanResult RomFolderScanner::scan(const QVector<RomFolder>& folders) {
  RetroArchScanResult result;
  QSet<QString> seenPaths;
  for (const RomFolder& folder : folders) {
    const QFileInfo info(folder.path);
    if (!info.isDir() || !info.isReadable()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("ROM folder is unavailable: %1").arg(folder.path));
      continue;
    }
    result.roots.append(folder.path);
    const ConsoleDefinition* console = ConsoleCatalog::find(folder.system);
    if (console == nullptr || console->dedicatedSource || console->extensions.isEmpty()) {
      continue;
    }
    QDirIterator iterator(folder.path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      const QString filePath = canonicalPath(iterator.next());
      if (seenPaths.contains(filePath) || !hasExtension(filePath, console->extensions)) {
        continue;
      }
      QString title = displayTitle(QFileInfo(filePath).fileName());
      if (title.isEmpty()) {
        title = QFileInfo(filePath).completeBaseName();
      }
      result.games.append(RetroArchGameRecord{.gameId = identityFor(filePath),
                                              .title = title,
                                              .contentPath = filePath,
                                              .corePath = {},
                                              .coreName = {},
                                              .coverPath = sidecarCover(filePath),
                                              .heroPath = {},
                                              .system = console->libretroPlaylist,
                                              .playtimeSeconds = 0,
                                              .lastPlayed = 0,
                                              .flatpak = false});
      seenPaths.insert(filePath);
    }
  }
  return result;
}

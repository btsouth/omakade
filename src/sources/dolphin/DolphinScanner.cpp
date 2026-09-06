#include "sources/dolphin/DolphinScanner.h"

#include "library/ConsoleCatalog.h"
#include "sources/FlatpakInstall.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>

namespace {
constexpr qint64 kMaximumIniBytes = 4 * 1024 * 1024;
constexpr quint32 kGameCubeMagic = 0xC2339F3D;
constexpr quint32 kWiiMagic = 0x5D1C9EA3;

const QStringList& discExtensions() {
  static const QStringList extensions = {QStringLiteral("rvz"), QStringLiteral("wia"), QStringLiteral("iso"),
                                         QStringLiteral("gcm"), QStringLiteral("gcz"), QStringLiteral("wbfs"),
                                         QStringLiteral("ciso")};
  return extensions;
}

bool isDiscFile(const QString& fileName) {
  const QString suffix = QFileInfo(fileName).suffix();
  for (const QString& extension : discExtensions()) {
    if (suffix.compare(extension, Qt::CaseInsensitive) == 0) {
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
  // "Legend of Zelda, The - The Wind Waker" reads better with the article in front.
  static const QRegularExpression trailingArticle(QStringLiteral("^(.+?), (The|A|An)( - .*)?$"));
  const QRegularExpressionMatch match = trailingArticle.match(name.simplified());
  if (match.hasMatch()) {
    name = match.captured(2) + QLatin1Char(' ') + match.captured(1) + match.captured(3);
  }
  return name.simplified();
}

DolphinDiscHeader parseDiscHeader(const QByteArray& header, const QString& platformHint) {
  DolphinDiscHeader parsed;
  if (header.size() < 0x60) {
    return parsed;
  }
  static const QRegularExpression discId(QStringLiteral("^[A-Z0-9]{6}$"));
  const QString id = QString::fromLatin1(header.left(6));
  if (!discId.match(id).hasMatch()) {
    return parsed;
  }
  parsed.discId = id;
  const QByteArray rawTitle = header.mid(0x20, 0x40);
  const int end = rawTitle.indexOf('\0');
  parsed.title = QString::fromLatin1(end < 0 ? rawTitle : rawTitle.left(end)).simplified();
  if (qFromBigEndian<quint32>(header.constData() + 0x18) == kWiiMagic) {
    parsed.platform = QStringLiteral("Wii");
  } else if (qFromBigEndian<quint32>(header.constData() + 0x1C) == kGameCubeMagic) {
    parsed.platform = QStringLiteral("GameCube");
  } else {
    parsed.platform = platformHint;
  }
  return parsed;
}

QString sidecarCover(const QString& discPath) {
  const QFileInfo disc(discPath);
  const QString stem = disc.absolutePath() + QLatin1Char('/') + disc.completeBaseName();
  for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

QStringList iniGameFolders(const QString& root) {
  QStringList folders;
  QFile file(root + QStringLiteral("/Dolphin.ini"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > kMaximumIniBytes) {
    return folders;
  }
  static const QRegularExpression isoPath(QStringLiteral("^ISOPath\\d+\\s*=\\s*(.+)$"));
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const QRegularExpressionMatch match = isoPath.match(line);
    if (match.hasMatch()) {
      const QString folder = expandPath(match.captured(1));
      if (!folder.isEmpty()) {
        folders.append(folder);
      }
    }
  }
  return folders;
}

QStringList autoGameFolders() {
  QStringList folders;
  const QString home = QDir::homePath();
  const QStringList roots = {home + QStringLiteral("/Emulation/roms"), home + QStringLiteral("/Emulation/Games"),
                             QStringLiteral("/data/Emulation/roms"), QStringLiteral("/data/Emulation/Games")};
  for (const QString& root : roots) {
    const QDir directory(root);
    if (!directory.exists()) {
      continue;
    }
    for (const QFileInfo& entry : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const ConsoleDefinition* console = ConsoleCatalog::find(entry.fileName());
      if (console != nullptr &&
          (console->id == QLatin1String("gamecube") || console->id == QLatin1String("wii"))) {
        folders.append(entry.absoluteFilePath());
      }
    }
  }
  return folders;
}
}  // namespace

QStringList DolphinScanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList roots;
  for (const QString& candidate :
       {home + QStringLiteral("/.config/dolphin-emu"),
        home + QStringLiteral("/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu")}) {
    if (QFileInfo(candidate + QStringLiteral("/Dolphin.ini")).isFile()) {
      roots.append(candidate);
    }
  }
  return roots;
}

bool DolphinScanner::dolphinInstalled() {
  return !QStandardPaths::findExecutable(QStringLiteral("dolphin-emu")).isEmpty() ||
         !QStandardPaths::findExecutable(QStringLiteral("dolphin-emu-nogui")).isEmpty() ||
         flatpakAppInstalled(QStringLiteral("org.DolphinEmu.dolphin-emu"));
}

DolphinDiscHeader DolphinScanner::readDiscHeader(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray head = file.read(0x100);
  if (head.size() < 0x100) {
    return {};
  }
  if (head.startsWith("RVZ\x01") || head.startsWith("WIA\x01")) {
    // Header 2 follows the 0x48-byte header 1: disc type, compression, level,
    // chunk size, then the first 0x80 bytes of the disc header.
    const auto discType = qFromBigEndian<quint32>(head.constData() + 0x48);
    const QString hint = discType == 2 ? QStringLiteral("Wii") : QStringLiteral("GameCube");
    return parseDiscHeader(head.mid(0x58, 0x80), hint);
  }
  if (head.startsWith("WBFS")) {
    if (!file.seek(0x200)) {
      return {};
    }
    return parseDiscHeader(file.read(0x80), QStringLiteral("Wii"));
  }
  if (head.startsWith("CISO")) {
    if (!file.seek(0x8000)) {
      return {};
    }
    return parseDiscHeader(file.read(0x80), QString{});
  }
  return parseDiscHeader(head.left(0x80), QString{});
}

QString DolphinScanner::coverFromDolphinCache(const QString& discId) {
  if (discId.isEmpty()) {
    return {};
  }
  const QString home = QDir::homePath();
  for (const QString& directory :
       {home + QStringLiteral("/.local/share/dolphin-emu/Cache/GameCovers"),
        home + QStringLiteral("/.cache/dolphin-emu/GameCovers"),
        home + QStringLiteral("/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Cache/GameCovers"),
        home + QStringLiteral("/.var/app/org.DolphinEmu.dolphin-emu/cache/dolphin-emu/GameCovers")}) {
    const QString candidate = directory + QLatin1Char('/') + discId + QStringLiteral(".png");
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

DolphinScanResult DolphinScanner::scan(const QStringList& roots, const QStringList& extraFolders,
                                       bool autoDiscover) {
  DolphinScanResult result;
  QStringList folders = extraFolders;
  for (const QString& root : roots) {
    if (!QFileInfo(root + QStringLiteral("/Dolphin.ini")).isFile()) {
      continue;
    }
    result.roots.append(root);
    folders += iniGameFolders(root);
  }
  if (autoDiscover) {
    folders += autoGameFolders();
  }
  folders.removeDuplicates();
  const bool flatpakOnly = !roots.isEmpty() &&
                           std::all_of(roots.cbegin(), roots.cend(), [](const QString& root) {
                             return root.contains(QStringLiteral("/.var/app/org.DolphinEmu.dolphin-emu/"));
                           }) &&
                           QStandardPaths::findExecutable(QStringLiteral("dolphin-emu")).isEmpty();
  QSet<QString> seenPaths;
  QSet<QString> seenIds;
  for (const QString& folder : folders) {
    const QFileInfo info(folder);
    if (!info.isDir() || !info.isReadable()) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Game folder is unavailable: %1").arg(folder));
      continue;
    }
    result.folders.append(folder);
    QDirIterator iterator(folder, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      const QString path = QDir::cleanPath(iterator.next());
      const QFileInfo fileInfo(path);
      if (!isDiscFile(fileInfo.fileName()) || seenPaths.contains(path)) {
        continue;
      }
      const DolphinDiscHeader header = readDiscHeader(path);
      const QString gameId = header.valid() ? header.discId : QStringLiteral("path:%1").arg(path);
      if (seenIds.contains(gameId)) {
        continue;
      }
      // Disc headers keep short internal names ("ZELDA WIND WAKER"); the file
      // name is usually the fuller title, so prefer it when there is one.
      QString title = cleanTitle(fileInfo.fileName());
      if (title.isEmpty()) {
        title = header.title;
      }
      if (title.isEmpty()) {
        continue;
      }
      QString cover = sidecarCover(path);
      if (cover.isEmpty()) {
        cover = coverFromDolphinCache(header.discId);
      }
      result.games.append(DolphinGameRecord{
          .gameId = gameId,
          .discId = header.discId,
          .title = title,
          .path = path,
          .platform = header.platform.isEmpty() ? QStringLiteral("GameCube") : header.platform,
          .coverPath = cover,
          .flatpak = flatpakOnly,
          .flatpakAppId = flatpakOnly ? QStringLiteral("org.DolphinEmu.dolphin-emu") : QString{}});
      seenPaths.insert(path);
      seenIds.insert(gameId);
    }
  }
  return result;
}

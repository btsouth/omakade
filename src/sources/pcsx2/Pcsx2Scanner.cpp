#include "sources/pcsx2/Pcsx2Scanner.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <cstring>
#include <QStandardPaths>
#include <QtEndian>

namespace {
constexpr quint32 kCacheSignature = 0x45434C47; // 'GLCE' when read little-endian
constexpr qint64 kMaximumCacheBytes = 128 * 1024 * 1024;
constexpr int kPlayedTimeSerialLength = 32;
constexpr int kPlayedTimeTotalLength = 20;
constexpr int kPlayedTimeLastLength = 20;
constexpr int kMaximumTitleLength = 256;

bool isScannableFilename(const QString& path) {
  static const QStringList extensions = {
      QStringLiteral(".iso"), QStringLiteral(".bin"), QStringLiteral(".img"),
      QStringLiteral(".mdf"), QStringLiteral(".gz"),  QStringLiteral(".cso"),
      QStringLiteral(".zso"), QStringLiteral(".chd"), QStringLiteral(".elf")};
  for (const QString& extension : extensions) {
    if (path.endsWith(extension, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

struct PlayedTimeEntry {
  qint64 totalSeconds = 0;
  qint64 lastPlayed = 0;
};

QHash<QString, PlayedTimeEntry> loadPlayedTime(const QString& root) {
  QHash<QString, PlayedTimeEntry> played;
  QFile file(root + QStringLiteral("/inis/playtime.dat"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return played;
  }
  // PCSX2 format: "{serial:<32} {total:<20} {last:<20}\n"
  const int lineLength =
      kPlayedTimeSerialLength + 1 + kPlayedTimeTotalLength + 1 + kPlayedTimeLastLength;
  while (!file.atEnd()) {
    QByteArray line = file.readLine();
    while (line.endsWith('\n') || line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.size() != lineLength) {
      continue;
    }
    const QString serial =
        QString::fromUtf8(line.left(kPlayedTimeSerialLength)).trimmed();
    const qint64 totalTime = QString::fromUtf8(
                                 line.mid(kPlayedTimeSerialLength + 1, kPlayedTimeTotalLength))
                                 .trimmed()
                                 .toLongLong();
    const qint64 lastTime =
        QString::fromUtf8(line.mid(kPlayedTimeSerialLength + 1 + kPlayedTimeTotalLength + 1,
                                   kPlayedTimeLastLength))
            .trimmed()
            .toLongLong();
    if (serial.isEmpty() || totalTime < 0 || lastTime < 0 || played.contains(serial)) {
      continue;
    }
    played.insert(serial, {totalTime, lastTime});
  }
  return played;
}

QString regionName(quint8 region) {
  switch (region) {
  case 0:
  case 1:
  case 2:
  case 3:
    return QStringLiteral("NTSC");
  case 4:
    return QStringLiteral("NTSC-K");
  case 6:
    return QStringLiteral("NTSC-U");
  case 7:
    return QStringLiteral("Other");
  default:
    if (region >= 8) {
      return QStringLiteral("PAL");
    }
    return QStringLiteral("NTSC-J");
  }
}

QString coverFor(const QString& root, const QString& path, const QString& serial,
                 const QString& title) {
  const QString coversRoot = root + QStringLiteral("/covers");
  // PCSX2 lookup order: file title, serial, then game title.
  const QString fileTitle = QFileInfo(path).completeBaseName();
  QStringList stems;
  if (!fileTitle.isEmpty() && fileTitle != title) {
    stems.append(fileTitle);
  }
  if (!serial.isEmpty()) {
    stems.append(serial);
  }
  if (!title.isEmpty()) {
    stems.append(title);
  }
  for (const QString& stemRef : stems) {
    QString stem = stemRef;
    const QString sanitized =
        stem.replace(QRegularExpression(QStringLiteral("[/\\\\?%*:|\"<>\\x01-\\x1f]")),
                     QStringLiteral("_"));
    for (const QString& extension :
         {QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".png"),
          QStringLiteral(".webp")}) {
      const QString candidate = coversRoot + QLatin1Char('/') + sanitized + extension;
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  return {};
}
} // namespace

QStringList Pcsx2Scanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      home + QStringLiteral("/.config/PCSX2"),
      home + QStringLiteral("/.var/app/net.pcsx2.PCSX2/config/PCSX2"),
  };
  candidates.removeDuplicates();

  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/inis/PCSX2.ini")).isFile() ||
        QFileInfo(root + QStringLiteral("/cache/gamelist.cache")).isFile()) {
      roots.append(root);
    }
  }
  return roots;
}

Pcsx2ScanResult Pcsx2Scanner::scan(const QStringList& roots) {
  Pcsx2ScanResult result;
  QSet<QString> seenGameIds;
  QSet<QString> seenPaths;
  for (const QString& root : roots) {
    const QString cachePath = root + QStringLiteral("/cache/gamelist.cache");
    QFile file(cachePath);
    if (!file.exists()) {
      continue;
    }
    result.roots.append(root);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumCacheBytes) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(cachePath));
      continue;
    }
    const QByteArray cache = file.readAll();
    file.close();

    quint32 signature = 0;
    quint32 version = 0;
    if (cache.size() < 8) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Truncated game list cache %1").arg(cachePath));
      continue;
    }
    quint32 rawSignature = 0;
    quint32 rawVersion = 0;
    std::memcpy(&rawSignature, cache.constData(), sizeof(rawSignature));
    std::memcpy(&rawVersion, cache.constData() + 4, sizeof(rawVersion));
    signature = qFromLittleEndian(rawSignature);
    version = qFromLittleEndian(rawVersion);
    if (signature != kCacheSignature) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Unrecognized game list cache %1").arg(cachePath));
      continue;
    }
    if (version != 32 && version != 34) {
      result.warnings.append(QStringLiteral(
          "Unsupported PCSX2 cache version %1; rescan in PCSX2 to refresh").arg(version));
    }

    // Entry layout: str path, str serial, str title
    // [, str title_sort, str title_en when version >= 33],
    // u8 type, u8 region, u64 total_size, u64 last_modified, u32 crc, u8 compat.
    const bool hasExtraTitles = version >= 33;
    const bool flatpak = root.contains(QStringLiteral("/.var/app/net.pcsx2.PCSX2/"));
    const QHash<QString, PlayedTimeEntry> playedTime = loadPlayedTime(root);

    int offset = 8;
    bool corrupt = false;
    while (offset < cache.size()) {
      auto readString = [&cache, &offset]() -> QString {
        if (offset + 4 > cache.size()) {
          offset = cache.size() + 1;
          return {};
        }
        quint32 rawLength = 0;
        std::memcpy(&rawLength, cache.constData() + offset, sizeof(rawLength));
        const quint32 length = qFromLittleEndian(rawLength);
        offset += 4;
        if (length > static_cast<quint32>(kMaximumTitleLength * 16) ||
            offset + static_cast<qsizetype>(length) > cache.size()) {
          offset = cache.size() + 1;
          return {};
        }
        const QString value =
            QString::fromUtf8(cache.constData() + offset, static_cast<qsizetype>(length));
        offset += static_cast<qsizetype>(length);
        return value;
      };

      const QString path = readString();
      const QString serial = readString();
      const QString title = readString();
      if (hasExtraTitles) {
        readString();
        readString();
      }
      if (offset >= cache.size() + 1) {
        // A string read aborted mid-entry: the cache is malformed.
        result.warnings.append(
            QStringLiteral("Malformed cache entry in %1").arg(cachePath));
        corrupt = true;
        break;
      }
      if (offset + 1 + 1 + 8 + 8 + 4 + 1 > cache.size() || offset < 0) {
        if (offset != cache.size() + 1) {
          // Trailing bytes that cannot form an entry: treat as corruption.
          result.warnings.append(
              QStringLiteral("Truncated cache entry in %1").arg(cachePath));
          corrupt = true;
        }
        break;
      }
      const quint8 type = static_cast<quint8>(cache.at(offset));
      offset += 1;
      const quint8 region = static_cast<quint8>(cache.at(offset));
      offset += 1;
      quint64 totalSize = 0;
      std::memcpy(&totalSize, cache.constData() + offset, sizeof(totalSize));
      totalSize = qFromLittleEndian(totalSize);
      offset += 8;
      offset += 8; // last modified time
      offset += 4; // crc
      const quint8 compatibility = static_cast<quint8>(cache.at(offset));
      offset += 1;
      if (compatibility > 6) {
        // PCSX2 compatibility ratings run 0 (Unknown) through 6 (Perfect); higher
        // values indicate a malformed entry, but do not abort the whole cache.
        result.warnings.append(
            QStringLiteral("Unexpected compatibility rating in %1").arg(cachePath));
        continue;
      }

      const QString filePath = QDir::cleanPath(path);
      if (filePath.isEmpty() || title.trimmed().isEmpty() ||
          !isScannableFilename(filePath) || seenPaths.contains(filePath)) {
        continue;
      }
      const QString recordKey =
          serial.isEmpty() ? QStringLiteral("path:%1").arg(filePath) : serial;
      if (seenGameIds.contains(recordKey)) {
        // Same serial (or path) already imported from another root; keep first.
        continue;
      }
      if (!QFileInfo::exists(filePath)) {
        // PCSX2 keeps stale cache entries for moved or deleted games.
        continue;
      }

      Pcsx2GameRecord record;
      record.gameId = serial.isEmpty() ? QStringLiteral("path:%1").arg(filePath) : serial;
      record.title = title.trimmed();
      record.path = filePath;
      record.serial = serial;
      record.region = regionName(region);
      record.flatpak = flatpak;
      record.isElf = (type == 2);
      record.coverPath = coverFor(root, filePath, serial, record.title);
      if (type == 0 || type == 1) {
        // Disc entries: playtime is keyed by serial.
        const PlayedTimeEntry played = playedTime.value(serial);
        record.playtimeSeconds = played.totalSeconds;
        record.lastPlayed = played.lastPlayed;
      }
      result.games.append(record);
      seenPaths.insert(filePath);
      seenGameIds.insert(recordKey);
    }
    if (corrupt) {
      result.incomplete = true;
    }
  }
  return result;
}
#include "achievements/RetroAchievementsHasher.h"

#include <QCryptographicHash>
#include <QFile>
#include <QScopeGuard>

#include <zip.h>

namespace {
constexpr qint64 kMaximumHashableBytes = 512LL * 1024 * 1024;

QString normalized(const QString& value) {
  QString out;
  out.reserve(value.size());
  for (const QChar& character : value) {
    if (character.isLetterOrNumber()) {
      out.append(character.toLower());
    } else if (!out.isEmpty() && !out.endsWith(QLatin1Char(' '))) {
      out.append(QLatin1Char(' '));
    }
  }
  return out.trimmed();
}

struct ConsoleRule {
  const char* keyword;
  const char* raConsoleName;
  RetroAchievementsHashRule rule;
};

// Checked in order; more specific keywords (e.g. "game boy advance") must precede the shorter
// ones they would otherwise also match (e.g. "game boy"). Keywords are chosen to avoid colliding
// with unrelated console names (e.g. "genesis" contains "nes", so bare "nes" is never used here).
constexpr ConsoleRule kRules[] = {
    {"game boy advance", "Game Boy Advance", RetroAchievementsHashRule::WholeFileMd5},
    {"gba", "Game Boy Advance", RetroAchievementsHashRule::WholeFileMd5},
    {"game boy color", "Game Boy Color", RetroAchievementsHashRule::WholeFileMd5},
    {"gbc", "Game Boy Color", RetroAchievementsHashRule::WholeFileMd5},
    {"game boy", "Game Boy", RetroAchievementsHashRule::WholeFileMd5},
    {"super nintendo", "Super Nintendo Entertainment System",
     RetroAchievementsHashRule::SnesHeaderStrip},
    {"super famicom", "Super Nintendo Entertainment System",
     RetroAchievementsHashRule::SnesHeaderStrip},
    {"snes", "Super Nintendo Entertainment System", RetroAchievementsHashRule::SnesHeaderStrip},
    {"nintendo entertainment system", "Nintendo Entertainment System",
     RetroAchievementsHashRule::NesHeaderStrip},
    {"famicom", "Nintendo Entertainment System", RetroAchievementsHashRule::NesHeaderStrip},
    {"virtual boy", "Virtual Boy", RetroAchievementsHashRule::WholeFileMd5},
    {"mega drive", "Genesis/Mega Drive", RetroAchievementsHashRule::WholeFileMd5},
    {"genesis", "Genesis/Mega Drive", RetroAchievementsHashRule::WholeFileMd5},
    {"master system", "Master System", RetroAchievementsHashRule::WholeFileMd5},
    {"game gear", "Game Gear", RetroAchievementsHashRule::WholeFileMd5},
    {"atari 7800", "Atari 7800", RetroAchievementsHashRule::Atari7800HeaderStrip},
    {"7800", "Atari 7800", RetroAchievementsHashRule::Atari7800HeaderStrip},
    {"lynx", "Atari Lynx", RetroAchievementsHashRule::AtariLynxHeaderStrip},
    {"atari 2600", "Atari 2600", RetroAchievementsHashRule::WholeFileMd5},
    {"2600", "Atari 2600", RetroAchievementsHashRule::WholeFileMd5},
    {"pc engine", "PC Engine", RetroAchievementsHashRule::WholeFileMd5},
    {"turbografx", "PC Engine", RetroAchievementsHashRule::WholeFileMd5},
    {"turbo grafx", "PC Engine", RetroAchievementsHashRule::WholeFileMd5},
    {"wonderswan color", "WonderSwan Color", RetroAchievementsHashRule::WholeFileMd5},
    {"wonderswan", "WonderSwan", RetroAchievementsHashRule::WholeFileMd5},
    {"neo geo pocket color", "Neo Geo Pocket Color", RetroAchievementsHashRule::WholeFileMd5},
    {"neo geo pocket", "Neo Geo Pocket", RetroAchievementsHashRule::WholeFileMd5},
    {"colecovision", "ColecoVision", RetroAchievementsHashRule::WholeFileMd5},
    {"vectrex", "Vectrex", RetroAchievementsHashRule::WholeFileMd5},
    {"intellivision", "Intellivision", RetroAchievementsHashRule::WholeFileMd5},
    {"odyssey", "Odyssey2", RetroAchievementsHashRule::WholeFileMd5},
    {"msx2", "MSX2", RetroAchievementsHashRule::WholeFileMd5},
    {"msx", "MSX", RetroAchievementsHashRule::WholeFileMd5},
    {"pokemon mini", "Pokemon Mini", RetroAchievementsHashRule::WholeFileMd5},
    {"channel f", "Fairchild Channel F", RetroAchievementsHashRule::WholeFileMd5},
};

struct ContentLocation {
  QString archivePath;
  QString innerPath;
};

// RetroArch stores archived content as "archive.zip#inner/path.rom" (see
// RetroArchScanner::runtimeFileName for the same convention). An empty innerPath means the
// content isn't archived at all.
ContentLocation locate(const QString& contentPath) {
  const qsizetype marker = contentPath.lastIndexOf(QLatin1Char('#'));
  if (marker < 0) {
    return {.archivePath = contentPath, .innerPath = {}};
  }
  return {.archivePath = contentPath.left(marker), .innerPath = contentPath.mid(marker + 1)};
}

std::optional<QByteArray> readPlainFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumHashableBytes) {
    return std::nullopt;
  }
  return file.readAll();
}

std::optional<QByteArray> readZipEntry(const QString& archivePath, const QString& innerPath) {
  int errorCode = 0;
  zip_t* archive = zip_open(archivePath.toUtf8().constData(), ZIP_RDONLY, &errorCode);
  if (archive == nullptr) {
    return std::nullopt;
  }
  const auto closeArchive = qScopeGuard([archive] { zip_close(archive); });

  zip_stat_t stat;
  zip_stat_init(&stat);
  const QByteArray innerPathUtf8 = innerPath.toUtf8();
  if (zip_stat(archive, innerPathUtf8.constData(), 0, &stat) != 0 ||
      (stat.valid & ZIP_STAT_SIZE) == 0 ||
      stat.size > static_cast<zip_uint64_t>(kMaximumHashableBytes)) {
    return std::nullopt;
  }

  zip_file_t* entry = zip_fopen(archive, innerPathUtf8.constData(), 0);
  if (entry == nullptr) {
    return std::nullopt;
  }
  const auto closeEntry = qScopeGuard([entry] { zip_fclose(entry); });

  QByteArray bytes(static_cast<qsizetype>(stat.size), Qt::Uninitialized);
  const zip_int64_t bytesRead = zip_fread(entry, bytes.data(), stat.size);
  if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
    return std::nullopt;
  }
  return bytes;
}

std::optional<QByteArray> readWholeContent(const QString& contentPath) {
  const ContentLocation location = locate(contentPath);
  return location.innerPath.isEmpty() ? readPlainFile(location.archivePath)
                                      : readZipEntry(location.archivePath, location.innerPath);
}

std::optional<QByteArray> hashBytes(const QByteArray& bytes, qint64 skipBytes) {
  if (skipBytes > bytes.size()) {
    return std::nullopt;
  }
  return QCryptographicHash::hash(bytes.sliced(skipBytes), QCryptographicHash::Md5).toHex();
}
} // namespace

RetroAchievementsConsole RetroAchievementsHasher::consoleFor(const QString& playlistDbName) {
  const QString key = normalized(playlistDbName);
  if (!key.isEmpty()) {
    for (const ConsoleRule& candidate : kRules) {
      if (key.contains(QLatin1String(candidate.keyword))) {
        return {.raConsoleName = QString::fromLatin1(candidate.raConsoleName),
                .rule = candidate.rule};
      }
    }
  }
  return {.raConsoleName = {}, .rule = RetroAchievementsHashRule::Unsupported};
}

std::optional<QByteArray> RetroAchievementsHasher::hashFile(const QString& contentPath,
                                                             RetroAchievementsHashRule rule) {
  if (rule == RetroAchievementsHashRule::Unsupported) {
    return std::nullopt;
  }
  const std::optional<QByteArray> content = readWholeContent(contentPath);
  if (!content.has_value()) {
    return std::nullopt;
  }
  const QByteArray& bytes = *content;
  switch (rule) {
  case RetroAchievementsHashRule::WholeFileMd5:
    return hashBytes(bytes, 0);
  case RetroAchievementsHashRule::NesHeaderStrip:
    return hashBytes(bytes, bytes.size() >= 4 && bytes.first(4) == QByteArray("NES\x1A", 4) ? 16 : 0);
  case RetroAchievementsHashRule::SnesHeaderStrip:
    return hashBytes(bytes, (bytes.size() % 0x8000) == 512 ? 512 : 0);
  case RetroAchievementsHashRule::Atari7800HeaderStrip: {
    const bool hasHeader = bytes.size() >= 10 && bytes.at(0) == 1 &&
                           bytes.sliced(1, 9) == QByteArray("ATARI7800", 9);
    return hashBytes(bytes, hasHeader ? 128 : 0);
  }
  case RetroAchievementsHashRule::AtariLynxHeaderStrip: {
    const bool hasHeader =
        bytes.size() >= 5 && bytes.first(4) == QByteArray("LYNX", 4) && bytes.at(4) == '\0';
    return hashBytes(bytes, hasHeader ? 64 : 0);
  }
  case RetroAchievementsHashRule::Unsupported:
    return std::nullopt;
  }
  return std::nullopt;
}

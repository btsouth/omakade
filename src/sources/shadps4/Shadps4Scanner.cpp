#include "sources/shadps4/Shadps4Scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

namespace {
constexpr int kMaximumScanDepth = 5;
constexpr qint64 kMaximumConfigBytes = 4 * 1024 * 1024;
constexpr qint64 kMaximumSfoBytes = 256 * 1024;

QString expandPath(QString path) {
  path = path.trimmed();
  if (path.startsWith(QStringLiteral("~/"))) {
    path.replace(0, 1, QDir::homePath());
  }
  return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

QString firstExistingImage(const QStringList& candidates) {
  for (const QString& candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

QStringList quotedTomlStrings(const QString& text) {
  QStringList values;
  static const QRegularExpression quoted(QStringLiteral("\"((?:\\\\.|[^\"\\\\])*)\""));
  QRegularExpressionMatchIterator it = quoted.globalMatch(text);
  while (it.hasNext()) {
    QString value = it.next().captured(1);
    value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    if (!value.isEmpty()) {
      values.append(value);
    }
  }
  return values;
}

QStringList installDirectoriesFromConfig(const QString& contents) {
  QStringList directories;
  static const QRegularExpression installDirs(
      QStringLiteral("(?ms)^\\s*installDirs\\s*=\\s*\\[([^\\]]*)\\]"));
  const QRegularExpressionMatch dirsMatch = installDirs.match(contents);
  if (dirsMatch.hasMatch()) {
    directories += quotedTomlStrings(dirsMatch.captured(1));
  }
  static const QRegularExpression installDir(
      QStringLiteral("(?m)^\\s*installDir\\s*=\\s*\"((?:\\\\.|[^\"\\\\])*)\"\\s*$"));
  const QRegularExpressionMatch dirMatch = installDir.match(contents);
  if (dirMatch.hasMatch()) {
    QString value = dirMatch.captured(1);
    value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    if (!value.isEmpty()) {
      directories.append(value);
    }
  }
  directories.removeDuplicates();
  return directories;
}

struct ParamSfo {
  QString title;
  QString titleId;
  QString category;
};

bool readParamSfo(const QString& path, ParamSfo* result) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 20 || file.size() > kMaximumSfoBytes) {
    return false;
  }
  const QByteArray data = file.readAll();
  if (data.size() < 20 || !data.startsWith(QByteArray("\0PSF", 4))) {
    return false;
  }
  const auto keyTable = qFromLittleEndian<quint32>(data.constData() + 8);
  const auto dataTable = qFromLittleEndian<quint32>(data.constData() + 12);
  const auto count = qFromLittleEndian<quint32>(data.constData() + 16);
  if (count == 0 || count > 256 || keyTable >= static_cast<quint32>(data.size()) ||
      dataTable >= static_cast<quint32>(data.size())) {
    return false;
  }
  const qint64 indexBytes = static_cast<qint64>(count) * 16;
  if (20 + indexBytes > data.size()) {
    return false;
  }
  auto stringAt = [&](quint32 offset, quint32 length) {
    if (offset >= static_cast<quint32>(data.size())) {
      return QString{};
    }
    const char* start = data.constData() + offset;
    const qsizetype available = data.size() - static_cast<qsizetype>(offset);
    const qsizetype take =
        length == 0 ? available : qMin(available, static_cast<qsizetype>(length));
    const QByteArray slice(start, take);
    const int nul = slice.indexOf('\0');
    return QString::fromUtf8(nul >= 0 ? slice.left(nul) : slice).trimmed();
  };
  for (quint32 index = 0; index < count; ++index) {
    const char* entry = data.constData() + 20 + static_cast<qsizetype>(index) * 16;
    const auto keyOffset = qFromLittleEndian<quint16>(entry);
    const auto paramLen = qFromLittleEndian<quint32>(entry + 4);
    const auto dataOffset = qFromLittleEndian<quint32>(entry + 12);
    const QString key = stringAt(keyTable + keyOffset, 64);
    const QString value = stringAt(dataTable + dataOffset, paramLen);
    if (key == QStringLiteral("TITLE")) {
      result->title = value.left(256);
    } else if (key == QStringLiteral("TITLE_ID")) {
      result->titleId = value.left(16);
    } else if (key == QStringLiteral("CATEGORY")) {
      result->category = value.left(8);
    }
  }
  return !result->title.isEmpty() || !result->titleId.isEmpty();
}

bool isAddOnCategory(const QString& category, bool baseAlreadyImported) {
  if (category.compare(QStringLiteral("ac"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  // Merged dumps often keep the patch SFO category "gp". Keep that folder when
  // it is the only install; skip it when the base game is already imported.
  return baseAlreadyImported && category.compare(QStringLiteral("gp"), Qt::CaseInsensitive) == 0;
}

bool looksLikeGameDirectory(const QDir& directory) {
  return QFileInfo::exists(directory.filePath(QStringLiteral("sce_sys/param.sfo"))) &&
         QFileInfo::exists(directory.filePath(QStringLiteral("eboot.bin")));
}

void collectGames(const QString& directory, int depth, bool flatpak, const QString& flatpakAppId,
                  QSet<QString>* seenPaths, QSet<QString>* seenIds, Shadps4ScanResult* result) {
  if (depth > kMaximumScanDepth) {
    return;
  }
  const QDir dir(directory);
  if (!dir.exists()) {
    return;
  }
  if (looksLikeGameDirectory(dir)) {
    const QString eboot = QDir::cleanPath(dir.filePath(QStringLiteral("eboot.bin")));
    if (seenPaths->contains(eboot)) {
      return;
    }
    ParamSfo sfo;
    if (!readParamSfo(dir.filePath(QStringLiteral("sce_sys/param.sfo")), &sfo) ||
        isAddOnCategory(sfo.category, seenIds->contains(sfo.titleId))) {
      return;
    }
    QString title = sfo.title;
    if (title.isEmpty()) {
      title = dir.dirName();
    }
    if (title.isEmpty()) {
      return;
    }
    const QString gameId =
        sfo.titleId.isEmpty() ? QStringLiteral("path:%1").arg(eboot) : sfo.titleId;
    if (seenIds->contains(gameId)) {
      return;
    }
    const QString sceSys = dir.filePath(QStringLiteral("sce_sys"));
    result->games.append(Shadps4GameRecord{
        .gameId = gameId,
        .titleId = sfo.titleId,
        .title = title,
        .path = eboot,
        .coverPath = firstExistingImage({sceSys + QStringLiteral("/icon0.png"),
                                         sceSys + QStringLiteral("/icon0.jpg"),
                                         sceSys + QStringLiteral("/icon0.jpeg")}),
        .heroPath = firstExistingImage({sceSys + QStringLiteral("/pic0.png"),
                                        sceSys + QStringLiteral("/pic1.png")}),
        .flatpak = flatpak,
        .flatpakAppId = flatpakAppId});
    seenPaths->insert(eboot);
    seenIds->insert(gameId);
    return;
  }
  const QFileInfoList children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& child : children) {
    collectGames(child.absoluteFilePath(), depth + 1, flatpak, flatpakAppId, seenPaths, seenIds,
                 result);
  }
}
} // namespace

QStringList Shadps4Scanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      home + QStringLiteral("/.config/shadps4"),
      home + QStringLiteral("/.config/shadPS4"),
      home + QStringLiteral("/.local/share/shadps4"),
      home + QStringLiteral("/.local/share/shadPS4"),
      home + QStringLiteral("/.var/app/net.shadps4.shadPS4/config/shadPS4"),
      home + QStringLiteral("/.var/app/net.shadps4.shadPS4/data/shadPS4"),
  };
  candidates.removeDuplicates();

  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/config.toml")).isFile()) {
      roots.append(root);
    }
  }
  return roots;
}

Shadps4ScanResult Shadps4Scanner::scan(const QStringList& roots) {
  Shadps4ScanResult result;
  QSet<QString> seenPaths;
  QSet<QString> seenIds;
  for (const QString& root : roots) {
    const QString configPath = root + QStringLiteral("/config.toml");
    QFile configFile(configPath);
    if (!configFile.exists()) {
      continue;
    }
    result.roots.append(root);
    if (!configFile.open(QIODevice::ReadOnly) || configFile.size() > kMaximumConfigBytes) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(configPath));
      continue;
    }
    const QString contents = QString::fromUtf8(configFile.readAll());
    const bool flatpak = root.contains(QStringLiteral("/.var/app/net.shadps4.shadPS4/"));
    const QString flatpakAppId =
        flatpak ? QStringLiteral("net.shadps4.shadPS4") : QStringLiteral("");
    const QStringList directories = installDirectoriesFromConfig(contents);
    if (directories.isEmpty()) {
      continue;
    }
    for (const QString& configured : directories) {
      const QString expanded = expandPath(configured);
      const QFileInfo directoryInfo(expanded);
      if (!directoryInfo.isDir() || !directoryInfo.isReadable()) {
        result.incomplete = true;
        result.warnings.append(
            QStringLiteral("Game directory is unavailable: %1").arg(expanded));
        continue;
      }
      collectGames(expanded, 0, flatpak, flatpakAppId, &seenPaths, &seenIds, &result);
    }
  }
  return result;
}

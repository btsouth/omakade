#include "sources/xenia/XeniaScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
constexpr int kMaximumScanDepth = 5;
constexpr qint64 kMaximumTomlBytes = 4 * 1024 * 1024;

const QStringList& discExtensions() {
  static const QStringList extensions = {QStringLiteral("iso"), QStringLiteral("xex"),
                                         QStringLiteral("zar")};
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

bool looksLikeTitleDirectory(const QDir& directory) {
  // Extracted dumps keep the executable at the root; the data folder marks a
  // full dump rather than a folder that merely contains other games.
  return QFileInfo::exists(directory.filePath(QStringLiteral("default.xex")));
}

QString cleanTitle(const QString& fileName) {
  static const QRegularExpression bracketed(QStringLiteral("\\[.*?\\]"));
  static const QRegularExpression parenthesized(QStringLiteral("\\(.*?\\)"));
  QString name = QFileInfo(fileName).completeBaseName();
  name.remove(bracketed);
  name.remove(parenthesized);
  // "Legend of Zelda, The - ..." reads better with the article in front.
  static const QRegularExpression trailingArticle(QStringLiteral("^(.+?), (The|A|An)( - .*)?$"));
  const QRegularExpressionMatch match = trailingArticle.match(name.simplified());
  if (match.hasMatch()) {
    name = match.captured(2) + QLatin1Char(' ') + match.captured(1) + match.captured(3);
  }
  return name.simplified();
}

QString titleFromDisc(const QString& isoPath) {
  // XGD discs store the title in the UDF volume label region; the primary
  // volume descriptor sits at sector 16 (offset 0x8000) in both ISO9660 and
  // XGD2 images. Xenia games commonly carry the readable name there, but the
  // label is short and sometimes uppercase-abbreviated, so treat it as a hint
  // and fall back to the file name when it looks unusable.
  QFile file(isoPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  if (!file.seek(0x8000 + 40) || file.size() < 0x8028 + 32) {
    return {};
  }
  const QByteArray label = file.read(32);
  const int end = label.indexOf('\0');
  const QString name = QString::fromLatin1(end < 0 ? label : label.left(end))
                           .simplified();
  if (name.size() >= 3 && name.contains(QLatin1Char(' '))) {
    return name;
  }
  return {};
}

QString sidecarCover(const QString& titlePath) {
  const QFileInfo title(titlePath);
  const QString stem = title.absolutePath() + QLatin1Char('/') + title.completeBaseName();
  for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                   QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

QStringList singleQuotedTomlStrings(const QString& text) {
  QStringList values;
  static const QRegularExpression quoted(QStringLiteral("'([^']*)'"));
  QRegularExpressionMatchIterator it = quoted.globalMatch(text);
  while (it.hasNext()) {
    const QString value = it.next().captured(1);
    if (!value.isEmpty()) {
      values.append(value);
    }
  }
  return values;
}

void normalizeWindowsPath(QString* path) {
  if (path->length() >= 2 && path->at(1) == QLatin1Char(':')) {
    // Wine maps drive letters to Unix roots: "Z:\home\salt\..." is
    // "/home/salt/..." and "C:\foo" falls back to the C drive prefix used by
    // some prefixes ("Z" is the standard root mapping; other letters rarely
    // appear in saved paths). Drive prefix and leading backslash come off,
    // then separators flip to forward slashes.
    if (path->length() >= 3 && (*path)[2] == QLatin1Char('\\')) {
      path->remove(0, 3);
    } else {
      path->remove(0, 2);
    }
    path->replace(QLatin1Char('\\'), QLatin1Char('/'));
    path->prepend(QLatin1Char('/'));
  }
  *path = QDir::cleanPath(*path);
}

struct RecentTitle {
  QString path;
  QString title;
};

QVector<RecentTitle> recentTitles(const QString& storageRoot) {
  QVector<RecentTitle> titles;
  QFile file(storageRoot + QStringLiteral("/recent.toml"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text) ||
      file.size() > kMaximumTomlBytes) {
    return titles;
  }
  // Entries look like:
  // [0]
  // last_run_time = 1788911947
  // path = 'Z:\home\salt\Games\Fable II\default.xex'
  // title_name = 'Fable II'
  static const QRegularExpression pathValue(
      QStringLiteral("^\\s*path\\s*=\\s*'([^']*)'"));
  static const QRegularExpression nameValue(
      QStringLiteral("^\\s*title_name\\s*=\\s*'([^']*)'"));
  RecentTitle current;
  bool pending = false;
  const QList<QByteArray> lines = file.readAll().split('\n');
  auto flush = [&]() {
    if (pending && !current.path.isEmpty()) {
      titles.append(current);
    }
    current = RecentTitle{};
    pending = false;
  };
  for (const QByteArray& raw : lines) {
    const QString line = QString::fromUtf8(raw);
    if (line.trimmed().startsWith(QLatin1Char('['))) {
      flush();
      continue;
    }
    const QRegularExpressionMatch pathMatch = pathValue.match(line);
    if (pathMatch.hasMatch()) {
      if (pending) {
        flush();
      }
      pending = true;
      current.path = pathMatch.captured(1);
      continue;
    }
    const QRegularExpressionMatch nameMatch = nameValue.match(line);
    if (nameMatch.hasMatch()) {
      if (pending) {
        current.title = nameMatch.captured(1);
      }
    }
  }
  flush();
  for (RecentTitle& title : titles) {
    normalizeWindowsPath(&title.path);
  }
  return titles;
}

void collectGames(const QString& directory, int depth, bool flatpak,
                  const QString& flatpakAppId, QSet<QString>* seenPaths,
                  QSet<QString>* seenIds, XeniaScanResult* result) {
  if (depth > kMaximumScanDepth) {
    return;
  }
  const QDir dir(directory);
  if (!dir.exists()) {
    return;
  }
  if (looksLikeTitleDirectory(dir)) {
    const QString xex = QDir::cleanPath(dir.filePath(QStringLiteral("default.xex")));
    if (seenPaths->contains(xex)) {
      return;
    }
    QString title = cleanTitle(dir.dirName());
    if (title.isEmpty()) {
      return;
    }
    const QString gameId = QStringLiteral("path:%1").arg(xex);
    if (seenIds->contains(gameId)) {
      return;
    }
    result->games.append(XeniaGameRecord{.gameId = gameId,
                                         .title = title,
                                         .path = xex,
                                         .coverPath = sidecarCover(xex),
                                         .flatpak = flatpak,
                                         .flatpakAppId = flatpakAppId});
    seenPaths->insert(xex);
    seenIds->insert(gameId);
    return;
  }
  // Disc images are games in their own right; deeper folders may hold more.
  const QFileInfoList children =
      dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& child : children) {
    if (child.isDir()) {
      collectGames(child.absoluteFilePath(), depth + 1, flatpak, flatpakAppId, seenPaths,
                   seenIds, result);
      continue;
    }
    if (!isDiscFile(child.fileName())) {
      continue;
    }
    const QString path = QDir::cleanPath(child.absoluteFilePath());
    if (seenPaths->contains(path)) {
      continue;
    }
    // ".xex" files that are not named default.xex are standalone titles too.
    if (child.suffix().compare(QStringLiteral("xex"), Qt::CaseInsensitive) == 0) {
      QString title = cleanTitle(child.completeBaseName());
      const QString gameId = QStringLiteral("path:%1").arg(path);
      if (title.isEmpty() || seenIds->contains(gameId)) {
        continue;
      }
      result->games.append(XeniaGameRecord{.gameId = gameId,
                                           .title = title,
                                           .path = path,
                                           .coverPath = sidecarCover(path),
                                           .flatpak = flatpak,
                                           .flatpakAppId = flatpakAppId});
      seenPaths->insert(path);
      seenIds->insert(gameId);
      continue;
    }
    // ISO images: prefer the volume label, fall back to the file name.
    QString title = titleFromDisc(path);
    if (title.isEmpty()) {
      title = cleanTitle(child.completeBaseName());
    }
    const QString gameId = QStringLiteral("path:%1").arg(path);
    if (title.isEmpty() || seenIds->contains(gameId)) {
      continue;
    }
    result->games.append(XeniaGameRecord{.gameId = gameId,
                                         .title = title,
                                         .path = path,
                                         .coverPath = sidecarCover(path),
                                         .flatpak = flatpak,
                                         .flatpakAppId = flatpakAppId});
    seenPaths->insert(path);
    seenIds->insert(gameId);
  }
}
} // namespace

QStringList XeniaScanner::discoverRoots() {
  const QString home = QDir::homePath();
  QStringList candidates = {
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
          QStringLiteral("/Xenia"),
      home + QStringLiteral("/.local/share/Xenia"),
      home + QStringLiteral("/Xenia"),
      home + QStringLiteral("/.var/app/org.xenia.xenia/data/Xenia"),
  };
  candidates.removeDuplicates();

  QStringList roots;
  for (const QString& root : candidates) {
    if (QFileInfo(root + QStringLiteral("/xenia-canary.config.toml")).isFile() ||
        QFileInfo(root + QStringLiteral("/xenia.config.toml")).isFile() ||
        QFileInfo(root + QStringLiteral("/recent.toml")).isFile()) {
      roots.append(root);
    }
  }
  return roots;
}

XeniaScanResult XeniaScanner::scan(const QStringList& roots) {
  XeniaScanResult result;
  QSet<QString> seenPaths;
  QSet<QString> seenIds;

  for (const QString& root : roots) {
    const bool hasConfig =
        QFileInfo(root + QStringLiteral("/xenia-canary.config.toml")).isFile() ||
        QFileInfo(root + QStringLiteral("/xenia.config.toml")).isFile();
    if (!hasConfig && !QFileInfo(root + QStringLiteral("/recent.toml")).isFile()) {
      continue;
    }
    result.roots.append(root);

    // Games the emulator has actually run are the highest-confidence entries:
    // recent.toml records the exact launch path and a display title.
    for (const RecentTitle& recent : recentTitles(root)) {
      const QFileInfo info(recent.path);
      if (!info.isFile() || !info.isReadable()) {
        continue;
      }
      if (seenPaths.contains(recent.path)) {
        continue;
      }
      const QString gameId = QStringLiteral("path:%1").arg(recent.path);
      if (seenIds.contains(gameId)) {
        continue;
      }
      QString title = recent.title;
      if (title.isEmpty()) {
        title = cleanTitle(info.completeBaseName());
      }
      if (title.isEmpty()) {
        continue;
      }
      result.games.append(XeniaGameRecord{.gameId = gameId,
                                          .title = title,
                                          .path = recent.path,
                                          .coverPath = sidecarCover(recent.path),
                                          .flatpak = false,
                                          .flatpakAppId = QStringLiteral("")});
      seenPaths.insert(recent.path);
      seenIds.insert(gameId);
    }

    // The storage root itself doubles as a game folder: users keep dumps and
    // ISOs beside the config, and Xenia installs content under
    // content/<profile>/<title id>/. Scanning the root covers both.
    collectGames(root, 0, false, QStringLiteral(""), &seenPaths, &seenIds, &result);
  }
  return result;
}

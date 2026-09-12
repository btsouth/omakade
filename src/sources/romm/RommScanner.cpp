#include "sources/romm/RommScanner.h"

#include "library/ConsoleCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr qsizetype kMaximumPayloadBytes = 16 * 1024 * 1024;
constexpr qsizetype kMaximumPageItems = 1000;

QString confinedFile(const QString& root, QString relative) {
  if (relative.size() > 4096 || relative.contains(QChar::Null) ||
      relative.contains(QLatin1Char('\\'))) {
    return {};
  }
  while (relative.startsWith(QLatin1Char('/')))
    relative.remove(0, 1);
  const QString cleaned = QDir::cleanPath(relative);
  if (cleaned.isEmpty() || cleaned == QStringLiteral(".") ||
      cleaned == QStringLiteral("..") || cleaned.startsWith(QStringLiteral("../"))) {
    return {};
  }
  const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
  if (canonicalRoot.isEmpty() || !QFileInfo(canonicalRoot).isDir())
    return {};
  const QString candidate = QDir(canonicalRoot).absoluteFilePath(cleaned);
  const QFileInfo file(candidate);
  const QString canonical = file.canonicalFilePath();
  if (!file.isFile() || file.isSymLink() || canonical.isEmpty() ||
      !canonical.startsWith(canonicalRoot + QLatin1Char('/'))) {
    return {};
  }
  return canonical;
}

const ConsoleDefinition* consoleFor(const QJsonObject& item) {
  for (const QString& name :
       {item.value(QStringLiteral("platform_slug")).toString(),
        item.value(QStringLiteral("platform_fs_slug")).toString(),
        item.value(QStringLiteral("platform_display_name")).toString()}) {
    if (const auto* console = ConsoleCatalog::find(name))
      return console;
  }
  return nullptr;
}

bool supportedFile(const QString& path, const ConsoleDefinition& console) {
  const QString suffix = QFileInfo(path).suffix();
  return console.extensions.contains(suffix, Qt::CaseInsensitive);
}

QString joinedFilePath(const QJsonObject& file) {
  QString path = file.value(QStringLiteral("file_path")).toString();
  const QString name = file.value(QStringLiteral("file_name")).toString();
  if (path.isEmpty())
    return name;
  if (name.isEmpty() || path == name || path.endsWith(QLatin1Char('/') + name))
    return path;
  return path + QLatin1Char('/') + name;
}

QString resolveContentPath(const QJsonObject& item, const QString& root,
                           const ConsoleDefinition& console) {
  const QJsonArray files = item.value(QStringLiteral("files")).toArray();
  for (const bool gameOnly : {true, false}) {
    for (const QJsonValue& value : files) {
      const QJsonObject file = value.toObject();
      const QString category = file.value(QStringLiteral("category")).toString().toLower();
      if (gameOnly ? category != QStringLiteral("game")
                   : (!category.isEmpty() && category != QStringLiteral("game"))) {
        continue;
      }
      const QString candidate = confinedFile(root, joinedFilePath(file));
      if (!candidate.isEmpty() && supportedFile(candidate, console))
        return candidate;
    }
  }
  const QString fallback =
      item.value(QStringLiteral("fs_path")).toString() + QLatin1Char('/') +
      item.value(QStringLiteral("fs_name")).toString();
  const QString candidate = confinedFile(root, fallback);
  return supportedFile(candidate, console) ? candidate : QString{};
}

QString boundedText(const QJsonValue& value, qsizetype maximum) {
  const QString text = value.toString().trimmed();
  if (text.size() > maximum || text.contains(QChar::Null))
    return {};
  return text;
}
} // namespace

RommScanResult RommScanner::parsePage(const QByteArray& payload,
                                      const QString& localLibraryRoot) {
  RommScanResult result;
  if (payload.isEmpty() || payload.size() > kMaximumPayloadBytes) {
    result.complete = false;
    result.warnings.append(QStringLiteral("RomM returned an empty or oversized response."));
    return result;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    result.complete = false;
    result.warnings.append(QStringLiteral("RomM returned invalid catalog data."));
    return result;
  }
  const QJsonObject response = document.object();
  const QJsonValue itemsValue = response.value(QStringLiteral("items"));
  if (!itemsValue.isArray() || itemsValue.toArray().size() > kMaximumPageItems) {
    result.complete = false;
    result.warnings.append(QStringLiteral("RomM returned an invalid catalog page."));
    return result;
  }
  const QJsonArray items = itemsValue.toArray();
  const int offset = qMax(0, response.value(QStringLiteral("offset")).toInt());
  const int limit = qBound(1, response.value(QStringLiteral("limit")).toInt(items.size()), 1000);
  result.total = qBound(0, response.value(QStringLiteral("total")).toInt(items.size()), 10000000);
  result.nextOffset = offset + items.size();
  result.hasMore = result.nextOffset < result.total ||
                   (!response.contains(QStringLiteral("total")) && items.size() == limit);

  QSet<QString> seen;
  int unavailable = 0;
  for (const QJsonValue& value : items) {
    const QJsonObject item = value.toObject();
    const qint64 id = item.value(QStringLiteral("id")).toInteger();
    const QString appId = id > 0 ? QString::number(id) : QString{};
    const ConsoleDefinition* console = consoleFor(item);
    if (appId.isEmpty() || console == nullptr || seen.contains(appId)) {
      ++unavailable;
      continue;
    }
    const QString path = resolveContentPath(item, localLibraryRoot, *console);
    if (path.isEmpty()) {
      ++unavailable;
      continue;
    }
    QString title = boundedText(item.value(QStringLiteral("name")), 512);
    if (title.isEmpty())
      title = boundedText(item.value(QStringLiteral("fs_name_no_tags")), 512);
    if (title.isEmpty())
      title = QFileInfo(path).completeBaseName();
    if (title.isEmpty()) {
      ++unavailable;
      continue;
    }
    QString cover = boundedText(item.value(QStringLiteral("path_cover_small")), 4096);
    if (cover.isEmpty())
      cover = boundedText(item.value(QStringLiteral("url_cover")), 4096);
    result.games.append({.appId = appId,
                         .title = title,
                         .description = boundedText(item.value(QStringLiteral("summary")), 16384),
                         .contentPath = path,
                         .system = console->id,
                         .platform = console->displayName,
                         .coverReference = cover});
    seen.insert(appId);
  }
  if (unavailable > 0) {
    result.warnings.append(
        QStringLiteral("Skipped %1 RomM entries without a supported local game file.")
            .arg(unavailable));
  }
  return result;
}

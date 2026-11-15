#include "sources/lichii/LichiiScanner.h"

#include "artwork/SwitchTitleReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>

namespace {
constexpr int kMaximumScanDepth = 5;
constexpr qint64 kMaximumConfigBytes = 4 * 1024 * 1024;
constexpr qint64 kMaximumStateBytes = 1024 * 1024;

const QStringList& gameExtensions() {
  static const QStringList extensions = {QStringLiteral("nup"), QStringLiteral("nsp"),
                                         QStringLiteral("xci"), QStringLiteral("nsz"),
                                         QStringLiteral("xcz"), QStringLiteral("nro")};
  return extensions;
}

bool isGameFile(const QString& fileName) {
  const QString suffix = QFileInfo(fileName).suffix();
  for (const QString& extension : gameExtensions()) {
    if (suffix.compare(extension, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

bool hasSuffix(const QString& path, const QString& suffix) {
  return QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) == 0;
}

// Application ids are 01xx (Switch) or 04xx (Switch 2) with the low thirteen bits
// clear. An update sets bit 0x800 and add-on content counts up from there.
bool validTitleId(const QString& id) {
  static const QRegularExpression pattern(QStringLiteral("^0[14]00[0-9A-F]{12}$"));
  return pattern.match(id).hasMatch();
}

bool isApplicationId(const QString& id) {
  bool okay = false;
  const quint64 value = id.toULongLong(&okay, 16);
  return okay && (value & 0x1FFF) == 0;
}

QString titleIdFromFileName(const QString& fileName) {
  static const QRegularExpression pattern(
      QStringLiteral("(?<![0-9A-Fa-f])(0[14]00[0-9A-Fa-f]{12})(?![0-9A-Fa-f])"));
  const QRegularExpressionMatch match = pattern.match(fileName);
  return match.hasMatch() ? match.captured(1).toUpper() : QString{};
}

// A .nup package starts "NUP\1" and holds the offset of a "TOC\1" block whose
// first entry begins with the application id. The layout is read from the
// packages Lichii loads, not from a published specification, so every field is
// checked and anything unexpected leaves the id unknown.
QString titleIdFromPackage(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray header = file.read(16);
  if (header.size() != 16 || !header.startsWith(QByteArrayLiteral("NUP\x01"))) {
    return {};
  }
  const quint64 contents = qFromLittleEndian<quint64>(header.constData() + 8);
  constexpr quint64 kContentsHeaderBytes = 0x30;
  if (contents < 16 || contents > 0x10000 || !file.seek(static_cast<qint64>(contents))) {
    return {};
  }
  const QByteArray block = file.read(kContentsHeaderBytes + 8);
  if (block.size() != kContentsHeaderBytes + 8 || !block.startsWith(QByteArrayLiteral("TOC\x01"))) {
    return {};
  }
  const quint64 id = qFromLittleEndian<quint64>(block.constData() + kContentsHeaderBytes);
  const QString text = QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')).toUpper();
  return validTitleId(text) ? text : QString{};
}

// Dump tools tag add-on files in brackets: "Game [UPD]", "Game (DLC 3)". A bare
// word is left alone, because "Patch Quest" is a game.
bool isAddOnFileName(const QString& fileName) {
  static const QRegularExpression addOn(
      QStringLiteral("[\\[\\(]\\s*(?:dlc|update|upd|patch)\\b[^\\]\\)]*[\\]\\)]"),
      QRegularExpression::CaseInsensitiveOption);
  return addOn.match(QFileInfo(fileName).completeBaseName()).hasMatch();
}

QString cleanTitle(const QString& name) {
  static const QRegularExpression bracketed(QStringLiteral("\\[.*?\\]"));
  static const QRegularExpression parenthesized(QStringLiteral("\\(.*?\\)"));
  static const QRegularExpression bareId(
      QStringLiteral("(?<![0-9A-Fa-f])0[14]00[0-9A-Fa-f]{12}(?![0-9A-Fa-f])"));
  // "Game v1.2.0" loses its version; a name that is nothing but a build number,
  // as in "<title id>_v131072", is no name at all.
  static const QRegularExpression version(QStringLiteral("(?:\\s+v\\d+(?:\\.\\d+)+|^v\\d+)$"),
                                          QRegularExpression::CaseInsensitiveOption);
  QString title = name;
  title.remove(bracketed);
  title.remove(parenthesized);
  title.remove(bareId);
  title.replace(QLatin1Char('_'), QLatin1Char(' '));
  title = title.simplified();
  title.remove(version);
  // "Legend of Zelda, The - ..." reads better with the article in front.
  static const QRegularExpression trailingArticle(QStringLiteral("^(.+?), (The|A|An)( - .*)?$"));
  const QRegularExpressionMatch match = trailingArticle.match(title.simplified());
  if (match.hasMatch()) {
    title = match.captured(2) + QLatin1Char(' ') + match.captured(1) + match.captured(3);
  }
  static const QRegularExpression edges(QStringLiteral("^[\\s\\-_.]+|[\\s\\-_.]+$"));
  title.remove(edges);
  return title.simplified();
}

bool usableTitle(const QString& title) {
  if (title.trimmed().isEmpty() || title.contains(QChar::ReplacementCharacter)) {
    return false;
  }
  return std::none_of(title.cbegin(), title.cend(),
                      [](QChar character) { return character.category() == QChar::Other_Control; });
}

// A portable Lichii bundle keeps one game in "games" beside its own "user"
// folder, and the file is often named after the title id or an abbreviation. The
// bundle folder then carries the name the user knows the game by.
QString bundleTitle(const QFileInfo& game, QHash<QString, int>* gameCounts) {
  const QDir games = game.dir();
  if (games.dirName().compare(QStringLiteral("games"), Qt::CaseInsensitive) != 0) {
    return {};
  }
  QDir bundle = games;
  if (!bundle.cdUp() || !QFileInfo(bundle.filePath(QStringLiteral("user"))).isDir()) {
    return {};
  }
  // Counted once per folder, so a large shared "games" folder is listed only once.
  const QString key = games.absolutePath();
  if (!gameCounts->contains(key)) {
    int count = 0;
    const QFileInfoList files = games.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
      if (isGameFile(file.fileName())) {
        ++count;
      }
    }
    gameCounts->insert(key, count);
  }
  return gameCounts->value(key) == 1 ? cleanTitle(bundle.dirName()) : QString{};
}

QString sidecarCover(const QString& gamePath) {
  const QFileInfo game(gamePath);
  const QString stem = game.absolutePath() + QLatin1Char('/') + game.completeBaseName();
  for (const QString& extension :
       {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

struct GameDirectory {
  QString path;
  bool deepScan = false;
};

struct Config {
  QVector<GameDirectory> directories;
  QStringList recentFiles;
};

bool readConfig(const QString& path, Config* config) {
  const QFileInfo info(path);
  if (!info.isReadable() || info.size() > kMaximumConfigBytes) {
    return false;
  }
  // Lichii writes this file through QSettings, so QSettings is the reader that
  // undoes its escaping. Nothing is ever set on it, so the file is not written.
  QSettings settings(path, QSettings::IniFormat);
  if (settings.status() != QSettings::NoError) {
    return false;
  }
  settings.beginGroup(QStringLiteral("UI"));
  const int count = qBound(0, settings.beginReadArray(QStringLiteral("Paths/gamedirs")), 256);
  for (int index = 0; index < count; ++index) {
    settings.setArrayIndex(index);
    const QString directory = settings.value(QStringLiteral("path")).toString().trimmed();
    // "SDMC", "UserNAND" and "SysNAND" name the emulated console's own storage
    // rather than a folder of game files.
    if (!QDir::isAbsolutePath(directory)) {
      continue;
    }
    config->directories.append(
        {QDir::cleanPath(directory), settings.value(QStringLiteral("deep_scan")).toBool()});
  }
  settings.endArray();
  // Recent files are stored as one string joined with ", ".
  const QVariant recent = settings.value(QStringLiteral("Paths/recentFiles"));
  const QStringList entries =
      recent.typeId() == QMetaType::QStringList
          ? recent.toStringList()
          : recent.toString().split(QStringLiteral(", "), Qt::SkipEmptyParts);
  for (const QString& entry : entries) {
    const QString file = entry.trimmed();
    if (QDir::isAbsolutePath(file)) {
      config->recentFiles.append(QDir::cleanPath(file));
    }
  }
  // QSettings may only find a malformed line while answering the reads above.
  return settings.status() == QSettings::NoError;
}

// play_time/*.bin holds pairs of little-endian 64-bit values: title id, seconds.
QHash<QString, qint64> playtimes(const QString& dataPath) {
  QHash<QString, qint64> seconds;
  const QDir directory(dataPath + QStringLiteral("/play_time"));
  const QFileInfoList files = directory.entryInfoList({QStringLiteral("*.bin")}, QDir::Files);
  for (const QFileInfo& info : files) {
    QFile file(info.absoluteFilePath());
    if (info.size() > kMaximumStateBytes || info.size() % 16 != 0 ||
        !file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QByteArray data = file.readAll();
    for (qsizetype offset = 0; offset + 16 <= data.size(); offset += 16) {
      const quint64 id = qFromLittleEndian<quint64>(data.constData() + offset);
      const quint64 played = qFromLittleEndian<quint64>(data.constData() + offset + 8);
      const QString titleId = QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')).toUpper();
      // Ten years of play is not a real counter; ignore a corrupt record.
      if (!validTitleId(titleId) || played > 10ULL * 365 * 24 * 3600) {
        continue;
      }
      seconds.insert(titleId, qMax(seconds.value(titleId), static_cast<qint64>(played)));
    }
  }
  return seconds;
}

// launched.json: {"<title id>": {"launch_count": 3, "timestamp": 1794664067}}
QHash<QString, qint64> lastLaunches(const QString& cachePath) {
  QHash<QString, qint64> launches;
  QFile file(cachePath + QStringLiteral("/launched.json"));
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumStateBytes) {
    return launches;
  }
  const QJsonObject titles = QJsonDocument::fromJson(file.readAll()).object();
  for (auto title = titles.begin(); title != titles.end(); ++title) {
    const QString titleId = title.key().toUpper();
    // Older Lichii builds stored the bare timestamp instead of an object.
    const QJsonValue entry = title.value();
    const double seconds = entry.isObject()
                               ? entry.toObject().value(QStringLiteral("timestamp")).toDouble()
                               : entry.toDouble();
    // Unix seconds up to the year 9999; anything else is not a real launch time.
    if (validTitleId(titleId) && seconds > 0 && seconds < 253402300800.0) {
      launches.insert(titleId, static_cast<qint64>(seconds));
    }
  }
  return launches;
}

struct ScanState {
  QStringList keyFiles;
  QStringList titleKeyFiles;
  QHash<QString, qint64> playtimes;
  QHash<QString, qint64> launches;
  QSet<QString> seenPaths;
  QHash<QString, int> bundleGameCounts;
  LichiiScanResult* result = nullptr;
};

void addGame(const QString& filePath, ScanState* state) {
  const QString path = QDir::cleanPath(filePath);
  const QFileInfo info(path);
  if (state->seenPaths.contains(path) || isAddOnFileName(info.fileName())) {
    return;
  }
  if (!info.isReadable()) {
    state->result->incomplete = true;
    state->result->warnings.append(QStringLiteral("Could not read %1").arg(path));
    return;
  }
  state->seenPaths.insert(path);

  QString titleId = titleIdFromFileName(info.fileName());
  if (titleId.isEmpty() && hasSuffix(path, QStringLiteral("nup"))) {
    titleId = titleIdFromPackage(path);
  }
  // Switch dumps carry their name and icon in control data that the console
  // keys unlock. Lichii's own keys come first; the Switch 2 package format keeps
  // that data out of reach, so those games are named from their files instead.
  QString embeddedTitle;
  QString cover = sidecarCover(path);
  if ((hasSuffix(path, QStringLiteral("nsp")) || hasSuffix(path, QStringLiteral("xci"))) &&
      SwitchTitleReader::keysAvailable(state->keyFiles)) {
    SwitchTitleInfo embedded;
    const QString cacheKey =
        titleId.isEmpty() ? QString::fromLatin1(
                                QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256)
                                    .toHex()
                                    .left(32))
                          : titleId;
    const QString icon =
        SwitchTitleReader::cachedIcon(path, cacheKey, SwitchTitleReader::defaultCacheRoot(),
                                      &embedded, state->keyFiles, state->titleKeyFiles);
    if (titleId.isEmpty() && validTitleId(embedded.titleId.toUpper())) {
      titleId = embedded.titleId.toUpper();
    }
    embeddedTitle = embedded.title.simplified();
    if (cover.isEmpty()) {
      cover = icon;
    }
  }
  if (!titleId.isEmpty() && !isApplicationId(titleId)) {
    return; // an update or add-on file, not a game of its own
  }

  QString title = usableTitle(embeddedTitle) ? embeddedTitle
                                             : bundleTitle(info, &state->bundleGameCounts);
  if (title.isEmpty()) {
    title = cleanTitle(info.completeBaseName());
  }
  if (title.isEmpty()) {
    // The file is named after its title id alone. A folder made for the game
    // names it better than the id does; a shared folder of dumps does not.
    static const QStringList shared = {QStringLiteral("games"),   QStringLiteral("roms"),
                                       QStringLiteral("rom"),     QStringLiteral("switch"),
                                       QStringLiteral("switch2"), QStringLiteral("switch 2"),
                                       QStringLiteral("nup"),     QStringLiteral("nsp"),
                                       QStringLiteral("xci"),     QStringLiteral("downloads")};
    const QString folder = cleanTitle(info.dir().dirName());
    title = shared.contains(folder.toLower()) ? QString{} : folder;
  }
  if (title.isEmpty()) {
    title = titleId.isEmpty() ? info.completeBaseName() : titleId;
  }
  state->result->games.append(LichiiGameRecord{.gameId = QStringLiteral("path:%1").arg(path),
                                               .titleId = titleId,
                                               .title = title,
                                               .path = path,
                                               .coverPath = cover,
                                               .playtimeSeconds = state->playtimes.value(titleId),
                                               .lastPlayed = state->launches.value(titleId)});
}

void collectGames(const QString& directory, int depth, bool deepScan, ScanState* state) {
  const QDir dir(directory);
  if (!dir.isReadable()) {
    // An unreadable folder lists as empty; its games must not look deleted.
    state->result->incomplete = true;
    state->result->warnings.append(QStringLiteral("Could not read %1").arg(directory));
    return;
  }
  const QFileInfoList children =
      dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo& child : children) {
    if (child.isDir()) {
      // Lichii only looks below a folder the user marked for a deep scan.
      if (deepScan && depth < kMaximumScanDepth && !child.isSymLink()) {
        collectGames(child.absoluteFilePath(), depth + 1, deepScan, state);
      }
      continue;
    }
    if (isGameFile(child.fileName())) {
      addGame(child.absoluteFilePath(), state);
    }
  }
}
} // namespace

QVector<LichiiInstall> LichiiScanner::discoverInstalls() {
  const LichiiInstall install{
      .configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                    QStringLiteral("/lichii"),
      .dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                  QStringLiteral("/lichii"),
      .cachePath = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                   QStringLiteral("/lichii")};
  if (QFileInfo(install.configPath + QStringLiteral("/qt-config.ini")).isFile()) {
    return {install};
  }
  return {};
}

LichiiScanResult LichiiScanner::scan(const QVector<LichiiInstall>& installs) {
  LichiiScanResult result;
  ScanState state;
  state.result = &result;

  for (const LichiiInstall& install : installs) {
    const QString configFile = install.configPath + QStringLiteral("/qt-config.ini");
    if (!QFileInfo(configFile).isFile()) {
      continue;
    }
    result.roots.append(install.configPath);
    Config config;
    if (!readConfig(configFile, &config)) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(configFile));
      continue;
    }
    state.keyFiles = QStringList{install.dataPath + QStringLiteral("/keys/prod.keys")} +
                     SwitchTitleReader::defaultKeyFiles();
    state.titleKeyFiles = QStringList{install.dataPath + QStringLiteral("/keys/title.keys")} +
                          SwitchTitleReader::defaultTitleKeyFiles();
    state.playtimes = playtimes(install.dataPath);
    state.launches = lastLaunches(install.cachePath);

    for (const GameDirectory& directory : config.directories) {
      const QFileInfo info(directory.path);
      if (!info.isDir() || !info.isReadable()) {
        // A missing folder is often a drive that is not mounted right now. Report
        // the scan as incomplete so the cached library is kept, not emptied.
        result.incomplete = true;
        result.warnings.append(
            QStringLiteral("Game directory is unavailable: %1").arg(directory.path));
        continue;
      }
      collectGames(directory.path, 0, directory.deepScan, &state);
    }
    // A game opened with "Load File" sits outside the configured folders; the
    // recent list is the only place Lichii remembers it. A recent file that has
    // since been deleted is history, not a missing library.
    for (const QString& recent : config.recentFiles) {
      if (isGameFile(recent) && QFileInfo(recent).isFile()) {
        addGame(recent, &state);
      }
    }
  }
  return result;
}

bool LichiiScanner::isSwitch2(const LichiiGameRecord& game) {
  return game.titleId.startsWith(QStringLiteral("04")) ||
         hasSuffix(game.path, QStringLiteral("nup"));
}

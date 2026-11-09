#include "saves/SaveBackups.h"
#include "saves/SaveLayouts.h"
#include "tracking/ProcFs.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace {
constexpr qint64 maxSave = 8 * 1024 * 1024;
QString hash(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
bool safeFile(const QString& path, qint64 limit) {
  const QFileInfo info(path);
  return info.isFile() && !info.isSymLink() && info.size() > 0 && info.size() <= limit &&
         info.canonicalFilePath() == info.absoluteFilePath();
}
QByteArray read(const QString& path, qint64 limit) {
  if (!safeFile(path, limit))
    return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  const auto bytes = file.read(limit + 1);
  return bytes.size() <= limit ? bytes : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  if (!QFileInfo::exists(path))
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  return file.write(bytes) == bytes.size() && file.commit();
}
QMap<QString, QString> config(const QString& path, bool* okay) {
  const auto bytes = read(path, 1024 * 1024);
  *okay = !bytes.isEmpty();
  QMap<QString, QString> result;
  const QRegularExpression setting("^\\s*([a-zA-Z0-9_]+)\\s*=\\s*\"([^\"]*)\"\\s*(?:#.*)?$");
  for (const auto& line : QString::fromUtf8(bytes).split('\n')) {
    if (line.trimmed().startsWith("#include")) {
      *okay = false;
      return {};
    }
    const auto match = setting.match(line);
    if (match.hasMatch())
      result[match.captured(1)] = match.captured(2);
    else if (!line.trimmed().isEmpty() && !line.trimmed().startsWith('#')) {
      // Config syntax we cannot interpret must not lead to a guessed save location.
      *okay = false;
      return {};
    }
  }
  return result;
}
QString coreName(const QString& core) {
  static const QMap<QString, QString> supported = {
      {"snes9x_libretro.so", "Snes9x"},
      {"nestopia_libretro.so", "Nestopia"},
      {"mupen64plus_next_libretro.so", "Mupen64Plus-Next"},
      {"mgba_libretro.so", "mGBA"},
      {"genesis_plus_gx_libretro.so", "Genesis Plus GX"}};
  return supported.value(QFileInfo(core).fileName());
}
bool retroArchRunning() {
  const QStringList names{"retroarch",
                          "pcsx2",
                          "pcsx2-qt",
                          "rpcs3",
                          "ppsspp",
                          "PPSSPPSDL",
                          "dolphin-emu",
                          "dolphin-emu-nogui",
                          "ryujinx",
                          "ryujinx-wrapper",
                          "shadps4",
                          "cemu",
                          "melonds",
                          "eden",
                          "yuzu",
                          "suyu",
                          "sudachi",
                          "snes9x",
                          "snes9x-gtk",
                          "nestopia",
                          "fceux",
                          "mednafen",
                          "mgba",
                          "sameboy",
                          "bsnes",
                          "mupen64plus",
                          "blastem",
                          "gens",
                          "duckstation",
                          "duckstation-qt",
                          "flycast"};
  for (const auto& process : ProcFs::listProcesses()) {
    const QString executable = QFileInfo(process.arguments.value(0)).fileName().toLower();
    if (names.contains(process.comm.toLower()) || names.contains(executable))
      return true;
  }
  return false;
}
QJsonObject manifest(const QString& directory) {
  return QJsonDocument::fromJson(read(directory + "/manifest.json", 16384)).object();
}
bool sharedAliases(const QString& setsRoot, const QString& game, bool* exists, QJsonArray* keys,
                   QString* error) {
  const QString path = setsRoot + '/' + hash(game.toUtf8()) + "/shared.json";
  *exists = QFileInfo::exists(path);
  *keys = QJsonArray{};
  if (!*exists)
    return true;
  const QByteArray bytes = read(path, 1024 * 1024);
  const QJsonDocument document = QJsonDocument::fromJson(bytes);
  if (bytes.isEmpty() || !document.isObject() || !document.object().value("keys").isArray()) {
    *error = "The shared save-backup index is damaged.";
    return false;
  }
  const QJsonArray values = document.object().value("keys").toArray();
  if (values.size() > 32) {
    *error = "The shared save-backup index has too many entries.";
    return false;
  }
  static const QRegularExpression sharedKey("^shared:[a-f0-9]{64}$");
  for (const QJsonValue& value : values)
    if (!value.isString() || !sharedKey.match(value.toString()).hasMatch()) {
      *error = "The shared save-backup index is damaged.";
      return false;
    }
  *keys = values;
  return true;
}
bool directoryHasEntries(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists())
    return false;
  if (!info.isDir() || info.isSymLink() || info.canonicalFilePath() != info.absoluteFilePath())
    return true;
  return !QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                               QDir::NoSymLinks).isEmpty();
}
bool safeDirectory(const QString& path) {
  const QFileInfo info(path);
  return info.isDir() && !info.isSymLink() &&
         info.canonicalFilePath() == info.absoluteFilePath();
}
bool copyBackupFile(const QString& source, const QString& destination) {
  const QFileInfo info(source);
  if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != info.absoluteFilePath() ||
      QFileInfo::exists(destination) || !QFile::copy(source, destination))
    return false;
  return QFile::setPermissions(destination, QFile::ReadOwner | QFile::WriteOwner);
}
} // namespace

SaveBackups::SaveBackups(QObject* parent)
    : SaveBackups(QDir::homePath(),
                  QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                      "/retroarch/retroarch.cfg",
                  QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                      "/omakade/save-backups",
                  retroArchRunning, parent) {}
SaveBackups::SaveBackups(QString home, QString configPath, QString root,
                         std::function<bool()> running, QObject* parent)
    : QObject(parent), m_home(std::move(home)), m_config(std::move(configPath)),
      m_root(std::move(root)), m_running(std::move(running)), m_sets(m_root + "/sets", m_running) {
  const auto policy=manifest(m_root+"/policy");
  m_customPolicy=policy.value("format").toInt()==1;
  m_retention=qBound(2,policy.value("retention").toInt(10),50);
  m_storageLimitMiB=qBound(256,policy.value("storageLimitMiB").toInt(2048),8192);
  m_sets.setPolicy(m_retention,qint64(m_storageLimitMiB)*1024*1024,m_customPolicy ? m_root : QString{});
}
QString SaveBackups::gameRoot(const QString& game) const {
  return m_root + '/' + hash(game.toUtf8());
}
QString SaveBackups::discover(const QString& game, const QString& core, bool allowMissing) const {
  const QString name = coreName(core);
  if (name.isEmpty() || !QFileInfo(game).isAbsolute() || !QFileInfo(game).isFile())
    return {};
  const QString extension = QFileInfo(game).suffix().toLower();
  if ((name == "Snes9x" && extension != "sfc" && extension != "smc") ||
      (name == "Nestopia" && extension != "nes") ||
      (name == "Mupen64Plus-Next" && extension != "n64" && extension != "z64" &&
       extension != "v64") ||
      (name == "mGBA" && extension != "gba") ||
      (name == "Genesis Plus GX" && extension != "md" && extension != "gen" && extension != "smd" &&
       extension != "sms" && extension != "gg"))
    return {};
  // mGBA Game Boy titles may require a separate RTC file. Genesis CD titles
  // use different backup storage. Keep both outside this single-file adapter.
  bool okay = false;
  const auto settings = config(m_config, &okay);
  if (!okay)
    return {};
  const auto expand = [this](QString path) {
    if (path.startsWith("~/"))
      path.replace(0, 1, m_home);
    return QFileInfo(path).isAbsolute() ? QDir::cleanPath(path) : QString{};
  };
  if (settings.value("sort_savefiles_by_content_enable") != "false")
    return {};
  if (settings.value("auto_overrides_enable") != "false") {
    const QString overrides = expand(settings.value("rgui_config_directory"));
    if (overrides.isEmpty())
      return {};
    const QStringList names{name, QFileInfo(game).dir().dirName(),
                            QFileInfo(game).completeBaseName()};
    for (const auto& overrideName : names) {
      const QString path = overrides + '/' + name + '/' + overrideName + ".cfg";
      if (!QFileInfo::exists(path))
        continue;
      const auto values = config(path, &okay);
      if (!okay)
        return {};
      for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.key().contains("savefile") || it.key() == "rgui_config_directory")
          return {};
    }
  }
  QString directory;
  if (settings.value("savefiles_in_content_dir") == "true")
    directory = QFileInfo(game).absolutePath();
  else if (settings.value("savefiles_in_content_dir") == "false") {
    directory = expand(settings.value("savefile_directory"));
    if (directory.isEmpty())
      return {};
    if (settings.value("sort_savefiles_enable") == "true")
      directory += '/' + name;
    else if (settings.value("sort_savefiles_enable") != "false")
      return {};
  } else
    return {};
  const QString path = directory + '/' + QFileInfo(game).completeBaseName() + ".srm";
  if (safeFile(path, maxSave))
    return path;
  const QFileInfo parent(directory);
  if (allowMissing && !QFileInfo::exists(path) && !QFileInfo(path).isSymLink() && parent.isDir() &&
      parent.canonicalFilePath() == parent.absoluteFilePath())
    return path;
  return {};
}
QVariantList SaveBackups::list(const QString& game) const {
  QVariantList result = m_sets.versions(game);
  const QDir directory(gameRoot(game));
  if (QFileInfo(directory.path()).canonicalFilePath() !=
      QFileInfo(directory.path()).absoluteFilePath())
    return result;
  for (const auto& id : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                            QDir::Name | QDir::Reversed)) {
    if (!QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(id).hasMatch())
      continue;
    const auto item = manifest(directory.filePath(id));
    if (item.value("game").toString() != game || item.value("format").toInt() != 1)
      continue;
    result.append(QVariantMap{{"id", id},
                              {"createdAt", item.value("createdAt").toString()},
                              {"bytes", item.value("bytes").toInt()}});
  }
  std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
    return a.toMap()["createdAt"].toString() > b.toMap()["createdAt"].toString();
  });
  return result;
}
QVariantMap SaveBackups::previewRelocationBackups(const QString& oldGame,
                                                  const QString& newGame) const {
  QVariantMap result{{QStringLiteral("ok"), false}, {QStringLiteral("refusal"), QString{}}};
  if (oldGame.isEmpty() || newGame.isEmpty() || oldGame == newGame) {
    result[QStringLiteral("refusal")] = "Choose a different game path.";
    return result;
  }
  const QString setsRoot = m_root + "/sets";
  const QString oldLegacyRoot = gameRoot(oldGame);
  const QString oldSetRoot = setsRoot + '/' + hash(oldGame.toUtf8());
  if ((QFileInfo::exists(oldLegacyRoot) && !safeDirectory(oldLegacyRoot)) ||
      (QFileInfo::exists(oldSetRoot) && !safeDirectory(oldSetRoot))) {
    result[QStringLiteral("refusal")] = "Existing save backups are redirected or unavailable.";
    return result;
  }
  const QString newLegacyRoot = gameRoot(newGame);
  const QString newSetRoot = setsRoot + '/' + hash(newGame.toUtf8());
  if (directoryHasEntries(newLegacyRoot) || directoryHasEntries(newSetRoot) ||
      !list(newGame).isEmpty()) {
    result[QStringLiteral("refusal")] = "Save backups already exist at the new location.";
    return result;
  }
  const QVariantList oldVersions = list(oldGame);
  QStringList legacyVersions;
  QStringList setVersions;
  bool shared = false;
  for (const QVariant& value : oldVersions) {
    const QVariantMap entry = value.toMap();
    const QString id = entry.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QStringLiteral("set-"))) {
      legacyVersions.append(id);
    } else if (entry.value(QStringLiteral("storageKey")).toString() == oldGame) {
      setVersions.append(id);
    } else {
      shared = true;
    }
  }
  bool aliasExists = false;
  QJsonArray aliasKeys;
  QString aliasError;
  if (!sharedAliases(setsRoot, oldGame, &aliasExists, &aliasKeys, &aliasError)) {
    result[QStringLiteral("refusal")] = aliasError;
    return result;
  }
  shared = shared || !aliasKeys.isEmpty();
  const bool copyable = !legacyVersions.isEmpty() || !setVersions.isEmpty();
  const bool hasBackups = copyable || shared;
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("hasBackups"), hasBackups);
  result.insert(QStringLiteral("copyable"), copyable);
  result.insert(QStringLiteral("legacyVersions"), legacyVersions);
  result.insert(QStringLiteral("setVersions"), setVersions);
  result.insert(QStringLiteral("hasLegacy"), !legacyVersions.isEmpty());
  result.insert(QStringLiteral("hasSets"), !setVersions.isEmpty());
  result.insert(QStringLiteral("hasShared"), shared);
  result.insert(QStringLiteral("sharedAliasExists"), aliasExists);
  result.insert(QStringLiteral("sharedKeys"), aliasKeys.toVariantList());
  if (hasBackups && (!QFileInfo(m_root).isDir() || QFileInfo(m_root).isSymLink() ||
                     QFileInfo(m_root).canonicalFilePath() != QFileInfo(m_root).absoluteFilePath())) {
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("refusal")] = "The save backup folder is unavailable.";
    return result;
  }
  if (hasBackups && (m_running() || recoveryPending())) {
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("refusal")] =
        "Close emulators and finish pending save recovery before relocating.";
    return result;
  }
  return result;
}
bool SaveBackups::relocationReceiptMatches(const QString& newGame,
                                           const QVariantMap& receipt) const {
  if (newGame.isEmpty() || receipt.value(QStringLiteral("newGame")).toString() != newGame)
    return false;
  const QStringList expectedLegacy =
      receipt.value(QStringLiteral("legacyVersions")).toStringList();
  const QStringList expectedSets = receipt.value(QStringLiteral("setVersions")).toStringList();
  const QStringList expectedShared = receipt.value(QStringLiteral("sharedKeys")).toStringList();
  if (expectedLegacy.isEmpty() && expectedSets.isEmpty() && expectedShared.isEmpty())
    return false;
  const QString legacyRoot = gameRoot(newGame);
  const QString setsRoot = m_root + "/sets";
  const QString setRoot = setsRoot + '/' + hash(newGame.toUtf8());
  const auto entries = [](const QString& path) {
    QStringList result;
    const QFileInfo info(path);
    if (!info.exists())
      return result;
    if (!info.isDir() || info.isSymLink() || info.canonicalFilePath() != info.absoluteFilePath())
      return QStringList{QStringLiteral("<unsafe>")};
    return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                    QDir::NoSymLinks,
                                QDir::Name);
  };
  QStringList sortedExpectedLegacy = expectedLegacy;
  sortedExpectedLegacy.sort();
  QStringList sortedExpectedSets = expectedSets;
  sortedExpectedSets.sort();
  QStringList expectedLegacyEntries = expectedLegacy;
  QStringList expectedSetEntries;
  for (const QString& version : expectedSets) {
    if (!version.startsWith(QStringLiteral("set-")))
      return false;
    expectedSetEntries.append(version.mid(4));
  }
  if (!expectedShared.isEmpty())
    expectedSetEntries.append(QStringLiteral("shared.json"));
  expectedLegacyEntries.sort();
  expectedSetEntries.sort();
  if (entries(legacyRoot) != expectedLegacyEntries || entries(setRoot) != expectedSetEntries)
    return false;

  QStringList actualLegacy;
  QStringList actualSets;
  QStringList actualShared;
  const QVariantList listed = list(newGame);
  for (const QVariant& value : listed) {
    const QVariantMap item = value.toMap();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString storageKey = item.value(QStringLiteral("storageKey")).toString();
    if (!id.startsWith(QStringLiteral("set-")))
      actualLegacy.append(id);
    else if (storageKey == newGame)
      actualSets.append(id);
    else
      actualShared.append(storageKey);
  }
  actualLegacy.sort();
  actualSets.sort();
  actualShared.removeDuplicates();
  actualShared.sort();
  QStringList sortedShared = expectedShared;
  sortedShared.removeDuplicates();
  sortedShared.sort();
  if (actualLegacy != sortedExpectedLegacy || actualSets != sortedExpectedSets ||
      actualShared != sortedShared)
    return false;

  for (const QString& version : expectedLegacy) {
    if (!QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version).hasMatch())
      return false;
    const QString directory = legacyRoot + '/' + version;
    const QJsonObject item = manifest(directory);
    const QByteArray bytes = read(directory + "/save.srm", maxSave);
    if (!safeDirectory(directory) || item.value(QStringLiteral("format")).toInt() != 1 ||
        item.value(QStringLiteral("game")).toString() != newGame || bytes.isEmpty() ||
        item.value(QStringLiteral("sha256")).toString() != hash(bytes) ||
        item.value(QStringLiteral("bytes")).toInt() != bytes.size() ||
        discover(newGame, item.value(QStringLiteral("core")).toString(), true) !=
            item.value(QStringLiteral("source")).toString())
      return false;
  }
  QString validationError;
  if (!m_sets.validateGameCopies(newGame, expectedSets,
                                 [this](const QJsonObject& context) { return resolve(context); },
                                 &validationError))
    return false;
  bool aliasExists = false;
  QJsonArray aliasKeys;
  if (!sharedAliases(setsRoot, newGame, &aliasExists, &aliasKeys, &validationError))
    return false;
  QStringList actualAliasKeys;
  for (const QJsonValue& key : aliasKeys)
    actualAliasKeys.append(key.toString());
  return actualAliasKeys == expectedShared &&
         (expectedShared.isEmpty() || aliasExists);
}
bool SaveBackups::relocationSourceMatches(const QString& oldGame,
                                          const QVariantMap& receipt) const {
  QStringList legacyVersions;
  QStringList setVersions;
  for (const QVariant& value : list(oldGame)) {
    const QVariantMap item = value.toMap();
    const QString id = item.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QStringLiteral("set-")))
      legacyVersions.append(id);
    else if (item.value(QStringLiteral("storageKey")).toString() == oldGame)
      setVersions.append(id);
  }
  QStringList expectedLegacy = receipt.value(QStringLiteral("legacyVersions")).toStringList();
  QStringList expectedSets = receipt.value(QStringLiteral("setVersions")).toStringList();
  legacyVersions.sort();
  setVersions.sort();
  expectedLegacy.sort();
  expectedSets.sort();
  if (legacyVersions != expectedLegacy || setVersions != expectedSets)
    return false;
  bool aliasExists = false;
  QJsonArray aliases;
  QString error;
  if (!sharedAliases(m_root + "/sets", oldGame, &aliasExists, &aliases, &error))
    return false;
  QStringList sharedKeys;
  for (const QJsonValue& alias : aliases)
    sharedKeys.append(alias.toString());
  return sharedKeys == receipt.value(QStringLiteral("sharedKeys")).toStringList();
}
bool SaveBackups::copyRelocationBackups(const QString& oldGame, const QString& newGame,
                                        QVariantMap* receipt, QString* error,
                                        const SaveSetStore::CopyFile& copyFile) {
  if (!receipt || !error)
    return false;
  *receipt = {};
  error->clear();
  const QVariantMap preview = previewRelocationBackups(oldGame, newGame);
  if (!preview.value(QStringLiteral("ok")).toBool()) {
    *error = preview.value(QStringLiteral("refusal")).toString();
    return false;
  }
  const QStringList legacyVersions = preview.value(QStringLiteral("legacyVersions")).toStringList();
  const QStringList setVersions = preview.value(QStringLiteral("setVersions")).toStringList();
  const QStringList sharedKeys = preview.value(QStringLiteral("sharedKeys")).toStringList();
  if (legacyVersions.isEmpty() && setVersions.isEmpty() && sharedKeys.isEmpty())
    return true;
  if (m_running() || recoveryPending()) {
    *error = "Close emulators and finish pending save recovery before copying backups.";
    return false;
  }
  if (!QDir().mkpath(m_root) || !safeDirectory(m_root) ||
      !QDir().mkpath(m_root + "/sets") || !safeDirectory(m_root + "/sets")) {
    *error = "The save backup folder is unavailable or redirected.";
    return false;
  }
  QLockFile legacyLock(m_root + "/.lock");
  QLockFile setsLock(m_root + "/sets/.lock");
  legacyLock.setStaleLockTime(0);
  setsLock.setStaleLockTime(0);
  if (!legacyLock.tryLock(0) || !setsLock.tryLock(0) || m_running() || recoveryPending()) {
    *error = "Save backups are busy or recovery is pending. Try again when emulators are closed.";
    return false;
  }
  if (directoryHasEntries(gameRoot(newGame)) ||
      directoryHasEntries(m_root + "/sets/" + hash(newGame.toUtf8())) || !list(newGame).isEmpty()) {
    *error = "Save backups already exist at the new location.";
    return false;
  }
  const QVariantMap lockedPreview = previewRelocationBackups(oldGame, newGame);
  if (!lockedPreview.value(QStringLiteral("ok")).toBool() ||
      lockedPreview.value(QStringLiteral("legacyVersions")).toStringList() != legacyVersions ||
      lockedPreview.value(QStringLiteral("setVersions")).toStringList() != setVersions ||
      lockedPreview.value(QStringLiteral("sharedKeys")).toStringList() != sharedKeys) {
    *error = lockedPreview.value(QStringLiteral("refusal")).toString();
    if (error->isEmpty())
      *error = "The save-backup history changed. Review the relocation again.";
    return false;
  }
  bool aliasExists = false;
  QJsonArray aliasKeys;
  if (!sharedAliases(m_root + "/sets", oldGame, &aliasExists, &aliasKeys, error))
    return false;
  QStringList currentSharedKeys;
  for (const QJsonValue& key : aliasKeys)
    currentSharedKeys.append(key.toString());
  if (currentSharedKeys != sharedKeys) {
    *error = "The shared save-backup index changed. Review the relocation again.";
    return false;
  }

  SaveSetStore::CopyFile copier = copyFile ? copyFile : SaveSetStore::CopyFile(copyBackupFile);
  QTemporaryDir legacyStage(m_root + "/.relocation-legacy-XXXXXX");
  QTemporaryDir setsStage(m_root + "/sets/.relocation-sets-XXXXXX");
  if (!legacyStage.isValid() || !setsStage.isValid()) {
    *error = "Could not prepare a temporary save-backup copy.";
    return false;
  }
  QStringList copiedLegacy;
  for (const QString& version : legacyVersions) {
    if (!QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version).hasMatch()) {
      *error = "A legacy save-backup identifier is invalid.";
      return false;
    }
    const QString sourceDirectory = gameRoot(oldGame) + '/' + version;
    const QJsonObject sourceManifest = manifest(sourceDirectory);
    const QByteArray savedBytes = read(sourceDirectory + "/save.srm", maxSave);
    if (!safeDirectory(sourceDirectory) || sourceManifest.value(QStringLiteral("format")).toInt() != 1 ||
        sourceManifest.value(QStringLiteral("game")).toString() != oldGame || savedBytes.isEmpty() ||
        sourceManifest.value(QStringLiteral("sha256")).toString() != hash(savedBytes) ||
        sourceManifest.value(QStringLiteral("bytes")).toInt() != savedBytes.size()) {
      *error = "A legacy save backup is damaged or no longer belongs to the old path.";
      return false;
    }
    const QString newSavePath = discover(newGame, sourceManifest.value(QStringLiteral("core")).toString(),
                                         true);
    if (newSavePath.isEmpty()) {
      *error = "The legacy save location could not be resolved for the new game path.";
      return false;
    }
    const QString destinationDirectory = legacyStage.path() + '/' + version;
    if (!QDir().mkpath(destinationDirectory) ||
        !copier(sourceDirectory + "/save.srm", destinationDirectory + "/save.srm")) {
      *error = "Could not copy a legacy save backup.";
      return false;
    }
    QJsonObject destinationManifest = sourceManifest;
    destinationManifest.insert(QStringLiteral("game"), newGame);
    destinationManifest.insert(QStringLiteral("source"), newSavePath);
    if (!write(destinationDirectory + "/manifest.json",
               QJsonDocument(destinationManifest).toJson()) ||
        read(destinationDirectory + "/save.srm", maxSave) != savedBytes) {
      *error = "The copied legacy save backup failed verification.";
      return false;
    }
    const QJsonObject verifiedManifest = manifest(destinationDirectory);
    if (verifiedManifest.value(QStringLiteral("format")).toInt() != 1 ||
        verifiedManifest.value(QStringLiteral("game")).toString() != newGame ||
        verifiedManifest.value(QStringLiteral("source")).toString() != newSavePath ||
        verifiedManifest.value(QStringLiteral("sha256")).toString() != hash(savedBytes) ||
        verifiedManifest.value(QStringLiteral("bytes")).toInt() != savedBytes.size()) {
      *error = "The copied legacy save manifest failed verification.";
      return false;
    }
    copiedLegacy.append(version);
  }

  QStringList copiedSets;
  if (!setVersions.isEmpty() &&
      !m_sets.stageGameCopy(oldGame, newGame,
                            [this](const QJsonObject& context) { return resolve(context); },
                            setsStage.path(), copier, &copiedSets, error))
    return false;
  if (copiedSets != setVersions) {
    *error = "The per-game save-set list changed. Review the relocation again.";
    return false;
  }
  if (!sharedKeys.isEmpty()) {
    QJsonArray keys;
    for (const QString& key : sharedKeys)
      keys.append(key);
    if (!write(setsStage.path() + "/shared.json",
               QJsonDocument(QJsonObject{{QStringLiteral("keys"), keys}}).toJson())) {
      *error = "Could not stage the shared save-backup links.";
      return false;
    }
  }
  if (m_running() || recoveryPending()) {
    *error = "Save recovery started while the backups were being copied.";
    return false;
  }

  QStringList committedRoots;
  const auto commitStage = [&committedRoots](QTemporaryDir& stage, const QString& parent,
                                             const QString& destination) {
    if (!directoryHasEntries(stage.path()))
      return false;
    if (QFileInfo::exists(destination) &&
        (directoryHasEntries(destination) || !QDir().rmdir(destination)))
      return false;
    const QString stageName = QFileInfo(stage.path()).fileName();
    const QString destinationName = QFileInfo(destination).fileName();
    if (!QDir(parent).rename(stageName, destinationName))
      return false;
    stage.setAutoRemove(false);
    committedRoots.append(destination);
    return true;
  };
  const auto removeCommitted = [&committedRoots] {
    bool removed = true;
    for (auto it = committedRoots.crbegin(); it != committedRoots.crend(); ++it)
      removed = QDir(*it).removeRecursively() && removed;
    committedRoots.clear();
    return removed;
  };
  const QString destinationLegacyRoot = gameRoot(newGame);
  const QString destinationSetRoot = m_root + "/sets/" + hash(newGame.toUtf8());
  if ((!copiedLegacy.isEmpty() &&
       !commitStage(legacyStage, m_root, destinationLegacyRoot)) ||
      ((!copiedSets.isEmpty() || !sharedKeys.isEmpty()) &&
       !commitStage(setsStage, m_root + "/sets", destinationSetRoot))) {
    const bool removed = removeCommitted();
    *error = removed ? "Could not atomically commit the copied save backups."
                     : "Could not commit the save backups or remove the partial destination copy.";
    return false;
  }

  QStringList verifiedLegacy;
  QStringList verifiedSets;
  for (const QVariant& value : list(newGame)) {
    const QVariantMap entry = value.toMap();
    const QString id = entry.value(QStringLiteral("id")).toString();
    if (id.startsWith(QStringLiteral("set-"))) {
      if (entry.value(QStringLiteral("storageKey")).toString() == newGame)
        verifiedSets.append(id);
    } else {
      verifiedLegacy.append(id);
    }
  }
  bool newAliasExists = false;
  QJsonArray newAliasKeys;
  QString verifyError;
  const bool aliasOkay = sharedAliases(m_root + "/sets", newGame, &newAliasExists,
                                       &newAliasKeys, &verifyError);
  QStringList verifiedSharedKeys;
  for (const QJsonValue& key : newAliasKeys)
    verifiedSharedKeys.append(key.toString());
  if (verifiedLegacy != copiedLegacy || verifiedSets != copiedSets ||
      (!sharedKeys.isEmpty() && (!aliasOkay || !newAliasExists || verifiedSharedKeys != sharedKeys))) {
    const bool removed = removeCommitted();
    *error = removed ? "The copied save backups failed final verification."
                     : "The copied save backups failed verification and the destination could not be removed.";
    return false;
  }
  *receipt = {{QStringLiteral("newGame"), newGame},
              {QStringLiteral("legacyVersions"), copiedLegacy},
              {QStringLiteral("setVersions"), copiedSets},
              {QStringLiteral("sharedKeys"), sharedKeys}};
  return true;
}
bool SaveBackups::rollbackRelocationBackups(const QString& newGame, const QVariantMap& receipt,
                                            QString* error) {
  if (!error)
    return false;
  error->clear();
  if (receipt.isEmpty())
    return true;
  if (receipt.value(QStringLiteral("newGame")).toString() != newGame) {
    *error = "The relocation backup receipt does not match the new path.";
    return false;
  }
  if (m_running() || recoveryPending()) {
    *error = "Could not remove the copied backups while recovery or an emulator is active.";
    return false;
  }
  if (!QDir().mkpath(m_root) || !safeDirectory(m_root) ||
      !QDir().mkpath(m_root + "/sets") || !safeDirectory(m_root + "/sets")) {
    *error = "The save backup folder is unavailable or redirected.";
    return false;
  }
  QLockFile legacyLock(m_root + "/.lock");
  QLockFile setsLock(m_root + "/sets/.lock");
  legacyLock.setStaleLockTime(0);
  setsLock.setStaleLockTime(0);
  if (!legacyLock.tryLock(0) || !setsLock.tryLock(0) || m_running() || recoveryPending()) {
    *error = "Save backups are busy or recovery is pending.";
    return false;
  }
  const QString legacyRoot = gameRoot(newGame);
  const QString setRoot = m_root + "/sets/" + hash(newGame.toUtf8());
  QStringList legacyDirectories;
  QStringList setDirectories;
  for (const QString& version : receipt.value(QStringLiteral("legacyVersions")).toStringList()) {
    if (!QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version).hasMatch()) {
      *error = "The relocation receipt contains an invalid legacy version.";
      return false;
    }
    const QString directory = legacyRoot + '/' + version;
    const QJsonObject item = manifest(directory);
    const QByteArray bytes = read(directory + "/save.srm", maxSave);
    if (!safeDirectory(directory) || item.value(QStringLiteral("format")).toInt() != 1 ||
        item.value(QStringLiteral("game")).toString() != newGame || bytes.isEmpty() ||
        item.value(QStringLiteral("sha256")).toString() != hash(bytes) ||
        item.value(QStringLiteral("bytes")).toInt() != bytes.size()) {
      *error = "Could not safely identify a copied legacy backup.";
      return false;
    }
    legacyDirectories.append(directory);
  }
  for (const QString& version : receipt.value(QStringLiteral("setVersions")).toStringList()) {
    if (!version.startsWith(QStringLiteral("set-")) ||
        !QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version.mid(4)).hasMatch()) {
      *error = "The relocation receipt contains an invalid save-set version.";
      return false;
    }
    const QString directory = setRoot + '/' + version.mid(4);
    const QByteArray manifestBytes = read(directory + "/manifest.json", 16 * 1024 * 1024);
    const QJsonObject item = QJsonDocument::fromJson(manifestBytes).object();
    if (!safeDirectory(directory) || manifestBytes.isEmpty() ||
        item.value(QStringLiteral("format")).toInt() != 2 ||
        item.value(QStringLiteral("game")).toString() != newGame ||
        item.value(QStringLiteral("context")).toObject().value(QStringLiteral("game")).toString() !=
            newGame) {
      *error = "Could not safely identify a copied save-set backup.";
      return false;
    }
    setDirectories.append(directory);
  }
  const QStringList copiedShared = receipt.value(QStringLiteral("sharedKeys")).toStringList();
  const QString aliasPath = setRoot + "/shared.json";
  if (!copiedShared.isEmpty()) {
    bool aliasExists = false;
    QJsonArray aliasKeys;
    if (!sharedAliases(m_root + "/sets", newGame, &aliasExists, &aliasKeys, error))
      return false;
    QStringList currentKeys;
    for (const QJsonValue& key : aliasKeys)
      currentKeys.append(key.toString());
    if (!aliasExists || currentKeys != copiedShared) {
      *error = "Could not safely identify the copied shared-backup links.";
      return false;
    }
  }
  for (const QString& directory : legacyDirectories)
    if (!QDir(directory).removeRecursively()) {
      *error = "Could not remove a copied legacy backup.";
      return false;
    }
  for (const QString& directory : setDirectories)
    if (!QDir(directory).removeRecursively()) {
      *error = "Could not remove a copied save-set backup.";
      return false;
    }
  if (!copiedShared.isEmpty() && !QFile::remove(aliasPath)) {
    *error = "Could not remove the copied shared-backup links.";
    return false;
  }
  const auto emptyDirectory = [](const QString& path) {
    return QDir(path).exists() &&
           QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                QDir::NoSymLinks).isEmpty();
  };
  if (emptyDirectory(legacyRoot))
    QDir().rmdir(legacyRoot);
  if (emptyDirectory(setRoot))
    QDir().rmdir(setRoot);
  return true;
}
SaveLayout SaveBackups::resolve(const QJsonObject& context) const {
  return resolveSaveLayout(context, m_home, m_config);
}
bool SaveBackups::retryRecovery() {
  QString error;
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &error)) {
    report(error);
    return false;
  }
  report("Save recovery finished.");
  return true;
}
bool SaveBackups::keepCurrentSaves() {
  QString error, folder;
  if (!m_sets.archiveRecovery(&folder, &error)) {
    report(error);
    return false;
  }
  report(folder.isEmpty() ? QString("No save recovery is pending.")
                          : "Kept the current saves. Recovery copies were moved to " + folder + ".");
  return true;
}
bool SaveBackups::protectLaunch(const QString& source, const QString& game, const QString& core,
                                bool flatpak, const QString& id, const QString& runner,
                                const QString& target) {
  const QJsonObject context{{"source", source},   {"game", game}, {"core", core},
                            {"flatpak", flatpak}, {"id", id},     {"runner", runner},
                            {"target", target}};
  QString error;
  // Roll back an interrupted restore even with protection off, but only block when it is on.
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &error)) {
    if (!m_enabled) {
      report(error + " Save protection is off, so the game was launched anyway.", true);
      return true;
    }
    if (m_sets.pendingAffects(game, resolve(context))) {
      report(error + " Launch is paused for this game. In Save Backups, retry recovery or "
                     "keep the current saves.",
             true);
      return false;
    }
    report(error + " This game's saves were not copied.", true);
    return true;
  }
  if (!m_enabled)
    return true;
  if (m_running()) {
    report("Save copy skipped while an emulator is running.", true);
    return true;
  }
  const auto layout = resolve(context);
  if (!layout.valid()) {
    report(layout.error, true);
    return true;
  }
  if (layout.allowEmptySnapshot && layout.files.isEmpty() && layout.trees.isEmpty()) {
    return true;
  }
  if (!m_sets.snapshot(game, context, layout, &error, layout.allowEmptySnapshot))
    report(error, true);
  else {
    if (!m_game.isEmpty())
      m_versions = list(m_game);
    ++m_revision;
    emit changed();
  }
  return true;
}
void SaveBackups::selectLaunch(const QString& source, const QString& game, const QString& core,
                               bool flatpak, const QString& id, const QString& runner,
                               const QString& target) {
  m_game = game;
  m_context = {{"source", source},
               {"game", game},
               {"core", core},
               {"flatpak", flatpak},
               {"id", id},
               {"runner", runner},
               {"target", target}};
  m_versions = list(game);
  m_message.clear();
  if (source == "RetroArch" && (core.isEmpty() || core == "DETECT")) {
    report("Existing saves are copied before launch using the selected emulator.");
    return;
  }
  const auto layout = resolve(m_context);
  report(
      layout.valid()
          ? (layout.description +
             (count(game) == 0 ? ". No backup yet. Existing saves are copied before launch." : "."))
          : layout.error);
}
int SaveBackups::count(const QString& game) const { return game.isEmpty() ? 0 : list(game).size(); }
qint64 SaveBackups::storageBytes() const {
  qint64 total = 0;
  for (const auto& version : m_versions)
    total += qMax<qint64>(0, version.toMap()["bytes"].toLongLong());
  return total;
}
bool SaveBackups::canSnapshot() const {
  return !m_game.isEmpty() && !m_context.isEmpty() && resolve(m_context).valid();
}
void SaveBackups::selectGame(const QString& game) {
  m_game = game;
  m_context = {};
  m_versions = list(game);
  m_message.clear();
  emit changed();
}
void SaveBackups::report(const QString& message, bool warn) {
  m_message = message;
  ++m_revision;
  m_versions = list(m_game);
  emit changed();
  if (warn) {
    qWarning().noquote() << "Omakade save protection:" << message;
    emit warning(message);
  }
}
bool SaveBackups::snapshot(const QString& game, const QString& core, const QString& source,
                           QString* error) {
  const auto bytes = read(source, maxSave);
  if (bytes.isEmpty()) {
    *error = "Could not read the save safely.";
    return false;
  }
  const auto digest = hash(bytes);
  auto existing = list(game);
  existing.erase(std::remove_if(existing.begin(), existing.end(),
                                [](const QVariant& v) {
                                  return v.toMap()["id"].toString().startsWith("set-");
                                }),
                 existing.end());
  if (!existing.isEmpty()) {
    const QString previous = gameRoot(game) + '/' + existing.first().toMap().value("id").toString();
    const auto saved = manifest(previous);
    if (saved.value("sha256").toString() == digest &&
        saved.value("bytes").toInt() == bytes.size() &&
        saved.value("source").toString() == source && saved.value("core").toString() == core &&
        hash(read(previous + "/save.srm", maxSave)) == digest)
      return true;
  }
  qint64 used = 0;
  int entries = 0;
  QDirIterator files(m_root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (files.hasNext()) {
    files.next();
    if (!m_customPolicy && files.filePath().startsWith(m_root + "/sets/")) continue;
    if (files.filePath().startsWith(m_root + "/sets/recovery-archive/")) continue;
    used += files.fileInfo().size();
    if (++entries > 20000 || used + bytes.size() > qint64(m_customPolicy ? m_storageLimitMiB : 256)*1024*1024) {
      *error = "The backup storage limit was reached. Existing backups were kept.";
      return false;
    }
  }
  const QString root = gameRoot(game);
  if (!QDir().mkpath(root) ||
      QFileInfo(root).canonicalFilePath() != QFileInfo(root).absoluteFilePath()) {
    *error = "The save backup folder is unavailable.";
    return false;
  }
  QFile::setPermissions(m_root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  QFile::setPermissions(root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  QTemporaryDir stage(root + "/.pending-XXXXXX");
  if (!stage.isValid()) {
    *error = "Could not create a save backup.";
    return false;
  }
  const auto now = QDateTime::currentDateTimeUtc();
  QJsonObject item{{"format", 1},
                   {"game", game},
                   {"core", core},
                   {"source", source},
                   {"createdAt", now.toString(Qt::ISODateWithMs)},
                   {"sha256", digest},
                   {"bytes", bytes.size()}};
  if (!write(stage.path() + "/save.srm", bytes) ||
      !write(stage.path() + "/manifest.json", QJsonDocument(item).toJson()) ||
      read(stage.path() + "/save.srm", maxSave) != bytes || read(source, maxSave) != bytes ||
      m_running()) {
    *error = "The save changed or could not be backed up safely.";
    return false;
  }
  const qint64 previousTime =
      existing.isEmpty() ? 0
                         : existing.first().toMap().value("id").toString().left(17).toLongLong();
  const QString nextTime =
      QString::number(qMax(now.toString("yyyyMMddHHmmsszzz").toLongLong(), previousTime + 1));
  const QString id = nextTime + '-' + QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().rename(stage.path(), root + '/' + id)) {
    *error = "Could not finish the save backup.";
    return false;
  }
  stage.setAutoRemove(false);
  // Only prune this game's own committed version directories after a complete new copy.
  auto versions = list(game);
  versions.erase(std::remove_if(versions.begin(), versions.end(),
                                [](const QVariant& v) {
                                  return v.toMap()["id"].toString().startsWith("set-");
                                }),
                 versions.end());
  for (int i = qMax(m_retention,int(existing.size())); i < versions.size(); ++i)
    QDir(root + '/' + versions[i].toMap().value("id").toString()).removeRecursively();
  return true;
}
bool SaveBackups::protect(const QString& game, const QString& core, bool flatpak) {
  if (!m_enabled || flatpak || m_running())
    return true;
  const QString source = discover(game, core);
  if (source.isEmpty())
    return true;
  if (!QDir().mkpath(m_root)) {
    report("Could not create the save backup folder. The game can still launch.", true);
    return false;
  }
  if (QFileInfo(m_root).canonicalFilePath() != QFileInfo(m_root).absoluteFilePath()) {
    report("The save backup folder is redirected. No backup was written.", true);
    return false;
  }
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) {
    report("Save backup is busy. The game can still launch.", true);
    return false;
  }
  QString error;
  const bool okay = snapshot(game, core, source, &error);
  if (!okay)
    report(error + " The game can still launch.", true);
  else {
    ++m_revision;
    emit changed();
  }
  return okay;
}
bool SaveBackups::snapshotSelected() {
  if (!canSnapshot()) {
    report("No supported save location is available for this game.");
    return false;
  }
  QString error;
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &error)) {
    report(error);
    return false;
  }
  const auto previousVersions = list(m_game);
  if (!m_sets.snapshot(m_game, m_context, resolve(m_context), &error)) {
    report(error);
    return false;
  }
  const auto nextVersions = list(m_game);
  report(nextVersions.isEmpty()             ? "No existing saves to back up."
         : nextVersions == previousVersions ? "No changes since the latest backup."
                                            : "Save backup created.");
  return true;
}
bool SaveBackups::deleteVersion(const QString& version) {
  const auto fail = [this](const QString& error) {
    report(error);
    return false;
  };
  QString error;
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &error))
    return fail(error);
  if (m_game.isEmpty())
    return fail("Choose a save backup first.");
  if (version.startsWith("set-")) {
    if (!m_sets.remove(m_game, version, &error))
      return fail(error);
  } else {
    if (!QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version).hasMatch())
      return fail("Choose a save backup first.");
    if (QFileInfo(m_root).canonicalFilePath() != QFileInfo(m_root).absoluteFilePath())
      return fail("The save backup folder is unavailable.");
    QLockFile lock(m_root + "/.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
      return fail("Save backups are busy. Try again.");
    if (m_running())
      return fail("Close emulators before deleting a save backup.");
    const QString directory = gameRoot(m_game) + '/' + version;
    const auto item = manifest(directory);
    if (QFileInfo(directory).canonicalFilePath() != QFileInfo(directory).absoluteFilePath() ||
        item.value("format").toInt() != 1 || item.value("game").toString() != m_game)
      return fail("That save backup is unavailable or damaged.");
    if (!QDir(directory).removeRecursively())
      return fail("Could not delete the save backup.");
  }
  report("Backup deleted. Your current save was not changed.");
  return true;
}
bool SaveBackups::restore(const QString& version) {
  const auto fail = [this](const QString& error) {
    report(error);
    return false;
  };
  if (version.startsWith("set-")) {
    QString error;
    if (!m_sets.restore(
            m_game, version, [this](const QJsonObject& c) { return resolve(c); }, &error))
      return fail(error);
    report("Save set restored. Your previous progress is also in the backup list.");
    return true;
  }
  QString recoveryError;
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &recoveryError))
    return fail(recoveryError);
  if (m_game.isEmpty() || !QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(version).hasMatch())
    return fail("Choose a save backup first.");
  if (QFileInfo(m_root).canonicalFilePath() != QFileInfo(m_root).absoluteFilePath())
    return fail("The save backup folder is unavailable.");
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0))
    return fail("Save backups are busy. Try again.");
  if (m_running())
    return fail("Close emulators before restoring a save.");
  const QString directory = gameRoot(m_game) + '/' + version;
  const auto item = manifest(directory);
  if (item.value("format").toInt() != 1 || item.value("game").toString() != m_game)
    return fail("This backup does not belong to this game.");
  const QString source = discover(m_game, item.value("core").toString(), true);
  if (source.isEmpty() || source != item.value("source").toString())
    return fail("The save location has changed or is unavailable. Nothing was restored.");
  const auto bytes = read(directory + "/save.srm", maxSave);
  if (bytes.isEmpty() || hash(bytes) != item.value("sha256").toString() ||
      bytes.size() != item.value("bytes").toInt())
    return fail("The backup is damaged. Nothing was restored.");
  const auto current = read(source, maxSave);
  QString error;
  if (QFileInfo::exists(source) && !snapshot(m_game, item.value("core").toString(), source, &error))
    return fail("Current save could not be protected. " + error);
  if (m_running() || discover(m_game, item.value("core").toString(), true) != source ||
      read(source, maxSave) != current)
    return fail("The current save changed. Nothing was restored.");
  if (!write(source, bytes))
    return fail("Could not restore the save. The current save was kept.");
  report(current.isEmpty() ? "Save restored."
                           : "Save restored. Your previous save is also in the backup list.");
  return true;
}

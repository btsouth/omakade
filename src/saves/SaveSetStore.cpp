#include "saves/SaveSetStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <filesystem>

namespace {
constexpr qint64 limit = 512LL * 1024 * 1024;
constexpr int fileLimit = 20000;
QString digest(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
bool safePath(QString path) {
  if (path.contains(QChar::Null) || !QFileInfo(path).isAbsolute() || QDir::cleanPath(path) != path)
    return false;
  while (!path.isEmpty()) {
    const QFileInfo f(path);
    if (f.isSymLink())
      return false;
    if (f.exists())
      return f.canonicalFilePath() == f.absoluteFilePath();
    const QString parent = f.absolutePath();
    if (parent == path)
      return false;
    path = parent;
  }
  return false;
}
bool load(const QString& path, QByteArray* data) {
  QFile f(path);
  if (!safePath(path) || !QFileInfo(path).isFile() || f.size() > limit ||
      !f.open(QIODevice::ReadOnly))
    return false;
  *data = f.read(limit + 1);
  return data->size() <= limit && f.error() == QFile::NoError;
}
bool put(const QString& path, const QByteArray& data) {
  if (!safePath(path) || !QDir().mkpath(QFileInfo(path).absolutePath()) || !safePath(path))
    return false;
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  if (!QFileInfo::exists(path))
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  return f.write(data) == data.size() && f.commit();
}
QJsonObject json(const QString& path) {
  QByteArray data;
  if (QFileInfo(path).size() > 16 * 1024 * 1024 || !load(path, &data))
    return {};
  return QJsonDocument::fromJson(data).object();
}
void clearAbandonedStages(const QString& root) {
  const QDir directory(root);
  for (const auto& name :
       directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks)) {
    const QString path = directory.filePath(name);
    if (!safePath(path))
      continue;
    if (QRegularExpression("^\\.recovery-[A-Za-z0-9]+$").match(name).hasMatch())
      QDir(path).removeRecursively();
    else if (QRegularExpression("^[a-f0-9]{64}$").match(name).hasMatch()) {
      const QDir game(path);
      for (const auto& stage :
           game.entryList({".pending-*"}, QDir::Dirs | QDir::Hidden | QDir::NoSymLinks))
        if (safePath(game.filePath(stage)))
          QDir(game.filePath(stage)).removeRecursively();
    }
  }
}
QJsonObject scope(const SaveLayout& l) {
  return {{"files", QJsonArray::fromStringList(l.files)},
          {"trees", QJsonArray::fromStringList(l.trees)},
          {"patterns", QJsonArray::fromStringList(l.patterns)},
          {"relativePattern", l.relativePattern}};
}
bool allowed(const QString& path, const SaveLayout& l) {
  if (!safePath(path))
    return false;
  if (l.files.contains(path))
    return true;
  for (const auto& root : l.trees)
    if (path.startsWith(root + '/'))
      return (l.patterns.isEmpty() || QDir::match(l.patterns, QFileInfo(path).fileName())) &&
             (l.relativePattern.isEmpty() || QRegularExpression(l.relativePattern)
                                                 .match(QDir(root).relativeFilePath(path))
                                                 .hasMatch());
  return false;
}
bool collect(const SaveLayout& l, QMap<QString, QByteArray>* result, QString* error) {
  if (!l.valid()) {
    *error = l.error.isEmpty() ? "No save location was identified." : l.error;
    return false;
  }
  QStringList paths = l.files;
  for (const auto& root : l.trees) {
    const bool rootExisted = QFileInfo::exists(root);
    if (!safePath(root) || (rootExisted && !QFileInfo(root).isDir())) {
      *error = "A save folder is redirected or unavailable.";
      return false;
    }
    // QDirIterator silently skips directories it cannot read. An incomplete
    // enumeration would make restore interpret omitted saves as deleted files.
    std::error_code scanError;
    std::filesystem::recursive_directory_iterator it(QFile::encodeName(root).constData(),
                                                     scanError),
        end;
    if (!rootExisted && scanError == std::errc::no_such_file_or_directory)
      continue; // A not-yet-created save bank is a legitimate empty set.
    int visited = 0;
    while (!scanError && it != end) {
      const QFileInfo entry(QFile::decodeName(it->path().c_str()));
      if (++visited > fileLimit || entry.isSymLink()) {
        *error = "The save folder contains redirected paths or too many files.";
        return false;
      }
      if (!entry.exists()) {
        *error = "A save folder changed or could not be read completely.";
        return false;
      }
      if (!entry.isDir() && allowed(entry.filePath(), l))
        paths << entry.filePath();
      it.increment(scanError);
    }
    if (scanError) {
      *error = "A save folder changed or could not be read completely.";
      return false;
    }
  }
  paths.removeDuplicates();
  qint64 total = 0;
  for (const auto& path : paths) {
    if (!allowed(path, l)) {
      *error = "A save path is unsafe.";
      return false;
    }
    if (!QFileInfo::exists(path))
      continue;
    QByteArray data;
    if (!load(path, &data) || (total += data.size()) > limit || result->size() >= fileLimit) {
      *error = "The save set could not be read or exceeds 512 MiB.";
      return false;
    }
    result->insert(path, data);
  }
  return true;
}
QString gameRoot(const QString& root, const QString& game) {
  return root + '/' + digest(game.toUtf8());
}
bool validId(const QString& id) {
  return QRegularExpression("^[0-9]{17}-[a-f0-9]{32}$").match(id).hasMatch();
}
bool byteCount(const QJsonValue& value) {
  return value.isDouble() && value.toInteger(-1) >= 0 && value.toInteger(-1) <= limit &&
         value.toDouble() == double(value.toInteger(-1));
}
bool snapshotManifest(const QJsonObject& m) {
  return m["format"].toDouble() == 2 && m["game"].isString() && m["context"].isObject() &&
         m["scope"].isObject() && m["shared"].isBool() && m["entries"].isArray() &&
         byteCount(m["bytes"]);
}
bool unpack(const QString& directory, const QJsonValue& entryValue, const SaveLayout& layout,
            QMap<QString, QByteArray>* data, QString* error) {
  if (!entryValue.isArray()) {
    *error = "The save backup has a missing or invalid file list.";
    return false;
  }
  const auto entries = entryValue.toArray();
  qint64 bytes = 0;
  if (entries.size() > fileLimit) {
    *error = "Too many saved files.";
    return false;
  }
  for (const auto& entry : entries) {
    const auto item = entry.toObject();
    const QString path = item["path"].toString();
    const QString blob = item["blob"].toString();
    QByteArray value;
    if (!entry.isObject() || !byteCount(item["bytes"]) || !allowed(path, layout) ||
        data->contains(path) || !QRegularExpression("^[0-9]+$").match(blob).hasMatch() ||
        !load(directory + '/' + blob, &value) || digest(value) != item["sha256"].toString() ||
        value.size() != item["bytes"].toInteger() || (bytes += value.size()) > limit) {
      *error = "The save backup is damaged or its paths no longer match.";
      return false;
    }
    data->insert(path, value);
  }
  return true;
}
bool unpackSnapshot(const QString& directory, const QJsonObject& m, const SaveLayout& layout,
                    QMap<QString, QByteArray>* data, QString* error) {
  if (!snapshotManifest(m)) {
    *error = "The save backup manifest is damaged.";
    return false;
  }
  if (!unpack(directory, m["entries"], layout, data, error))
    return false;
  qint64 bytes = 0;
  for (const auto& value : *data)
    bytes += value.size();
  if (bytes != m["bytes"].toInteger()) {
    *error = "The save backup size does not match its manifest.";
    return false;
  }
  return true;
}
bool pack(const QString& directory, const QMap<QString, QByteArray>& data, QJsonArray* entries) {
  int i = 0;
  for (auto it = data.cbegin(); it != data.cend(); ++it) {
    const QString blob = QString::number(i++);
    if (!put(directory + '/' + blob, it.value()))
      return false;
    entries->append(QJsonObject{{"path", it.key()},
                                {"blob", blob},
                                {"bytes", it.value().size()},
                                {"sha256", digest(it.value())}});
  }
  return true;
}
bool sameFile(const QString& path, const QMap<QString, QByteArray>& data) {
  if (!data.contains(path))
    return safePath(path) && !QFileInfo::exists(path);
  QByteArray actual;
  return load(path, &actual) && actual == data[path];
}
bool apply(const QString& path, const QMap<QString, QByteArray>& data) {
  if (data.contains(path))
    return put(path, data[path]);
  return safePath(path) && (!QFileInfo::exists(path) || QFile::remove(path));
}
bool matchingRoles(const QStringList& fromPaths, const QMap<QString, QString>& fromRoles,
                   const QStringList& toPaths, const QMap<QString, QString>& toRoles) {
  if (fromPaths.size() != fromRoles.size() || toPaths.size() != toRoles.size() ||
      fromPaths.size() != toPaths.size())
    return false;
  QStringList fromKeys, toKeys;
  for (const QString& path : fromPaths) {
    const QString key = fromRoles.value(path);
    if (key.isEmpty() || fromKeys.contains(key))
      return false;
    fromKeys.append(key);
  }
  for (const QString& path : toPaths) {
    const QString key = toRoles.value(path);
    if (key.isEmpty() || toKeys.contains(key))
      return false;
    toKeys.append(key);
  }
  fromKeys.sort();
  toKeys.sort();
  return fromKeys == toKeys;
}
bool matchingLayoutRoles(const SaveLayout& from, const SaveLayout& to) {
  return !from.shared && !to.shared && from.patterns == to.patterns &&
         from.relativePattern == to.relativePattern &&
         matchingRoles(from.files, from.relocationFiles, to.files, to.relocationFiles) &&
         matchingRoles(from.trees, from.relocationTrees, to.trees, to.relocationTrees);
}
QString remapLayoutPath(const QString& path, const SaveLayout& from, const SaveLayout& to) {
  if (from.files.contains(path))
    return to.relocationFiles.key(from.relocationFiles.value(path));
  int best = -1;
  QString mapped;
  for (const QString& root : from.trees) {
    if ((path == root || path.startsWith(root + '/')) && root.size() > best) {
      best = root.size();
      mapped = to.relocationTrees.key(from.relocationTrees.value(root)) + path.mid(root.size());
    }
  }
  return mapped;
}
QString remapContextPath(const QString& path, const QString& from, const QString& to) {
  if (path == from || path.startsWith(from + '/') || path.startsWith(from + '#'))
    return to + path.mid(from.size());
  return path;
}
} // namespace

SaveSetStore::SaveSetStore(QString root, std::function<bool()> running)
    : m_root(std::move(root)), m_running(std::move(running)) {}
void SaveSetStore::setPolicy(int retention, qint64 bytes, const QString& budgetRoot) {
  m_retention=qBound(2,retention,50);m_storageLimit=qBound<qint64>(256LL*1024*1024,bytes,8LL*1024*1024*1024);
  m_budgetRoot=budgetRoot;
}
bool SaveSetStore::pending() const { return QFileInfo::exists(m_root + "/.restore"); }
QVariantList SaveSetStore::versions(const QString& game) const {
  QVariantList out;
  QStringList keys{game};
  const auto shared = json(gameRoot(m_root, game) + "/shared.json")["keys"].toArray();
  if (shared.size() > 32)
    return out;
  for (const auto& k : shared)
    if (QRegularExpression("^shared:[a-f0-9]{64}$").match(k.toString()).hasMatch())
      keys << k.toString();
  keys.removeDuplicates();
  for (const auto& key : keys) {
    const QDir root(gameRoot(m_root, key));
    if (!safePath(root.path()))
      continue;
    for (const auto& id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                         QDir::Name | QDir::Reversed)) {
      if (!validId(id))
        continue;
      const auto m = json(root.filePath(id + "/manifest.json"));
      if (m["format"].toInt() != 2 || m["game"].toString() != key)
        continue;
      out << QVariantMap{{"id", "set-" + id},
                         {"createdAt", m["createdAt"].toString()},
                         {"bytes", m["bytes"].toInteger()},
                         {"shared", m["shared"].toBool()},
                         {"description", m["description"].toString()},
                         {"verifiedAtCapture", m["verifiedAtCapture"].toBool()},
                         {"storageKey", key}};
    }
  }
  std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
    return a.toMap()["createdAt"].toString() > b.toMap()["createdAt"].toString();
  });
  return out;
}
bool SaveSetStore::stageGameCopy(const QString& oldGame, const QString& newGame,
                                 const Resolver& resolve, const QString& destination,
                                 const CopyFile& copyFile, QStringList* copiedVersions,
                                 QString* error) const {
  error->clear();
  copiedVersions->clear();
  if (oldGame.isEmpty() || newGame.isEmpty() || oldGame == newGame ||
      !safePath(destination) || !QFileInfo(destination).isDir()) {
    *error = "The save backup copy destination is unavailable.";
    return false;
  }
  const QString sourceRoot = gameRoot(m_root, oldGame);
  const QVariantList allVersions = versions(oldGame);
  QStringList perGameVersions;
  for (const QVariant& value : allVersions) {
    const QVariantMap entry = value.toMap();
    const QString version = entry.value(QStringLiteral("id")).toString();
    if (entry.value(QStringLiteral("storageKey")).toString() == oldGame &&
        version.startsWith(QStringLiteral("set-")))
      perGameVersions.append(version);
  }
  if (perGameVersions.isEmpty())
    return true;
  if (!safePath(sourceRoot) || !QFileInfo(sourceRoot).isDir()) {
    *error = "A save-set backup source is unavailable or redirected.";
    return false;
  }
  for (const QString& version : perGameVersions) {
    if (!version.startsWith(QStringLiteral("set-")) || !validId(version.mid(4))) {
      *error = "A save-set backup identifier is invalid.";
      return false;
    }
    const QString sourceDirectory = sourceRoot + '/' + version.mid(4);
    const QJsonObject sourceManifest = json(sourceDirectory + "/manifest.json");
    const QJsonObject oldContext = sourceManifest.value(QStringLiteral("context")).toObject();
    const SaveLayout oldLayout = resolve(oldContext);
    QMap<QString, QByteArray> oldData;
    if (!safePath(sourceDirectory) || !snapshotManifest(sourceManifest) ||
        sourceManifest.value(QStringLiteral("game")).toString() != oldGame ||
        sourceManifest.value(QStringLiteral("shared")).toBool() ||
        !sourceManifest.value(QStringLiteral("context")).isObject() ||
        oldContext.value(QStringLiteral("game")).toString() != oldGame || !oldLayout.valid() ||
        sourceManifest.value(QStringLiteral("scope")).toObject() != scope(oldLayout) ||
        !unpackSnapshot(sourceDirectory, sourceManifest, oldLayout, &oldData, error)) {
      if (error->isEmpty())
        *error = "A save-set backup is damaged or no longer matches its original layout.";
      return false;
    }

    QJsonObject newContext = oldContext;
    for (const QString& field : {QStringLiteral("game"), QStringLiteral("target")}) {
      const QString path = newContext.value(field).toString();
      if (!path.isEmpty())
        newContext.insert(field, remapContextPath(path, oldGame, newGame));
    }
    if (newContext.value(QStringLiteral("game")).toString() != newGame) {
      *error = "The save-set context could not be mapped to the new game path.";
      return false;
    }
    const SaveLayout newLayout = resolve(newContext);
    if (!newLayout.valid()) {
      *error = newLayout.error.isEmpty() ? "The save-set layout could not be resolved at the new path."
                                         : newLayout.error;
      return false;
    }
    if (!matchingLayoutRoles(oldLayout, newLayout)) {
      *error = "The save-set paths could not be mapped to the new save layout.";
      return false;
    }
    QMap<QString, QByteArray> newData;
    for (auto it = oldData.cbegin(); it != oldData.cend(); ++it) {
      const QString newPath = remapLayoutPath(it.key(), oldLayout, newLayout);
      if (newPath.isEmpty() || !allowed(newPath, newLayout) || newData.contains(newPath)) {
        *error = "The save-set paths could not be mapped to the new save layout.";
        return false;
      }
      newData.insert(newPath, it.value());
    }

    const QString destinationDirectory = destination + '/' + version.mid(4);
    if (!QDir().mkpath(destinationDirectory) || !safePath(destinationDirectory)) {
      *error = "Could not stage a save-set backup copy.";
      return false;
    }
    QJsonArray newEntries;
    const QJsonArray oldEntries = sourceManifest.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : oldEntries) {
      const QJsonObject entry = value.toObject();
      const QString blob = entry.value(QStringLiteral("blob")).toString();
      const QString oldPath = entry.value(QStringLiteral("path")).toString();
      const QString newPath = remapLayoutPath(oldPath, oldLayout, newLayout);
      const QString sourceBlob = sourceDirectory + '/' + blob;
      const QString destinationBlob = destinationDirectory + '/' + blob;
      if (!QRegularExpression("^[0-9]+$").match(blob).hasMatch() || newPath.isEmpty() ||
          !copyFile(sourceBlob, destinationBlob)) {
        *error = "Could not copy a verified save-set backup.";
        return false;
      }
      QJsonObject newEntry = entry;
      newEntry.insert(QStringLiteral("path"), newPath);
      newEntries.append(newEntry);
    }
    QJsonObject newManifest = sourceManifest;
    newManifest.insert(QStringLiteral("game"), newGame);
    newManifest.insert(QStringLiteral("context"), newContext);
    newManifest.insert(QStringLiteral("scope"), scope(newLayout));
    newManifest.insert(QStringLiteral("entries"), newEntries);
    if (!put(destinationDirectory + "/manifest.json", QJsonDocument(newManifest).toJson())) {
      *error = "Could not write the copied save-set manifest.";
      return false;
    }
    QMap<QString, QByteArray> verified;
    if (!unpackSnapshot(destinationDirectory, newManifest, newLayout, &verified, error) ||
        verified != newData) {
      *error = "The copied save-set backup failed manifest verification.";
      return false;
    }
    copiedVersions->append(version);
  }
  return true;
}
bool SaveSetStore::validateGameCopies(const QString& game, const QStringList& expectedVersions,
                                      const Resolver& resolve, QString* error) const {
  error->clear();
  QSet<QString> expected;
  for (const QString& version : expectedVersions)
    expected.insert(version);
  QSet<QString> actual;
  for (const QVariant& value : versions(game)) {
    const QVariantMap entry = value.toMap();
    const QString version = entry.value(QStringLiteral("id")).toString();
    if (entry.value(QStringLiteral("storageKey")).toString() == game &&
        version.startsWith(QStringLiteral("set-")))
      actual.insert(version);
  }
  if (actual != expected) {
    *error = "The per-game save-set list no longer matches its relocation receipt.";
    return false;
  }
  const QString root = gameRoot(m_root, game);
  for (const QString& version : expectedVersions) {
    if (!version.startsWith(QStringLiteral("set-")) || !validId(version.mid(4))) {
      *error = "A save-set relocation receipt contains an invalid version.";
      return false;
    }
    const QString directory = root + '/' + version.mid(4);
    const QJsonObject saved = json(directory + "/manifest.json");
    const QJsonObject context = saved.value(QStringLiteral("context")).toObject();
    const SaveLayout layout = resolve(context);
    QMap<QString, QByteArray> verified;
    if (!safePath(directory) || !snapshotManifest(saved) ||
        saved.value(QStringLiteral("game")).toString() != game ||
        saved.value(QStringLiteral("shared")).toBool() ||
        context.value(QStringLiteral("game")).toString() != game || !layout.valid() ||
        saved.value(QStringLiteral("scope")).toObject() != scope(layout) ||
        !unpackSnapshot(directory, saved, layout, &verified, error)) {
      if (error->isEmpty())
        *error = "A copied save-set backup no longer passes manifest verification.";
      return false;
    }
  }
  return true;
}
bool SaveSetStore::snapshot(const QString& game, const QJsonObject& context,
                            const SaveLayout& layout, QString* error, bool allowEmpty) {
  if (!safePath(m_root) || !QDir().mkpath(m_root)) {
    *error = "The save backup folder is unavailable.";
    return false;
  }
  QFile::setPermissions(m_root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0) || pending() || m_running()) {
    *error = "Close emulators and finish pending save recovery first.";
    return false;
  }
  clearAbandonedStages(m_root);
  QMap<QString, QByteArray> data;
  if (!collect(layout, &data, error))
    return false;
  if (data.isEmpty() && !allowEmpty)
    return true;
  const QString storageKey =
      layout.shared
          ? "shared:" + digest(QJsonDocument(scope(layout)).toJson(QJsonDocument::Compact))
          : game;
  if (layout.shared) {
    const QString alias = gameRoot(m_root, game) + "/shared.json";
    auto keys = json(alias)["keys"].toArray();
    if (!keys.contains(storageKey))
      keys.append(storageKey);
    if (keys.size() > 32 || !put(alias, QJsonDocument(QJsonObject{{"keys", keys}}).toJson())) {
      *error = "Cannot record the shared save history.";
      return false;
    }
  }
  const auto old = versions(storageKey);
  if (!old.isEmpty()) {
    const QString dir =
        gameRoot(m_root, storageKey) + '/' + old.first().toMap()["id"].toString().mid(4);
    const auto m = json(dir + "/manifest.json");
    QMap<QString, QByteArray> previous;
    QString ignored;
    if ((layout.shared || m["context"].toObject() == context) &&
        m["scope"].toObject() == scope(layout) &&
        unpackSnapshot(dir, m, layout, &previous, &ignored) && previous == data)
      return true;
  }
  qint64 size = 0;
  for (const auto& bytes : data)
    size += bytes.size();
  qint64 used = 0;
  // Kept recovery copies are not backups and must not use up the backup budget.
  const QString archived = m_root + "/recovery-archive/";
  QDirIterator it(m_budgetRoot.isEmpty()?m_root:m_budgetRoot, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (!it.filePath().startsWith(archived))
      used += it.fileInfo().size();
  }
  if (used + size + 16 * 1024 * 1024 > m_storageLimit) {
    *error = "The backup storage limit was reached. Existing backups were kept.";
    return false;
  }
  const QString root = gameRoot(m_root, storageKey);
  if (!QDir().mkpath(root) || !safePath(root)) {
    *error = "Cannot create a save backup folder.";
    return false;
  }
  QTemporaryDir stage(root + "/.pending-XXXXXX");
  QJsonArray entries;
  if (!stage.isValid() || !pack(stage.path(), data, &entries)) {
    *error = "Cannot write the save backup.";
    return false;
  }
  const auto now = QDateTime::currentDateTimeUtc();
  const QJsonObject m{{"format", 2},
                      {"game", storageKey},
                      {"context", context},
                      {"scope", scope(layout)},
                      {"shared", layout.shared},
                      {"description", layout.description},
                      {"createdAt", now.toString(Qt::ISODateWithMs)},
                      {"bytes", size},
                      {"entries", entries}, {"verifiedAtCapture", true}};
  QMap<QString, QByteArray> check, live;
  if (!put(stage.path() + "/manifest.json", QJsonDocument(m).toJson()) ||
      !unpackSnapshot(stage.path(), m, layout, &check, error) || check != data ||
      !collect(layout, &live, error) || live != data || m_running()) {
    *error = "The save changed or could not be verified. No backup was committed.";
    return false;
  }
  const auto previous =
      old.isEmpty() ? 0 : old.first().toMap()["id"].toString().mid(4, 17).toLongLong();
  const QString id =
      QString::number(qMax(now.toString("yyyyMMddHHmmsszzz").toLongLong(), previous + 1)) + '-' +
      QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().rename(stage.path(), root + '/' + id)) {
    *error = "Cannot commit the save backup.";
    return false;
  }
  stage.setAutoRemove(false);
  const auto all = versions(storageKey);
  for (int i = qMax(m_retention,int(old.size())); i < all.size(); ++i)
    QDir(root + '/' + all[i].toMap()["id"].toString().mid(4)).removeRecursively();
  return true;
}
bool SaveSetStore::remove(const QString& game, const QString& version, QString* error) {
  if (!version.startsWith("set-") || !validId(version.mid(4))) {
    *error = "Choose a save backup first.";
    return false;
  }
  QString storageKey;
  for (const auto& item : versions(game)) {
    const auto entry = item.toMap();
    if (entry["id"].toString() == version) {
      storageKey = entry["storageKey"].toString();
      break;
    }
  }
  if (storageKey.isEmpty()) {
    *error = "That save backup is no longer available.";
    return false;
  }
  if (!safePath(m_root)) {
    *error = "The save backup folder is unavailable.";
    return false;
  }
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0) || pending() || m_running()) {
    *error = "Close emulators and finish pending save recovery first.";
    return false;
  }
  const QString directory = gameRoot(m_root, storageKey) + '/' + version.mid(4);
  const auto saved = json(directory + "/manifest.json");
  if (!safePath(directory) || saved["format"].toInt() != 2 ||
      saved["game"].toString() != storageKey) {
    *error = "That save backup is unavailable or damaged.";
    return false;
  }
  if (!QDir(directory).removeRecursively()) {
    *error = "Could not delete the save backup.";
    return false;
  }
  return true;
}
bool SaveSetStore::recover(const Resolver& resolve, QString* error) {
  if (!pending())
    return true;
  if (!safePath(m_root)) {
    *error = "The recovery folder is redirected.";
    return false;
  }
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0) || m_running()) {
    *error = "Close emulators before recovering an interrupted save restore.";
    return false;
  }
  const QString dir = m_root + "/.restore";
  const auto m = json(dir + "/manifest.json");
  if (m["format"].toDouble() != 2 || !m["context"].isObject() || !m["scope"].isObject() ||
      !m["committed"].isBool() || !m["before"].isArray() || !m["after"].isArray()) {
    *error = "The save recovery manifest is damaged. Recovery copies were kept.";
    return false;
  }
  const auto layout = resolve(m["context"].toObject());
  if (m["format"].toInt() != 2 || !layout.valid() || m["scope"].toObject() != scope(layout)) {
    *error = "Save recovery needs the original emulator save configuration.";
    return false;
  }
  QMap<QString, QByteArray> before, after;
  if (!unpack(dir + "/before", m["before"], layout, &before, error) ||
      !unpack(dir + "/after", m["after"], layout, &after, error))
    return false;
  auto paths = before.keys();
  paths << after.keys();
  paths.removeDuplicates();
  QMap<QString, QByteArray> live;
  if (!collect(layout, &live, error))
    return false;
  for (auto it = live.cbegin(); it != live.cend(); ++it)
    if (!paths.contains(it.key())) {
      *error = "Saves changed after the interrupted restore. Recovery copies were kept.";
      return false;
    }
  for (const auto& path : paths)
    if (!sameFile(path, before) && !sameFile(path, after)) {
      *error = "Saves changed after the interrupted restore. Recovery copies were kept.";
      return false;
    }
  // A committed journal only needs cleanup. Otherwise roll back every changed member.
  const auto& target = m["committed"].toBool() ? after : before;
  for (const auto& path : paths) {
    if (m_running() || (!sameFile(path, before) && !sameFile(path, after)) ||
        !apply(path, target)) {
      *error = "Save recovery is incomplete. Close emulators and retry before launching.";
      return false;
    }
  }
  // Move the finished journal aside first so an interrupted cleanup never leaves a
  // partial journal behind. Leftover .recovery-* folders are cleared later.
  const QString done = m_root + "/.recovery-" + QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().rename(dir, done)) {
    *error = "Recovered saves, but could not clear the recovery journal.";
    return false;
  }
  QDir(done).removeRecursively();
  return true;
}
bool SaveSetStore::pendingAffects(const QString& game, const SaveLayout& layout) const {
  if (!pending())
    return false;
  const auto m = json(m_root + "/.restore/manifest.json");
  const QString owner = m["game"].toString();
  if (owner.isEmpty() || owner == game || !m["scope"].isObject() || !layout.valid())
    return true;
  const auto journalScope = m["scope"].toObject();
  QStringList journal;
  for (const auto& list : {journalScope.value("files"), journalScope.value("trees")})
    for (const auto& path : list.toArray())
      journal << path.toString();
  const QStringList launch = layout.files + layout.trees;
  if (journal.isEmpty() || launch.isEmpty())
    return true;
  for (const auto& a : journal)
    for (const auto& b : launch)
      if (a.isEmpty() || b.isEmpty() || a == b || a.startsWith(b + '/') || b.startsWith(a + '/'))
        return true;
  return false;
}
bool SaveSetStore::archiveRecovery(QString* folder, QString* error) {
  if (!pending())
    return true;
  const QString dir = m_root + "/.restore";
  if (!safePath(m_root) || !safePath(dir)) {
    *error = "The recovery folder is redirected.";
    return false;
  }
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0) || m_running()) {
    *error = "Close emulators before keeping the current saves.";
    return false;
  }
  const QString archive = m_root + "/recovery-archive";
  const QString target =
      archive + '/' + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
  if (!QDir().mkpath(archive) || !safePath(archive) || QFileInfo::exists(target) ||
      !QDir().rename(dir, target)) {
    *error = "Could not move the recovery copies. Nothing was changed.";
    return false;
  }
  *folder = target;
  return true;
}
bool SaveSetStore::restore(const QString& game, const QString& version, const Resolver& resolve,
                           QString* error) {
  if (!version.startsWith("set-") || !validId(version.mid(4))) {
    *error = "Choose a save backup first.";
    return false;
  }
  if (!recover(resolve, error))
    return false;
  QString storageKey;
  for (const auto& v : versions(game))
    if (v.toMap()["id"].toString() == version)
      storageKey = v.toMap()["storageKey"].toString();
  if (storageKey.isEmpty()) {
    *error = "This backup does not belong to this game.";
    return false;
  }
  const QString dir = gameRoot(m_root, storageKey) + '/' + version.mid(4);
  const auto m = json(dir + "/manifest.json");
  const auto context = m["context"].toObject();
  const auto layout = resolve(context);
  if (m["format"].toInt() != 2 || m["game"].toString() != storageKey || !layout.valid() ||
      m["scope"].toObject() != scope(layout)) {
    *error = "The save location changed. Nothing was restored.";
    return false;
  }
  QMap<QString, QByteArray> after, before;
  if (!unpackSnapshot(dir, m, layout, &after, error))
    return false;
  if (!collect(layout, &before, error) || !snapshot(game, context, layout, error, true))
    return false;
  QLockFile lock(m_root + "/.lock");
  lock.setStaleLockTime(0);
  QMap<QString, QByteArray> current;
  if (!lock.tryLock(0) || pending() || m_running() || !collect(layout, &current, error) ||
      current != before) {
    *error = "The save changed or an emulator is running. Nothing was restored.";
    return false;
  }
  QTemporaryDir stage(m_root + "/.recovery-XXXXXX");
  QJsonArray oldEntries, newEntries;
  if (!stage.isValid() || !pack(stage.path() + "/before", before, &oldEntries) ||
      !pack(stage.path() + "/after", after, &newEntries)) {
    *error = "Could not prepare recovery copies. Nothing was restored.";
    return false;
  }
  QJsonObject journal{
      {"format", 2},          {"game", game},        {"context", context}, {"scope", scope(layout)},
      {"before", oldEntries}, {"after", newEntries}, {"committed", false}};
  QMap<QString, QByteArray> check, checkOld, live;
  if (!put(stage.path() + "/manifest.json", QJsonDocument(journal).toJson()) ||
      !unpack(stage.path() + "/before", oldEntries, layout, &checkOld, error) ||
      checkOld != before || !unpack(stage.path() + "/after", newEntries, layout, &check, error) ||
      check != after || !collect(layout, &live, error) || live != before || m_running() ||
      scope(resolve(context)) != scope(layout) ||
      !QDir().rename(stage.path(), m_root + "/.restore")) {
    *error = "Saves changed or recovery could not be prepared. Nothing was restored.";
    return false;
  }
  stage.setAutoRemove(false);
  auto paths = before.keys();
  paths << after.keys();
  paths.removeDuplicates();
  bool okay = true;
  for (const auto& path : paths)
    if (m_running() || !sameFile(path, before) || !apply(path, after)) {
      okay = false;
      break;
    }
  if (okay) {
    journal["committed"] = true;
    okay = put(m_root + "/.restore/manifest.json", QJsonDocument(journal).toJson());
  }
  lock.unlock();
  if (!recover(resolve, error))
    return false;
  if (!okay) {
    *error = "Restore could not finish. The previous save set was recovered.";
    return false;
  }
  return true;
}

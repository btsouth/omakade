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

namespace {
constexpr qint64 limit = 512LL * 1024 * 1024;
constexpr qint64 storageLimit = 2LL * 1024 * 1024 * 1024;
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
    if (!safePath(root) || (QFileInfo::exists(root) && !QFileInfo(root).isDir())) {
      *error = "A save folder is redirected or unavailable.";
      return false;
    }
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    int visited = 0;
    while (it.hasNext()) {
      it.next();
      if (++visited > fileLimit || it.fileInfo().isSymLink()) {
        *error = "The save folder contains redirected paths or too many files.";
        return false;
      }
      if (!it.fileInfo().isDir() && allowed(it.filePath(), l))
        paths << it.filePath();
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
bool unpack(const QString& directory, const QJsonArray& entries, const SaveLayout& layout,
            QMap<QString, QByteArray>* data, QString* error) {
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
    if (!allowed(path, layout) || data->contains(path) ||
        !QRegularExpression("^[0-9]+$").match(blob).hasMatch() ||
        !load(directory + '/' + blob, &value) || digest(value) != item["sha256"].toString() ||
        value.size() != item["bytes"].toInteger() || (bytes += value.size()) > limit) {
      *error = "The save backup is damaged or its paths no longer match.";
      return false;
    }
    data->insert(path, value);
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
} // namespace

SaveSetStore::SaveSetStore(QString root, std::function<bool()> running)
    : m_root(std::move(root)), m_running(std::move(running)) {}
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
                         {"storageKey", key}};
    }
  }
  std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
    return a.toMap()["createdAt"].toString() > b.toMap()["createdAt"].toString();
  });
  return out;
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
        unpack(dir, m["entries"].toArray(), layout, &previous, &ignored) && previous == data)
      return true;
  }
  qint64 size = 0;
  for (const auto& bytes : data)
    size += bytes.size();
  qint64 used = 0;
  QDirIterator it(m_root, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    used += it.fileInfo().size();
  }
  if (used + size + 16 * 1024 * 1024 > storageLimit) {
    *error = "Save backup storage is full (2 GiB). Existing backups were kept.";
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
                      {"entries", entries}};
  QMap<QString, QByteArray> check, live;
  if (!put(stage.path() + "/manifest.json", QJsonDocument(m).toJson()) ||
      !unpack(stage.path(), entries, layout, &check, error) || check != data ||
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
  for (int i = 10; i < all.size(); ++i)
    QDir(root + '/' + all[i].toMap()["id"].toString().mid(4)).removeRecursively();
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
  const auto layout = resolve(m["context"].toObject());
  if (m["format"].toInt() != 2 || !layout.valid() || m["scope"].toObject() != scope(layout)) {
    *error = "Save recovery needs the original emulator save configuration.";
    return false;
  }
  QMap<QString, QByteArray> before, after;
  if (!unpack(dir + "/before", m["before"].toArray(), layout, &before, error) ||
      !unpack(dir + "/after", m["after"].toArray(), layout, &after, error))
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
  if (!QDir(dir).removeRecursively()) {
    *error = "Recovered saves, but could not clear the recovery journal.";
    return false;
  }
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
  if (!unpack(dir, m["entries"].toArray(), layout, &after, error))
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

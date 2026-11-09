#include "tracking/PrivateStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr auto kDirectoryMode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr auto kFileMode = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

bool ownedByUser(const QFileInfo& info) {
  return info.exists() && !info.isSymLink() && info.ownerId() == ::getuid();
}

QString root(QStandardPaths::StandardLocation location) {
  const QString path = QStandardPaths::writableLocation(location);
  return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

} // namespace

namespace PrivateStorage {

bool makeDirectory(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    if (!QDir().mkpath(info.absolutePath()))
      return false;
    // Created owner-only, so it is never briefly open to other users.
    if (::mkdir(QFile::encodeName(info.absoluteFilePath()).constData(), S_IRWXU) != 0 &&
        errno != EEXIST)
      return false;
  }
  restrictDirectory(path);
  return QFileInfo(path).isDir();
}

void restrictDirectory(const QString& path) {
  const QFileInfo info(path);
  if (ownedByUser(info) && info.isDir())
    QFile::setPermissions(path, kDirectoryMode);
}

void restrictFile(const QString& path) {
  const QFileInfo info(path);
  if (ownedByUser(info) && info.isFile())
    QFile::setPermissions(path, kFileMode);
}

void restrictDatabase(const QString& path) {
  if (path.isEmpty() || path == QStringLiteral(":memory:"))
    return;
  for (const char* suffix : {"", "-wal", "-shm", "-journal"})
    restrictFile(path + QLatin1String(suffix));
}

void secureDefaults() {
  const QString data = root(QStandardPaths::GenericDataLocation);
  const QString config = root(QStandardPaths::GenericConfigLocation);
  const QString state = root(QStandardPaths::GenericStateLocation);
  const QString cache = root(QStandardPaths::GenericCacheLocation);
  for (const QString& base : {data, config, state, cache}) {
    if (!base.isEmpty())
      makeDirectory(base + QStringLiteral("/omakade"));
  }
  // Qt's own per-application folders, used for the ProtonDB cache, in-game overlay and
  // guide settings. Created here too, so the plain mkpath or QSettings that later fills
  // them on a fresh install lands inside an owner-only folder.
  for (const QString& base : {data, config, cache}) {
    if (!base.isEmpty())
      makeDirectory(base + QStringLiteral("/Omakade"));
  }
  if (!data.isEmpty()) {
    restrictDatabase(data + QStringLiteral("/omakade/library.sqlite3"));
    restrictDatabase(data + QStringLiteral("/omakade/romm-catalog.sqlite3"));
  }
  if (!config.isEmpty())
    restrictFile(config + QStringLiteral("/omakade/config.toml"));
}

} // namespace PrivateStorage

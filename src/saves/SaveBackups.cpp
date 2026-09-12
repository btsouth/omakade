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
                          "dolphin-emu",
                          "dolphin-emu-nogui",
                          "ryujinx",
                          "ryujinx-wrapper",
                          "shadps4",
                          "cemu",
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
bool SaveBackups::protectLaunch(const QString& source, const QString& game, const QString& core,
                                bool flatpak, const QString& id, const QString& runner,
                                const QString& target) {
  QString error;
  if (!m_sets.recover([this](const QJsonObject& c) { return resolve(c); }, &error)) {
    report(error + " Launch is paused until save recovery finishes.", true);
    return false;
  }
  if (!m_enabled)
    return true;
  const QJsonObject context{{"source", source},   {"game", game}, {"core", core},
                            {"flatpak", flatpak}, {"id", id},     {"runner", runner},
                            {"target", target}};
  if (m_running()) {
    report("Save copy skipped while an emulator is running.", true);
    return true;
  }
  const auto layout = resolve(context);
  if (!layout.valid()) {
    report(layout.error, true);
    return true;
  }
  if (!m_sets.snapshot(game, context, layout, &error))
    report(error, true);
  else {
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

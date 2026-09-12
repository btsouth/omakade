#include "saves/SaveBackups.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
bool safeLocalPath(QString path) {
  if (!QFileInfo(path).isAbsolute() || QDir::cleanPath(path) != path)
    return false;
  while (!path.isEmpty()) {
    if (QFileInfo(path).isSymLink())
      return false;
    const auto parent = QFileInfo(path).absolutePath();
    if (parent == path)
      break;
    path = parent;
  }
  return true;
}
} // namespace
bool SaveBackups::setPolicy(int retention, int storageMiB) {
  if (retention < 2 || retention > 50 || storageMiB < 256 || storageMiB > 8192) {
    report("Keep 2 to 50 versions and choose 256 to 8192 MiB.");
    return false;
  }
  if (m_running() || recoveryPending()) {
    report("Close emulators and resolve pending recovery before changing backup settings.");
    return false;
  }
  if (!safeLocalPath(m_root + "/policy/manifest.json")) {
    report("Backup settings storage contains a symbolic link.");
    return false;
  }
  if (!QDir().mkpath(m_root + "/policy")) {
    report("Could not create backup settings storage.");
    return false;
  }
  QSaveFile file(m_root + "/policy/manifest.json");
  const auto bytes =
      QJsonDocument(
          QJsonObject{{"format", 1}, {"retention", retention}, {"storageLimitMiB", storageMiB}})
          .toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    report("Could not save backup settings.");
    return false;
  }
  m_customPolicy = true;
  m_retention = retention;
  m_storageLimitMiB = storageMiB;
  m_sets.setPolicy(retention, qint64(storageMiB) * 1024 * 1024, m_root);
  report("Settings saved. Existing backups were kept. Review cleanup to reduce older versions.");
  return true;
}
QVariantMap SaveBackups::coverage(const QVariantMap& context) const {
  const auto layout = resolve(QJsonObject::fromVariantMap(context));
  const auto versions = list(context.value("game").toString());
  bool exists = false, unavailable = false;
  for (const auto& path : layout.files + layout.trees) {
    const QFileInfo f(path);
    exists = exists || f.exists();
    unavailable = unavailable || (f.exists() && !f.isReadable()) ||
                  (!f.exists() && !QFileInfo(f.absolutePath()).isDir());
  }
  const bool unsupported = layout.error.contains("unsupported", Qt::CaseInsensitive) ||
                           layout.error.contains("not supported", Qt::CaseInsensitive);
  const QString invalidStatus = unsupported ? "Unsupported adapter"
                                : layout.error.contains("ambiguous", Qt::CaseInsensitive)
                                    ? "Ambiguous layout"
                                    : "Save layout needs configuration";
  QString status = !m_enabled            ? "Protection off"
                   : recoveryPending()   ? "Recovery pending"
                   : !layout.valid()     ? invalidStatus
                   : unavailable         ? "Save location unavailable"
                   : !versions.isEmpty() ? "Backups available"
                   : exists              ? "Ready to back up"
                                         : "No saves yet";
  QString verified;
  for (const auto& v : versions)
    if (v.toMap().value("verifiedAtCapture").toBool()) {
      verified = v.toMap().value("createdAt").toString();
      break;
    }
  return {
      {"status", status},           {"valid", layout.valid()}, {"description", layout.description},
      {"error", layout.error},      {"files", layout.files},   {"trees", layout.trees},
      {"shared", layout.shared},    {"versions", versions},    {"count", versions.size()},
      {"latestVerified", verified}, {"enabled", m_enabled}};
}
QString SaveBackups::layoutFile() const {
  const auto cfg = m_home == QDir::homePath()
                       ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                       : m_home + "/.config";
  return cfg + "/omakade/save-layouts.json";
}
QVariantMap SaveBackups::previewCustomFiles(const QVariantMap& context, const QStringList& files,
                                            bool shared) const {
  const QStringList supported{"RetroArch",  "PCSX2",    "Ryujinx", "Cemu",    "Dolphin",
                              "shadPS4",    "mgba",     "sameboy", "bsnes",   "snes9x",
                              "snes9x-gtk", "nestopia", "fceux",   "mednafen"};
  const auto fail = [](const QString& error) {
    return QVariantMap{{"valid", false}, {"error", error}};
  };
  if (!supported.contains(context.value("source").toString()) ||
      context.value("game").toString().isEmpty())
    return fail("Choose a supported emulator installation first.");
  if (files.isEmpty() || files.size() > 32)
    return fail("Choose 1 to 32 explicit save files. Broad folders are not accepted.");
  QStringList selected;
  qint64 bytes = 0;
  for (const auto& input : files) {
    const auto path = input.trimmed();
    const QFileInfo f(path);
    if (!f.isAbsolute() || !f.isFile() || f.isSymLink() || !f.isReadable() ||
        f.canonicalFilePath() != f.absoluteFilePath() || path.contains(QChar::Null) ||
        path.size() > 4096 || path.startsWith(m_root + '/'))
      return fail("Every selection must be a readable regular save file outside the backup store, "
                  "without symlinks.");
    if (!QStringList{"srm", "sav", "rtc", "mcr", "ps2", "gci", "raw", "dat", "bin", "rks", "sra",
                     "eep", "fla"}
             .contains(f.suffix().toLower()))
      return fail("This file type is not supported for an explicit save layout.");
    if (selected.contains(f.canonicalFilePath()))
      continue;
    selected << f.canonicalFilePath();
    bytes += f.size();
    if (bytes > 512LL * 1024 * 1024)
      return fail("The selected save files exceed 512 MiB.");
  }
  return {{"valid", true},
          {"files", selected},
          {"bytes", bytes},
          {"shared", shared},
          {"description", "Explicit save files"}};
}
bool SaveBackups::setCustomFiles(const QVariantMap& context, const QStringList& files,
                                 bool shared) {
  const auto preview = previewCustomFiles(context, files, shared);
  if (!preview.value("valid").toBool()) {
    report(preview.value("error").toString());
    return false;
  }
  return writeLayout(context, preview.value("files").toStringList(), shared, false);
}
bool SaveBackups::resetCustomFiles(const QVariantMap& context) {
  return writeLayout(context, {}, false, true);
}
bool SaveBackups::writeLayout(const QVariantMap& context, const QStringList& files, bool shared,
                              bool remove) {
  if (m_running() || recoveryPending()) {
    report("Close emulators and resolve pending recovery before changing save layouts.");
    return false;
  }
  const auto path = layoutFile();
  if (!safeLocalPath(path) || !QDir().mkpath(QFileInfo(path).absolutePath())) {
    report("Save layout settings storage is unavailable or contains a symbolic link.");
    return false;
  }
  QLockFile lock(path + ".lock");
  if (!lock.tryLock(0)) {
    report("Save layout settings are busy.");
    return false;
  }
  QJsonObject document{{"format", 1}, {"layouts", QJsonArray{}}};
  QFile previous(path);
  if (previous.exists()) {
    if (previous.size() > 1024 * 1024 || !previous.open(QIODevice::ReadOnly)) {
      report("Could not read existing save layouts.");
      return false;
    }
    const auto parsed = QJsonDocument::fromJson(previous.readAll());
    if (!parsed.isObject() || parsed.object().value("format").toInt() != 1 ||
        !parsed.object().value("layouts").isArray()) {
      report("Existing save layouts are damaged; they were kept.");
      return false;
    }
    document = parsed.object();
  }
  QJsonArray entries;
  for (const auto& value : document.value("layouts").toArray()) {
    const auto entry = value.toObject();
    if (entry.value("source").toString() == context.value("source").toString() &&
        entry.value("game").toString() == context.value("game").toString() &&
        entry.value("flatpak").toBool() == context.value("flatpak").toBool())
      continue;
    entries.append(value);
  }
  if (!remove)
    entries.append(QJsonObject{{"source", context.value("source").toString()},
                               {"game", context.value("game").toString()},
                               {"flatpak", context.value("flatpak").toBool()},
                               {"description", "Explicit save files"},
                               {"files", QJsonArray::fromStringList(files)},
                               {"shared", shared}});
  if (entries.size() > 500) {
    report("The save layout limit was reached.");
    return false;
  }
  document["layouts"] = entries;
  const auto bytes = QJsonDocument(document).toJson();
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
      !output.commit()) {
    report("Could not save the layout. Previous settings were kept.");
    return false;
  }
  report(remove ? "Automatic save layout restored."
                : "Save layout updated. No game saves were changed.");
  return true;
}

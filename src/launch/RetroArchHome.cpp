#include "launch/RetroArchHome.h"

#include <QCryptographicHash>
#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace RetroArchHome {
namespace {

const QString kKey = QStringLiteral("joypad_autoconfig_dir");
const QString kMissing = QStringLiteral("#missing");

bool write(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QStringList read(const QString& path, bool* ok = nullptr) {
  QFile file(path);
  const bool opened = file.open(QIODevice::ReadOnly);
  if (ok) *ok = opened;
  return opened ? QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')) : QStringList{};
}

// The setting's line and value, or -1 when retroarch.cfg leaves it out.
int find(const QStringList& lines, QString* value) {
  static const QRegularExpression line(
      QStringLiteral(R"re(^\s*joypad_autoconfig_dir\s*=\s*"?([^"]*)"?\s*$)re"));
  for (int index = 0; index < lines.size(); ++index) {
    const auto match = line.match(lines.at(index));
    if (match.hasMatch()) {
      if (value) *value = match.captured(1).trimmed();
      return index;
    }
  }
  return -1;
}

// RetroArch saves paths under the home folder as "~/...".
QString expanded(const QString& path) {
  return QDir::cleanPath(path.startsWith(QStringLiteral("~/")) ? QDir::homePath() + path.mid(1) : path);
}

bool samePath(const QString& left, const QString& right) {
  return !left.isEmpty() && expanded(left) == expanded(right);
}

QString quoted(const QString& path) {
  return kKey + QStringLiteral(" = \"") + path + QLatin1Char('"');
}

// Names, sizes and times of every profile: a different set means a fresh copy.
QByteArray fingerprint(const QString& directory) {
  QStringList entries{directory};
  QDirIterator files(directory, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
  while (files.hasNext()) {
    const QFileInfo file(files.next());
    entries.append(QStringLiteral("%1 %2 %3").arg(file.filePath().mid(directory.size()))
                       .arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()));
  }
  entries.sort();
  return QCryptographicHash::hash(entries.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha1).toHex();
}

// Copies the profiles without their menu binds. Every other line stays as it was.
bool copyProfiles(const QString& source, const QString& target) {
  const QByteArray stamp = fingerprint(source);
  QFile recorded(target + QStringLiteral("/.omakade-source"));
  if (recorded.open(QIODevice::ReadOnly) && recorded.readAll().trimmed() == stamp) return true;
  recorded.close();
  static const QRegularExpression menu(QStringLiteral(R"re(^\s*input_menu_toggle_(btn|axis)\s*=)re"));
  const QString staging = target + QStringLiteral(".new");
  QDir(staging).removeRecursively();
  QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
  while (files.hasNext()) {
    const QString path = files.next();
    QStringList lines = read(path);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const QString& line) { return menu.match(line).hasMatch(); }),
                lines.end());
    if (!write(staging + path.mid(source.size()), lines.join(QLatin1Char('\n')).toUtf8())) return false;
  }
  if (!write(staging + QStringLiteral("/.omakade-source"), stamp + '\n')) return false;
  QDir(target).removeRecursively();
  return QDir().rename(staging, target);
}

} // namespace

Paths paths(bool flatpak) {
  if (flatpak) {
    const QString app = QDir::homePath() + QStringLiteral("/.var/app/org.libretro.RetroArch");
    // The sandbox always sees its own ~/.var/app directory at the same path.
    return {app + QStringLiteral("/config/retroarch/retroarch.cfg"),
            app + QStringLiteral("/cache/omakade-home.cfg"),
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                QStringLiteral("/omakade/retroarch-home-flatpak"),
            app + QStringLiteral("/cache/omakade-autoconfig"),
            app + QStringLiteral("/config/retroarch/autoconfig")};
  }
  const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  const QString own = config + QStringLiteral("/retroarch/autoconfig");
  return {config + QStringLiteral("/retroarch/retroarch.cfg"),
          QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
              QStringLiteral("/omakade/retroarch-home.cfg"),
          QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
              QStringLiteral("/omakade/retroarch-home"),
          QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
              QStringLiteral("/omakade/retroarch-autoconfig"),
          QDir(own).exists() ? own : QStringLiteral("/usr/share/libretro/autoconfig")};
}

QString prepare(const Paths& paths) {
  const QStringList lines = read(paths.config);
  QString value;
  const int index = find(lines, &value);
  // An earlier session that was never repaired already points at the copy.
  const bool ours = samePath(value, paths.profiles) && QFileInfo::exists(paths.marker);
  QString source = ours || value.isEmpty() || value == QStringLiteral("default") ? paths.fallbackProfiles : expanded(value);
  if (ours) {
    QFile marker(paths.marker);
    if (marker.open(QIODevice::ReadOnly)) {
      static const QRegularExpression original(QStringLiteral(R"re(=\s*"?([^"]*)"?\s*$)re"));
      const auto match = original.match(QString::fromUtf8(marker.readAll()).trimmed());
      if (match.hasMatch() && !match.captured(1).trimmed().isEmpty()) source = expanded(match.captured(1).trimmed());
    }
  }
  if (!QDir(source).exists() || !copyProfiles(source, paths.profiles)) return {};
  if (!QFileInfo::exists(paths.marker) &&
      !write(paths.marker, (index < 0 ? kMissing : lines.at(index)).toUtf8() + '\n'))
    return {};
  if (!write(paths.override, quoted(paths.profiles).toUtf8() + '\n')) return {};
  return paths.override;
}

void repair(const Paths& paths) {
  QFile marker(paths.marker);
  if (!marker.open(QIODevice::ReadOnly)) return;
  const QString original = QString::fromUtf8(marker.readAll()).trimmed();
  marker.close();
  bool ok = false;
  QStringList lines = read(paths.config, &ok);
  QString value;
  const int index = ok ? find(lines, &value) : -1;
  // Anything but the copy is the user's newer choice, or RetroArch did not save.
  if (index >= 0 && samePath(value, paths.profiles)) {
    if (original == kMissing) lines.removeAt(index);
    else lines[index] = original;
    if (!write(paths.config, lines.join(QLatin1Char('\n')).toUtf8())) return;
  }
  QFile::remove(paths.marker);
}

bool retroArchRunning() {
  const QStringList processes = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& pid : processes) {
    bool numeric = false;
    pid.toLongLong(&numeric);
    if (!numeric) continue;
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "retroarch") return true;
  }
  return false;
}

} // namespace RetroArchHome

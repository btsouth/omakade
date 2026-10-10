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

const QString kProfilesKey = QStringLiteral("joypad_autoconfig_dir");
const QString kComboKey = QStringLiteral("input_menu_toggle_gamepad_combo");
const QString kL3R3 = QStringLiteral("2"); // RetroArch's L3 + R3 combo
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
int find(const QStringList& lines, const QString& key, QString* value) {
  const QRegularExpression line(QStringLiteral(R"re(^\s*%1\s*=\s*"?([^"]*)"?\s*$)re").arg(key));
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

QString quoted(const QString& key, const QString& value) {
  return key + QStringLiteral(" = \"") + value + QLatin1Char('"');
}

// What a launch appended, as RetroArch may have saved it back.
bool appended(const Paths& paths, const QString& key, const QString& value) {
  return key == kProfilesKey ? samePath(value, paths.profiles) : value == kL3R3;
}

// The recorded original lines: key, then the line or #missing.
QList<QPair<QString, QString>> originals(const Paths& paths) {
  QList<QPair<QString, QString>> result;
  for (const QString& entry : read(paths.marker)) {
    const int tab = entry.indexOf(QLatin1Char('\t'));
    if (tab > 0) result.append({entry.left(tab), entry.mid(tab + 1)});
  }
  return result;
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

QString menuCombo(qint64 pid) {
  QFile commandLine(QStringLiteral("/proc/%1/cmdline").arg(pid));
  if (pid <= 0 || !commandLine.open(QIODevice::ReadOnly)) return {};
  const QByteArray arguments = commandLine.readAll();
  for (const bool flatpak : {false, true}) {
    const Paths own = paths(flatpak);
    if (!arguments.contains(QFile::encodeName(own.override))) continue;
    // The combo in force: appended, or the user's own, which an append never replaces.
    QString combo;
    find(read(own.override), kComboKey, &combo);
    if (combo.isEmpty()) find(read(own.config), kComboKey, &combo);
    static const QStringList names{QString{}, QStringLiteral("Down + Y + L1 + R1"), QStringLiteral("L3 + R3"),
                                   QStringLiteral("L1 + R1 + Start + Select"), QStringLiteral("Start + Select"),
                                   QStringLiteral("L3 + R1"), QStringLiteral("L1 + R1"), QStringLiteral("Hold Start"),
                                   QStringLiteral("Hold Select"), QStringLiteral("Down + Select"), QStringLiteral("L2 + R2")};
    bool number = false;
    const int index = combo.toInt(&number);
    return number && index > 0 && index < names.size() ? names.at(index) : QString{};
  }
  return {};
}

QString prepare(const Paths& paths) {
  const QStringList lines = read(paths.config);
  QString value;
  const int index = find(lines, kProfilesKey, &value);
  // An earlier session that was never repaired already points at the copy: copy the
  // profiles it recorded instead.
  QString source = value.isEmpty() || value == QStringLiteral("default") ? paths.fallbackProfiles : expanded(value);
  if (samePath(value, paths.profiles)) {
    source = paths.fallbackProfiles;
    for (const auto& [key, original] : originals(paths)) {
      QString recorded;
      if (key == kProfilesKey && find({original}, kProfilesKey, &recorded) == 0 && !recorded.isEmpty())
        source = expanded(recorded);
    }
  }
  if (!QDir(source).exists() || !copyProfiles(source, paths.profiles)) return {};
  if (!QFileInfo::exists(paths.marker)) {
    QStringList record;
    for (const QString& key : {kProfilesKey, kComboKey}) {
      const int at = find(lines, key, nullptr);
      record.append(key + QLatin1Char('\t') + (at < 0 ? kMissing : lines.at(at)));
    }
    if (!write(paths.marker, record.join(QLatin1Char('\n')).toUtf8() + '\n')) return {};
  }
  // Keep RetroArch's menu on the controller: L3 + R3, unless the user chose a combo.
  QString combo;
  find(lines, kComboKey, &combo);
  QString appendedLines = quoted(kProfilesKey, paths.profiles) + QLatin1Char('\n');
  if (combo.isEmpty() || combo == QStringLiteral("0") || combo == kL3R3)
    appendedLines += quoted(kComboKey, kL3R3) + QLatin1Char('\n');
  if (!write(paths.override, appendedLines.toUtf8())) return {};
  return paths.override;
}

void repair(const Paths& paths) {
  if (!QFileInfo::exists(paths.marker)) return;
  const auto recorded = originals(paths);
  bool ok = false;
  QStringList lines = read(paths.config, &ok);
  bool changed = false;
  for (const auto& [key, original] : recorded) {
    QString value;
    const int index = ok ? find(lines, key, &value) : -1;
    // Anything but what was appended is the user's newer choice, or RetroArch did not save.
    if (index < 0 || !appended(paths, key, value)) continue;
    if (original == kMissing) lines.removeAt(index);
    else lines[index] = original;
    changed = true;
  }
  if (changed && !write(paths.config, lines.join(QLatin1Char('\n')).toUtf8())) return;
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

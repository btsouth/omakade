#include "launch/RetroArchHome.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace RetroArchHome {
namespace {

const QRegularExpression& bindLine() {
  static const QRegularExpression line(
      QStringLiteral(R"(^\s*input_menu_toggle_btn\s*=\s*"?([^"]*)"?\s*$)"));
  return line;
}

const QString kMissing = QStringLiteral("#missing");

bool write(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

// The bind's line and value, or -1 when retroarch.cfg leaves it out.
int find(const QStringList& lines, QString* value) {
  for (int index = 0; index < lines.size(); ++index) {
    const auto match = bindLine().match(lines.at(index));
    if (match.hasMatch()) {
      if (value) *value = match.captured(1).trimmed();
      return index;
    }
  }
  return -1;
}

QStringList read(const QString& path, bool* ok = nullptr) {
  QFile file(path);
  const bool opened = file.open(QIODevice::ReadOnly);
  if (ok) *ok = opened;
  return opened ? QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')) : QStringList{};
}

} // namespace

Paths paths(bool flatpak) {
  if (flatpak) {
    const QString app = QDir::homePath() + QStringLiteral("/.var/app/org.libretro.RetroArch");
    // The sandbox always sees its own ~/.var/app directory at the same path.
    return {app + QStringLiteral("/config/retroarch/retroarch.cfg"),
            app + QStringLiteral("/cache/omakade-home.cfg"),
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                QStringLiteral("/omakade/retroarch-home-flatpak")};
  }
  return {QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
              QStringLiteral("/retroarch/retroarch.cfg"),
          QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
              QStringLiteral("/omakade/retroarch-home.cfg"),
          QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
              QStringLiteral("/omakade/retroarch-home")};
}

QString prepare(const Paths& paths) {
  const QStringList lines = read(paths.config);
  QString value;
  const int index = find(lines, &value);
  const bool unset = index < 0 || value.isEmpty() || value == QStringLiteral("nul");
  const bool ours = value == QLatin1String(kUnusedButton) && QFileInfo::exists(paths.marker);
  if (!unset && !ours) return {};
  // Keep the first recorded original if an earlier session was never repaired.
  if (!QFileInfo::exists(paths.marker) &&
      !write(paths.marker, (index < 0 ? kMissing : lines.at(index)).toUtf8() + '\n'))
    return {};
  if (!write(paths.override,
             QByteArrayLiteral("input_menu_toggle_btn = \"") + kUnusedButton + "\"\n"))
    return {};
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
  // Anything but our value is the user's newer choice, or RetroArch did not save.
  if (index >= 0 && value == QLatin1String(kUnusedButton)) {
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

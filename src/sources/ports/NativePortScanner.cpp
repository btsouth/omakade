#include "sources/ports/NativePortScanner.h"

#include "sources/desktop/DesktopEntry.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace {
constexpr int kMaximumFolders = 2000;

// The port a launcher runs, judged by its executable first and then by the paths its
// wrapper script names, folder by folder.
const NativePort* portFor(const DesktopLauncher& launcher, const PortCatalog& catalog,
                          bool* direct) {
  *direct = false;
  if (const NativePort* port = catalog.matchFile(QFileInfo(launcher.entry.executable).fileName())) {
    *direct = true;
    return port;
  }
  for (const QString& program : launcher.localPrograms) {
    if (const NativePort* port = catalog.matchFile(program)) {
      return port;
    }
  }
  for (const QString& path : launcher.paths) {
    for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
      if (const NativePort* port = catalog.matchFile(part)) {
        if (launcher.entry.categories.contains(QStringLiteral("Game")) ||
            PortCatalog::normalizedTitle(launcher.entry.title) ==
                PortCatalog::normalizedTitle(port->game)) {
          return port;
        }
      }
    }
  }
  return nullptr;
}

// Several entries can run one port (its settings, a clip recorder); the one named after the
// game and filed as a game is the one to play.
int rank(const DesktopLauncher& launcher, const NativePort& port, bool direct) {
  int value = direct ? 1 : 0;
  if (launcher.entry.categories.contains(QStringLiteral("Game"))) value += 2;
  if (PortCatalog::normalizedTitle(launcher.entry.title) == PortCatalog::normalizedTitle(port.game))
    value += 4;
  if (launcher.entry.title.compare(port.game, Qt::CaseInsensitive) == 0) value += 8;
  return value;
}

QString iconFile(const QString& icon) {
  const QFileInfo info(icon);
  return info.isAbsolute() && info.isFile() ? info.absoluteFilePath() : QString{};
}

LauncherGameRecord recordFor(const NativePort& port) {
  return {.gameId = port.id,
          .title = port.game,
          .system = port.system,
          .subtitle = port.name,
          .path = {},
          .serial = {},
          .coverPath = {},
          .heroPath = {},
          .executable = {},
          .arguments = {},
          .directory = {},
          .desktopEntry = {},
          .link = port.repo};
}
} // namespace

NativePortScanner::Options NativePortScanner::defaultOptions() {
  const QString home = QDir::homePath();
  return {.applicationDirectories = DesktopEntry::applicationDirectories(),
          .appImageDirectories = {home + QStringLiteral("/Applications"),
                                  home + QStringLiteral("/AppImages"),
                                  home + QStringLiteral("/.local/bin"),
                                  home + QStringLiteral("/Games")}};
}

LauncherScanResult NativePortScanner::scan(const Options& options, const PortCatalog& catalog) {
  LauncherScanResult result;
  QHash<QString, int> best;
  QHash<QString, int> ranks;
  for (const DesktopLauncher& launcher : DesktopLaunchers::scan(options.applicationDirectories)) {
    bool direct = false;
    const NativePort* port = portFor(launcher, catalog, &direct);
    if (port == nullptr) {
      continue;
    }
    const int value = rank(launcher, *port, direct);
    if (best.contains(port->id) && ranks.value(port->id) >= value) {
      continue;
    }
    LauncherGameRecord record = recordFor(*port);
    record.path = launcher.entry.executable;
    record.coverPath = iconFile(launcher.entry.icon);
    record.executable = launcher.entry.executable;
    record.arguments = launcher.entry.arguments;
    record.directory = launcher.entry.directory;
    record.desktopEntry = launcher.entry.path;
    if (best.contains(port->id)) {
      result.games[best.value(port->id)] = record;
    } else {
      best.insert(port->id, static_cast<int>(result.games.size()));
      result.games.append(record);
    }
    ranks.insert(port->id, value);
  }

  // A folder of AppImages, or one folder per port inside it, as release archives unpack.
  int budget = kMaximumFolders;
  for (const QString& directory : options.appImageDirectories) {
    QFileInfoList files;
    const QStringList patterns{QStringLiteral("*.AppImage"), QStringLiteral("*.appimage")};
    files += QDir(directory).entryInfoList(patterns, QDir::Files, QDir::Name);
    for (const QFileInfo& folder : QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      if (budget-- <= 0) break;
      files += QDir(folder.absoluteFilePath()).entryInfoList(patterns, QDir::Files, QDir::Name);
    }
    for (const QFileInfo& file : files) {
      const NativePort* port = file.isExecutable() ? catalog.matchFile(file.fileName()) : nullptr;
      if (port == nullptr || best.contains(port->id)) {
        continue;
      }
      LauncherGameRecord record = recordFor(*port);
      record.path = file.absoluteFilePath();
      record.executable = file.absoluteFilePath();
      record.directory = file.absolutePath();
      best.insert(port->id, static_cast<int>(result.games.size()));
      result.games.append(record);
    }
  }
  for (const LauncherGameRecord& game : result.games) {
    result.roots.append(game.desktopEntry.isEmpty() ? game.path : game.desktopEntry);
  }
  result.roots.removeDuplicates();
  return result;
}

#include "sources/kyty/KytyScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr int kConfiguredDepth = 4;
constexpr int kScriptFolderDepth = 3;
constexpr int kMaximumFolders = 20000;
constexpr qint64 kMaximumParamBytes = 1024 * 1024;

bool mentionsKyty(const DesktopLauncher& launcher) {
  if (launcher.script.contains(QStringLiteral("kyty"), Qt::CaseInsensitive)) {
    return true;
  }
  for (const QString& path : launcher.paths) {
    if (path.contains(QStringLiteral("kyty"), Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

QString emulatorIn(const QString& candidate) {
  const QFileInfo info(candidate);
  QStringList options;
  if (info.isDir()) {
    options = {candidate + QStringLiteral("/kyty_emulator"),
               candidate + QStringLiteral("/current/kyty_emulator")};
  } else if (info.fileName() == QLatin1String("kyty_emulator")) {
    options = {candidate};
  } else if (info.isFile()) {
    options = {info.absolutePath() + QStringLiteral("/kyty_emulator")};
  }
  for (const QString& option : options) {
    const QFileInfo file(option);
    if (file.isFile() && file.isExecutable()) {
      return QDir::cleanPath(file.absoluteFilePath());
    }
  }
  return {};
}

QString localizedTitle(const QJsonObject& root) {
  const QJsonObject localized = root.value(QStringLiteral("localizedParameters")).toObject();
  const auto titleIn = [&localized](const QString& language) {
    return localized.value(language).toObject().value(QStringLiteral("titleName")).toString().trimmed();
  };
  QString title = titleIn(localized.value(QStringLiteral("defaultLanguage")).toString());
  if (title.isEmpty()) {
    title = titleIn(QStringLiteral("en-US"));
  }
  for (auto it = localized.constBegin(); title.isEmpty() && it != localized.constEnd(); ++it) {
    title = it.value().toObject().value(QStringLiteral("titleName")).toString().trimmed();
  }
  return title;
}

struct Ps5Game {
  QString directory;
  QString title;
  QString titleId;
};

bool readGame(const QDir& directory, Ps5Game* game) {
  QFile param(directory.filePath(QStringLiteral("sce_sys/param.json")));
  if (!QFileInfo::exists(directory.filePath(QStringLiteral("eboot.bin"))) ||
      param.size() > kMaximumParamBytes || !param.open(QIODevice::ReadOnly)) {
    return false;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(param.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  const QJsonObject root = document.object();
  if (root.isEmpty()) {
    return false;
  }
  game->directory = QDir::cleanPath(directory.absolutePath());
  game->title = localizedTitle(root).left(256);
  game->titleId = root.value(QStringLiteral("titleId")).toString().trimmed().left(16);
  const QString contentId = root.value(QStringLiteral("contentId")).toString();
  if (game->titleId.isEmpty() && contentId.size() == 36 && contentId.at(6) == QLatin1Char('-') &&
      contentId.at(16) == QLatin1Char('_')) {
    game->titleId = contentId.mid(7, 9);
  }
  if (game->title.isEmpty()) {
    game->title = directory.dirName();
  }
  return true;
}

void collect(const QString& path, int depth, int* budget, QSet<QString>* visited,
             QVector<Ps5Game>* games, LauncherScanResult* result) {
  if (depth < 0 || *budget <= 0) {
    return;
  }
  const QString canonical = QFileInfo(path).canonicalFilePath();
  if (canonical.isEmpty() || visited->contains(canonical)) {
    return;
  }
  visited->insert(canonical);
  --*budget;
  const QDir directory(path);
  Ps5Game game;
  if (QFileInfo::exists(directory.filePath(QStringLiteral("sce_sys/param.json")))) {
    if (readGame(directory, &game)) {
      games->append(game);
    } else {
      result->incomplete = true;
      result->warnings.append(QStringLiteral("Could not read PS5 game metadata in %1").arg(path));
    }
    return;
  }
  for (const QFileInfo& child : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                        QDir::Name)) {
    collect(child.absoluteFilePath(), depth - 1, budget, visited, games, result);
  }
}

// A script names this game when it names its folder, a folder holding only this game, or
// its title ID.
bool launcherNames(const DesktopLauncher& launcher, const Ps5Game& game,
                   const QVector<Ps5Game>& games) {
  if (!game.titleId.isEmpty()) {
    const QRegularExpression word(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(game.titleId)));
    if (launcher.script.contains(word)) {
      return true;
    }
  }
  for (const QString& path : launcher.paths) {
    if (path == game.directory) {
      return true;
    }
    if (!game.directory.startsWith(path + QLatin1Char('/'))) {
      continue;
    }
    bool onlyThisGame = true;
    // Other copies of the same game, such as a patched one beside it, do not count.
    for (const Ps5Game& other : games) {
      onlyThisGame = onlyThisGame && (other.directory == game.directory ||
                                      (!game.titleId.isEmpty() && other.titleId == game.titleId) ||
                                      !other.directory.startsWith(path + QLatin1Char('/')));
    }
    if (onlyThisGame && QFileInfo(path).isDir() && path != QDir::homePath()) {
      return true;
    }
  }
  return false;
}

int launcherRank(const DesktopLauncher& launcher, const Ps5Game& game) {
  int rank = 0;
  if (launcher.entry.categories.contains(QStringLiteral("Game"))) rank += 2;
  if (launcher.entry.title.compare(game.title, Qt::CaseInsensitive) == 0) rank += 4;
  return rank;
}

QString firstExisting(const QStringList& candidates) {
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}
} // namespace

KytyScanner::Options KytyScanner::defaultOptions() {
  const QString home = QDir::homePath();
  const QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME", home + QStringLiteral("/.config"));
  Options options{.applicationDirectories = DesktopEntry::applicationDirectories(),
          .configFiles = {configHome + QStringLiteral("/Kyty/Kyty.ini")},
          .installDirectories = {home + QStringLiteral("/.local/opt/kytyps5"),
                                 home + QStringLiteral("/.local/opt/KytyPS5"),
                                 home + QStringLiteral("/.local/share/KytyPS5"),
                                 home + QStringLiteral("/Applications/KytyPS5"),
                                 home + QStringLiteral("/KytyPS5"),
                                 QStringLiteral("/opt/kytyps5"), QStringLiteral("/opt/KytyPS5")},
          .gameDirectories = {}};
  for (const QString& directory : {home + QStringLiteral("/Emulation/roms/ps5"),
                                   home + QStringLiteral("/Emulation/Games/PS5"),
                                   QStringLiteral("/data/Emulation/Games/PS5")}) {
    if (QFileInfo(directory).isDir()) options.gameDirectories.append(directory);
  }
  return options;
}

QStringList KytyScanner::emulatorArguments(const QString& gameDirectory) {
  return {QStringLiteral("--game"), gameDirectory, QStringLiteral("--fullscreen")};
}

LauncherScanResult KytyScanner::scan(const Options& options) {
  LauncherScanResult result;
  QVector<DesktopLauncher> launchers;
  for (const DesktopLauncher& launcher : DesktopLaunchers::scan(options.applicationDirectories)) {
    if (mentionsKyty(launcher)) {
      launchers.append(launcher);
    }
  }

  QString emulator = QStandardPaths::findExecutable(QStringLiteral("kyty_emulator"));
  QStringList candidates = options.installDirectories;
  for (const DesktopLauncher& launcher : launchers) {
    candidates += launcher.paths;
  }
  for (const QString& candidate : candidates) {
    if (emulator.isEmpty()) {
      emulator = emulatorIn(candidate);
    }
  }

  QStringList configs = options.configFiles;
  if (!emulator.isEmpty()) {
    configs.append(QFileInfo(emulator).absolutePath() + QStringLiteral("/Kyty.ini"));
  }
  for (const DesktopLauncher& launcher : launchers) {
    configs.append(launcher.entry.directory + QStringLiteral("/Kyty.ini"));
  }
  configs.removeDuplicates();

  QStringList configured;
  if (!emulator.isEmpty() || !launchers.isEmpty()) {
    for (const QString& directory : options.gameDirectories) {
      if (QFileInfo(directory).isDir()) {
        configured.append(directory);
        result.roots.append(directory);
      } else {
        result.incomplete = true;
        result.warnings.append(QStringLiteral("Game folder is unavailable: %1").arg(directory));
      }
    }
  }
  for (const QString& config : configs) {
    if (!QFileInfo(config).isFile()) {
      continue;
    }
    result.roots.append(config);
    const QSettings settings(config, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
      result.incomplete = true;
      result.warnings.append(QStringLiteral("Could not read %1").arg(config));
      continue;
    }
    QStringList folders = settings.value(QStringLiteral("game_dirs")).toStringList();
    folders.append(settings.value(QStringLiteral("game_dir")).toString());
    for (const QString& folder : folders) {
      if (folder.trimmed().isEmpty()) {
        continue;
      }
      if (!QFileInfo(folder).isDir()) {
        result.incomplete = true;
        result.warnings.append(QStringLiteral("Game folder is unavailable: %1").arg(folder));
        continue;
      }
      configured.append(QDir::cleanPath(folder));
    }
  }
  if (!emulator.isEmpty()) {
    result.roots.append(emulator);
  }
  for (const DesktopLauncher& launcher : launchers) {
    result.roots.append(launcher.entry.path);
  }
  result.roots.removeDuplicates();

  QVector<Ps5Game> games;
  QSet<QString> visited;
  int budget = kMaximumFolders;
  configured.removeDuplicates();
  for (const QString& folder : configured) {
    collect(folder, kConfiguredDepth, &budget, &visited, &games, &result);
  }
  const QString emulatorFolder = emulator.isEmpty() ? QString{} : QFileInfo(emulator).absolutePath();
  for (const DesktopLauncher& launcher : launchers) {
    for (const QString& path : launcher.paths) {
      const QFileInfo info(path);
      if (info.isDir() && path != QStringLiteral("/") && path != QDir::homePath() &&
          !(emulatorFolder + QLatin1Char('/')).startsWith(path + QLatin1Char('/'))) {
        collect(path, kScriptFolderDepth, &budget, &visited, &games, &result);
      }
    }
  }
  if (budget <= 0) {
    result.incomplete = true;
    result.warnings.append(QStringLiteral("Stopped looking for Kyty games after %1 folders")
                               .arg(kMaximumFolders));
  }

  QSet<QString> seenIds;
  for (const Ps5Game& game : games) {
    const QString gameId =
        game.titleId.isEmpty() ? QStringLiteral("path:%1").arg(game.directory) : game.titleId;
    if (seenIds.contains(gameId)) {
      continue;
    }
    const DesktopLauncher* wrapper = nullptr;
    for (const DesktopLauncher& launcher : launchers) {
      if (launcherNames(launcher, game, games) &&
          (wrapper == nullptr || launcherRank(launcher, game) > launcherRank(*wrapper, game))) {
        wrapper = &launcher;
      }
    }
    if (wrapper == nullptr && emulator.isEmpty()) {
      result.warnings.append(
          QStringLiteral("Found %1, but not kyty_emulator to run it").arg(game.title));
      continue;
    }
    seenIds.insert(gameId);
    const QString sceSys = game.directory + QStringLiteral("/sce_sys");
    LauncherGameRecord record{
        .gameId = gameId,
        .title = game.title,
        .system = QStringLiteral("ps5"),
        .subtitle = QStringLiteral("Kyty"),
        .path = game.directory,
        .serial = game.titleId,
        .coverPath = firstExisting({sceSys + QStringLiteral("/icon0.png")}),
        .heroPath = firstExisting({sceSys + QStringLiteral("/pic0.png"),
                                   sceSys + QStringLiteral("/pic1.png")}),
        .executable = emulator,
        .arguments = emulatorArguments(game.directory),
        .directory = emulatorFolder,
        .desktopEntry = {},
        .link = {}};
    if (wrapper != nullptr) {
      record.executable = wrapper->entry.executable;
      record.arguments = wrapper->entry.arguments;
      record.directory = wrapper->entry.directory;
      record.desktopEntry = wrapper->entry.path;
    }
    result.games.append(record);
  }
  return result;
}

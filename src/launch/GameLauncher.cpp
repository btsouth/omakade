#include "launch/GameLauncher.h"

#include "launch/SteamLauncher.h"
#include "sources/FlatpakInstall.h"
#include "sources/battlenet/BattleNetScanner.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {
bool validLutrisId(const QString& id) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
  return digits.match(id).hasMatch();
}

bool validHeroicTarget(const QString& id, const QString& runner) {
  static const QRegularExpression appId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,255}$"));
  return appId.match(id).hasMatch() &&
         (runner == QStringLiteral("legendary") || runner == QStringLiteral("gog") ||
          runner == QStringLiteral("nile") || runner == QStringLiteral("sideload"));
}

bool validFaugusId(const QString& id) {
  static const QRegularExpression gameId(
      QStringLiteral("^[\\p{L}\\p{N}_-][\\p{L}\\p{N}_.-]{0,254}$"),
      QRegularExpression::UseUnicodePropertiesOption);
  return gameId.match(id).hasMatch();
}

bool installedTargetExists(const QString& path) {
  if (QFileInfo::exists(path)) {
    return true;
  }
  const qsizetype archiveSeparator = path.indexOf(QLatin1Char('#'));
  return archiveSeparator > 0 && QFileInfo::exists(path.left(archiveSeparator));
}

bool validBattleNetId(const QString& id) {
  static const QRegularExpression product(
      QStringLiteral("^[A-Za-z][A-Za-z0-9._-]{0,63}(@[a-f0-9]{8})?$"));
  return product.match(id).hasMatch();
}

bool validBattleNetPrefix(const QString& prefix) {
  const QString cleaned = QDir::cleanPath(prefix);
  return cleaned.startsWith(QLatin1Char('/')) && !cleaned.contains(QStringLiteral("/../")) &&
         !cleaned.contains(QChar::Null);
}

QString battleNetExecutable(const QString& prefix) {
  const QStringList candidates = {
      prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net.exe"),
      prefix + QStringLiteral("/drive_c/Program Files/Battle.net/Battle.net.exe"),
      prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net Launcher.exe"),
      prefix + QStringLiteral("/drive_c/Program Files/Battle.net/Battle.net Launcher.exe")};
  for (const QString& candidate : candidates) {
    if (QFileInfo(candidate).isFile()) {
      return candidate;
    }
  }
  return candidates.constFirst();
}

QString wineExecutable() {
  const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
  return wine.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("wine64")) : wine;
}

QString bottlesBottleName(const QString& prefix) {
  QFile file(prefix + QStringLiteral("/bottle.yml"));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    static const QRegularExpression name(
        QStringLiteral("(?m)^Name:\\s*[\"']?([^\"'\\n]+?)[\"']?\\s*$"));
    const QRegularExpressionMatch match = name.match(QString::fromUtf8(file.readAll()));
    if (match.hasMatch()) {
      const QString value = match.captured(1).trimmed();
      if (!value.isEmpty()) {
        return value;
      }
    }
  }
  return QFileInfo(prefix).fileName();
}
} // namespace

GameLauncher::GameLauncher(QObject* parent) : QObject(parent) {}

QString GameLauncher::lastError() const { return m_lastError; }

LaunchCommand GameLauncher::lutrisCommand(const QString& id, bool flatpak) {
  if (!validLutrisId(id)) {
    return {};
  }
  const QString target = QStringLiteral("lutris:rungameid/%1").arg(id);
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"), QStringLiteral("net.lutris.Lutris"), target}}
             : LaunchCommand{QStringLiteral("lutris"), {target}};
}

LaunchCommand GameLauncher::heroicCommand(const QString& id, const QString& runner, bool flatpak) {
  if (!validHeroicTarget(id, runner)) {
    return {};
  }
  QUrl url(QStringLiteral("heroic://launch"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("appName"), id);
  query.addQueryItem(QStringLiteral("runner"), runner);
  query.addQueryItem(QStringLiteral("gui"), QStringLiteral("false"));
  url.setQuery(query);
  const QString target = url.toString(QUrl::FullyEncoded);
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"), QStringLiteral("com.heroicgameslauncher.hgl"),
                              QStringLiteral("--no-gui"), target}}
             : LaunchCommand{QStringLiteral("heroic"), {QStringLiteral("--no-gui"), target}};
}

LaunchCommand GameLauncher::faugusCommand(const QString& id, bool flatpak) {
  if (!validFaugusId(id)) {
    return {};
  }
  return flatpak
             ? LaunchCommand{QStringLiteral("flatpak"),
                             {QStringLiteral("run"),
                              QStringLiteral("--command=/app/bin/faugus-launcher"),
                              QStringLiteral("io.github.Faugus.faugus-launcher"),
                              QStringLiteral("--game"), id}}
             : LaunchCommand{QStringLiteral("faugus-launcher"),
                             {QStringLiteral("--game"), id}};
}

LaunchCommand GameLauncher::retroArchCommand(const QString& contentPath, const QString& corePath,
                                             bool flatpak) {
  if (contentPath.trimmed().isEmpty() || corePath.trimmed().isEmpty() ||
      corePath == QStringLiteral("DETECT")) {
    return {};
  }
  return flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch"),
                                  QStringLiteral("-L"), corePath, contentPath}}
                 : LaunchCommand{QStringLiteral("retroarch"),
                                 {QStringLiteral("-L"), corePath, contentPath}};
}

LaunchCommand GameLauncher::battleNetCommand(const QString& id, const QString& prefix,
                                             const QString& runner, bool flatpak) {
  if (!validBattleNetId(id) || !validBattleNetPrefix(prefix)) {
    return {};
  }
  const QString launchCode = BattleNetScanner::launchCodeForProduct(id);
  const QString exe = battleNetExecutable(prefix);
  const QString execArg = QStringLiteral("--exec=launch %1").arg(launchCode);
  if (runner == QStringLiteral("bottles")) {
    const QString bottle = bottlesBottleName(prefix);
    if (bottle.isEmpty()) {
      return {};
    }
    return flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                   {QStringLiteral("run"), QStringLiteral("--command=bottles-cli"),
                                    QStringLiteral("com.usebottles.bottles"), QStringLiteral("run"),
                                    QStringLiteral("-b"), bottle, QStringLiteral("-e"), exe,
                                    QStringLiteral("--"), execArg}}
                   : LaunchCommand{QStringLiteral("bottles-cli"),
                                   {QStringLiteral("run"), QStringLiteral("-b"), bottle,
                                    QStringLiteral("-e"), exe, QStringLiteral("--"), execArg}};
  }
  if (runner == QStringLiteral("proton")) {
    return {QStringLiteral("umu-run"), {exe, execArg}};
  }
  return {QStringLiteral("wine"), {exe, execArg}};
}

bool GameLauncher::launch(const QString& source, const QString& id, bool flatpak,
                          const QString& runner, const QString& installPath,
                          const QString& launchTarget) {
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) != 0 &&
      !installPath.isEmpty() && !installedTargetExists(installPath)) {
    setError(QStringLiteral(
                 "The installed files are missing. Rescan or repair this game in %1.")
                 .arg(source));
    return false;
  }
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0) {
    const QUrl url = SteamLauncher::launchUrl(id);
    if (!url.isValid() || url.isEmpty()) {
      setError(QStringLiteral("This game has an invalid Steam App ID."));
      return false;
    }
    if (!QDesktopServices::openUrl(url)) {
      setError(QStringLiteral("Steam could not open the game. Check that Steam is installed."));
      return false;
    }
    setError({});
    return true;
  }
  if (source.compare(QStringLiteral("Lutris"), Qt::CaseInsensitive) == 0) {
    return launchLutris(id, flatpak, false);
  }
  if (source.compare(QStringLiteral("Heroic"), Qt::CaseInsensitive) == 0) {
    return launchHeroic(id, runner, flatpak, false);
  }
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) == 0) {
    return launchFaugus(id, flatpak, false);
  }
  if (source.compare(QStringLiteral("RetroArch"), Qt::CaseInsensitive) == 0) {
    return launchRetroArch(installPath, launchTarget, flatpak, false);
  }
  if (source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0) {
    return launchBattleNet(id, launchTarget, runner, flatpak, false);
  }
  setError(QStringLiteral("%1 games cannot be launched yet.").arg(source));
  return false;
}

bool GameLauncher::manage(const QString& source, const QString& id, bool flatpak,
                          const QString& runner, const QString& launchTarget) {
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) == 0) {
    const QUrl url = SteamLauncher::manageUrl(id);
    if (!url.isValid() || url.isEmpty() || !QDesktopServices::openUrl(url)) {
      setError(QStringLiteral("Steam could not open the game details."));
      return false;
    }
    setError({});
    return true;
  }
  if (source.compare(QStringLiteral("Lutris"), Qt::CaseInsensitive) == 0) {
    return launchLutris(id, flatpak, true);
  }
  if (source.compare(QStringLiteral("Heroic"), Qt::CaseInsensitive) == 0) {
    return launchHeroic(id, runner, flatpak, true);
  }
  if (source.compare(QStringLiteral("Faugus"), Qt::CaseInsensitive) == 0) {
    return launchFaugus(id, flatpak, true);
  }
  if (source.compare(QStringLiteral("RetroArch"), Qt::CaseInsensitive) == 0) {
    return launchRetroArch({}, {}, flatpak, true);
  }
  if (source.compare(QStringLiteral("Battle.net"), Qt::CaseInsensitive) == 0) {
    return launchBattleNet(id, launchTarget, runner, flatpak, true);
  }
  setError(QStringLiteral("%1 does not provide game management yet.").arg(source));
  return false;
}

bool GameLauncher::install(const QString& source, const QString& id) {
  if (source.compare(QStringLiteral("Steam"), Qt::CaseInsensitive) != 0) {
    setError(QStringLiteral("%1 games cannot be installed here yet.").arg(source));
    return false;
  }
  const QUrl url = SteamLauncher::installUrl(id);
  if (!url.isValid() || url.isEmpty()) {
    setError(QStringLiteral("This game has an invalid Steam App ID."));
    return false;
  }
  if (!QDesktopServices::openUrl(url)) {
    setError(QStringLiteral("Steam could not start the installation."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchLutris(const QString& id, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("lutris");
  const bool available = !QStandardPaths::findExecutable(executable).isEmpty();
  LaunchCommand command;
  if (manageOnly) {
    command = flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                      {QStringLiteral("run"), QStringLiteral("net.lutris.Lutris")}}
                      : LaunchCommand{QStringLiteral("lutris"), {}};
  } else {
    command = lutrisCommand(id, flatpak);
  }
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Lutris ID."));
    return false;
  }
  if (!available) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Lutris is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("net.lutris.Lutris"),
                                       QStringLiteral("Lutris"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Lutris could not be started. Open Lutris and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchHeroic(const QString& id, const QString& runner, bool flatpak,
                                bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("heroic");
  const bool available = !QStandardPaths::findExecutable(executable).isEmpty();
  LaunchCommand command;
  if (manageOnly) {
    command =
        flatpak
            ? LaunchCommand{QStringLiteral("flatpak"),
                            {QStringLiteral("run"), QStringLiteral("com.heroicgameslauncher.hgl")}}
            : LaunchCommand{QStringLiteral("heroic"), {}};
  } else {
    command = heroicCommand(id, runner, flatpak);
  }
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Heroic target."));
    return false;
  }
  if (!available) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Heroic is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("com.heroicgameslauncher.hgl"),
                                       QStringLiteral("Heroic"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Heroic could not be started. Open Heroic and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchFaugus(const QString& id, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak")
                                     : QStringLiteral("faugus-launcher");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("Faugus is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error = flatpakError(QStringLiteral("io.github.Faugus.faugus-launcher"),
                                       QStringLiteral("Faugus"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak
                 ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"),
                                  QStringLiteral("io.github.Faugus.faugus-launcher")}}
                 : LaunchCommand{QStringLiteral("faugus-launcher"), {}})
          : faugusCommand(id, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Faugus ID."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Faugus could not be started. Open Faugus and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchRetroArch(const QString& contentPath, const QString& corePath,
                                   bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("retroarch");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("RetroArch is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error =
        flatpakError(QStringLiteral("org.libretro.RetroArch"), QStringLiteral("RetroArch"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak
                 ? LaunchCommand{QStringLiteral("flatpak"),
                                 {QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch")}}
                 : LaunchCommand{QStringLiteral("retroarch"), {}})
          : retroArchCommand(contentPath, corePath, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("Set a core association for this game in RetroArch, then rescan."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("RetroArch could not be started. Open RetroArch and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchBattleNet(const QString& id, const QString& prefix, const QString& runner,
                                   bool flatpak, bool manageOnly) {
  LaunchCommand command = battleNetCommand(id, prefix, runner, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Battle.net target."));
    return false;
  }
  const QString exe = battleNetExecutable(prefix);
  if (!QFileInfo(exe).isFile()) {
    setError(QStringLiteral(
        "Battle.net is not installed in this Wine prefix. Install it, then rescan."));
    return false;
  }
  const QString execArg =
      QStringLiteral("--exec=launch %1").arg(BattleNetScanner::launchCodeForProduct(id));
  const bool bottlesAvailable =
      runner == QStringLiteral("bottles") &&
      !QStandardPaths::findExecutable(command.program).isEmpty();
  const bool protonAvailable = runner == QStringLiteral("proton") &&
                               !QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty();
  if (bottlesAvailable && flatpak) {
    const QString error =
        flatpakError(QStringLiteral("com.usebottles.bottles"), QStringLiteral("Bottles"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  if (!bottlesAvailable && !protonAvailable) {
    const QString wine = wineExecutable();
    if (wine.isEmpty()) {
      if (runner == QStringLiteral("proton")) {
        setError(QStringLiteral(
            "umu-launcher is not installed. Install it or Wine to launch Battle.net games."));
      } else if (runner == QStringLiteral("bottles")) {
        setError(flatpak ? QStringLiteral("The Bottles Flatpak is not installed.")
                         : QStringLiteral("Bottles is not installed."));
      } else {
        setError(QStringLiteral("Wine is not installed."));
      }
      return false;
    }
    command = {wine, manageOnly ? QStringList{exe} : QStringList{exe, execArg}};
  } else if (manageOnly && !command.arguments.isEmpty() &&
             command.arguments.constLast().startsWith(QStringLiteral("--exec="))) {
    command.arguments.removeLast();
    if (!command.arguments.isEmpty() && command.arguments.constLast() == QStringLiteral("--")) {
      command.arguments.removeLast();
    }
  }

  QProcess process;
  process.setProgram(command.program);
  process.setArguments(command.arguments);
  process.setWorkingDirectory(QFileInfo(exe).absolutePath());
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("WINEPREFIX"), QDir::cleanPath(prefix));
  if (protonAvailable) {
    environment.insert(QStringLiteral("GAMEID"), QStringLiteral("0"));
    environment.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"),
                       QFileInfo(prefix + QStringLiteral("/..")).absoluteFilePath());
  }
  process.setProcessEnvironment(environment);
  if (!process.startDetached()) {
    setError(QStringLiteral("Battle.net could not be started. Open Battle.net and try again."));
    return false;
  }
  setError({});
  return true;
}

QString GameLauncher::flatpakError(const QString& appId, const QString& launcherName) const {
  return flatpakAppInstalled(appId)
             ? QString{}
             : QStringLiteral("The %1 Flatpak is not installed.").arg(launcherName);
}

void GameLauncher::setError(const QString& error) {
  if (m_lastError == error) {
    return;
  }
  m_lastError = error;
  emit lastErrorChanged();
}

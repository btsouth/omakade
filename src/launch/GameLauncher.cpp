#include "launch/GameLauncher.h"

#include "launch/SteamLauncher.h"
#include "sources/FlatpakInstall.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {
bool validLutrisId(const QString& id) {
  static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]*$"));
  return digits.match(id).hasMatch();
}

bool validPcsx2Id(const QString& id) {
  static const QRegularExpression serial(
      QStringLiteral("^[A-Za-z]{4}-[0-9A-Za-z]{5}$"));
  if (serial.match(id).hasMatch()) {
    return true;
  }
  static const QRegularExpression pathKey(QStringLiteral("^path:[/\\p{L}0-9 ._()&'\\[\\]-]{1,500}$"),
                                          QRegularExpression::UseUnicodePropertiesOption);
  return pathKey.match(id).hasMatch();
}

bool validRyujinxId(const QString& id) {
  if (id.startsWith(QStringLiteral("path:"))) {
    static const QRegularExpression pathKey(
        QStringLiteral("^path:[/\\p{L}0-9 ._()&'\\[\\]-]{1,500}$"),
        QRegularExpression::UseUnicodePropertiesOption);
    return pathKey.match(id).hasMatch();
  }
  static const QRegularExpression titleId(QStringLiteral("^[0-9A-Fa-f]{16}$"));
  return titleId.match(id).hasMatch();
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

LaunchCommand GameLauncher::pcsx2Command(const QString& id, bool isElf, bool flatpak) {
  if (!validPcsx2Id(id)) {
    return {};
  }
  // PCSX2 boots a disc image by passing its path directly; ELF entries need -elf <file>.
  if (!id.startsWith(QStringLiteral("path:"))) {
    return {};
  }
  const QString target = id.mid(5);
  QStringList arguments;
  if (isElf) {
    arguments << QStringLiteral("-elf") << target;
  } else {
    arguments << target;
  }
  if (flatpak) {
    QStringList flatpakArguments{QStringLiteral("run"), QStringLiteral("net.pcsx2.PCSX2"),
                                 QStringLiteral("--")};
    flatpakArguments = flatpakArguments + arguments;
    return LaunchCommand{QStringLiteral("flatpak"), flatpakArguments};
  }
  return LaunchCommand{QStringLiteral("pcsx2-qt"), arguments};
}

LaunchCommand GameLauncher::ryujinxCommand(const QString& id, const QString& nativeExecutable) {
  if (!validRyujinxId(id)) {
    return {};
  }
  const QString target = id.startsWith(QStringLiteral("path:")) ? id.mid(5) : id;
  if (!nativeExecutable.isEmpty()) {
    return LaunchCommand{nativeExecutable, {target}};
  }
  return LaunchCommand{QStringLiteral("flatpak"),
                       {QStringLiteral("run"), QStringLiteral("io.github.ryubing.Ryujinx"),
                        QStringLiteral("--"), target}};
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
  if (source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0) {
    return launchPcsx2(id, launchTarget == QStringLiteral("elf"), flatpak, false);
  }
  if (source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0) {
    return launchRyujinx(id, flatpak, false);
  }
  setError(QStringLiteral("%1 games cannot be launched yet.").arg(source));
  return false;
}

bool GameLauncher::manage(const QString& source, const QString& id, bool flatpak,
                          const QString& runner) {
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
  if (source.compare(QStringLiteral("PCSX2"), Qt::CaseInsensitive) == 0) {
    return launchPcsx2(id, false, flatpak, true);
  }
  if (source.compare(QStringLiteral("Ryujinx"), Qt::CaseInsensitive) == 0) {
    return launchRyujinx(id, flatpak, true);
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

bool GameLauncher::launchPcsx2(const QString& id, bool isElf, bool flatpak, bool manageOnly) {
  const QString executable = flatpak ? QStringLiteral("flatpak") : QStringLiteral("pcsx2-qt");
  if (QStandardPaths::findExecutable(executable).isEmpty()) {
    setError(flatpak ? QStringLiteral("Flatpak is not installed.")
                     : QStringLiteral("PCSX2 is not installed."));
    return false;
  }
  if (flatpak) {
    const QString error =
        flatpakError(QStringLiteral("net.pcsx2.PCSX2"), QStringLiteral("PCSX2"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                     {QStringLiteral("run"), QStringLiteral("net.pcsx2.PCSX2")}}
                     : LaunchCommand{QStringLiteral("pcsx2-qt"), {}})
          : pcsx2Command(id, isElf, flatpak);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid PCSX2 target."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("PCSX2 could not be started. Open PCSX2 and try again."));
    return false;
  }
  setError({});
  return true;
}

bool GameLauncher::launchRyujinx(const QString& id, bool flatpak, bool manageOnly) {
  QString nativeExecutable;
  if (!flatpak) {
    // Native installs ship the binary under different names depending on the package.
    for (const QString& candidate :
         {QStringLiteral("ryujinx-wrapper"), QStringLiteral("Ryujinx"),
          QStringLiteral("ryujinx")}) {
      if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
        nativeExecutable = candidate;
        break;
      }
    }
    if (nativeExecutable.isEmpty()) {
      setError(QStringLiteral("Ryujinx is not installed."));
      return false;
    }
  }
  if (flatpak) {
    const QString error =
        flatpakError(QStringLiteral("io.github.ryubing.Ryujinx"), QStringLiteral("Ryujinx"));
    if (!error.isEmpty()) {
      setError(error);
      return false;
    }
  }
  const LaunchCommand command =
      manageOnly
          ? (flatpak ? LaunchCommand{QStringLiteral("flatpak"),
                                     {QStringLiteral("run"),
                                      QStringLiteral("io.github.ryubing.Ryujinx")}}
                     : LaunchCommand{nativeExecutable, {}})
          : ryujinxCommand(id, nativeExecutable);
  if (!command.isValid()) {
    setError(QStringLiteral("This game has an invalid Ryujinx target."));
    return false;
  }
  if (!QProcess::startDetached(command.program, command.arguments)) {
    setError(QStringLiteral("Ryujinx could not be started. Open Ryujinx and try again."));
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

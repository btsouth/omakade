#include "launch/GameLauncher.h"
#include "library/ConsoleCatalog.h"
#include "library/ManualGameModel.h"
#include "saves/SaveBackups.h"
#include "sources/FlatpakInstall.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
const QStringList emulatorSources{"RetroArch", "PCSX2",   "Ryujinx", "Cemu",
                                  "Dolphin",   "shadPS4", "RomM"};
QString routeFor(const QVariantMap& game) {
  const auto source = game.value("source").toString(), system = game.value("system").toString();
  if (source != "RomM")
    return source;
  return system == "ps2"                             ? "PCSX2"
         : system == "switch"                        ? "Ryujinx"
         : system == "wiiu"                          ? "Cemu"
         : system == "ps4"                           ? "shadPS4"
         : (system == "wii" || system == "gamecube") ? "Dolphin"
                                                     : "RetroArch";
}
QString contentFile(const QString& path) {
  if (QFileInfo::exists(path))
    return path;
  for (int marker = path.indexOf('#'); marker > 0; marker = path.indexOf('#', marker + 1)) {
    const auto suffix = QFileInfo(path.left(marker)).suffix().toLower();
    if ((suffix == "zip" || suffix == "7z") && marker + 1 < path.size())
      return path.left(marker);
  }
  return path;
}
QString executable(const QStringList& names) {
  for (const auto& name : names)
    if (!QStandardPaths::findExecutable(name).isEmpty())
      return name;
  return {};
}
QString flatpakId(const QString& source) {
  if (source == "RetroArch")
    return "org.libretro.RetroArch";
  if (source == "PCSX2")
    return "net.pcsx2.PCSX2";
  if (source == "Cemu")
    return "info.cemu.Cemu";
  if (source == "Dolphin")
    return "org.DolphinEmu.dolphin-emu";
  if (source == "shadPS4")
    return "net.shadps4.shadPS4";
  if (source == "Ryujinx")
    return flatpakAppInstalled("io.github.ryubing.Ryujinx") ? "io.github.ryubing.Ryujinx"
                                                            : "org.ryujinx.Ryujinx";
  return {};
}
} // namespace
bool GameLauncher::contentAvailable(const QString& path, bool allowArchiveEntry) {
  if (QFileInfo::exists(path))
    return true;
  const auto file = contentFile(path);
  return allowArchiveEntry && file != path && QFileInfo(file).isFile() &&
         QFileInfo(file).isReadable();
}
GameLauncher::~GameLauncher() {
  if (!m_setupConnection.isEmpty()) {
    m_setupDatabase.close();
    m_setupDatabase = {};
    QSqlDatabase::removeDatabase(m_setupConnection);
  }
}
QString GameLauncher::setupKey(const QVariantMap& i) {
  return i.value("source").toString() + QChar::Null + i.value("runner").toString() + QChar::Null +
         i.value("appId").toString();
}
QString GameLauncher::storedSetupKey(const QVariantMap& installation) const {
  const auto exact = setupKey(installation);
  if (m_setups.contains(exact) || installation.value("source") == "RomM")
    return exact;
  const auto path = installation.value("installPath").toString();
  if (path.isEmpty())
    return exact;
  const auto scope = installation.value("source").toString() + QChar(0) +
                     installation.value("runner").toString() + QChar(0);
  QString found;
  for (auto it = m_setups.cbegin(); it != m_setups.cend(); ++it) {
    if (!it.key().startsWith(scope) || it.value().value("path").toString() != path)
      continue;
    if (!found.isEmpty())
      return exact;
    found = it.key();
  }
  return found.isEmpty() ? exact : found;
}
void GameLauncher::setSetupDatabase(const QString& path) {
  if (!m_setupConnection.isEmpty() || path.isEmpty())
    return;
  m_setupConnection = "launch-setup-" + QUuid::createUuid().toString();
  m_setupDatabase = QSqlDatabase::addDatabase("QSQLITE", m_setupConnection);
  m_setupDatabase.setDatabaseName(path);
  if (!m_setupDatabase.open()) {
    setError("Launch settings storage is unavailable.");
    return;
  }
  QSqlQuery q(m_setupDatabase);
  if (!q.exec("CREATE TABLE IF NOT EXISTS launch_setups (game_key TEXT PRIMARY KEY, mode TEXT NOT "
              "NULL, core TEXT NOT NULL, flatpak INTEGER NOT NULL, path TEXT NOT NULL)")) {
    setError("Could not initialize launch settings.");
    return;
  }
  if (!q.exec("SELECT game_key,mode,core,flatpak,path FROM launch_setups"))
    return;
  while (q.next())
    m_setups[q.value(0).toString()] = {{"mode", q.value(1)},
                                       {"core", q.value(2)},
                                       {"flatpak", q.value(3).toBool()},
                                       {"path", q.value(4)}};
  emit setupChanged();
}
QStringList GameLauncher::setupOptions(const QVariantMap& i) const {
  if (!emulatorSources.contains(i.value("source").toString()))
    return {};
  QStringList options{"Automatic", routeFor(i)};
  const auto* console = ConsoleCatalog::find(i.value("system").toString());
  if (console && !console->dedicatedSource) {
    if (!options.contains("RetroArch"))
      options.append("RetroArch");
    options.append(console->standaloneExecutables);
  }
  options.removeDuplicates();
  return options;
}
bool GameLauncher::saveSetup(const QVariantMap& i, const QString& mode, const QString& core,
                             bool flatpak, const QString& path) {
  if (i.value("appId").toString().isEmpty() || !setupOptions(i).contains(mode) ||
      (!core.isEmpty() && (!QFileInfo(core).isAbsolute() || !QFileInfo(core).isFile() ||
                           !core.endsWith("_libretro.so"))) ||
      (!path.isEmpty() &&
       (!QFileInfo(path).isAbsolute() || !QFileInfo(contentFile(path)).exists())) ||
      path.size() > 4096 || core.size() > 4096 || path.contains(QChar::Null) ||
      core.contains(QChar::Null) ||
      (flatpak &&
       !QStringList{"Automatic", "RetroArch", "PCSX2", "Ryujinx", "Cemu", "Dolphin", "shadPS4"}
            .contains(mode))) {
    setError("Choose a supported emulator and existing game/core paths.");
    return false;
  }
  if (!core.isEmpty() && mode != "Automatic" && mode != "RetroArch") {
    setError("A libretro core applies only to RetroArch.");
    return false;
  }
  const auto* console = ConsoleCatalog::find(i.value("system").toString());
  if (!path.isEmpty() && console && QFileInfo(path).isFile() &&
      !console->extensions.contains(QFileInfo(contentFile(path)).suffix(), Qt::CaseInsensitive) &&
      !path.contains('#')) {
    setError("Choose a file for this game's console.");
    return false;
  }
  const auto previousKey = storedSetupKey(i), storageKey = setupKey(i);
  if (!m_setupDatabase.isOpen() || !m_setupDatabase.transaction()) {
    setError("Could not save launch settings. Previous setup kept.");
    return false;
  }
  QSqlQuery q(m_setupDatabase);
  q.prepare("INSERT OR REPLACE INTO launch_setups VALUES(?,?,?,?,?)");
  q.addBindValue(storageKey);
  q.addBindValue(mode);
  q.addBindValue(core.isNull() ? QString("") : core);
  q.addBindValue(flatpak);
  q.addBindValue(path.isNull() ? QString("") : path);
  bool saved = q.exec();
  if (saved && previousKey != storageKey) {
    q.prepare("DELETE FROM launch_setups WHERE game_key=?");
    q.addBindValue(previousKey);
    saved = q.exec();
  }
  if (!saved || !m_setupDatabase.commit()) {
    m_setupDatabase.rollback();
    setError("Could not save launch settings. Previous setup kept.");
    return false;
  }
  m_setups.remove(previousKey);
  m_setups[storageKey] = {{"mode", mode}, {"core", core}, {"flatpak", flatpak}, {"path", path}};
  setError({});
  emit setupChanged();
  return true;
}
bool GameLauncher::resetSetup(const QVariantMap& i) {
  QSqlQuery q(m_setupDatabase);
  q.prepare("DELETE FROM launch_setups WHERE game_key=?");
  const auto storageKey = storedSetupKey(i);
  q.addBindValue(storageKey);
  if (!m_setupDatabase.isOpen() || !q.exec()) {
    setError("Could not reset launch settings.");
    return false;
  }
  m_setups.remove(storageKey);
  setError({});
  emit setupChanged();
  return true;
}
GameLauncher::EmulatorPlan GameLauncher::plannedEmulator(const QVariantMap& i) const {
  EmulatorPlan p;
  const auto setup = m_setups.value(storedSetupKey(i));
  const QString mode = setup.value("mode", "Automatic").toString();
  p.source = mode == "Automatic" ? routeFor(i) : mode;
  p.path = setup.value("path").toString();
  if (p.path.isEmpty())
    p.path = (QStringList{"Cemu", "Dolphin", "shadPS4"}.contains(routeFor(i)) &&
              !i.value("launchTarget").toString().isEmpty())
                 ? i.value("launchTarget").toString()
                 : i.value("installPath").toString();
  if (p.path.isEmpty())
    p.path = i.value("appId").toString().startsWith("path:") ? i.value("appId").toString().mid(5)
                                                             : i.value("launchTarget").toString();
  p.core = setup.value("core").toString();
  if (p.core.isEmpty() && routeFor(i) == "RetroArch")
    p.core = i.value("launchTarget").toString();
  p.flatpak =
      setup.contains("flatpak") ? setup.value("flatpak").toBool() : i.value("flatpak").toBool();
  if (!setupOptions(i).contains(mode)) {
    p.error = "The saved emulator is not supported for this console. Reset to Automatic.";
    return p;
  }
  if (!contentAvailable(p.path)) {
    p.error = "The installed files are missing. Choose the game's new location in Launch Setup.";
    return p;
  }
  if (i.value("source") == "RomM" && setup.value("path").toString().isEmpty()) {
    const auto root = QFileInfo(m_rommRoot).canonicalFilePath();
    const QFileInfo file(contentFile(p.path));
    if (root.isEmpty() || !file.isFile() || file.isSymLink() ||
        !file.canonicalFilePath().startsWith(root + '/')) {
      p.error = "The RomM mount or game is unavailable. Reconnect the mounted library or choose an "
                "explicit local file.";
      return p;
    }
  }
  const auto configuredRuntime = i.value("runner").toString();
  const QString fp =
      p.source == "Ryujinx" &&
              QStringList{"io.github.ryubing.Ryujinx", "org.ryujinx.Ryujinx"}.contains(
                  configuredRuntime)
          ? configuredRuntime
          : flatpakId(p.source);
  QString native;
  if (p.source == "PCSX2")
    native = executable({"pcsx2-qt"});
  else if (p.source == "Ryujinx")
    native = executable({"ryujinx-wrapper", "Ryujinx", "ryujinx"});
  else if (p.source == "shadPS4")
    native = executable({"shadps4", "shadPS4"});
  else if (p.source == "Dolphin")
    native = executable({"dolphin-emu", "dolphin-emu-nogui"});
  else if (p.source == "Cemu")
    native = executable({"cemu", "Cemu"});
  if (i.value("source") == "RomM" && setup.isEmpty() && p.source != "RetroArch" &&
      native.isEmpty() && flatpakAppInstalled(fp))
    p.flatpak = true;
  if (p.source == "RetroArch") {
    if (mode == "RetroArch") {
      GameLauncher resolver;
      resolver.setPreferStandaloneEmulators(false);
      p.command = resolver.plannedCartridgeCommand(p.path, p.core, p.flatpak,
                                                   i.value("system").toString(), &p.error);
      if (p.command.isValid() && p.command.program != "retroarch" &&
          p.command.program != "flatpak") {
        p.command = {};
        p.error = "The selected RetroArch runtime or compatible core is unavailable. Install it or "
                  "reset to Automatic.";
      }
    } else
      p.command = plannedCartridgeCommand(p.path, p.core, p.flatpak, i.value("system").toString(),
                                          &p.error);
    if (p.command.isValid()) {
      const int arg = p.command.arguments.indexOf("-L");
      if (arg >= 0 && arg + 1 < p.command.arguments.size())
        p.core = p.command.arguments[arg + 1];
      if (p.command.program != "retroarch" && p.command.program != "flatpak") {
        p.source = p.command.program;
        p.flatpak = false;
      }
    }
  } else if (p.source == "PCSX2")
    p.command =
        pcsx2Command("path:" + p.path, p.path.endsWith(".elf", Qt::CaseInsensitive), p.flatpak);
  else if (p.source == "Ryujinx")
    p.command = ryujinxCommand("path:" + p.path, p.flatpak ? QString{} : native,
                               p.flatpak ? fp : QString{});
  else if (p.source == "shadPS4")
    p.command = shadps4Command(p.path, p.flatpak ? QString{} : native, p.flatpak ? fp : QString{});
  else if (p.source == "Dolphin")
    p.command = dolphinCommand(p.path, native, p.flatpak);
  else if (p.source == "Cemu") {
    p.command = cemuCommand(p.path, p.flatpak);
    if (!p.flatpak && !native.isEmpty())
      p.command.program = native;
  } else
    p.command = resolvedCartridgeCommand(p.path, {}, false, true, p.source, {}, false);
  if (!p.error.isEmpty())
    return p;
  if (p.flatpak && !fp.isEmpty())
    p.error = flatpakError(fp, p.source);
  if (p.error.isEmpty() &&
      (!p.command.isValid() || QStandardPaths::findExecutable(p.command.program).isEmpty()))
    p.error = "The selected emulator is unavailable. Install it or choose another supported setup.";
  return p;
}
QVariantMap GameLauncher::inspect(const QVariantMap& i) const {
  const bool supported = emulatorSources.contains(i.value("source").toString());
  QVariantMap result = m_setups.value(storedSetupKey(i));
  result["supported"] = supported;
  result["options"] = setupOptions(i);
  result["overridden"] = m_setups.contains(storedSetupKey(i));
  if (!result.contains("mode"))
    result["mode"] = "Automatic";
  if (!supported) {
    result["available"] = !i.contains("installed") || i.value("installed").toBool();
    result["delegated"] = i.value("source") != "Manual";
    if (i.value("source") == "Manual") {
      QString program, directory, error;
      QStringList arguments;
      result["available"] = ManualGameModel::validateLaunch(i.value("launchTarget").toString(),
                                                            i.value("appId").toString(), &program,
                                                            &arguments, &directory, &error);
      result["program"] = program;
      result["error"] = error;
    }
    result["summary"] = i.value("source") == "Manual"
                            ? "Native entry; edit its executable in Edit Game."
                            : "Launch requests are handled by " + i.value("source").toString() +
                                  ". Process start is not guaranteed by a request.";
    return result;
  }
  const auto p = plannedEmulator(i);
  result["effectiveSource"] = p.source;
  result["gamePath"] = p.path;
  result["resolvedCore"] = p.core;
  result["resolvedFlatpak"] = p.flatpak;
  result["program"] = p.command.program;
  result["available"] = p.error.isEmpty();
  result["error"] = p.error;
  result["summary"] = p.source + (p.flatpak ? " · Flatpak" : " · Native");
  result["saveContext"] = QVariantMap{{"source", p.source},
                                      {"game", p.path},
                                      {"core", p.core},
                                      {"flatpak", p.flatpak},
                                      {"id", i.value("appId")},
                                      {"runner", i.value("runner")},
                                      {"target", p.source == "RetroArch" ? QString{} : p.path}};
  return result;
}
bool GameLauncher::launchPlannedEmulator(const QVariantMap& i) {
  const auto p = plannedEmulator(i);
  if (!p.error.isEmpty()) {
    setError(p.error);
    return false;
  }
  if (m_saveBackups &&
      !m_saveBackups->protectLaunch(p.source, p.path, p.core, p.flatpak,
                                    i.value("appId").toString(), i.value("runner").toString(),
                                    p.source == "RetroArch" ? QString{} : p.path)) {
    setError(m_saveBackups->message());
    return false;
  }
  if (!startTracked(p.command)) {
    setError("Could not start the selected emulator. Check its permissions and installation.");
    return false;
  }
  setError({});
  return true;
}
void GameLauncher::copyLaunchDetails(const QVariantMap& i) const {
  const auto plan = inspect(i);
  QString text = "Omakade launch\nSource: " + i.value("source").toString() +
                 "\nConsole: " + i.value("system").toString() +
                 "\nRoute: " + plan.value("summary").toString() +
                 "\nProgram: " + QFileInfo(plan.value("program").toString()).fileName() +
                 "\nGame: " + plan.value("gamePath").toString() +
                 "\nStatus: " + plan.value("error", "Ready").toString();
  text.replace(QDir::homePath(), "~");
  QGuiApplication::clipboard()->setText(text);
}

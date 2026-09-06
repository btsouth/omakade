#include "library/ConsoleCatalog.h"

#include <QHash>
#include <QRegularExpression>

namespace {
QString normalized(QString value) {
  static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
  value = value.trimmed().toCaseFolded();
  value.replace(QLatin1Char('_'), QLatin1Char(' '));
  value.remove(separators);
  return value;
}

const QVector<ConsoleDefinition>& definitions() {
  static const QVector<ConsoleDefinition> consoles = {
      {.id = QStringLiteral("snes"),
       .displayName = QStringLiteral("Super Nintendo"),
       .libretroPlaylist = QStringLiteral("Nintendo - Super Nintendo Entertainment System"),
       .aliases = {QStringLiteral("Nintendo - SNES"), QStringLiteral("SNES"),
                   QStringLiteral("Super Nintendo Entertainment System"),
                   QStringLiteral("Super Famicom")},
       .folderNames = {QStringLiteral("snes"), QStringLiteral("sfc"), QStringLiteral("sneshd")},
       .extensions = {QStringLiteral("sfc"), QStringLiteral("smc"), QStringLiteral("fig"),
                      QStringLiteral("swc")},
       .standaloneExecutables = {QStringLiteral("snes9x-gtk"), QStringLiteral("snes9x"),
                                 QStringLiteral("bsnes")},
       .retroArchCores = {QStringLiteral("snes9x_libretro"), QStringLiteral("bsnes_libretro")}},
      {.id = QStringLiteral("nes"),
       .displayName = QStringLiteral("Nintendo Entertainment System"),
       .libretroPlaylist = QStringLiteral("Nintendo - Nintendo Entertainment System"),
       .aliases = {QStringLiteral("Nintendo - NES"), QStringLiteral("NES"),
                   QStringLiteral("Famicom")},
       .folderNames = {QStringLiteral("nes"), QStringLiteral("fc")},
       .extensions = {QStringLiteral("nes"), QStringLiteral("unf"), QStringLiteral("unif"),
                      QStringLiteral("fds")},
       .standaloneExecutables = {QStringLiteral("nestopia"), QStringLiteral("fceux"),
                                 QStringLiteral("mednafen")},
       .retroArchCores = {QStringLiteral("nestopia_libretro"), QStringLiteral("fceumm_libretro")}},
      {.id = QStringLiteral("gb"),
       .displayName = QStringLiteral("Game Boy"),
       .libretroPlaylist = QStringLiteral("Nintendo - Game Boy"),
       .aliases = {QStringLiteral("Game Boy")},
       .folderNames = {QStringLiteral("gb")},
       .extensions = {QStringLiteral("gb")},
       .standaloneExecutables = {QStringLiteral("sameboy"), QStringLiteral("mgba")},
       .retroArchCores = {QStringLiteral("gambatte_libretro"), QStringLiteral("sameboy_libretro")}},
      {.id = QStringLiteral("gbc"),
       .displayName = QStringLiteral("Game Boy Color"),
       .libretroPlaylist = QStringLiteral("Nintendo - Game Boy Color"),
       .aliases = {QStringLiteral("Game Boy Color")},
       .folderNames = {QStringLiteral("gbc")},
       .extensions = {QStringLiteral("gbc")},
       .standaloneExecutables = {QStringLiteral("sameboy"), QStringLiteral("mgba")},
       .retroArchCores = {QStringLiteral("gambatte_libretro"), QStringLiteral("sameboy_libretro")}},
      {.id = QStringLiteral("gba"),
       .displayName = QStringLiteral("Game Boy Advance"),
       .libretroPlaylist = QStringLiteral("Nintendo - Game Boy Advance"),
       .aliases = {QStringLiteral("Game Boy Advance"), QStringLiteral("GBA")},
       .folderNames = {QStringLiteral("gba")},
       .extensions = {QStringLiteral("gba"), QStringLiteral("agb")},
       .standaloneExecutables = {QStringLiteral("mgba")},
       .retroArchCores = {QStringLiteral("mgba_libretro")}},
      {.id = QStringLiteral("n64"),
       .displayName = QStringLiteral("Nintendo 64"),
       .libretroPlaylist = QStringLiteral("Nintendo - Nintendo 64"),
       .aliases = {QStringLiteral("Nintendo 64"), QStringLiteral("N64")},
       .folderNames = {QStringLiteral("n64")},
       .extensions = {QStringLiteral("n64"), QStringLiteral("z64"), QStringLiteral("v64")},
       .standaloneExecutables = {QStringLiteral("mupen64plus")},
       .retroArchCores = {QStringLiteral("mupen64plus_next_libretro"),
                          QStringLiteral("parallel_n64_libretro")}},
      {.id = QStringLiteral("genesis"),
       .displayName = QStringLiteral("Sega Genesis"),
       .libretroPlaylist = QStringLiteral("Sega - Mega Drive - Genesis"),
       .aliases = {QStringLiteral("Sega Genesis"), QStringLiteral("Mega Drive"),
                   QStringLiteral("Genesis")},
       .folderNames = {QStringLiteral("genesis"), QStringLiteral("megadrive"), QStringLiteral("md")},
       .extensions = {QStringLiteral("md"), QStringLiteral("gen"), QStringLiteral("smd"),
                      QStringLiteral("bin")},
       .standaloneExecutables = {QStringLiteral("blastem"), QStringLiteral("gens")},
       .retroArchCores = {QStringLiteral("genesis_plus_gx_libretro"),
                          QStringLiteral("picodrive_libretro")}},
      {.id = QStringLiteral("psx"),
       .displayName = QStringLiteral("PlayStation"),
       .libretroPlaylist = QStringLiteral("Sony - PlayStation"),
       .aliases = {QStringLiteral("PlayStation"), QStringLiteral("PSX"), QStringLiteral("PS1")},
       .folderNames = {QStringLiteral("psx"), QStringLiteral("ps1")},
       .extensions = {QStringLiteral("cue"), QStringLiteral("chd"), QStringLiteral("pbp"),
                      QStringLiteral("iso")},
       .standaloneExecutables = {QStringLiteral("duckstation-qt"), QStringLiteral("duckstation")},
       .retroArchCores = {QStringLiteral("pcsx_rearmed_libretro"),
                          QStringLiteral("swanstation_libretro")}},
      {.id = QStringLiteral("dreamcast"),
       .displayName = QStringLiteral("Sega Dreamcast"),
       .libretroPlaylist = QStringLiteral("Sega - Dreamcast"),
       .aliases = {QStringLiteral("Dreamcast"), QStringLiteral("DC")},
       .folderNames = {QStringLiteral("dreamcast"), QStringLiteral("dc")},
       .extensions = {QStringLiteral("gdi"), QStringLiteral("cdi"), QStringLiteral("chd")},
       .standaloneExecutables = {QStringLiteral("flycast")},
       .retroArchCores = {QStringLiteral("flycast_libretro")}},
      {.id = QStringLiteral("gamecube"),
       .displayName = QStringLiteral("GameCube"),
       .libretroPlaylist = QStringLiteral("Nintendo - GameCube"),
       .aliases = {QStringLiteral("Nintendo GameCube"), QStringLiteral("GC"), QStringLiteral("NGC")},
       .folderNames = {QStringLiteral("gamecube"), QStringLiteral("gc"), QStringLiteral("ngc")},
       .extensions = {QStringLiteral("rvz"), QStringLiteral("wia"), QStringLiteral("iso"), QStringLiteral("gcm"),
                      QStringLiteral("gcz"), QStringLiteral("ciso")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
      {.id = QStringLiteral("wii"),
       .displayName = QStringLiteral("Wii"),
       .libretroPlaylist = QStringLiteral("Nintendo - Wii"),
       .aliases = {QStringLiteral("Nintendo Wii")},
       .folderNames = {QStringLiteral("wii")},
       .extensions = {QStringLiteral("rvz"), QStringLiteral("wia"), QStringLiteral("iso"), QStringLiteral("wbfs"),
                      QStringLiteral("ciso")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
      {.id = QStringLiteral("ps2"),
       .displayName = QStringLiteral("PlayStation 2"),
       .libretroPlaylist = QStringLiteral("Sony - PlayStation 2"),
       .aliases = {QStringLiteral("PlayStation 2"), QStringLiteral("PS2")},
       .folderNames = {QStringLiteral("ps2")},
       .extensions = {QStringLiteral("iso"), QStringLiteral("chd"), QStringLiteral("cso")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
      {.id = QStringLiteral("switch"),
       .displayName = QStringLiteral("Nintendo Switch"),
       .libretroPlaylist = QStringLiteral("Nintendo - Nintendo Switch"),
       .aliases = {QStringLiteral("Nintendo Switch"), QStringLiteral("Switch")},
       .folderNames = {QStringLiteral("switch")},
       .extensions = {QStringLiteral("nsp"), QStringLiteral("xci")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
      {.id = QStringLiteral("wiiu"),
       .displayName = QStringLiteral("Wii U"),
       .libretroPlaylist = QStringLiteral("Nintendo - Wii U"),
       .aliases = {QStringLiteral("Wii U"), QStringLiteral("WiiU")},
       .folderNames = {QStringLiteral("wiiu")},
       .extensions = {QStringLiteral("wud"), QStringLiteral("wux"), QStringLiteral("wua"),
                      QStringLiteral("rpx")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
      {.id = QStringLiteral("ps4"),
       .displayName = QStringLiteral("PlayStation 4"),
       .libretroPlaylist = QStringLiteral("Sony - PlayStation 4"),
       .aliases = {QStringLiteral("PlayStation 4"), QStringLiteral("PS4")},
       .folderNames = {QStringLiteral("ps4")},
       .extensions = {QStringLiteral("pkg")},
       .standaloneExecutables = {},
       .retroArchCores = {},
       .dedicatedSource = true},
  };
  return consoles;
}

// Every name a console answers to, normalized once. The catalog is consulted for
// each row while filtering and sorting, so lookups must not rescan the table.
const QHash<QString, int>& lookupTable() {
  static const QHash<QString, int> table = [] {
    QHash<QString, int> built;
    const QVector<ConsoleDefinition>& consoles = definitions();
    for (int index = 0; index < consoles.size(); ++index) {
      const ConsoleDefinition& console = consoles.at(index);
      QStringList names = {console.id, console.displayName, console.libretroPlaylist};
      names += console.aliases;
      names += console.folderNames;
      for (const QString& name : names) {
        const QString key = normalized(name);
        if (!key.isEmpty() && !built.contains(key)) {
          built.insert(key, index);
        }
      }
    }
    return built;
  }();
  return table;
}
} // namespace

const QVector<ConsoleDefinition>& ConsoleCatalog::all() { return definitions(); }

const ConsoleDefinition* ConsoleCatalog::find(const QString& raw) {
  if (raw.isEmpty()) {
    return nullptr;
  }
  const int index = lookupTable().value(normalized(raw), -1);
  return index < 0 ? nullptr : &definitions().at(index);
}

QString ConsoleCatalog::idFor(const QString& raw) {
  const ConsoleDefinition* console = find(raw);
  return console == nullptr ? raw.trimmed() : console->id;
}

QString ConsoleCatalog::displayNameFor(const QString& raw) {
  const ConsoleDefinition* console = find(raw);
  if (console != nullptr) {
    return console->displayName;
  }
  const QString trimmed = raw.trimmed();
  const qsizetype dash = trimmed.lastIndexOf(QStringLiteral(" - "));
  return dash >= 0 ? trimmed.mid(dash + 3) : trimmed;
}

QStringList ConsoleCatalog::defaultCardSystems() {
  QStringList systems;
  for (const ConsoleDefinition& console : definitions()) {
    systems.append(console.id);
  }
  return systems;
}

bool ConsoleCatalog::isDedicatedSource(const QString& raw) {
  const ConsoleDefinition* console = find(raw);
  return console != nullptr && console->dedicatedSource;
}

QString ConsoleCatalog::libretroPlaylistFor(const QString& raw) {
  const ConsoleDefinition* console = find(raw);
  return console == nullptr ? raw.trimmed() : console->libretroPlaylist;
}

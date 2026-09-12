#include "saves/SaveLayouts.h"
#include "artwork/ZArchiveReader.h"
#include "sources/dolphin/DolphinScanner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace {
QString text(const QString& path) {
  QFile f(path);
  if (f.size() > 4 * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
    return {};
  return QString::fromUtf8(f.readAll());
}
QString expand(QString path, const QString& home, const QString& base = {}) {
  if (path.startsWith("~/"))
    path.replace(0, 1, home);
  if (path.isEmpty())
    return {};
  if (!QFileInfo(path).isAbsolute())
    path = base + '/' + path;
  return QDir::cleanPath(path);
}
QMap<QString, QString> quoted(const QString& path, bool* okay) {
  const auto input = text(path);
  *okay = !input.isEmpty();
  QMap<QString, QString> out;
  const QRegularExpression re("^\\s*([A-Za-z0-9_]+)\\s*=\\s*\"([^\"]*)\"\\s*(?:#.*)?$");
  for (auto line : input.split('\n')) {
    line = line.trimmed();
    if (line.isEmpty() || (line.startsWith('#') && !line.startsWith("#include")))
      continue;
    const auto match = re.match(line);
    if (!match.hasMatch()) {
      *okay = false;
      return {};
    }
    out[match.captured(1)] = match.captured(2);
  }
  return out;
}
QString xml(const QString& file, const QString& name) {
  QXmlStreamReader x(text(file));
  while (!x.atEnd()) {
    x.readNext();
    if (x.isStartElement() && x.name() == name)
      return x.readElementText();
  }
  return {};
}
QString firstRoot(const QStringList& roots, const QString& marker) {
  for (const auto& r : roots)
    if (QFileInfo::exists(r + '/' + marker))
      return r;
  return roots.value(0);
}
} // namespace
SaveLayout resolveSaveLayout(const QJsonObject& c, const QString& home, const QString& raConfig) {
  SaveLayout l;
  const QString source = c["source"].toString(),
                game = (c["target"].toString().isEmpty() ? c["game"].toString()
                                                         : c["target"].toString()),
                id = c["id"].toString();
  const bool flatpak = c["flatpak"].toBool();
  QString cfg = home + "/.config", data = home + "/.local/share";
  if (home == QDir::homePath()) {
    cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  }
  const QString overridesFile = cfg + "/omakade/save-layouts.json";
  if (QFileInfo::exists(overridesFile)) {
    const auto doc = QJsonDocument::fromJson(text(overridesFile).toUtf8());
    if (!doc.isObject() || doc.object()["format"].toInt() != 1 ||
        !doc.object()["layouts"].isArray()) {
      l.error = "The local save-layout configuration is invalid.";
      return l;
    }
    for (const auto& value : doc.object()["layouts"].toArray()) {
      const auto rule = value.toObject();
      if (rule["source"].toString() != source || rule["game"].toString() != c["game"].toString() ||
          rule["flatpak"].toBool() != flatpak)
        continue;
      for (const auto& p : rule["files"].toArray())
        l.files << expand(p.toString(), home);
      for (const auto& p : rule["trees"].toArray())
        l.trees << expand(p.toString(), home);
      l.description = rule["description"].toString("Configured emulator saves");
      l.shared = rule["shared"].toBool();
      l.relativePattern = rule["relativePattern"].toString();
      if (!QRegularExpression(l.relativePattern).isValid()) {
        l.error = "The configured save-file filter is invalid.";
        return l;
      }
      l.files.removeDuplicates();
      l.trees.removeDuplicates();
      l.files.sort();
      l.trees.sort();
      if (!l.valid())
        l.error = "The configured save layout has no files or folders.";
      return l;
    }
  }
  const auto sandbox = [&](const QString& app) {
    if (flatpak) {
      cfg = home + "/.var/app/" + app + "/config";
      data = home + "/.var/app/" + app + "/data";
    }
  };
  l.description = source + " saves";
  if (source == "RetroArch") {
    sandbox("org.libretro.RetroArch");
    const QString core = QFileInfo(c["core"].toString()).completeBaseName();
    static const QMap<QString, QString> names = {{"snes9x_libretro", "Snes9x"},
                                                 {"bsnes_libretro", "bsnes"},
                                                 {"nestopia_libretro", "Nestopia"},
                                                 {"fceumm_libretro", "FCEUmm"},
                                                 {"gambatte_libretro", "Gambatte"},
                                                 {"sameboy_libretro", "SameBoy"},
                                                 {"mgba_libretro", "mGBA"},
                                                 {"mupen64plus_next_libretro", "Mupen64Plus-Next"},
                                                 {"parallel_n64_libretro", "ParaLLEl N64"},
                                                 {"genesis_plus_gx_libretro", "Genesis Plus GX"},
                                                 {"picodrive_libretro", "PicoDrive"},
                                                 {"pcsx_rearmed_libretro", "PCSX-ReARMed"},
                                                 {"swanstation_libretro", "SwanStation"},
                                                 {"flycast_libretro", "Flycast"}};
    const QString name = names.value(core);
    if (name.isEmpty()) {
      l.error = "This RetroArch core has no verified save layout.";
      return l;
    }
    bool okay;
    auto settings = quoted(flatpak ? cfg + "/retroarch/retroarch.cfg" : raConfig, &okay);
    if (!okay) {
      l.error = "RetroArch save configuration could not be read.";
      return l;
    }
    const QString content =
        game.contains('#') && !QFileInfo::exists(game) ? game.section('#', 0, 0) : game;
    const QString base =
        QFileInfo(content == game ? game : game.section('#', 1)).completeBaseName();
    const QString overrideSetting = settings.value("rgui_config_directory");
    const QString overrides = overrideSetting.isEmpty() || overrideSetting == "default"
                                  ? cfg + "/retroarch/config"
                                  : expand(overrideSetting, home);
    if (settings.value("auto_overrides_enable", "true") != "false") {
      for (const auto& n : QStringList{name, QFileInfo(content).dir().dirName(), base}) {
        const QString f = overrides + '/' + name + '/' + n + ".cfg";
        if (!QFileInfo::exists(f))
          continue;
        const auto values = quoted(f, &okay);
        if (!okay) {
          l.error = "A RetroArch save override could not be read.";
          return l;
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it)
          settings[it.key()] = it.value();
      }
    }
    // Resolve RetroArch's sentinel before expanding ordinary relative paths.
    // The standard Unix frontend uses its XDG config root, including in Flatpak.
    const QString saveSetting = settings.value("savefile_directory");
    QString folder =
        saveSetting == "default" ? cfg + "/retroarch/saves" : expand(saveSetting, home);
    if (settings.value("savefiles_in_content_dir", "false") == "true")
      folder = QFileInfo(content).absolutePath();
    else {
      if (folder.isEmpty())
        folder = QFileInfo(content).absolutePath();
    }
    if (settings.value("sort_savefiles_by_content_enable", "false") == "true")
      folder += '/' + QFileInfo(content).dir().dirName();
    if (settings.value("sort_savefiles_enable", "false") == "true")
      folder += '/' + name;
    l.files = {folder + '/' + base + ".srm", folder + '/' + base + ".rtc"};
    l.description = name + " in-game save and clock data";
    if (core == "nestopia_libretro")
      l.files << folder + '/' + base + ".sav" << folder + '/' + base + ".ups"
              << folder + '/' + base + ".ips";
    if (core == "genesis_plus_gx_libretro") {
      l.files << folder + '/' + base + ".brm";
      for (const auto& region : {"E", "U", "J"})
        l.files << folder + "/scd_" + region + ".brm";
      l.shared = true;
    }
    if (core == "pcsx_rearmed_libretro" || core == "swanstation_libretro") {
      // Optional shared second card and named cards are kept as a shared set.
      l.trees << folder;
      l.patterns = {"*.mcd"};
      l.shared = true;
    }
    if (core == "flycast_libretro") {
      const QString systemSetting = settings.value("system_directory");
      QString system = systemSetting == "default" ? QString{} : expand(systemSetting, home);
      if (system.isEmpty())
        system = QFileInfo(content).absolutePath();
      l.files.clear();
      // Include both per-content and shared VMUs, covering either core option without changing it.
      for (const auto& port : {"A1", "A2", "B1", "B2", "C1", "C2", "D1", "D2"}) {
        l.files << folder + '/' + base + '.' + port + ".bin"
                << system + "/dc/vmu_save_" + port + ".bin";
      }
      l.shared = true;
      l.description = "Dreamcast VMUs (including shared cards)";
    }
  } else if (source == "PCSX2") {
    sandbox("net.pcsx2.PCSX2");
    const QString root = firstRoot({cfg + "/PCSX2", data + "/PCSX2"}, "inis/PCSX2.ini");
    QSettings ini(root + "/inis/PCSX2.ini", QSettings::IniFormat);
    const QString cards =
        expand(ini.value("Folders/MemoryCards", "memcards").toString(), home, root);
    l.trees << cards;
    l.shared = true;
    l.description = "PS2 memory cards (all games on these cards)";
    // Include configured external cards, including per-game and multitap selections.
    QStringList settings{root + "/inis/PCSX2.ini"};
    const QDir perGame(root + "/gamesettings");
    for (const auto& f : perGame.entryList({"*.ini"}, QDir::Files))
      settings << perGame.filePath(f);
    for (const auto& file : settings) {
      QSettings s(file, QSettings::IniFormat);
      for (const auto& k : s.allKeys())
        if (k.startsWith("MemoryCards/") && k.endsWith("_Filename")) {
          const QString path = expand(s.value(k).toString(), home, cards);
          if (path.isEmpty() || path.startsWith(cards + '/'))
            continue;
          if (QFileInfo(path).isDir())
            l.trees << path;
          else
            l.files << path;
        }
    }
  } else if (source == "Dolphin") {
    sandbox("org.DolphinEmu.dolphin-emu");
    QSettings ini(cfg + "/dolphin-emu/Dolphin.ini", QSettings::IniFormat);
    const QString root = data + "/dolphin-emu";
    const auto header = DolphinScanner::readDiscHeader(game);
    const QString disc = header.valid() ? header.discId : id;
    if (!QRegularExpression("^[A-Za-z0-9]{6}$").match(disc).hasMatch()) {
      // Compressed formats without a readable disc ID still have known save banks.
      // Limit the NAND walk to game save data, never installed content or system settings.
      QString nand = expand(ini.value("General/NANDRootPath").toString(), home);
      if (nand.isEmpty())
        nand = root + "/Wii";
      l.trees = {root + "/GC", nand + "/title"};
      l.relativePattern =
          "^(?:.*\\.gci|MemoryCard[^/]*\\.raw|0001000[0145]/[0-9a-fA-F]{8}/data/.+)$";
      QStringList settings{cfg + "/dolphin-emu/Dolphin.ini"};
      const QDir gameSettings(root + "/GameSettings");
      for (const auto& f : gameSettings.entryList({"*.ini"}, QDir::Files))
        settings << gameSettings.filePath(f);
      for (const auto& file : settings) {
        QSettings values(file, QSettings::IniFormat);
        for (const auto& key : values.allKeys()) {
          const QString path = expand(values.value(key).toString(), home);
          if (path.isEmpty())
            continue;
          if (key.startsWith("Core/GCIFolder") && key.contains("Path"))
            l.trees << path;
          if (key == "Core/MemcardAPath" || key == "Core/MemcardBPath") {
            QString stem = path;
            stem.remove(QRegularExpression("\\.(USA|JAP|EUR)\\.raw$"));
            if (stem.endsWith(".raw"))
              stem.chop(4);
            for (const QString& region : QStringList{"USA", "JAP", "EUR"})
              l.files << stem + '.' + region + ".raw";
          }
        }
      }
      l.files.removeDuplicates();
      l.trees.removeDuplicates();
      l.files.sort();
      l.trees.sort();
      l.shared = true;
      l.description = "Dolphin save banks (all games in these cards and Wii save folders)";
      return l;
    }
    QMap<QString, QVariant> options;
    for (const auto& k : ini.allKeys())
      options[k] = ini.value(k);
    for (const auto& prefix : QStringList{disc.left(1), disc.left(3), disc}) {
      QSettings override(root + "/GameSettings/" + prefix + ".ini", QSettings::IniFormat);
      if (override.status() != QSettings::NoError) {
        l.error = "A Dolphin game override could not be read.";
        return l;
      }
      for (const auto& k : override.allKeys())
        options[k] = override.value(k);
    }
    if (header.platform == "Wii" || disc.startsWith('R') || disc.startsWith('S')) {
      QString nand = expand(options.value("General/NANDRootPath").toString(), home);
      if (nand.isEmpty())
        nand = root + "/Wii";
      const QString low = QString::fromLatin1(disc.left(4).toLatin1().toHex());
      l.trees << nand + "/title/00010000/" + low + "/data";
      l.description = "Wii in-game saves";
    } else {
      const QString region = disc[3] == 'J' ? "JAP" : (disc[3] == 'E' ? "USA" : "EUR");
      l.shared = true;
      l.description = "GameCube memory cards (all games on these cards)";
      for (const QString slot : {"A", "B"}) {
        QString folder = options.value("Core/GCIFolder" + slot + "Path").toString();
        if (folder.isEmpty())
          folder = root + "/GC/" + region + "/Card " + slot;
        else {
          folder = expand(folder, home);
          folder.remove(QRegularExpression("/(USA|JAP|EUR|JPN)$"));
          folder += '/' + (region == "JAP" ? "JPN" : region);
        }
        const QString direct = options.value("Core/GCIFolder" + slot + "PathOverride").toString();
        l.trees << (direct.isEmpty() ? folder : expand(direct, home));
        QString card = options.value("Core/Memcard" + slot + "Path").toString();
        if (card.isEmpty())
          card = root + "/GC/MemoryCard" + slot + '.' + region + ".raw";
        else {
          card = expand(card, home);
          card.remove(QRegularExpression("\\.(USA|JAP|EUR)\\.raw$"));
          if (card.endsWith(".raw"))
            card.chop(4);
          card += '.' + region + ".raw";
        }
        l.files << card;
        // Dolphin also uses size-specific names for smaller raw cards.
        for (const QString blocks : {"59", "123", "251", "507", "1019"}) {
          QString smaller = card;
          smaller.chop(4);
          l.files << smaller + '.' + blocks + ".raw";
        }
      }
    }
  } else if (source == "Cemu") {
    sandbox("info.cemu.Cemu");
    const QString root = firstRoot({cfg + "/Cemu", data + "/Cemu"}, "settings.xml");
    QString mlc = expand(xml(root + "/settings.xml", "mlc_path"), home, root);
    if (mlc.isEmpty())
      mlc = data + "/Cemu/mlc01";
    QString title = id.toLower();
    if (!QRegularExpression("^[0-9a-f]{16}$").match(title).hasMatch())
      title = xml(QFileInfo(game).absolutePath() + "/../meta/meta.xml", "title_id").toLower();
    if (!QRegularExpression("^[0-9a-f]{16}$").match(title).hasMatch() &&
        game.endsWith(".wua", Qt::CaseInsensitive)) {
      if (auto archive = ZArchiveReader::open(game))
        for (const auto& entry : archive->list({})) {
          const auto match = QRegularExpression("^(00050000[0-9a-fA-F]{8})_v[0-9]+$").match(entry);
          if (match.hasMatch() && archive->isDirectory(entry)) {
            title = match.captured(1).toLower();
            break;
          }
        }
    }
    if (!QRegularExpression("^[0-9a-f]{16}$").match(title).hasMatch()) {
      for (const QString& cacheRoot : QStringList{root, data + "/Cemu"}) {
        QXmlStreamReader cache(text(cacheRoot + "/title_list_cache.xml"));
        QString candidate;
        while (!cache.atEnd()) {
          cache.readNext();
          if (cache.isStartElement() && cache.name() == "title")
            candidate = cache.attributes().value("titleId").toString().toLower();
          else if (cache.isStartElement() && cache.name() == "path" &&
                   cache.readElementText() == game && candidate.startsWith("00050000"))
            title = candidate;
        }
      }
    }
    if (!QRegularExpression("^[0-9a-f]{16}$").match(title).hasMatch()) {
      l.error = "The Wii U title ID could not be read.";
      return l;
    }
    l.trees << mlc + "/usr/save/" + title.left(8) + '/' + title.mid(8);
    l.shared = true;
    l.description = "Wii U title saves (all profiles)";
  } else if (source == "shadPS4") {
    sandbox(c["runner"].toString().isEmpty() ? "net.shadps4.shadPS4" : c["runner"].toString());
    const QStringList roots{data + "/shadPS4", cfg + "/shadPS4", cfg + "/shadps4",
                            data + "/shadps4"};
    QString root = firstRoot(roots, "config.json");
    if (!QFileInfo::exists(root + "/config.json"))
      root = firstRoot(roots, "config.toml");
    QString serial = id.toUpper();
    if (!QRegularExpression("^CUSA[0-9]{5}$").match(serial).hasMatch())
      serial = QRegularExpression("CUSA[0-9]{5}").match(game).captured();
    if (serial.isEmpty()) {
      l.error = "The PS4 title ID could not be read.";
      return l;
    }
    const QString conf = text(root + "/config.toml");
    QString save = QRegularExpression("saveDataPath\\s*=\\s*\"([^\"]*)\"").match(conf).captured(1);
    if (!save.isEmpty())
      save = expand(save, home, root);
    QString homes = root + "/home";
    if (QFileInfo::exists(root + "/config.json")) {
      const auto config = QJsonDocument::fromJson(text(root + "/config.json").toUtf8());
      if (!config.isObject()) {
        l.error = "The shadPS4 configuration could not be read.";
        return l;
      }
      const QString configured =
          expand(config.object()["General"].toObject()["home_dir"].toString(), home, root);
      if (!configured.isEmpty())
        homes = configured;
    }
    l.shared = true;
    l.description = "PS4 title saves (all profiles)";
    for (const QString& user : QDir(homes).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
      l.trees << homes + '/' + user + "/savedata/" + serial;
    if (!save.isEmpty()) {
      for (const QString& user : QDir(save).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        l.trees << save + '/' + user + '/' + serial;
    }
    if (l.trees.isEmpty())
      l.trees << homes + "/1000/savedata/" + serial;
  } else if (source == "Ryujinx") {
    sandbox(c["runner"].toString().isEmpty() ? "io.github.ryubing.Ryujinx"
                                             : c["runner"].toString());
    const QString root = cfg + "/Ryujinx";
    // Ryujinx's save bank and its index form one unit. This is explicitly a shared backup.
    l.trees << root + "/bis/user/save" << root + "/bis/user/saveMeta"
            << root + "/bis/system/save/8000000000000000";
    l.shared = true;
    l.description = "Ryujinx save bank (all games and profiles)";
  } else {
    // Native cartridge emulators use their own save directories. Preserve entire card banks
    // where filenames are hashes or internal ROM names, never guess those names from the title.
    const QString base = QFileInfo(game).absolutePath() + '/' + QFileInfo(game).completeBaseName();
    if (source == "mgba" || source == "sameboy" || source == "bsnes") {
      l.files << base + ".sav" << base + ".srm" << base + ".rtc";
      if (source == "mgba") {
        QSettings s(cfg + "/mgba/config.ini", QSettings::IniFormat);
        const QString p =
            expand(s.value("ports.qt/savegamePath", s.value("savegamePath")).toString(), home);
        if (!p.isEmpty())
          l.files << p + '/' + QFileInfo(game).completeBaseName() + ".sav";
      }
    } else if (source == "snes9x" || source == "snes9x-gtk") {
      QString folder = QRegularExpression("(?:^|\\n)SRAMDirectory\\s*=\\s*([^\\n#]*)")
                           .match(text(cfg + "/snes9x/snes9x.conf"))
                           .captured(1)
                           .trimmed();
      folder = expand(folder, home);
      l.files << (folder.isEmpty() ? base : folder + '/' + QFileInfo(game).completeBaseName()) +
                     ".srm";
    } else if (source == "nestopia") {
      l.trees << data + "/nestopia/save" << cfg + "/nestopia/save";
      l.shared = true;
    } else if (source == "fceux") {
      QString root = home + "/.fceux";
      if (home == QDir::homePath()) {
        if (!qEnvironmentVariable("FCEUX_CONFIG_DIR").isEmpty())
          root = qEnvironmentVariable("FCEUX_CONFIG_DIR");
        else if (!qEnvironmentVariable("FCEUX_HOME").isEmpty())
          root = qEnvironmentVariable("FCEUX_HOME") + "/.fceux";
      }
      l.trees << QDir::cleanPath(root) + "/sav";
      l.shared = true;
    } else if (source == "mednafen") {
      const QString root =
          home == QDir::homePath() && !qEnvironmentVariable("MEDNAFEN_HOME").isEmpty()
              ? qEnvironmentVariable("MEDNAFEN_HOME")
              : home + "/.mednafen";
      l.trees << QDir::cleanPath(root) + "/sav";
      l.shared = true;
    } else if (source == "mupen64plus") {
      QSettings s(cfg + "/mupen64plus/mupen64plus.cfg", QSettings::IniFormat);
      QString p = expand(s.value("Core/SaveSRAMPath").toString(), home);
      l.trees << (p.isEmpty() ? data + "/mupen64plus/save" : p);
      l.patterns = {"*.eep", "*.mpk", "*.sra", "*.fla"};
      l.shared = true;
    } else if (source == "blastem") {
      QString settings = text(firstRoot({cfg + "/blastem", cfg}, "blastem.cfg") + "/blastem.cfg");
      QString path = QRegularExpression("(?:^|\\n)\\s*save_path\\s+([^\\n#]+)")
                         .match(settings)
                         .captured(1)
                         .trimmed();
      if (path.isEmpty())
        path = "$USERDATA/blastem/$ROMNAME";
      path.replace("$USERDATA", data)
          .replace("$HOME", home)
          .replace("$ROMDIR", QFileInfo(game).absolutePath())
          .replace("$ROMNAME", QFileInfo(game).completeBaseName());
      if (path.contains('$')) {
        l.error = "The BlastEm save template needs an explicit local save layout.";
        return l;
      }
      for (const QString name : {"save.sram", "save.eeprom", "save.nor", "save.hbpt"})
        l.files << QDir::cleanPath(path) + '/' + name;
    } else if (source == "gens") {
      l.trees << home + "/.gens";
      l.patterns = {"*.srm", "*.brm"};
      l.shared = true;
    } else if (source == "duckstation" || source == "duckstation-qt") {
      const QString root = data + "/duckstation";
      QSettings s(root + "/settings.ini", QSettings::IniFormat);
      const QString cards =
          expand(s.value("MemoryCards/Directory", "memcards").toString(), home, root);
      l.trees << cards;
      QStringList settings{root + "/settings.ini"};
      const QDir perGame(root + "/gamesettings");
      for (const auto& file : perGame.entryList({"*.ini"}, QDir::Files))
        settings << perGame.filePath(file);
      for (const auto& file : settings) {
        QSettings values(file, QSettings::IniFormat);
        for (const auto& key : values.allKeys())
          if (QRegularExpression("^MemoryCards/Card[0-9]+Path$").match(key).hasMatch()) {
            const QString path = expand(values.value(key).toString(), home, cards);
            if (!path.isEmpty() && !path.startsWith(cards + '/'))
              l.files << path;
          }
      }
      l.shared = true;
    } else if (source == "flycast") {
      QSettings settings(cfg + "/flycast/emu.cfg", QSettings::IniFormat);
      l.trees << data + "/flycast" << home + "/.reicast/data";
      for (const QString& key :
           QStringList{"config/Dreamcast.VMUPath", "config/Dreamcast.SavePath"}) {
        const QString path = expand(settings.value(key).toString(), home);
        if (!path.isEmpty())
          l.trees << path;
      }
      l.patterns = {"*vmu_save_*.bin"};
      l.shared = true;
    } else {
      l.error = "This emulator has no verified save layout.";
      return l;
    }
  }
  l.files.removeDuplicates();
  l.trees.removeDuplicates();
  l.files.sort();
  l.trees.sort();
  return l;
}

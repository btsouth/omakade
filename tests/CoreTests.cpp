#include <QNetworkReply>
#include <QBuffer>
#include "metadata/GameMetadata.h"
#include "achievements/AchievementModel.h"
#include "achievements/RetroAchievementsApi.h"
#include "achievements/RetroAchievementsHasher.h"
#include "achievements/RetroAchievementsService.h"

#include <zip.h>
#include "achievements/SteamAchievementApi.h"
#include "app/AppSettings.h"
#include "artwork/SwitchTitleReader.h"
#include "artwork/TgaImage.h"
#include "artwork/ZArchiveReader.h"
#include "app/SingleInstance.h"
#include "input/ControllerInput.h"
#include "input/ControllerFocusGuard.h"
#include "input/CouchCursorManager.h"
#include "launch/GameLauncher.h"
#include "launch/PlayRequest.h"
#include "launch/SteamLauncher.h"
#include "library/BattleNetGameModel.h"
#include "library/FaugusGameModel.h"
#include "library/GameRoles.h"
#include "library/HeroicGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LutrisGameModel.h"
#include "library/MockGameModel.h"
#include "library/Pcsx2GameModel.h"
#include "library/RyujinxGameModel.h"
#include "library/Shadps4GameModel.h"
#include "library/CemuGameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/SteamGameModel.h"
#include "library/SteamOwnedGamesApi.h"
#include "library/ConsoleCatalog.h"
#include "library/ConsolePortalModel.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameInsightsService.h"
#include "metadata/IgdbApi.h"
#include "sources/battlenet/BattleNetScanner.h"
#include "sources/faugus/FaugusScanner.h"
#include "sources/heroic/HeroicScanner.h"
#include "sources/pcsx2/Pcsx2Scanner.h"
#include "sources/ryujinx/RyujinxScanner.h"
#include "sources/shadps4/Shadps4Scanner.h"
#include "sources/cemu/CemuScanner.h"
#include "sources/dolphin/DolphinScanner.h"
#include "library/DolphinGameModel.h"
#include "sources/lutris/LutrisScanner.h"
#include "sources/retro/RomFolderScanner.h"
#include "sources/retroarch/RetroArchScanner.h"
#include "sources/steam/SteamScanner.h"
#include "sources/steam/ValveKeyValues.h"
#include "streaming/SunshineIntegration.h"
#include "theme/OmarchyTheme.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QRandomGenerator>
#include <QBuffer>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWindow>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>

namespace {
// A launcher-style source that, like Lutris or Heroic, has no Installed role at all.
class LauncherOnlyModel final : public QAbstractListModel {
public:
  explicit LauncherOnlyModel(QString title, QString coverPath = {}, QObject* parent = nullptr)
      : QAbstractListModel(parent), m_title(std::move(title)), m_coverPath(std::move(coverPath)) {}
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : 1;
  }
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid()) {
      return {};
    }
    switch (role) {
    case GameRoles::Title:
      return m_title;
    case GameRoles::Source:
      return QStringLiteral("Lutris");
    case GameRoles::AppId:
      return QStringLiteral("celeste");
    case GameRoles::Runner:
      return QString{};
    case GameRoles::CoverPath:
      return m_coverPath.isEmpty() ? QString{} : QUrl::fromLocalFile(m_coverPath).toString();
    case GameRoles::Hidden:
    case GameRoles::Favorite:
      return false;
    default:
      return {};
    }
  }
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{GameRoles::Title, "title"},
            {GameRoles::Source, "source"},
            {GameRoles::AppId, "appId"},
            {GameRoles::Runner, "runner"},
            {GameRoles::CoverPath, "coverPath"},
            {GameRoles::Hidden, "hidden"}};
  }

private:
  QString m_title;
  QString m_coverPath;
};

class DelayedSteamModel final : public QAbstractListModel {
public:
  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return !parent.isValid() && m_available ? 1 : 0;
  }
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!m_available || !index.isValid() || index.row() != 0) {
      return {};
    }
    switch (role) {
    case GameRoles::Title:
      return QStringLiteral("Portal 2");
    case GameRoles::Source:
      return QStringLiteral("Steam");
    case GameRoles::Runner:
      return QString{};
    case GameRoles::AppId:
      return QStringLiteral("620");
    case GameRoles::Installed:
      return true;
    default:
      return {};
    }
  }
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{GameRoles::Title, "title"},
            {GameRoles::Source, "source"},
            {GameRoles::Runner, "runner"},
            {GameRoles::AppId, "appId"},
            {GameRoles::Installed, "installed"}};
  }
  void publish() {
    beginInsertRows({}, 0, 0);
    m_available = true;
    endInsertRows();
  }

private:
  bool m_available = false;
};

void writeFile(const QString& path, const QByteArray& contents) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
  QCOMPARE(file.write(contents), contents.size());
}

QByteArray binaryCString(const QByteArray& value) {
  QByteArray out = value;
  out.append('\0');
  return out;
}

QByteArray binaryKey(char type, const QByteArray& key) {
  QByteArray out;
  out.append(type);
  out.append(binaryCString(key));
  return out;
}

QByteArray binaryStringField(const QByteArray& key, const QByteArray& value) {
  return binaryKey(0x01, key) + binaryCString(value);
}

QByteArray binaryIntField(const QByteArray& key, quint32 value) {
  QByteArray out = binaryKey(0x02, key);
  out.append(char(value & 0xff));
  out.append(char((value >> 8) & 0xff));
  out.append(char((value >> 16) & 0xff));
  out.append(char((value >> 24) & 0xff));
  return out;
}

QByteArray sampleShortcutsVdf(const QByteArray& iconPath) {
  QByteArray shortcut = binaryKey(0x00, "0");
  shortcut += binaryIntField("appid", 3236138358u);
  shortcut += binaryStringField("AppName", "Soulframe");
  shortcut += binaryStringField("Exe", "/games/Soulframe/Launcher.exe");
  shortcut += binaryStringField("StartDir", "/games/Soulframe");
  shortcut += binaryStringField("icon", iconPath);
  shortcut += binaryIntField("LastPlayTime", 1700000001);
  shortcut += binaryKey(0x00, "tags");
  shortcut += char(0x08);
  shortcut += char(0x08);

  QByteArray skipped = binaryKey(0x00, "1");
  skipped += binaryIntField("appid", 1);
  skipped += binaryStringField("AppName", "");
  skipped += char(0x08);

  QByteArray shortcuts = binaryKey(0x00, "shortcuts");
  shortcuts += shortcut;
  shortcuts += skipped;
  shortcuts += char(0x08);
  shortcuts += char(0x08);
  return shortcuts;
}

auto redirectCacheHome(const QString& path) {
  const bool wasSet = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
  const QByteArray previous = qgetenv("XDG_CACHE_HOME");
  qputenv("XDG_CACHE_HOME", path.toUtf8());
  return qScopeGuard([wasSet, previous] {
    if (wasSet) {
      qputenv("XDG_CACHE_HOME", previous);
    } else {
      qunsetenv("XDG_CACHE_HOME");
    }
  });
}

QByteArray sampleTheme(const QByteArray& accent = "#7aa2f7") {
  return "mode = \"dark\"\n"
         "accent = \"" +
         accent +
         "\"\n"
         "selection = \"#292e42\"\n"
         "muted = \"#414868\"\n"
         "background = \"#1a1b26\"\n"
         "dark_background = \"#13141c\"\n"
         "darker_background = \"#0e0e14\"\n"
         "lighter_background = \"#24283b\"\n"
         "foreground = \"#a9b1d6\"\n"
         "dark_foreground = \"#565f89\"\n"
         "light_foreground = \"#b4bee6\"\n"
         "bright_foreground = \"#c0caf5\"\n"
         "red = \"#f7768e\"\n"
         "yellow = \"#e0af68\"\n"
         "green = \"#9ece6a\"\n"
         "cyan = \"#449dab\"\n"
         "blue = \"#7aa2f7\"\n"
         "magenta = \"#ad8ee6\"\n";
}

QByteArray manifest(const QByteArray& appId, const QByteArray& name,
                    const QByteArray& installDirectory) {
  return "\"AppState\"\n{\n\"appid\" \"" + appId + "\"\n\"name\" \"" + name +
         "\"\n\"StateFlags\" \"4\"\n\"installdir\" \"" + installDirectory + "\"\n}\n";
}

void createSteamFixture(const QString& root, const QString& secondLibrary) {
  const QByteArray folders = QStringLiteral("\"libraryfolders\"\n{\n\"0\" { \"path\" \"%1\" }\n"
                                            "\"1\" { \"path\" \"%2\" }\n}\n")
                                 .arg(root, secondLibrary)
                                 .toUtf8();
  writeFile(root + QStringLiteral("/config/libraryfolders.vdf"), folders);
  writeFile(root + QStringLiteral("/steamapps/appmanifest_10.acf"),
            manifest("10", "Counter-Strike", "Counter-Strike"));
  writeFile(root + QStringLiteral("/steamapps/appmanifest_1070560.acf"),
            manifest("1070560", "Steam Linux Runtime 1.0 (scout)", "SteamLinuxRuntime"));
  writeFile(secondLibrary + QStringLiteral("/steamapps/appmanifest_20.acf"),
            manifest("20", "Team Fortress Classic", "Team Fortress Classic"));
  writeFile(secondLibrary + QStringLiteral("/steamapps/appmanifest_10.acf"),
            manifest("10", "Counter-Strike", "Counter-Strike"));
  writeFile(root + QStringLiteral("/appcache/librarycache/10/library_600x900.jpg"), "cover");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/header.jpg"), "landscape");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/library_hero.jpg"), "hero");
  writeFile(root + QStringLiteral("/appcache/librarycache/20/hash/library_capsule.jpg"),
            "portrait");
  writeFile(root + QStringLiteral("/userdata/42/config/grid/10p.png"), "custom cover");
  writeFile(
      root + QStringLiteral("/userdata/42/config/librarycache/10.json"),
      R"([["achievements",{"data":{"nAchieved":1,"nTotal":2,"vecHighlight":[{"strID":"WIN_ONE","strName":"First Win","strDescription":"Win once","strImage":"","bAchieved":true,"rtUnlocked":1700000000,"flAchieved":42.5}],"vecUnachieved":[{"strID":"WIN_TWO","strName":"Second Win","strDescription":"Win twice","strImage":"","bAchieved":false,"flAchieved":20.0}],"vecAchievedHidden":[]}}]])");
  writeFile(root + QStringLiteral("/userdata/42/config/librarycache/achievement_progress.json"),
            R"({"mapCache":[[10,{"unlocked":1,"total":2}]]})");
  writeFile(root + QStringLiteral("/userdata/42/config/localconfig.vdf"),
            "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { "
            "\"10\" { \"LastPlayed\" \"1700000000\" \"Playtime\" \"125\" } } } } } }\n");
}

void createLutrisFixture(const QString& dataRoot) {
  QDir().mkpath(dataRoot);
  const QString connection =
      QStringLiteral("lutris-fixture-%1").arg(QUuid::createUuid().toString());
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(dataRoot + QStringLiteral("/pga.db"));
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (id INTEGER PRIMARY KEY, slug TEXT, name TEXT, runner TEXT, directory "
        "TEXT, platform TEXT, year INTEGER, lastplayed INTEGER, playtime REAL, installed INTEGER, "
        "configpath TEXT)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(7, 'signal-hill', 'Signal Hill', 'wine', '/games/signal', "
        "'Windows', 2024, 1700000000, 2.5, 1, 'signal-hill')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(8, 'not-installed', 'Not Installed', 'linux', '/games/no', "
        "'Linux', 2020, 0, 0, 0, 'not-installed')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES(9, 'broken', 'Broken Install', 'wine', '/games/broken', "
        "'Windows', 2020, 0, 0, 1, '')")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  writeFile(dataRoot + QStringLiteral("/coverart/signal-hill.jpg"), "cover");
}

void createHeroicFixture(const QString& root) {
  writeFile(
      root + QStringLiteral("/legendaryConfig/legendary/installed.json"),
      R"({"EpicApp":{"app_name":"EpicApp","title":"Epic Voyage","install_path":"/games/epic","is_dlc":false},"EpicDlc":{"app_name":"EpicDlc","title":"Epic DLC","install_path":"/games/dlc","is_dlc":true}})");
  writeFile(
      root + QStringLiteral("/store_cache/legendary_library.json"),
      R"({"library":[{"app_name":"EpicApp","title":"Epic Voyage","art_square":"https://example.test/epic-cover.jpg","art_background":"https://example.test/epic-hero.jpg"}]})");
  writeFile(root + QStringLiteral("/icons/EpicApp.jpg"), "cover");

  writeFile(root + QStringLiteral("/gog_store/installed.json"),
            R"({"installed":[{"appName":"12345","install_path":")" +
                (root + QStringLiteral("/gog-game")).toUtf8() + R"(","is_dlc":false}]})");
  writeFile(root + QStringLiteral("/gog-game/goggame-12345.info"),
            R"({"name":"GOG Quest","playTasks":[{"isPrimary":true,"type":"FileTask","path":"start.sh","arguments":"--profile \"couch mode\"","workingDir":""}]})");
  writeFile(root + QStringLiteral("/gog-game/start.sh"), "#!/bin/sh\n");
  writeFile(root + QStringLiteral("/store_cache/gog_library.json"),
            R"({"games":[{"app_name":"12345","title":"GOG Quest","art_square":""}]})");

  writeFile(root + QStringLiteral("/nile_config/nile/installed.json"),
            R"([{"id":"amazon-game","version":"1","path":"/games/amazon"}])");
  writeFile(
      root + QStringLiteral("/nile_config/nile/library.json"),
      R"([{"product":{"id":"amazon-game","title":"Amazon Trail","productDetail":{"iconUrl":"","details":{"backgroundUrl1":""}}}}])");

  writeFile(
      root + QStringLiteral("/sideload_apps/library.json"),
      R"({"games":[{"runner":"sideload","app_name":"j661Z9rpxqYRZSp45Jh92i","title":"Scott Pilgrim EX","install":{"executable":"/home/user/Games/Scott/Game.exe","platform":"Windows","is_dlc":false},"folder_name":"/home/user/Games/Scott","art_cover":"https://cdn2.steamgriddb.com/grid/cover.png","is_installed":true,"art_square":"https://cdn2.steamgriddb.com/grid/square.png"},{"runner":"sideload","app_name":"removedApp","title":"Removed","install":{"executable":"","platform":"Windows","is_dlc":false},"folder_name":"","is_installed":false}]})");
  writeFile(root + QStringLiteral("/images-cache/") +
                QString::fromLatin1(
                    QCryptographicHash::hash("https://cdn2.steamgriddb.com/grid/square.png",
                                             QCryptographicHash::Sha256)
                        .toHex()),
            "sideload cover");
  writeFile(
      root + QStringLiteral("/store/timestamp.json"),
      R"({"EpicApp":{"firstPlayed":"2026-08-01T20:00:00.000Z","lastPlayed":"2026-08-30T21:15:00.000Z","totalPlayed":125}})");
}

void createFaugusFixture(const QString& root) {
  writeFile(
      root + QStringLiteral("/games.json"),
      R"([{"gameid":"signal-hill","title":"Signal Hill","path":"~/Games/Signal/signal.exe","runner":"GE-Proton","playtime":7200},{"gameid":"linux-tool","title":"Linux Tool","path":"/games/linux-tool","runner":"Linux","playtime":0},{"gameid":"pokémon-外伝","title":"Pokémon 外伝","path":"/games/pokemon"},{"gameid":"signal-hill","title":"Duplicate","path":"/games/duplicate"},{"gameid":"bad;id","title":"Unsafe","path":"/games/unsafe"},{"gameid":"missing-path","title":"Missing Path","path":""}])");
  writeFile(root + QStringLiteral("/covers/signal-hill.png"), "cover");
  writeFile(root + QStringLiteral("/banners/signal-hill.png"), "hero");
  writeFile(root + QStringLiteral("/icons/linux-tool.png"), "icon");
}

void createBattleNetFixture(const QString& prefix) {
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Battle.net/Battle.net.exe"),
            "mz");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/World of Warcraft/cover.png"),
            "cover");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Overwatch/library_hero.jpg"),
            "hero");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Custom/game.exe"), "exe");
  writeFile(prefix + QStringLiteral("/drive_c/Program Files (x86)/Custom/icon.ico"), "icon");
  writeFile(prefix + QStringLiteral("/drive_c/users/steamuser/AppData/Roaming/Battle.net/"
                                    "Battle.net.config"),
            R"({"Games":{"wow":{"LastPlayed":1700000000},"Pro":{"LastPlayed":1700001000}}})");
  writeFile(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"),
            BattleNetScanner::encodeProductDb(
                {{QStringLiteral("agent"), QStringLiteral("agent"),
                  QStringLiteral("C:\\ProgramData\\Battle.net\\Agent"), true, true},
                 {QStringLiteral("wow"), QStringLiteral("wow"),
                  QStringLiteral("C:\\Program Files (x86)\\World of Warcraft"), true, true},
                 {QStringLiteral("pro"), QStringLiteral("pro"),
                  QStringLiteral("C:\\Program Files (x86)\\Overwatch"), true, true},
                 {QStringLiteral("d3"), QStringLiteral("d3"),
                  QStringLiteral("C:\\Program Files (x86)\\Diablo III"), false, false},
                 {QStringLiteral("custom_mod"), QStringLiteral("custom_mod"),
                  QStringLiteral("C:\\Program Files (x86)\\Custom"), true, true}}));
}

void createRetroArchFixture(const QString& root) {
  const QString content = root + QStringLiteral("/roms/Sonic & Tails.bin");
  const QString unassigned = root + QStringLiteral("/roms/Unassigned.nes");
  writeFile(content, "rom");
  writeFile(unassigned, "rom");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n"
                           "thumbnails_directory = \"%1/thumbnails\"\n")
                .arg(root)
                .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Sega - Mega Drive.lpl"),
      QStringLiteral(
          R"({"version":"1.5","default_core_path":"%1/cores/genesis_plus_gx_libretro.so","default_core_name":"Genesis Plus GX","items":[{"path":"%2","label":"Sonic & Tails","core_path":"DETECT","core_name":"DETECT","crc32":"00000000|crc","db_name":"Sega - Mega Drive.lpl"},{"path":"%2","label":"Duplicate","core_path":"DETECT","core_name":"DETECT"}]})")
          .arg(root, content)
          .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Nintendo.lpl"),
      QStringLiteral(
          R"({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1","label":"Unassigned","core_path":"DETECT","core_name":"DETECT"}]})")
          .arg(unassigned)
          .toUtf8());
  writeFile(root + QStringLiteral("/thumbnails/Sega - Mega Drive/Named_Boxarts/Sonic _ Tails.png"),
            "cover");
  writeFile(root + QStringLiteral("/thumbnails/Sega - Mega Drive/Named_Snaps/Sonic _ Tails.png"),
            "hero");
  writeFile(
      root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic & Tails.lrtl"),
      R"({"version":"1.0","runtime":"12:34:56","last_played":"2026-08-30 19:45:10","play_count":"4","state_slot":"0"})");
  writeFile(root + QStringLiteral("/playlists/content_history.lpl"),
            R"({"version":"1.5","items":[{"path":"/ignored","label":"Ignored"}]})");
}

void createPcsx2Fixture(const QString& root, qint64 playedSeconds = 14) {
  const QString rom = root + QStringLiteral("/roms/Crash Twinsanity (USA).iso");
  writeFile(rom, "iso");
  writeFile(root + QStringLiteral("/inis/PCSX2.ini"), "[GameList]\n");
  const QByteArray pathBytes = rom.toUtf8();
  QByteArray cache;
  const auto appendU32 = [&cache](quint32 value) {
    for (int k = 0; k < 4; ++k) {
      cache.append(static_cast<char>((value >> (8 * k)) & 0xFF));
    }
  };
  const auto appendU64 = [&cache](quint64 value) {
    for (int k = 0; k < 8; ++k) {
      cache.append(static_cast<char>((value >> (8 * k)) & 0xFF));
    }
  };
  cache.append("GLCE");
  appendU32(34);  // current PCSX2 writes version 34
  appendU32(static_cast<quint32>(pathBytes.size()));
  cache.append(pathBytes);
  const QByteArray serialBytes = QByteArray("SLUS-20909");
  appendU32(static_cast<quint32>(serialBytes.size()));
  cache.append(serialBytes);
  const QByteArray titleBytes = QByteArray("Crash Twinsanity");
  appendU32(static_cast<quint32>(titleBytes.size()));
  cache.append(titleBytes);
  const QByteArray empty;
  appendU32(0);  // title_sort (empty)
  cache.append(empty);
  appendU32(0);  // title_en (empty)
  cache.append(empty);
  cache.append('\0');
  cache.append('\x06');
  appendU64(123456789ULL);
  appendU64(1700000000ULL);
  appendU32(0x1A2B3C4DU);
  cache.append('\0');
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), cache);

  const QString playedLine = QStringLiteral("%1 %2 %3\n")
                                 .arg(QStringLiteral("SLUS-20909"), -32)
                                 .arg(playedSeconds, 20)
                                 .arg(1700000500, 20);
  writeFile(root + QStringLiteral("/inis/playtime.dat"), playedLine.toUtf8());
}

void createRyujinxFixture(const QString& root, const QString& romDirectory) {
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1\"]}")
                .arg(romDirectory)
                .toUtf8());
  writeFile(romDirectory + QStringLiteral("/Zelda [0100abcd12345678][v0].nsp"), "rom");
  // Real layout: gui is a directory containing metadata.json.
  writeFile(root + QStringLiteral("/games/0100ABCD12345678/gui/metadata.json"),
            "{\"title\":\"Custom Title\",\"timespan_played\":\"01:00:00\","
            "\"last_played_utc\":\"2026-08-30T19:45:10Z\"}");
  writeFile(root + QStringLiteral("/games/0100ABCD12345678/covers/box.jpg"), "icon");
}

QByteArray paramSfo(const QString& title, const QString& titleId, const QString& category) {
  struct Entry {
    QByteArray key;
    QByteArray value;
  };
  const QList<Entry> entries = {
      {QByteArrayLiteral("CATEGORY"), category.toUtf8()},
      {QByteArrayLiteral("TITLE"), title.toUtf8()},
      {QByteArrayLiteral("TITLE_ID"), titleId.toUtf8()},
  };
  QByteArray keys;
  QByteArray values;
  QByteArray index;
  auto le16 = [](quint16 value) {
    QByteArray bytes(2, 0);
    bytes[0] = static_cast<char>(value & 0xff);
    bytes[1] = static_cast<char>((value >> 8) & 0xff);
    return bytes;
  };
  auto le32 = [](quint32 value) {
    QByteArray bytes(4, 0);
    for (int i = 0; i < 4; ++i) {
      bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
    return bytes;
  };
  for (const Entry& entry : entries) {
    const quint16 keyOffset = static_cast<quint16>(keys.size());
    keys += entry.key;
    keys += '\0';
    const QByteArray value = entry.value + '\0';
    const quint32 dataOffset = static_cast<quint32>(values.size());
    values += value;
    index += le16(keyOffset);
    index += le16(0x0204);
    index += le32(static_cast<quint32>(value.size()));
    index += le32(static_cast<quint32>(value.size()));
    index += le32(dataOffset);
  }
  const quint32 keyTable = 20 + static_cast<quint32>(index.size());
  const quint32 dataTable = keyTable + static_cast<quint32>(keys.size());
  QByteArray out(QByteArray("\0PSF", 4));
  out += le32(0x00000101);
  out += le32(keyTable);
  out += le32(dataTable);
  out += le32(static_cast<quint32>(entries.size()));
  out += index;
  out += keys;
  out += values;
  return out;
}

void createShadps4Fixture(const QString& root, const QString& gamesDirectory) {
  writeFile(root + QStringLiteral("/config.toml"),
            QStringLiteral("[GUI]\ninstallDirs = [\"%1\"]\n").arg(gamesDirectory).toUtf8());
  const QString game = gamesDirectory + QStringLiteral("/CUSA00001");
  writeFile(game + QStringLiteral("/eboot.bin"), "elf");
  writeFile(game + QStringLiteral("/sce_sys/param.sfo"),
            paramSfo(QStringLiteral("Bloodborne"), QStringLiteral("CUSA00001"),
                     QStringLiteral("gd")));
  writeFile(game + QStringLiteral("/sce_sys/icon0.png"), "icon");
  writeFile(game + QStringLiteral("/sce_sys/pic0.png"), "hero");
}

void createCemuFixture(const QString& root, const QString& gamesDirectory) {
  writeFile(root + QStringLiteral("/settings.xml"),
            QStringLiteral("<content><GamePaths><Entry>%1</Entry></GamePaths></content>")
                .arg(gamesDirectory)
                .toUtf8());
  const QString title = gamesDirectory + QStringLiteral("/Mario");
  writeFile(title + QStringLiteral("/code/app.xml"),
            "<title_id>0005000010101D00</title_id>");
  writeFile(title + QStringLiteral("/code/game.rpx"), "rpx");
  writeFile(title + QStringLiteral("/meta/meta.xml"),
            "<menu><title_id>0005000010101D00</title_id>"
            "<longname_en>Super Mario 3D World</longname_en></menu>");
  writeFile(title + QStringLiteral("/meta/iconTex.png"), "icon");
}

QByteArray pfs0WithTicket(const QByteArray& titleId) {
  const QByteArray name = titleId + QByteArrayLiteral("0000000000000012.tik");
  QByteArray strings = name;
  strings.append('\0');
  auto le32 = [](quint32 value) {
    QByteArray bytes(4, 0);
    for (int i = 0; i < 4; ++i) {
      bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
    return bytes;
  };
  auto le64 = [](quint64 value) {
    QByteArray bytes(8, 0);
    for (int i = 0; i < 8; ++i) {
      bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
    return bytes;
  };
  QByteArray out(QByteArrayLiteral("PFS0"));
  out += le32(1);
  out += le32(static_cast<quint32>(strings.size()));
  out += le32(0);
  out += le64(0);
  out += le64(1);
  out += le32(0);
  out += le32(0);
  out += strings;
  out += 'x';
  return out;
}

} // namespace

class CoreTests final : public QObject {
  Q_OBJECT

private slots:
  void mockLibraryIsDeterministic();
  void libraryFiltersByModeAndSearch();
  void changingSourceLeavesConsoleDrillIn();
  void themeLoadsSemanticColors();
  void themeFallsBackWithoutOmarchy();
  void themeReloadsWhenActiveFileChanges();
  void themeFollowsShellLauncherTransparency();
  void themeResolvesCompactTerminalPalette();
  void themeFallsBackAccentToTerminalBlue();
  void themeResolvesLegacySemanticNames();
  void themeFindsLegacyOmarchyLocation();
  void themeFollowsAtomicDirectoryReplacement();
  void themeUsesMachineLauncherOverride();
  void valveKeyValuesParsesNestedAndEscapedValues();
  void valveKeyValuesRejectsMalformedInput();
  void valveKeyValuesRejectsExcessiveNesting();
  void steamScannerImportsLibrariesAndCustomArtwork();
  void steamScannerImportsNonSteamShortcuts();
  void steamScannerRejectsLandscapeCoverFallbackAndImportsAchievements();
  void steamScannerSurvivesMissingLibrariesAndBrokenManifests();
  void steamModelPersistsFavoritesAndHiddenState();
  void steamModelSkipsUnchangedRescans();
  void steamModelMigratesVersionOneDatabase();
  void steamModelMigratesUnscopedOwnedGamesCache();
  void achievementModelLoadsLocalSteamCache();
  void steamAchievementApiParsesPlayerSchemaAndRarity();
  void steamAchievementApiClassifiesFailures();
  void steamOwnedGamesApiParsesLibraryAndPrivacy();
  void steamOwnedGamesRemainOptionalAndFilterByInstallation();
  void steamOwnedLibraryHandlesTwoThousandGames();
  void steamLauncherBuildsSafeUrls();
  void lutrisScannerImportsOnlyLaunchableGames();
  void lutrisModelIsRepeatableAndPreservesLocalState();
  void malformedLutrisDataDoesNotReplaceCachedGames();
  void unifiedLibraryFiltersSourcesAndRoutesFavorites();
  void unifiedLibraryCanDisableSourcesAtRuntime();
  void customCoverPersistsAndResets();
  void explicitLinksPersistAndPreserveInstallations();
  void launchActivityPersistsAndSortsExactly();
  void organizationPersistsAndFilters();
  void lutrisLauncherBuildsSafeCommands();
  void heroicScannerImportsEpicGogAndAmazon();
  void gogScannerImportsLooseInstallsAndConfinesLaunchTasks();
  void heroicModelIsRepeatableAndPreservesLocalState();
  void malformedHeroicDataDoesNotReplaceCachedGames();
  void heroicAndGogScanFailuresAreIsolated();
  void coldManagedGogFailureDoesNotImportDirectGames();
  void removingLastDirectGogGameClearsCache();
  void heroicLauncherBuildsSafeCommands();
  void gogLauncherBuildsSafeCommands();
  void faugusScannerImportsLaunchableGamesAndArtwork();
  void faugusModelIsRepeatableAndPreservesLocalState();
  void malformedFaugusDataDoesNotReplaceCachedGames();
  void faugusLauncherBuildsSafeCommands();
  void launcherRefreshesRunAsynchronously();
  void absentLaunchersPersistEmptySourcePaths();
  void retroArchScannerImportsPlaylistsArtworkAndRuntime();
  void retroArchScannerResolvesFlatpakPathsAndCoreNames();
  void retroArchModelIsRepeatableAndPreservesLocalState();
  void malformedRetroArchDataDoesNotReplaceCachedGames();
  void retroArchLauncherBuildsSafeCommands();
  void pcsx2ScannerImportsCacheGamesAndPlaytime();
  void pcsx2ModelIsRepeatableAndPreservesLocalState();
  void malformedPcsx2DataDoesNotReplaceCachedGames();
  void pcsx2UnifiedFilterShowsGames();
  void pcsx2LauncherBuildsSafeCommands();
  void ryujinxScannerImportsRomsMetadataAndPlaytime();
  void ryujinxScannerReadsNspTitleIdAndLocalCovers();
  void ryujinxScannerSkipsConfiguredAddOnsAndUpdates();
  void ryujinxModelIsRepeatableAndPreservesLocalState();
  void malformedRyujinxDataDoesNotReplaceCachedGames();
  void ryujinxLauncherBuildsSafeCommands();
  void shadps4ScannerImportsGamesAndArtwork();
  void shadps4ScannerSkipsPatches();
  void shadps4ScannerKeepsMergedPatchDumps();
  void shadps4ModelIsRepeatableAndPreservesLocalState();
  void malformedShadps4DataDoesNotReplaceCachedGames();
  void shadps4LauncherBuildsSafeCommands();
  void cemuScannerImportsTitlesAndPackages();
  void cemuScannerUsesTitleListCacheForPackages();
  void cemuScannerSkipsUpdatesAndDlc();
  void cemuModelIsRepeatableAndPreservesLocalState();
  void malformedCemuDataDoesNotReplaceCachedGames();
  void cemuLauncherBuildsSafeCommands();
  void consolePortalsGroupRetroArchRomsAndCanFlatten();
  void consolePortalsDoNotRebuildTheLibraryWhenCoversChange();
  void consolePortalsDoNotMergeDifferentFiles();
  void romFoldersMergeWithPlaylistsByCanonicalPath();
  void romFoldersKeepSeparateCopies();
  void cartridgeLaunchResolverPrefersPlaylistCoreThenStandalone();
  void libretroCoverUrlsAndCachePathsAreStable();
  void battleNetScannerImportsInstalledGamesAndArtwork();
  void battleNetScannerDiscoversKnownPrefixes();
  void battleNetScannerKeepsInstallsFromSeparatePrefixes();
  void battleNetModelIsRepeatableAndPreservesLocalState();
  void battleNetModelMigratesLegacyRowsSafely();
  void malformedBattleNetDataDoesNotReplaceCachedGames();
  void oversizedBattleNetDatabaseDoesNotReplaceCachedGames();
  void battleNetLauncherBuildsSafeCommands();
  void launcherReportsInvalidAndStaleTargets();
  void igdbApiBuildsSafeQueriesAndParsesInsights();
  void igdbInsightsLoadFromOfflineCache();
  void retroAchievementsHasherAppliesHeaderStripRules();
  void retroAchievementsHasherReadsZipArchivedRoms();
  void retroAchievementsApiBuildsUrlsAndParsesResponses();
  void retroArchModelReadsCachedRetroAchievementsSummary();
  void retroAchievementsServiceClearsCacheOnAccountSwitch();
  void retroAchievementsServiceBlocksAccountSwitchWhileBusy();
  void stressLibraryContainsOneThousandGames();
  void settingsPersistReducedMotionAndCacheLimit();
  void launchKeysRoundTripAndResolveInstallations();
  void singleInstanceForwardsPlayAndQuitCommands();
  void sunshineIntegrationWritesOnlyItsOwnEntries();
  void secondInstanceRequestsActivation();
  void couchCursorFollowsInputMode();
  void virtualControllerConnectsAndMapsPrimaryButton();
  void thousandGameSearchStaysResponsive();
  void openingAConsoleDoesNotHangTheLibrary();
  void consoleFilterKeepsOtherSourcesOut();
  void dolphinScannerReadsDiscHeadersAndLaunches();
  void dolphinModelIsRepeatableAndPreservesLocalState();
  void dreamcastFoldersBecomeAPortal();
  void consoleLayoutsPinAndExpand();
  void metadataMatchingKeepsPlatformsAndEditions();
  void coverSizesPersistIndependently();
  void controllerNavigationFollowsWindowFocus();
  void metadataPersistsRatingsAndPreservesCustomArt();
  void portraitBatchContinuesAndKeepsRatingTimestamp();
  void portraitSelectionCompletesOnlyAfterSuccessfulSave();
  void probeEmbeddedArtwork();
  void switchTitleReaderReadsSyntheticDump();
  void zarchiveReaderAndTgaDecodeSyntheticArchive();
};

void CoreTests::mockLibraryIsDeterministic() {
  MockGameModel games;

  QCOMPARE(games.rowCount(), 100);
  QCOMPARE(games.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Aster Vale"));
  QCOMPARE(games.get(99).value(QStringLiteral("title")).toString(), QStringLiteral("Wild Orbit 4"));
  QVERIFY(games.get(0).value(QStringLiteral("favorite")).toBool());
}

void CoreTests::libraryFiltersByModeAndSearch() {
  MockGameModel games;
  LibraryFilterModel library;
  library.setSourceModel(&games);

  QCOMPARE(library.rowCount(), 100);

  library.setMode(LibraryFilterModel::Mode::Favorites);
  QCOMPARE(library.rowCount(), 13);

  library.setMode(LibraryFilterModel::Mode::Recent);
  QCOMPARE(library.rowCount(), 13);

  library.setMode(LibraryFilterModel::Mode::All);
  library.setSearchText(QStringLiteral("Aster Vale"));
  QCOMPARE(library.rowCount(), 4);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Aster Vale"));
}

void CoreTests::changingSourceLeavesConsoleDrillIn() {
  MockGameModel games;
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  library.setConsoleFilter(QStringLiteral("snes"));
  QCOMPARE(library.consoleFilter(), QStringLiteral("snes"));
  QCOMPARE(library.consoleTitle(), QStringLiteral("Super Nintendo"));
  library.setSourceFilter(QStringLiteral("Steam"));
  QVERIFY(library.consoleFilter().isEmpty());
  QVERIFY(library.consoleTitle().isEmpty());
}

void CoreTests::themeLoadsSemanticColors() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString current = stateHome + QStringLiteral("/omarchy/current");
  writeFile(current + QStringLiteral("/theme/colors.toml"), sampleTheme());
  writeFile(current + QStringLiteral("/theme.name"), "tokyo-night\n");

  OmarchyTheme theme(stateHome, configHome);

  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Tokyo Night"));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#7aa2f7")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#0e0e14")));
  QVERIFY(theme.mutedText().isValid());
}

void CoreTests::themeFallsBackWithoutOmarchy() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  OmarchyTheme theme(directory.path() + QStringLiteral("/state"),
                     directory.path() + QStringLiteral("/config"));

  QVERIFY(!theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Omakade Dark"));
  QVERIFY(theme.background().isValid());
  QVERIFY(theme.foreground().isValid());
}

void CoreTests::themeReloadsWhenActiveFileChanges() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString colors = stateHome + QStringLiteral("/omarchy/current/theme/colors.toml");
  writeFile(colors, sampleTheme());

  OmarchyTheme theme(stateHome, configHome);
  QSignalSpy changes(&theme, &OmarchyTheme::themeChanged);
  writeFile(colors, sampleTheme("#ff0000"));

  QTRY_COMPARE_WITH_TIMEOUT(theme.accent(), QColor(QStringLiteral("#ff0000")), 1500);
  QVERIFY(!changes.isEmpty());
}

void CoreTests::themeFollowsShellLauncherTransparency() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString themeRoot = stateHome + QStringLiteral("/omarchy/current/theme");
  writeFile(themeRoot + QStringLiteral("/colors.toml"), sampleTheme());
  const QString shell = themeRoot + QStringLiteral("/shell.toml");
  writeFile(shell, "[bar]\nbackground-alpha = 1.0\n[launcher]\nbackground-alpha = 0.63\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.surfaceAlpha(), 0.63);
  writeFile(shell, "[launcher]\nbackground-alpha = 0.91\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.91, 1500);
  writeFile(shell, "[launcher]\nbackground-alpha = 1.0\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 1.0, 1500);
}

void CoreTests::themeResolvesCompactTerminalPalette() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "accent = \"#589df6\"\nselection = \"#b5d5ff\"\n"
            "background = \"#1d2837\"\nforeground = \"#ffffff\"\n"
            "color0 = \"#000000\"\ncolor1 = \"#f9555f\"\n"
            "color2 = \"#21b089\"\ncolor3 = \"#fef02a\"\n"
            "color4 = \"#589df6\"\ncolor5 = \"#944d95\"\n"
            "color6 = \"#1f9ee7\"\ncolor7 = \"#bbbbbb\"\n"
            "color8 = \"#555555\"\ncolor9 = \"#fa8c8f\"\n"
            "color10 = \"#35bb9a\"\ncolor11 = \"#ffff55\"\n"
            "color12 = \"#589df6\"\ncolor13 = \"#e75699\"\n"
            "color14 = \"#3979bc\"\ncolor15 = \"#ffffff\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.mode(), QStringLiteral("dark"));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#589df6")));
  QCOMPARE(theme.selection(), QColor(QStringLiteral("#b5d5ff")));
  QCOMPARE(theme.background(), QColor(QStringLiteral("#1d2837")));
  QCOMPARE(theme.darkBackground(), QColor(QStringLiteral("#161e29")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#0f141c")));
  QCOMPARE(theme.lighterBackground(), QColor(QStringLiteral("#1d2837")));
  QCOMPARE(theme.darkForeground(), QColor(QStringLiteral("#555555")));
  QCOMPARE(theme.lightForeground(), QColor(QStringLiteral("#ffffff")));
  QCOMPARE(theme.brightForeground(), QColor(QStringLiteral("#ffffff")));
  QCOMPARE(theme.muted(), QColor(QStringLiteral("#555555")));
  QCOMPARE(theme.red(), QColor(QStringLiteral("#f9555f")));
  QCOMPARE(theme.green(), QColor(QStringLiteral("#21b089")));
  QCOMPARE(theme.yellow(), QColor(QStringLiteral("#fef02a")));
  QCOMPARE(theme.blue(), QColor(QStringLiteral("#589df6")));
}

void CoreTests::themeFallsBackAccentToTerminalBlue() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "background = \"#16181d\"\nforeground = \"#c5c5d2\"\ncolor4 = \"#6c9ef8\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#6c9ef8")));
}

void CoreTests::themeResolvesLegacySemanticNames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  writeFile(stateHome + QStringLiteral("/omarchy/current/theme/colors.toml"),
            "mode = \"dark\"\naccent = \"#fcef0c\"\n"
            "bg = \"#1b1d1e\"\ndark_bg = \"#353738\"\n"
            "darker_bg = \"#1a1b1c\"\nlighter_bg = \"#1b1d1e\"\n"
            "fg = \"#a7a8a3\"\ndark_fg = \"#8a8c89\"\n"
            "light_fg = \"#c5c5be\"\nbright_fg = \"#dadad5\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.background(), QColor(QStringLiteral("#1b1d1e")));
  QCOMPARE(theme.darkBackground(), QColor(QStringLiteral("#353738")));
  QCOMPARE(theme.darkerBackground(), QColor(QStringLiteral("#1a1b1c")));
  QCOMPARE(theme.lighterBackground(), QColor(QStringLiteral("#1b1d1e")));
  QCOMPARE(theme.foreground(), QColor(QStringLiteral("#a7a8a3")));
  QCOMPARE(theme.darkForeground(), QColor(QStringLiteral("#8a8c89")));
  QCOMPARE(theme.lightForeground(), QColor(QStringLiteral("#c5c5be")));
  QCOMPARE(theme.brightForeground(), QColor(QStringLiteral("#dadad5")));
}

void CoreTests::themeFindsLegacyOmarchyLocation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString current = configHome + QStringLiteral("/omarchy/current");
  writeFile(current + QStringLiteral("/theme/colors.toml"),
            "background = \"#202040\"\nforeground = \"#eeeeff\"\naccent = \"#6060ff\"\n");
  writeFile(current + QStringLiteral("/theme.name"), "legacy-blue\n");

  OmarchyTheme theme(stateHome, configHome);
  QVERIFY(theme.omarchyAvailable());
  QCOMPARE(theme.themeName(), QStringLiteral("Legacy Blue"));
  QCOMPARE(theme.background(), QColor(QStringLiteral("#202040")));
  QCOMPARE(theme.accent(), QColor(QStringLiteral("#6060ff")));
}

void CoreTests::themeFollowsAtomicDirectoryReplacement() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString root = stateHome + QStringLiteral("/omarchy/current");
  writeFile(root + QStringLiteral("/theme/colors.toml"),
            "background = \"#101020\"\nforeground = \"#eeeeff\"\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.background(), QColor(QStringLiteral("#101020")));

  writeFile(root + QStringLiteral("/next-theme/colors.toml"),
            "background = \"#302010\"\nforeground = \"#fff0ee\"\n");
  QVERIFY(QDir(root + QStringLiteral("/theme")).removeRecursively());
  QVERIFY(QDir(root).rename(QStringLiteral("next-theme"), QStringLiteral("theme")));
  QTRY_COMPARE_WITH_TIMEOUT(theme.background(), QColor(QStringLiteral("#302010")), 1500);
}

void CoreTests::themeUsesMachineLauncherOverride() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString stateHome = directory.path() + QStringLiteral("/state");
  const QString configHome = directory.path() + QStringLiteral("/config");
  const QString themeRoot = stateHome + QStringLiteral("/omarchy/current/theme");
  writeFile(themeRoot + QStringLiteral("/colors.toml"), sampleTheme());
  writeFile(themeRoot + QStringLiteral("/shell.toml"), "[launcher]\nbackground-alpha = 0.73\n");
  const QString userShell = configHome + QStringLiteral("/omarchy/shell.toml");
  writeFile(userShell, "[launcher]\nbackground-alpha = 0.91\n");

  OmarchyTheme theme(stateHome, configHome);
  QCOMPARE(theme.surfaceAlpha(), 0.91);

  writeFile(userShell, "[launcher]\nbackground-alpha = 0.64\n");
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.64, 1500);

  QVERIFY(QFile::remove(userShell));
  QTRY_COMPARE_WITH_TIMEOUT(theme.surfaceAlpha(), 0.73, 1500);
}

void CoreTests::valveKeyValuesParsesNestedAndEscapedValues() {
  ValveKeyValues values;
  QString error;
  QVERIFY(ValveKeyValuesParser::parse(
      "// comment\n\"Root\" { \"name\" \"A \\\"quoted\\\" game\" \"path\" "
      "\"/games/library\" \"label\" \"\" }",
      &values, &error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const ValveKeyValues* root = values.object(QStringLiteral("root"));
  QVERIFY(root != nullptr);
  QCOMPARE(root->value(QStringLiteral("NAME")), QStringLiteral("A \"quoted\" game"));
  QCOMPARE(root->value(QStringLiteral("path")), QStringLiteral("/games/library"));
  QCOMPARE(root->value(QStringLiteral("label")), QString());
}

void CoreTests::valveKeyValuesRejectsMalformedInput() {
  ValveKeyValues values;
  QString error;
  QVERIFY(!ValveKeyValuesParser::parse("\"Root\" { \"name\" \"unfinished\"", &values, &error));
  QVERIFY(!error.isEmpty());

  const QByteArray trailing = sampleShortcutsVdf("/icon.png") + QByteArray(1, 0x01);
  QVERIFY(!ValveKeyValuesParser::parseBinary(trailing, &values, &error));
  QCOMPARE(error, QStringLiteral("Unexpected trailing data"));
}

void CoreTests::valveKeyValuesRejectsExcessiveNesting() {
  QByteArray input;
  for (int depth = 0; depth < 129; ++depth) {
    input.append("\"key\" {");
  }
  input.append("\"value\" \"leaf\"");
  for (int depth = 0; depth < 129; ++depth) {
    input.append('}');
  }
  ValveKeyValues values;
  QString error;
  QVERIFY(!ValveKeyValuesParser::parse(input, &values, &error));
  QCOMPARE(error, QStringLiteral("Object nesting is too deep"));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QFile oversized(directory.path() + QStringLiteral("/oversized.vdf"));
  QVERIFY(oversized.open(QIODevice::WriteOnly));
  QVERIFY(oversized.resize(64LL * 1024 * 1024 + 1));
  oversized.close();
  QVERIFY(!ValveKeyValuesParser::parseFile(oversized.fileName(), &values, &error));
  QCOMPARE(error, QStringLiteral("File is too large"));
}

void CoreTests::steamScannerImportsLibrariesAndCustomArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.size(), 2);
  QCOMPARE(result.games.at(0).appId, QStringLiteral("10"));
  QCOMPARE(result.games.at(0).playtimeMinutes, 125);
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("10p.png")));
  QCOMPARE(result.games.at(1).appId, QStringLiteral("20"));
  QVERIFY(result.warnings.isEmpty());
}

void CoreTests::steamScannerImportsNonSteamShortcuts() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const QString icon = directory.path() + QStringLiteral("/soulframe-icon.png");
  writeFile(icon, "icon");
  writeFile(root + QStringLiteral("/userdata/42/config/shortcuts.vdf"),
            sampleShortcutsVdf(icon.toUtf8()));
  writeFile(root + QStringLiteral("/userdata/42/config/grid/3236138358p.png"), "shortcut cover");
  writeFile(root + QStringLiteral("/userdata/42/config/localconfig.vdf"),
            "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { "
            "\"10\" { \"LastPlayed\" \"1700000000\" \"Playtime\" \"125\" } "
            "\"3236138358\" { \"LastPlayed\" \"1690000000\" \"Playtime\" \"954\" } } } } } }\n");

  ValveKeyValues parsed;
  QVERIFY(ValveKeyValuesParser::parseBinaryFile(
      root + QStringLiteral("/userdata/42/config/shortcuts.vdf"), &parsed));
  QCOMPARE(parsed.object(QStringLiteral("shortcuts"))->object(QStringLiteral("0"))
               ->value(QStringLiteral("AppName")),
           QStringLiteral("Soulframe"));

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.size(), 3);
  const auto shortcut = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.appId == QStringLiteral("3236138358");
  });
  QVERIFY(shortcut != result.games.cend());
  QCOMPARE(shortcut->title, QStringLiteral("Soulframe"));
  QCOMPARE(shortcut->installDirectory, QStringLiteral("/games/Soulframe"));
  QCOMPARE(shortcut->playtimeMinutes, 954);
  QCOMPARE(shortcut->lastPlayed, 1700000001);
  QVERIFY(shortcut->coverPath.endsWith(QStringLiteral("3236138358p.png")));
  QVERIFY(result.warnings.isEmpty());
  QVERIFY(result.unreadableManifests.isEmpty());

  writeFile(root + QStringLiteral("/userdata/42/config/shortcuts.vdf"), "not a binary vdf");
  const SteamScanResult unreadable = SteamScanner::scan({root});
  QCOMPARE(unreadable.games.size(), 2);
  QCOMPARE(unreadable.unreadableManifests.size(), 1);
  QVERIFY(unreadable.unreadableManifests.constFirst().endsWith(QStringLiteral("shortcuts.vdf")));
  QVERIFY(unreadable.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("shortcuts.vdf")));
}

void CoreTests::steamScannerRejectsLandscapeCoverFallbackAndImportsAchievements() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  createSteamFixture(root, second);

  const SteamScanResult result = SteamScanner::scan({root});
  QCOMPARE(result.games.at(0).achievementsUnlocked, 1);
  QCOMPARE(result.games.at(0).achievementsTotal, 2);
  QCOMPARE(result.games.at(0).achievements.size(), 2);
  QCOMPARE(result.games.at(0).achievements.at(0).title, QStringLiteral("First Win"));
  QVERIFY(result.games.at(1).coverPath.endsWith(QStringLiteral("library_capsule.jpg")));
  QVERIFY(result.games.at(1).heroPath.endsWith(QStringLiteral("library_hero.jpg")));
}

void CoreTests::steamScannerSurvivesMissingLibrariesAndBrokenManifests() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Second Library");
  const QString missing = directory.path() + QStringLiteral("/Unmounted Drive/SteamLibrary");
  createSteamFixture(root, second);
  writeFile(root + QStringLiteral("/config/libraryfolders.vdf"),
            QStringLiteral("\"libraryfolders\"\n{\n\"0\" { \"path\" \"%1\" }\n\"1\" { \"path\" "
                           "\"%2\" }\n\"2\" { \"path\" \"%3\" }\n}\n")
                .arg(root, second, missing)
                .toUtf8());
  writeFile(root + QStringLiteral("/steamapps/appmanifest_30.acf"), "\"AppState\" {");
  writeFile(root + QStringLiteral("/steamapps/appmanifest_40.acf"), "\"Other\" { }");

  // A stale library path or a manifest Steam is still writing must not empty the library.
  const SteamScanResult result = SteamScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 2);
  QCOMPARE(result.unreadableManifests.size(), 2);
  QCOMPARE(result.warnings.size(), 3);
  QVERIFY(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Unmounted Drive")));
  QVERIFY(SteamScanner::isToolTitle(QStringLiteral("Proton 9.0 (Beta)")));
  QVERIFY(SteamScanner::isToolTitle(QStringLiteral("Steam Linux Runtime 3.0 (sniper)")));
  QVERIFY(!SteamScanner::isToolTitle(QStringLiteral("Protonic Blast")));

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  SteamGameModel model(database);
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 2")));
  QVERIFY(model.errorText().contains(QStringLiteral("appmanifest_30.acf")));

  LibraryFilterModel library;
  library.setSourceModel(&model);
  QCOMPARE(library.indexOf(QStringLiteral("Steam"), QString{}, QStringLiteral("20")), 1);
  QCOMPARE(library.indexOf(QStringLiteral("Steam"), QString{}, QStringLiteral("999")), -1);
  QCOMPARE(library.indexOf(QStringLiteral("Lutris"), QString{}, QStringLiteral("20")), -1);
}

void CoreTests::steamModelPersistsFavoritesAndHiddenState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createSteamFixture(root, second);

  {
    SteamGameModel model(database);
    model.refreshFromRoots({root});
    QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
    QCOMPARE(model.rowCount(), 2);
    model.toggleFavorite(0);
    model.toggleHidden(1);
    LibraryFilterModel filtered;
    filtered.setSourceModel(&model);
    QCOMPARE(filtered.rowCount(), 1);
    filtered.setMode(LibraryFilterModel::Mode::Hidden);
    QCOMPARE(filtered.rowCount(), 1);
    writeFile(second + QStringLiteral("/steamapps/appmanifest_20.acf"), "\"AppState\" {");
    model.refreshFromRoots({root});
    QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
    // The unreadable manifest keeps its cached installation instead of freezing the scan.
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 1")));
    QVERIFY(model.errorText().contains(QStringLiteral("appmanifest_20.acf")));
  }

  SteamGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 2);
  QVERIFY(reloaded.get(0).value(QStringLiteral("favorite")).toBool());
  QVERIFY(reloaded.get(1).value(QStringLiteral("hidden")).toBool());
}

void CoreTests::steamModelSkipsUnchangedRescans() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  createSteamFixture(root, second);

  SteamGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(resets.count(), 1);

  // Steam touches manifests constantly while downloading. A rescan that finds the same
  // library must not rebuild the model or the database.
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(resets.count(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Imported 2")));

  writeFile(second + QStringLiteral("/steamapps/appmanifest_30.acf"),
            manifest("30", "Day of Defeat", "Day of Defeat"));
  model.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 3000);
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(resets.count(), 2);
}

void CoreTests::steamModelMigratesVersionOneDatabase() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  const QString setupConnection = QStringLiteral("migration-setup");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setupConnection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (app_id TEXT PRIMARY KEY, title TEXT NOT NULL, favorite INTEGER NOT "
        "NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE installations (app_id TEXT PRIMARY KEY REFERENCES games(app_id), install_dir "
        "TEXT NOT NULL, library_path TEXT NOT NULL, manifest_path TEXT NOT NULL, cover_path TEXT, "
        "hero_path TEXT, logo_path TEXT, last_played INTEGER NOT NULL DEFAULT 0, playtime_minutes "
        "INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE source_state (source TEXT PRIMARY KEY, "
                                      "last_scan INTEGER, last_error TEXT)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games VALUES('440', 'Team Fortress 2', 1, 1)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO installations VALUES('440', 'Team Fortress 2', '/games', '/manifests/440', "
        "'', '', '', 1700000000, 600, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO source_state VALUES('steam', 1700000000, '')")));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 5")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(setupConnection);

  SteamGameModel model(database);
  UnifiedGameModel unified(database);
  SteamGameModel reopened(database);
  QCOMPARE(reopened.rowCount(), 1);
  QCOMPARE(reopened.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("440"));
  QVERIFY(reopened.get(0).value(QStringLiteral("favorite")).toBool());
  QVERIFY(reopened.get(0).value(QStringLiteral("hidden")).toBool());
  const QString verifyConnection = QStringLiteral("migration-verify");
  {
    QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
    verify.setDatabaseName(database);
    QVERIFY(verify.open());
    QSqlQuery query(verify);
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT paths FROM source_state WHERE source = 'steam'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString{});
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('achievement_summary', 'achievements')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
                       "'game_insights'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
                       "'owned_games'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('artwork_overrides', 'game_link_members', 'launch_activity')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    QVERIFY(query.exec(
        QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
                       "('game_organization', 'collections', 'collection_games')")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    verify.close();
  }
  QSqlDatabase::removeDatabase(verifyConnection);
}

void CoreTests::steamModelMigratesUnscopedOwnedGamesCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  const QString setupConnection = QStringLiteral("owned-migration-setup");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setupConnection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE games (app_id TEXT PRIMARY KEY, title TEXT NOT NULL, favorite INTEGER NOT "
        "NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO games VALUES('440', 'Team Fortress 2', 0, 0)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE owned_games (app_id TEXT PRIMARY KEY REFERENCES games(app_id), "
        "playtime_minutes INTEGER NOT NULL DEFAULT 0, synced_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO owned_games VALUES('440', 600, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 8")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(setupConnection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setSteamId(QStringLiteral("76561198000000000"));
  SteamGameModel model(database, &settings);
  QCOMPARE(model.rowCount(), 0);

  const QString verifyConnection = QStringLiteral("owned-migration-verify");
  {
    QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
    verify.setDatabaseName(database);
    QVERIFY(verify.open());
    QSqlQuery query(verify);
    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 9);
    QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(owned_games)")));
    bool hasSteamId = false;
    while (query.next()) {
      hasSteamId = hasSteamId || query.value(1).toString() == QStringLiteral("steam_id");
    }
    QVERIFY(hasSteamId);
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM owned_games")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    verify.close();
  }
  QSqlDatabase::removeDatabase(verifyConnection);
}

void CoreTests::achievementModelLoadsLocalSteamCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/Steam");
  const QString second = directory.path() + QStringLiteral("/Library");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createSteamFixture(root, second);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  SteamGameModel games(database, &settings);
  games.refreshFromRoots({root});
  QTRY_VERIFY_WITH_TIMEOUT(!games.scanning(), 3000);
  QCOMPARE(games.get(0).value(QStringLiteral("achievementsUnlocked")).toInt(), 1);
  QCOMPARE(games.get(0).value(QStringLiteral("achievementsTotal")).toInt(), 2);

  AchievementModel achievements(database, &settings);
  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.unlocked(), 1);
  QCOMPARE(achievements.total(), 2);
  QCOMPARE(achievements.rowCount(), 2);
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("First Win"));

  const QString updateConnection = QStringLiteral("achievement-sort-update");
  {
    QSqlDatabase update = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), updateConnection);
    update.setDatabaseName(database);
    QVERIFY(update.open());
    QSqlQuery query(update);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievements SET unlocked = 1, unlock_time = 1600000000 "
        "WHERE app_id = '10' AND api_name = 'WIN_TWO'")));
    update.close();
  }
  QSqlDatabase::removeDatabase(updateConnection);

  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("Second Win"));
  achievements.setSortMode(1);
  QCOMPARE(achievements.data(achievements.index(0), AchievementModel::TitleRole).toString(),
           QStringLiteral("First Win"));
  {
    QSqlDatabase update = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), updateConnection);
    update.setDatabaseName(database);
    QVERIFY(update.open());
    QSqlQuery query(update);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievement_summary SET unlocked = 0, total = 0, source = "
        "'retroachievements' WHERE app_id = '10'")));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM achievements WHERE app_id = '10'")));
    update.close();
  }
  QSqlDatabase::removeDatabase(updateConnection);
  achievements.load(QStringLiteral("10"));
  QCOMPARE(achievements.total(), 0);
  QCOMPARE(achievements.statusText(), QStringLiteral("This game has no RetroAchievements."));

  QVERIFY(AchievementModel::acceptsIconUrl(QUrl(QStringLiteral(
      "https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/10/icon.jpg"))));
  QVERIFY(AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://shared.steamstatic.com/icon.jpg"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://steamcdn-a.akamaihd.net.example.com/icon.jpg"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("http://steamcdn-a.akamaihd.net/icon.jpg"))));
  QVERIFY(AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://media.retroachievements.org/Badge/012345.png"))));
  QVERIFY(!AchievementModel::acceptsIconUrl(
      QUrl(QStringLiteral("https://media.retroachievements.org.example.com/Badge/x.png"))));
}

void CoreTests::steamAchievementApiParsesPlayerSchemaAndRarity() {
  const QByteArray player =
      R"({"playerstats":{"success":true,"achievements":[{"apiname":"FIRST","achieved":1,"unlocktime":1700000000},{"apiname":"HIDDEN","achieved":0,"unlocktime":0}]}})";
  const QByteArray schema =
      R"({"game":{"availableGameStats":{"achievements":[{"name":"FIRST","displayName":"First Step","description":"Begin","icon":"https://shared.steamstatic.com/first.jpg","hidden":0},{"name":"HIDDEN","displayName":"Secret","description":"","icon":"https://shared.steamstatic.com/hidden.jpg","hidden":1}]}}})";
  const QByteArray rarity =
      R"({"achievementpercentages":{"achievements":[{"name":"FIRST","percent":42.5},{"name":"HIDDEN","percent":3.25}]}})";

  SteamAchievementApiResult result;
  QString error;
  QCOMPARE(SteamAchievementApi::parse(player, schema, rarity, &result, &error),
           SteamApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(result.unlocked, 1);
  QCOMPARE(result.total, 2);
  QCOMPARE(result.achievements.at(0).title, QStringLiteral("First Step"));
  QCOMPARE(result.achievements.at(0).rarity, 42.5);
  QVERIFY(result.achievements.at(1).hidden);
}

void CoreTests::steamAchievementApiClassifiesFailures() {
  QCOMPARE(SteamAchievementApi::authenticatedHost(), QStringLiteral("api.steampowered.com"));
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(0, true), SteamApiState::Offline);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(403, false), SteamApiState::InvalidKey);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(429, false), SteamApiState::RateLimited);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(429, true), SteamApiState::RateLimited);
  QCOMPARE(SteamAchievementApi::classifyHttpResponse(500, true), SteamApiState::RemoteError);

  SteamAchievementApiResult result;
  QString error;
  const QByteArray privatePlayer =
      R"({"playerstats":{"success":false,"error":"Profile is private"}})";
  QCOMPARE(SteamAchievementApi::parse(privatePlayer, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!error.isEmpty());
  const QByteArray invalidKey = R"({"playerstats":{"success":false,"error":"Invalid API key"}})";
  QCOMPARE(SteamAchievementApi::parse(invalidKey, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::InvalidKey);
  QCOMPARE(SteamAchievementApi::parse("not json", R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::RemoteError);
  QVERIFY(SteamAchievementApi::isNoStatsResponse(
      R"({"playerstats":{"error":"Requested app has no stats","success":false}})"));
  QVERIFY(!SteamAchievementApi::isNoStatsResponse(privatePlayer));
  // Steam answers HTTP 403 with this body when game details are private; the key is valid.
  const QByteArray notPublic =
      R"({"playerstats":{"error":"Profile is not public","success":false}})";
  QVERIFY(SteamAchievementApi::isPrivateProfileResponse(notPublic));
  QVERIFY(SteamAchievementApi::isPrivateProfileResponse(privatePlayer));
  QVERIFY(!SteamAchievementApi::isPrivateProfileResponse(invalidKey));
  QVERIFY(!SteamAchievementApi::isPrivateProfileResponse("not json"));
  QCOMPARE(SteamAchievementApi::parse(notPublic, R"({"game":{}})", R"({})", &result, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!SteamAchievementApi::isNoStatsResponse("not json"));
  QVERIFY(!SteamAchievementApi::isNoStatsResponse(R"({"playerstats":{"success":true}})"));
}

void CoreTests::steamOwnedGamesApiParsesLibraryAndPrivacy() {
  QVector<SteamOwnedGameRecord> games;
  QString error;
  QCOMPARE(
      SteamOwnedGamesApi::parse(
          R"({"response":{"game_count":2,"games":[{"appid":10,"name":"Counter-Strike","playtime_forever":125},{"appid":30,"name":"Portal","playtime_forever":60}]}})",
          &games, &error),
      SteamApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(0).appId, QStringLiteral("10"));
  QCOMPARE(games.at(0).title, QStringLiteral("Counter-Strike"));
  QCOMPARE(games.at(0).playtimeMinutes, 125);

  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{}})", &games, &error),
           SteamApiState::PrivateProfile);
  QVERIFY(!error.isEmpty());
  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{"game_count":0}})", &games, &error),
           SteamApiState::Ready);
  QVERIFY(games.isEmpty());
  QCOMPARE(SteamOwnedGamesApi::parse(R"({"response":{"game_count":2}})", &games, &error),
           SteamApiState::RemoteError);
  // Steam's game_count is not reliable for large accounts; the array is the truth.
  QCOMPARE(SteamOwnedGamesApi::parse(
               R"({"response":{"game_count":2,"games":[{"appid":10,"name":"Counter-Strike"}]}})",
               &games, &error),
           SteamApiState::Ready);
  QCOMPARE(games.size(), 1);
  // Duplicates, nameless entries, and Steam tools are skipped instead of failing the sync.
  QCOMPARE(
      SteamOwnedGamesApi::parse(
          R"({"response":{"game_count":5,"games":[{"appid":10,"name":"Counter-Strike"},{"appid":10,"name":"Duplicate"},{"appid":11,"name":""},{"appid":1493710,"name":"Proton Experimental"},{"appid":1628350,"name":"Steam Linux Runtime 3.0 sniper"},{"appid":30,"name":"Portal"}]}})",
          &games, &error),
      SteamApiState::Ready);
  QCOMPARE(games.size(), 2);
  QCOMPARE(games.at(1).appId, QStringLiteral("30"));
  QCOMPARE(SteamOwnedGamesApi::parse("not json", &games, &error), SteamApiState::RemoteError);
  QVERIFY(SteamOwnedGamesApi::messageForState(SteamApiState::Offline)
              .contains(QStringLiteral("owned games")));
  QVERIFY(SteamOwnedGamesApi::messageForState(SteamApiState::RemoteError)
              .contains(QStringLiteral("owned games")));
  QVERIFY(!SteamOwnedGamesApi::messageForState(SteamApiState::Offline)
               .contains(QStringLiteral("achievement")));
}

void CoreTests::steamOwnedGamesRemainOptionalAndFilterByInstallation() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setSteamId(QStringLiteral("76561198000000000"));
  settings.setSteamId(QStringLiteral("12345678"));
  settings.setSteamId(QStringLiteral("tsouth89"));
  QCOMPARE(settings.steamId(), QStringLiteral("76561198000000000"));
  {
    SteamGameModel schema(database, &settings);
  }

  const QString connection = QStringLiteral("owned-games-fixture");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QSqlQuery query(setup);
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO games(app_id, title) VALUES('10', 'Counter-Strike'), ('30', 'Portal'), "
        "('40', 'Other Account Game')")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO owned_games(steam_id, app_id, playtime_minutes, synced_at) VALUES"
        "('76561198000000000', '10', 125, 1700000000), "
        "('76561198000000000', '30', 60, 1700000000), "
        "('76561198000000001', '40', 30, 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO installations(app_id, install_dir, library_path, manifest_path, cover_path, "
        "hero_path, logo_path, last_played, playtime_minutes, observed_at) VALUES"
        "('10', 'Counter-Strike', '/games', '/manifests/10', '', '', '', 0, 100, 1700000000)")));
    setup.close();
  }
  QSqlDatabase::removeDatabase(connection);

  SteamGameModel games(database, &settings);
  QCOMPARE(games.rowCount(), 2);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 1);
  QVERIFY(library.get(0).value(QStringLiteral("installed")).toBool());

  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("30"));
  QVERIFY(!library.get(0).value(QStringLiteral("installed")).toBool());

  settings.setSteamId(QStringLiteral("76561198000000001"));
  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(), QStringLiteral("40"));
}

void CoreTests::steamOwnedLibraryHandlesTwoThousandGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  const QString steamId = QStringLiteral("76561198000000000");
  settings.setSteamId(steamId);
  {
    SteamGameModel schema(database, &settings);
  }

  const QString connection = QStringLiteral("large-owned-games-fixture");
  {
    QSqlDatabase setup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    setup.setDatabaseName(database);
    QVERIFY(setup.open());
    QVERIFY(setup.transaction());
    QSqlQuery gameQuery(setup);
    gameQuery.prepare(QStringLiteral("INSERT INTO games(app_id, title) VALUES(?, ?)"));
    QSqlQuery ownedQuery(setup);
    ownedQuery.prepare(
        QStringLiteral("INSERT INTO owned_games(steam_id, app_id, playtime_minutes, synced_at) "
                       "VALUES(?, ?, ?, 1700000000)"));
    for (int index = 1; index <= 2000; ++index) {
      const QString appId = QString::number(index);
      gameQuery.bindValue(0, appId);
      gameQuery.bindValue(1, QStringLiteral("Owned Game %1").arg(index));
      QVERIFY(gameQuery.exec());
      ownedQuery.bindValue(0, steamId);
      ownedQuery.bindValue(1, appId);
      ownedQuery.bindValue(2, index);
      QVERIFY(ownedQuery.exec());
    }
    QVERIFY(setup.commit());
    setup.close();
  }
  QSqlDatabase::removeDatabase(connection);

  SteamGameModel games(database, &settings);
  QCOMPARE(games.rowCount(), 2000);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 0);
  library.setAvailability(LibraryFilterModel::Availability::AllGames);
  QCOMPARE(library.rowCount(), 2000);
  library.setAvailability(LibraryFilterModel::Availability::ReadyToInstall);
  QCOMPARE(library.rowCount(), 2000);
}

void CoreTests::steamLauncherBuildsSafeUrls() {
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://rungameid/440")));
  QCOMPARE(SteamLauncher::manageUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://nav/games/details/440")));
  QCOMPARE(SteamLauncher::installUrl(QStringLiteral("440")),
           QUrl(QStringLiteral("steam://install/440")));
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("3236138358")),
           QUrl(QStringLiteral("steam://rungameid/13899108412974694400")));
  QCOMPARE(SteamLauncher::launchUrl(QStringLiteral("13899108412974694400")),
           QUrl(QStringLiteral("steam://rungameid/13899108412974694400")));
  QVERIFY(SteamLauncher::launchUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());
  QVERIFY(SteamLauncher::installUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());

  // Launches go to the Steam client itself, not the desktop URL handler: some Steam
  // packages register no steam:// handler and xdg-open would fall back to a browser.
  // Native Steam is tried first, then Flatpak Steam, so a stale native binary still
  // reaches an installed Flatpak client.
  const QUrl launch = SteamLauncher::launchUrl(QStringLiteral("440"));
  const QList<LaunchCommand> both =
      SteamLauncher::steamCommands(launch, QStringLiteral("/usr/bin/steam"), true);
  QCOMPARE(both.size(), 2);
  QCOMPARE(both.at(0).program, QStringLiteral("/usr/bin/steam"));
  QCOMPARE(both.at(0).arguments, QStringList{QStringLiteral("steam://rungameid/440")});
  QCOMPARE(both.at(1).program, QStringLiteral("flatpak"));
  QCOMPARE(both.at(1).arguments,
           (QStringList{QStringLiteral("run"), QStringLiteral("com.valvesoftware.Steam"),
                        QStringLiteral("steam://rungameid/440")}));
  const QList<LaunchCommand> nativeOnly =
      SteamLauncher::steamCommands(launch, QStringLiteral("/usr/bin/steam"), false);
  QCOMPARE(nativeOnly.size(), 1);
  QCOMPARE(nativeOnly.at(0).program, QStringLiteral("/usr/bin/steam"));
  const QList<LaunchCommand> flatpakOnly = SteamLauncher::steamCommands(launch, QString{}, true);
  QCOMPARE(flatpakOnly.size(), 1);
  QCOMPARE(flatpakOnly.at(0).program, QStringLiteral("flatpak"));
  QVERIFY(SteamLauncher::steamCommands(launch, QString{}, false).isEmpty());
  QVERIFY(SteamLauncher::steamCommands(QUrl(QStringLiteral("https://example.com")),
                                       QStringLiteral("/usr/bin/steam"), true)
              .isEmpty());
  QVERIFY(SteamLauncher::steamCommands(QUrl{}, QStringLiteral("/usr/bin/steam"), true).isEmpty());
}

void CoreTests::lutrisScannerImportsOnlyLaunchableGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);

  const LutrisScanResult result = LutrisScanner::scan({dataRoot + QStringLiteral("/pga.db")});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().id, QStringLiteral("7"));
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Signal Hill"));
  QCOMPARE(result.games.constFirst().playtimeMinutes, 150);
  QVERIFY(result.games.constFirst().coverPath.endsWith(QStringLiteral("signal-hill.jpg")));
}

void CoreTests::lutrisModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);

  LutrisGameModel model(database);
  const QString source = dataRoot + QStringLiteral("/pga.db");
  model.refreshFromDatabases({source});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({source}));
  QVERIFY(model.lastScan() > 0);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromDatabases({source});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  LutrisGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({source}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedLutrisDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);
  LutrisGameModel model(database);
  model.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  QCOMPARE(model.rowCount(), 1);

  const QString malformed = directory.path() + QStringLiteral("/malformed.db");
  writeFile(malformed, "not sqlite");
  model.refreshFromDatabases({malformed});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Lutris scan interrupted")));
}

void CoreTests::unifiedLibraryFiltersSourcesAndRoutesFavorites() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);
  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(directory.path() + QStringLiteral("/omakade.sqlite3"));
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games;
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  LibraryFilterModel library;
  library.setSourceModel(&games);

  QCOMPARE(library.rowCount(), 3);
  library.setSourceFilter(QStringLiteral("Lutris"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Signal Hill"));
  library.toggleFavorite(0);
  QVERIFY(lutris.data(lutris.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::unifiedLibraryCanDisableSourcesAtRuntime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  createLutrisFixture(dataRoot);
  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(directory.path() + QStringLiteral("/omakade.sqlite3"));
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games;
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  QCOMPARE(games.rowCount(), 3);
  games.setSourceEnabled(QStringLiteral("Lutris"), false);
  QCOMPARE(games.rowCount(), 2);
  games.setSourceEnabled(QStringLiteral("Lutris"), true);
  QCOMPARE(games.rowCount(), 3);
}

void CoreTests::customCoverPersistsAndResets() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  const QString source = directory.path() + QStringLiteral("/cover.png");
  QImage image(20, 30, QImage::Format_RGB32);
  image.fill(Qt::red);
  QVERIFY(image.save(source));

  {
    MockGameModel demo(nullptr, 1);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    QVERIFY(library.setCustomCover(0, QUrl::fromLocalFile(source)));
    const QVariantMap game = library.get(0);
    QVERIFY(game.value(QStringLiteral("customCover")).toBool());
    QVERIFY(
        QFileInfo(QUrl(game.value(QStringLiteral("coverPath")).toString()).toLocalFile()).isFile());
  }

  MockGameModel demo(nullptr, 1);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QVERIFY(library.get(0).value(QStringLiteral("customCover")).toBool());
  QVERIFY(library.resetCustomCover(0));
  QVERIFY(!library.get(0).value(QStringLiteral("customCover")).toBool());
  writeFile(directory.path() + QStringLiteral("/invalid.png"), "not an image");
  QVERIFY(!library.setCustomCover(
      0, QUrl::fromLocalFile(directory.path() + QStringLiteral("/invalid.png"))));
}

void CoreTests::explicitLinksPersistAndPreserveInstallations() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString dataRoot = directory.path() + QStringLiteral("/lutris");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createLutrisFixture(dataRoot);

  {
    MockGameModel demo(nullptr, 2);
    LutrisGameModel lutris(database);
    lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    games.addSourceModel(&lutris);
    QCOMPARE(games.rowCount(), 3);
    QCOMPARE(games.linkCandidates(0, QStringLiteral("Signal")).size(), 1);
    QVERIFY(games.linkGames(0, QStringLiteral("Lutris"), QString{}, QStringLiteral("7")));
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.data(games.index(0), GameRoles::Linked).toBool());
    QCOMPARE(games.data(games.index(0), GameRoles::LinkedSources).toString(),
             QStringLiteral("Demo + Lutris"));
    const QVariantList installations = games.installations(0);
    QCOMPARE(installations.size(), 2);
    QCOMPARE(installations.at(0).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("Demo"));
    QCOMPARE(installations.at(1).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("Lutris"));
    QCOMPARE(installations.at(1).toMap().value(QStringLiteral("appId")).toString(),
             QStringLiteral("7"));
    games.setSourceEnabled(QStringLiteral("Demo"), false);
    QCOMPARE(games.rowCount(), 1);
    QCOMPARE(games.data(games.index(0), GameRoles::Source).toString(), QStringLiteral("Lutris"));
    QCOMPARE(games.installations(0).size(), 1);
    games.setSourceEnabled(QStringLiteral("Demo"), true);
    QCOMPARE(games.rowCount(), 2);
    QVERIFY(games.setCompletionStatus(0, QStringLiteral("completed")));
    QVERIFY(games.setTags(0, QStringLiteral("cross-platform")));
    QVERIFY(games.createCollection(QStringLiteral("Finished")));
    QVERIFY(games.setCollectionMembership(0, QStringLiteral("Finished"), true));
    for (const QVariant& installation : games.installations(0)) {
      QCOMPARE(installation.toMap().value(QStringLiteral("completionStatus")).toString(),
               QStringLiteral("completed"));
      QCOMPARE(installation.toMap().value(QStringLiteral("tags")).toStringList(),
               QStringList({QStringLiteral("cross-platform")}));
      QCOMPARE(installation.toMap().value(QStringLiteral("collections")).toStringList(),
               QStringList({QStringLiteral("Finished")}));
    }
    games.toggleFavorite(0);
    QVERIFY(!demo.data(demo.index(0), GameRoles::Favorite).toBool());

    LibraryFilterModel library;
    library.setSourceModel(&games);
    library.setSourceFilter(QStringLiteral("Lutris"));
    QCOMPARE(library.rowCount(), 1);
    QVERIFY(library.get(0).value(QStringLiteral("linked")).toBool());
  }

  MockGameModel demo(nullptr, 2);
  LutrisGameModel lutris(database);
  lutris.refreshFromDatabases({dataRoot + QStringLiteral("/pga.db")});
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  games.addSourceModel(&lutris);
  QCOMPARE(games.rowCount(), 2);
  QCOMPARE(games.installations(0).size(), 2);
  QCOMPARE(games.data(games.index(0), GameRoles::CompletionStatus).toString(),
           QStringLiteral("completed"));
  QCOMPARE(games.data(games.index(0), GameRoles::Collections).toStringList(),
           QStringList({QStringLiteral("Finished")}));
  QVERIFY(games.unlinkGames(0));
  QCOMPARE(games.rowCount(), 3);
}

void CoreTests::launchActivityPersistsAndSortsExactly() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");

  {
    MockGameModel demo(nullptr, 20);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    library.setMode(LibraryFilterModel::Mode::Recent);
    QCOMPARE(library.rowCount(), 9);

    library.setMode(LibraryFilterModel::Mode::All);
    int launchRow = -1;
    for (int row = 0; row < library.rowCount(); ++row) {
      if (library.get(row).value(QStringLiteral("appId")) == QStringLiteral("demo-10")) {
        launchRow = row;
        break;
      }
    }
    QVERIFY(launchRow >= 0);
    QVERIFY(library.recordLaunch(launchRow, QStringLiteral("Demo"), QString{},
                                 QStringLiteral("demo-10")));
    QVERIFY(!library.recordLaunch(launchRow, QStringLiteral("Steam"), QString{},
                                  QStringLiteral("10")));
    library.setMode(LibraryFilterModel::Mode::Recent);
    QCOMPARE(library.rowCount(), 10);
    library.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
    QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("demo-10"));
  }

  MockGameModel demo(nullptr, 20);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setMode(LibraryFilterModel::Mode::Recent);
  QCOMPARE(library.rowCount(), 10);
  library.setSortMode(LibraryFilterModel::SortMode::RecentlyPlayed);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
           QStringLiteral("demo-10"));
}

void CoreTests::organizationPersistsAndFilters() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");

  {
    MockGameModel demo(nullptr, 20);
    UnifiedGameModel games(database);
    games.addSourceModel(&demo);
    LibraryFilterModel library;
    library.setSourceModel(&games);
    int gameRow = -1;
    for (int row = 0; row < library.rowCount(); ++row) {
      if (library.get(row).value(QStringLiteral("appId")) == QStringLiteral("demo-10")) {
        gameRow = row;
        break;
      }
    }
    QVERIFY(gameRow >= 0);
    QVERIFY(!library.setCompletionStatus(gameRow, QStringLiteral("finished-ish")));
    QVERIFY(library.setCompletionStatus(gameRow, QStringLiteral("Playing")));
    QVERIFY(library.setTags(gameRow, QStringLiteral("Co-op, RPG, co-OP, Long game")));
    QVERIFY(library.createCollection(QStringLiteral("Weekend")));
    QVERIFY(!library.createCollection(QStringLiteral("weekend")));
    QVERIFY(library.setCollectionMembership(gameRow, QStringLiteral("Weekend"), true));

    const QVariantMap game = library.get(gameRow);
    QCOMPARE(game.value(QStringLiteral("completionStatus")).toString(),
             QStringLiteral("playing"));
    QCOMPARE(game.value(QStringLiteral("tags")).toStringList(),
             QStringList({QStringLiteral("Co-op"), QStringLiteral("Long game"),
                          QStringLiteral("RPG")}));
    QCOMPARE(game.value(QStringLiteral("collections")).toStringList(),
             QStringList({QStringLiteral("Weekend")}));
    QCOMPARE(library.collectionNames(), QStringList({QStringLiteral("Weekend")}));
    QCOMPARE(library.tagNames(),
             QStringList({QStringLiteral("Co-op"), QStringLiteral("Long game"),
                          QStringLiteral("RPG")}));

    library.setCompletionFilter(QStringLiteral("playing"));
    QCOMPARE(library.rowCount(), 1);
    library.setCompletionFilter({});
    library.setCollectionFilter(QStringLiteral("Weekend"));
    QCOMPARE(library.rowCount(), 1);
    library.setCollectionFilter({});
    library.setTagFilter(QStringLiteral("rpg"));
    QCOMPARE(library.rowCount(), 1);
    library.setSearchText(QStringLiteral("Long game"));
    QCOMPARE(library.rowCount(), 1);
  }

  MockGameModel demo(nullptr, 20);
  UnifiedGameModel games(database);
  games.addSourceModel(&demo);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setCompletionFilter(QStringLiteral("playing"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("appId")).toString(),
           QStringLiteral("demo-10"));
  QCOMPARE(library.get(0).value(QStringLiteral("collections")).toStringList(),
           QStringList({QStringLiteral("Weekend")}));
  QVERIFY(library.deleteCollection(QStringLiteral("weekend")));
  QVERIFY(library.collectionNames().isEmpty());
  QVERIFY(library.get(0).value(QStringLiteral("collections")).toStringList().isEmpty());
}

void CoreTests::lutrisLauncherBuildsSafeCommands() {
  const LaunchCommand native = GameLauncher::lutrisCommand(QStringLiteral("42"), false);
  QCOMPARE(native.program, QStringLiteral("lutris"));
  QCOMPARE(native.arguments, QStringList{QStringLiteral("lutris:rungameid/42")});
  const LaunchCommand flatpak = GameLauncher::lutrisCommand(QStringLiteral("42"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"), QStringLiteral("net.lutris.Lutris"),
                        QStringLiteral("lutris:rungameid/42")}));
  QVERIFY(!GameLauncher::lutrisCommand(QStringLiteral("42;touch /tmp/nope"), false).isValid());
}

void CoreTests::heroicScannerImportsEpicGogAndAmazon() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);

  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 4);
  QCOMPARE(result.games.at(0).runner, QStringLiteral("legendary"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Epic Voyage"));
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("EpicApp.jpg")));
  QCOMPARE(result.games.at(0).playtimeMinutes, 125);
  QVERIFY(result.games.at(0).lastPlayed > 0);
  QCOMPARE(result.games.at(1).runner, QStringLiteral("gog"));
  QCOMPARE(result.games.at(1).title, QStringLiteral("GOG Quest"));
  QCOMPARE(result.games.at(1).playtimeMinutes, 0);
  const std::optional<GogLaunchTask> launchTask =
      HeroicScanner::gogLaunchTask(result.games.at(1).installPath, result.games.at(1).appId);
  QVERIFY(launchTask.has_value());
  QVERIFY(launchTask->executablePath.endsWith(QStringLiteral("/gog-game/start.sh")));
  QCOMPARE(launchTask->arguments,
           QStringList({QStringLiteral("--profile"), QStringLiteral("couch mode")}));
  QCOMPARE(result.games.at(2).runner, QStringLiteral("nile"));
  QCOMPARE(result.games.at(2).title, QStringLiteral("Amazon Trail"));
  // Sideloaded games come from sideload_apps/library.json; uninstalled entries stay out.
  QCOMPARE(result.games.at(3).runner, QStringLiteral("sideload"));
  QCOMPARE(result.games.at(3).appId, QStringLiteral("j661Z9rpxqYRZSp45Jh92i"));
  QCOMPARE(result.games.at(3).title, QStringLiteral("Scott Pilgrim EX"));
  QCOMPARE(result.games.at(3).installPath, QStringLiteral("/home/user/Games/Scott"));
  QVERIFY(result.games.at(3).coverPath.contains(QStringLiteral("/images-cache/")));

  QTemporaryDir sideloadOnly;
  QVERIFY(sideloadOnly.isValid());
  const QString sideloadRoot = sideloadOnly.path() + QStringLiteral("/heroic");
  writeFile(
      sideloadRoot + QStringLiteral("/sideload_apps/library.json"),
      R"({"games":[{"runner":"sideload","app_name":"onlyGame","title":"Only Game","install":{"executable":"/games/only/game.exe","is_dlc":false},"folder_name":"","is_installed":true}]})");
  const HeroicScanResult sideload = HeroicScanner::scan({sideloadRoot});
  QCOMPARE(sideload.roots, QStringList({sideloadRoot}));
  QCOMPARE(sideload.games.size(), 1);
  QCOMPARE(sideload.games.at(0).installPath, QStringLiteral("/games/only"));
}

void CoreTests::gogScannerImportsLooseInstallsAndConfinesLaunchTasks() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/GOG Games");
  const QString game = root + QStringLiteral("/Signal Hill");
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"bin\\game.exe","workingDir":"bin","arguments":"--safe \"two words\""}]})");
  writeFile(game + QStringLiteral("/bin/game.exe"), "game");

  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.at(0).appId, QStringLiteral("98765"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("gog-direct"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Signal Hill"));

  const QString outside = directory.path() + QStringLiteral("/outside.exe");
  writeFile(outside, "outside");
  QVERIFY(QFile::link(outside, game + QStringLiteral("/bin/linked.exe")));
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"bin/linked.exe"}]})");
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765")).has_value());

  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Signal Hill","playTasks":[{"type":"FileTask","isPrimary":true,"path":"../escape.exe"}]})");
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765")).has_value());
  QVERIFY(!HeroicScanner::gogLaunchTask(game, QStringLiteral("98765;touch")).has_value());
}

void CoreTests::heroicModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QVERIFY(model.heroicDetected());
  QVERIFY(!model.gogDetected());
  QCOMPARE(model.data(model.index(2), GameRoles::Source).toString(), QStringLiteral("Heroic"));
  QCOMPARE(model.data(model.index(1), GameRoles::AppId).toString(), QStringLiteral("EpicApp"));
  QCOMPARE(model.data(model.index(1), GameRoles::Hours).toInt(), 2);
  QVERIFY(model.data(model.index(1), GameRoles::Recent).toBool());
  QVERIFY(!model.data(model.index(0), GameRoles::Recent).toBool());
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  HeroicGameModel reloaded(directory.path() + QStringLiteral("/omakade.sqlite3"));
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedHeroicDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  writeFile(root + QStringLiteral("/legendaryConfig/legendary/installed.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));
}

void CoreTests::heroicAndGogScanFailuresAreIsolated() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString heroicRoot = directory.path() + QStringLiteral("/heroic");
  const QString gogRoot = directory.path() + QStringLiteral("/GOG Games");
  const QString directGame = gogRoot + QStringLiteral("/Direct Quest");
  createHeroicFixture(heroicRoot);
  writeFile(directGame + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Direct Quest","playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh"}]})");
  writeFile(directGame + QStringLiteral("/start.sh"), "#!/bin/sh\n");

  HeroicGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.heroicDetected());
  QVERIFY(model.gogDetected());

  writeFile(heroicRoot + QStringLiteral("/legendaryConfig/legendary/installed.json"),
            "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));

  createHeroicFixture(heroicRoot);
  writeFile(heroicRoot + QStringLiteral("/gog_store/installed.json"), "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));

  createHeroicFixture(heroicRoot);
  writeFile(directGame + QStringLiteral("/goggame-98765.info"), "not json");
  model.refreshFromRoots({heroicRoot, gogRoot});
  QCOMPARE(model.rowCount(), 5);
  QVERIFY(model.statusText().contains(QStringLiteral("kept cached results")));
}

void CoreTests::coldManagedGogFailureDoesNotImportDirectGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/heroic");
  createHeroicFixture(root);
  writeFile(root + QStringLiteral("/gog_store/installed.json"), "not json");
  const HeroicScanResult result = HeroicScanner::scan({root});
  QVERIFY(result.managedGogIncomplete);
  for (const HeroicGameRecord& game : result.games) {
    QVERIFY(game.runner != QStringLiteral("gog-direct"));
  }
  HeroicGameModel model(directory.path() + QStringLiteral("/library.sqlite3"));
  model.refreshFromRoots({root});
  QVERIFY(!model.gogDetected());
  createHeroicFixture(root);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.heroicDetected());
  QVERIFY(!model.gogDetected());
}

void CoreTests::removingLastDirectGogGameClearsCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/GOG Games");
  const QString game = root + QStringLiteral("/Direct Quest");
  writeFile(game + QStringLiteral("/goggame-98765.info"),
            R"({"name":"Direct Quest","playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh"}]})");
  writeFile(game + QStringLiteral("/start.sh"), "#!/bin/sh\n");
  const QString database = directory.path() + QStringLiteral("/library.sqlite3");
  HeroicGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  model.refreshFromRoots({directory.path() + QStringLiteral("/missing")});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(QFile::remove(game + QStringLiteral("/goggame-98765.info")));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 0);
  QVERIFY(!model.gogDetected());
  HeroicGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 0);
}

void CoreTests::heroicLauncherBuildsSafeCommands() {
  const LaunchCommand native =
      GameLauncher::heroicCommand(QStringLiteral("EpicApp"), QStringLiteral("legendary"), false);
  QCOMPARE(native.program, QStringLiteral("heroic"));
  QCOMPARE(native.arguments.constFirst(), QStringLiteral("--no-gui"));
  QCOMPARE(native.arguments.constLast(),
           QStringLiteral("heroic://launch?appName=EpicApp&runner=legendary&gui=false"));
  const LaunchCommand flatpak =
      GameLauncher::heroicCommand(QStringLiteral("12345"), QStringLiteral("gog"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("com.heroicgameslauncher.hgl"));
  QVERIFY(!GameLauncher::heroicCommand(QStringLiteral("bad;id"), QStringLiteral("gog"), false)
               .isValid());
  QVERIFY(!GameLauncher::heroicCommand(QStringLiteral("good"), QStringLiteral("unknown"), false)
               .isValid());
  const LaunchCommand sideload = GameLauncher::heroicCommand(
      QStringLiteral("j661Z9rpxqYRZSp45Jh92i"), QStringLiteral("sideload"), false);
  QVERIFY(sideload.isValid());
  QVERIFY(sideload.arguments.constLast().contains(QStringLiteral("runner=sideload")));
}

void CoreTests::gogLauncherBuildsSafeCommands() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString nativeGame = directory.path() + QStringLiteral("/native");
  writeFile(nativeGame + QStringLiteral("/goggame-123.info"),
            R"({"playTasks":[{"type":"FileTask","isPrimary":true,"path":"start.sh","arguments":"--profile \"living room\""}]})");
  writeFile(nativeGame + QStringLiteral("/start.sh"), "#!/bin/sh\n");
  const LaunchCommand native =
      GameLauncher::gogCommand(QStringLiteral("123"), nativeGame);
  QVERIFY(native.program.endsWith(QStringLiteral("/native/start.sh")));
  QCOMPARE(native.arguments,
           QStringList({QStringLiteral("--profile"), QStringLiteral("living room")}));

  const QString windowsGame = directory.path() + QStringLiteral("/windows");
  writeFile(windowsGame + QStringLiteral("/goggame-456.info"),
            R"({"playTasks":[{"type":"FileTask","isPrimary":true,"path":"game.exe","arguments":"-windowed"}]})");
  writeFile(windowsGame + QStringLiteral("/game.exe"), "game");
  const QString prefix = directory.path() + QStringLiteral("/prefix");
  const LaunchCommand windows =
      GameLauncher::gogCommand(QStringLiteral("456"), windowsGame, prefix);
  QCOMPARE(windows.program, QStringLiteral("env"));
  QCOMPARE(windows.arguments.at(0), QStringLiteral("WINEPREFIX=%1").arg(prefix));
  QCOMPARE(windows.arguments.at(1), QStringLiteral("GAMEID=umu-default"));
  QCOMPARE(windows.arguments.at(2), QStringLiteral("STORE=gog"));
  QCOMPARE(windows.arguments.at(3), QStringLiteral("umu-run"));
  QVERIFY(windows.arguments.at(4).endsWith(QStringLiteral("/windows/game.exe")));
  QCOMPARE(windows.arguments.at(5), QStringLiteral("-windowed"));
  QVERIFY(!GameLauncher::gogCommand(QStringLiteral("456;touch"), windowsGame).isValid());
}

void CoreTests::faugusScannerImportsLaunchableGamesAndArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString nativeRoot = directory.path() + QStringLiteral("/faugus-launcher");
  const QString flatpakRoot =
      directory.path() +
      QStringLiteral("/.var/app/io.github.Faugus.faugus-launcher/data/faugus-launcher");
  createFaugusFixture(nativeRoot);
  createFaugusFixture(flatpakRoot);

  const FaugusScanResult result = FaugusScanner::scan({nativeRoot, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({nativeRoot, flatpakRoot}));
  QCOMPARE(result.games.size(), 3);
  QCOMPARE(result.games.at(0).gameId, QStringLiteral("signal-hill"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("Signal Hill"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("GE-Proton"));
  QCOMPARE(result.games.at(0).playtimeSeconds, 7200);
  QVERIFY(result.games.at(0).executablePath.startsWith(QDir::homePath()));
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("signal-hill.png")));
  QVERIFY(result.games.at(0).heroPath.endsWith(QStringLiteral("signal-hill.png")));
  QVERIFY(!result.games.at(0).flatpak);
  QVERIFY(result.games.at(1).coverPath.endsWith(QStringLiteral("linux-tool.png")));
  QCOMPARE(result.games.at(2).gameId, QStringLiteral("pokémon-外伝"));
}

void CoreTests::faugusModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/faugus-launcher");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createFaugusFixture(root);

  FaugusGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Faugus"));
  QCOMPARE(model.data(model.index(2), GameRoles::Hours).toInt(), 2);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  writeFile(root + QStringLiteral("/games.json"), "[]");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 0);
  createFaugusFixture(root);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  FaugusGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedFaugusDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/faugus-launcher");
  createFaugusFixture(root);
  FaugusGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  writeFile(root + QStringLiteral("/games.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Faugus scan interrupted")));
}

void CoreTests::faugusLauncherBuildsSafeCommands() {
  const LaunchCommand native = GameLauncher::faugusCommand(QStringLiteral("signal-hill"), false);
  QCOMPARE(native.program, QStringLiteral("faugus-launcher"));
  QCOMPARE(native.arguments,
           QStringList({QStringLiteral("--game"), QStringLiteral("signal-hill")}));
  const LaunchCommand flatpak = GameLauncher::faugusCommand(QStringLiteral("signal-hill"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"),
                        QStringLiteral("--command=/app/bin/faugus-launcher"),
                        QStringLiteral("io.github.Faugus.faugus-launcher"),
                        QStringLiteral("--game"), QStringLiteral("signal-hill")}));
  QVERIFY(!GameLauncher::faugusCommand(QStringLiteral("bad;id"), false).isValid());
  QVERIFY(GameLauncher::faugusCommand(QStringLiteral("pokémon-外伝"), false).isValid());
  QVERIFY(!GameLauncher::faugusCommand(QStringLiteral("../escape"), false).isValid());
}

void CoreTests::launcherRefreshesRunAsynchronously() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const bool dataHomeWasSet = qEnvironmentVariableIsSet("XDG_DATA_HOME");
  const bool configHomeWasSet = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
  const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
  const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
  const auto restoreEnvironment = qScopeGuard([&] {
    if (dataHomeWasSet) {
      qputenv("XDG_DATA_HOME", previousDataHome);
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
    if (configHomeWasSet) {
      qputenv("XDG_CONFIG_HOME", previousConfigHome);
    } else {
      qunsetenv("XDG_CONFIG_HOME");
    }
  });
  const QString dataHome = directory.path() + QStringLiteral("/data");
  const QString configHome = directory.path() + QStringLiteral("/config");
  qputenv("XDG_DATA_HOME", dataHome.toUtf8());
  qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  createLutrisFixture(dataHome + QStringLiteral("/lutris"));
  createHeroicFixture(configHome + QStringLiteral("/heroic"));
  createFaugusFixture(dataHome + QStringLiteral("/faugus-launcher"));
  createBattleNetFixture(directory.path() + QStringLiteral("/home/.wine"));
  const bool homeWasSet = qEnvironmentVariableIsSet("HOME");
  const QByteArray previousHome = qgetenv("HOME");
  qputenv("HOME", QByteArray(directory.path().toUtf8() + "/home"));
  const auto restoreHome = qScopeGuard([&] {
    if (homeWasSet) {
      qputenv("HOME", previousHome);
    } else {
      qunsetenv("HOME");
    }
  });

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  LutrisGameModel lutris(database);
  HeroicGameModel heroic(database);
  FaugusGameModel faugus(database);
  BattleNetGameModel battlenet(database);
  lutris.refresh();
  heroic.refresh();
  faugus.refresh();
  battlenet.refresh();
  QCOMPARE(lutris.statusText(), QStringLiteral("Scanning Lutris library"));
  QCOMPARE(heroic.statusText(), QStringLiteral("Scanning Heroic and GOG libraries"));
  QCOMPARE(faugus.statusText(), QStringLiteral("Scanning Faugus library"));
  QCOMPARE(battlenet.statusText(), QStringLiteral("Scanning Battle.net library"));
  QVERIFY(battlenet.scanning());
  QTRY_COMPARE_WITH_TIMEOUT(lutris.rowCount(), 1, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(heroic.rowCount(), 4, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(faugus.rowCount(), 3, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(battlenet.rowCount(), 3, 3000);
  QTRY_VERIFY_WITH_TIMEOUT(!battlenet.scanning(), 3000);
}

void CoreTests::absentLaunchersPersistEmptySourcePaths() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  {
    SteamGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    LutrisGameModel model(database);
    model.refreshFromDatabases({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    HeroicGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    FaugusGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    RetroArchGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    Pcsx2GameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    RyujinxGameModel model(database);
    model.refreshFromRoots({});
    QVERIFY(model.errorText().isEmpty());
  }
  {
    BattleNetGameModel model(database);
    model.refreshFromPrefixes({});
    QVERIFY(model.errorText().isEmpty());
  }

  const QString connection = QStringLiteral("empty-source-paths-") + QUuid::createUuid().toString();
  {
    QSqlDatabase stored = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    stored.setDatabaseName(database);
    QVERIFY(stored.open());
    QSqlQuery query(stored);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM source_state WHERE source IN "
        "('lutris', 'heroic', 'faugus', 'retroarch', 'pcsx2', 'ryujinx', 'battlenet') AND paths = '' AND paths "
        "IS NOT NULL")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 7);
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::retroArchScannerImportsPlaylistsArtworkAndRuntime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString flatpakRoot =
      directory.path() + QStringLiteral("/.var/app/org.libretro.RetroArch/config/retroarch");
  createRetroArchFixture(root);
  createRetroArchFixture(flatpakRoot);

  const RetroArchScanResult result = RetroArchScanner::scan({root, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root, flatpakRoot}));
  QCOMPARE(result.games.size(), 4);
  const auto sonic = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Sonic & Tails") && !game.flatpak;
  });
  QVERIFY(sonic != result.games.cend());
  QCOMPARE(sonic->coreName, QStringLiteral("Genesis Plus GX"));
  QVERIFY(sonic->corePath.endsWith(QStringLiteral("genesis_plus_gx_libretro.so")));
  QVERIFY(sonic->coverPath.endsWith(QStringLiteral("Sonic _ Tails.png")));
  QVERIFY(sonic->heroPath.endsWith(QStringLiteral("Sonic _ Tails.png")));
  QCOMPARE(sonic->playtimeSeconds, 45296);
  QVERIFY(sonic->lastPlayed > 0);
  const auto unassigned =
      std::find_if(result.games.cbegin(), result.games.cend(),
                   [](const auto& game) { return game.title == QStringLiteral("Unassigned"); });
  QVERIFY(unassigned != result.games.cend());
  QVERIFY(unassigned->corePath.isEmpty());
  QCOMPARE(std::count_if(result.games.cbegin(), result.games.cend(),
                         [](const auto& game) { return game.flatpak; }),
           2);

  writeFile(root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic & Tails.lrtl"),
            R"({"runtime":"9223372036854775807:00:00"})");
  const RetroArchScanResult hostileRuntime = RetroArchScanner::scan({root});
  const auto safeSonic =
      std::find_if(hostileRuntime.games.cbegin(), hostileRuntime.games.cend(),
                   [](const auto& game) { return game.title == QStringLiteral("Sonic & Tails"); });
  QVERIFY(safeSonic != hostileRuntime.games.cend());
  QCOMPARE(safeSonic->playtimeSeconds, 0);
}

void CoreTests::retroArchScannerResolvesFlatpakPathsAndCoreNames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString appDirectory =
      directory.path() + QStringLiteral("/.var/app/org.libretro.RetroArch");
  const QString root = appDirectory + QStringLiteral("/config/retroarch");
  const QString archive = directory.path() + QStringLiteral("/roms/Sonic Pack.zip");
  const QString snes = directory.path() + QStringLiteral("/roms/Yoshi`s Island.sfc");
  writeFile(archive, "zip");
  writeFile(snes, "rom");
  // The Flatpak writes sandbox paths into its config; they only exist inside the sandbox.
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            "playlist_directory = \"/var/config/retroarch/playlists\"\n"
            "thumbnails_directory = \"/var/config/retroarch/thumbnails\"\n"
            "runtime_log_directory = \"/var/config/retroarch/playlists/logs\"\n"
            "libretro_info_path = \"/var/config/retroarch/cores\"\n");
  writeFile(
      root + QStringLiteral("/cores/genesis_plus_gx_libretro.info"),
      "display_name = \"Sega - MS/GG/MD/CD (Genesis Plus GX)\"\ncorename = \"Genesis Plus GX\"\n");
  writeFile(
      root + QStringLiteral("/playlists/Sega - Mega Drive.lpl"),
      QStringLiteral(
          R"json({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1#Sonic.bin","label":"Sonic","core_path":"/var/config/retroarch/cores/genesis_plus_gx_libretro.so","core_name":"Sega - MS/GG/MD/CD (Genesis Plus GX)","db_name":"Sega - Mega Drive.lpl"}]})json")
          .arg(archive)
          .toUtf8());
  writeFile(
      root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
      QStringLiteral(
          R"json({"version":"1.5","default_core_path":"DETECT","default_core_name":"DETECT","items":[{"path":"%1","label":"Yoshi`s Island","core_path":"/var/config/retroarch/cores/snes9x_libretro.so","core_name":"Nintendo - SNES / SFC (Snes9x)","db_name":"Nintendo - SNES.lpl"}]})json")
          .arg(snes)
          .toUtf8());
  writeFile(root + QStringLiteral("/playlists/logs/Genesis Plus GX/Sonic.lrtl"),
            R"({"version":"1.0","runtime":"01:00:00","last_played":"2026-08-30 19:45:10"})");
  writeFile(root + QStringLiteral("/playlists/logs/Snes9x/Yoshi`s Island.lrtl"),
            R"({"version":"1.0","runtime":"02:00:00","last_played":"2026-08-30 19:45:10"})");
  writeFile(root + QStringLiteral("/thumbnails/Nintendo - SNES/Named_Boxarts/Yoshi_s Island.png"),
            "cover");

  QCOMPARE(RetroArchScanner::discoverRoots().contains(root), false);
  const RetroArchScanResult result = RetroArchScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 2);
  const auto sonic = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Sonic");
  });
  QVERIFY(sonic != result.games.cend());
  QVERIFY(sonic->flatpak);
  QCOMPARE(sonic->playtimeSeconds, 3600);
  const auto yoshi = std::find_if(result.games.cbegin(), result.games.cend(), [](const auto& game) {
    return game.title == QStringLiteral("Yoshi`s Island");
  });
  QVERIFY(yoshi != result.games.cend());
  QCOMPARE(yoshi->playtimeSeconds, 7200);
  QVERIFY(yoshi->coverPath.endsWith(QStringLiteral("Yoshi_s Island.png")));
}

void CoreTests::retroArchModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createRetroArchFixture(root);
  RetroArchGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  RetroArchGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 2);
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::malformedRetroArchDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  createRetroArchFixture(root);
  RetroArchGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  writeFile(root + QStringLiteral("/playlists/Nintendo.lpl"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(model.statusText().startsWith(QStringLiteral("RetroArch scan interrupted")));
}

void CoreTests::retroArchLauncherBuildsSafeCommands() {
  const QString content = QStringLiteral("/games/Sonic & Tails.bin");
  const QString core = QStringLiteral("/cores/genesis_plus_gx_libretro.so");
  const LaunchCommand native = GameLauncher::retroArchCommand(content, core, false);
  QCOMPARE(native.program, QStringLiteral("retroarch"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("-L"), core, content}));
  const LaunchCommand flatpak = GameLauncher::retroArchCommand(content, core, true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments,
           QStringList({QStringLiteral("run"), QStringLiteral("org.libretro.RetroArch"),
                        QStringLiteral("-L"), core, content}));
  QVERIFY(!GameLauncher::retroArchCommand(content, QStringLiteral("DETECT"), false).isValid());
  QVERIFY(!GameLauncher::retroArchCommand({}, core, false).isValid());
  const LaunchCommand playlist =
      GameLauncher::resolvedCartridgeCommand(content, core, false, true, QStringLiteral("snes9x"),
                                             QStringLiteral("/cores/snes9x_libretro.so"));
  QCOMPARE(playlist.program, QStringLiteral("retroarch"));
  QCOMPARE(playlist.arguments.constLast(), content);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString archive = directory.path() + QStringLiteral("/games.zip");
  writeFile(archive, "archive");
  GameLauncher launcher;
  QVERIFY(!launcher.launch(QStringLiteral("RetroArch"), QStringLiteral("id"), false, {},
                           archive + QStringLiteral("#Sonic.unknown"), {}));
  QVERIFY(!launcher.lastError().startsWith(QStringLiteral("The installed files are missing.")));
}

void CoreTests::battleNetScannerImportsInstalledGamesAndArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);

  const BattleNetScanResult result = BattleNetScanner::scan({prefix});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.prefixes, QStringList({QDir::cleanPath(prefix)}));
  QCOMPARE(result.games.size(), 3);
  QCOMPARE(result.games.at(0).productId, QStringLiteral("wow"));
  QCOMPARE(result.games.at(0).title, QStringLiteral("World of Warcraft"));
  QCOMPARE(result.games.at(0).launchCode, QStringLiteral("WoW"));
  QCOMPARE(result.games.at(0).runner, QStringLiteral("wine"));
  QCOMPARE(result.games.at(0).lastPlayed, 1700000000);
  QVERIFY(result.games.at(0).coverPath.endsWith(QStringLiteral("cover.png")));
  QCOMPARE(result.games.at(1).productId, QStringLiteral("pro"));
  QCOMPARE(result.games.at(1).title, QStringLiteral("Overwatch 2"));
  QCOMPARE(result.games.at(1).lastPlayed, 1700001000);
  QVERIFY(result.games.at(1).heroPath.endsWith(QStringLiteral("library_hero.jpg")));
  QCOMPARE(result.games.at(2).productId, QStringLiteral("custom_mod"));
  QCOMPARE(result.games.at(2).title, QStringLiteral("Custom mod"));
  QVERIFY(result.games.at(2).coverPath.isEmpty());
  QVERIFY(result.games.at(0).gameId.contains(QStringLiteral("wow@")));
  QVERIFY(result.games.at(0).gameId != result.games.at(0).productId);
  QVERIFY(BattleNetScanner::isToolProduct(QStringLiteral("agent")));
  QVERIFY(BattleNetScanner::isToolProduct(QStringLiteral("bna")));
  QVERIFY(!BattleNetScanner::isToolProduct(QStringLiteral("wow")));
  QCOMPARE(BattleNetScanner::slugForProduct(QStringLiteral("hero")),
           QStringLiteral("heroes-of-the-storm"));
  QCOMPARE(BattleNetScanner::coverUrl(QStringLiteral("hero")).toString(),
           QStringLiteral("https://lutris.net/games/cover/heroes-of-the-storm.jpg"));
  QCOMPARE(BattleNetScanner::heroUrl(QStringLiteral("hero")).toString(),
           QStringLiteral("https://lutris.net/games/banner/heroes-of-the-storm.jpg"));
  QVERIFY(BattleNetScanner::coverUrl(QStringLiteral("custom_mod")).isEmpty());

  bool ok = false;
  const QVector<BattleNetProductInstall> decoded = BattleNetScanner::decodeProductDb(
      BattleNetScanner::encodeProductDb({{QStringLiteral("wow"), QStringLiteral("wow"),
                                          QStringLiteral("C:\\Games\\WoW"), true, true}}),
      &ok);
  QVERIFY(ok);
  QCOMPARE(decoded.size(), 1);
  QCOMPARE(decoded.at(0).productCode, QStringLiteral("wow"));
  QCOMPARE(decoded.at(0).installPath, QStringLiteral("C:\\Games\\WoW"));
  QVERIFY(decoded.at(0).installed);
}

void CoreTests::battleNetScannerDiscoversKnownPrefixes() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const bool homeWasSet = qEnvironmentVariableIsSet("HOME");
  const bool dataHomeWasSet = qEnvironmentVariableIsSet("XDG_DATA_HOME");
  const QByteArray previousHome = qgetenv("HOME");
  const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
  const auto restoreEnvironment = qScopeGuard([&] {
    if (homeWasSet) {
      qputenv("HOME", previousHome);
    } else {
      qunsetenv("HOME");
    }
    if (dataHomeWasSet) {
      qputenv("XDG_DATA_HOME", previousDataHome);
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
  });
  const QString home = directory.path() + QStringLiteral("/home");
  const QString data = directory.path() + QStringLiteral("/data");
  qputenv("HOME", home.toUtf8());
  qputenv("XDG_DATA_HOME", data.toUtf8());

  createBattleNetFixture(home + QStringLiteral("/.wine"));
  createBattleNetFixture(data + QStringLiteral("/wineprefixes/bnet"));
  createBattleNetFixture(data + QStringLiteral("/bottles/bottles/Wow"));
  writeFile(data + QStringLiteral("/bottles/bottles/Wow/bottle.yml"), "Name: Wow\n");
  createBattleNetFixture(home + QStringLiteral("/Games/battlenet"));
  writeFile(home + QStringLiteral("/Games/battlenet/version"), "GE-Proton11-6\n");
  QVERIFY(QFile::link(home + QStringLiteral("/Games/battlenet"),
                      home + QStringLiteral("/Games/battlenet/pfx")));
  const QString steamRoot = home + QStringLiteral("/.local/share/Steam");
  createBattleNetFixture(steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx"));
  writeFile(steamRoot + QStringLiteral("/steamapps/compatdata/4242/version"), "9.0\n");
  writeFile(steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"),
            "\"libraryfolders\"\n{\n\"0\" { \"path\" \"" + steamRoot.toUtf8() + "\" }\n}\n");

  const QStringList prefixes = BattleNetScanner::discoverPrefixes();
  QVERIFY(prefixes.contains(QDir::cleanPath(home + QStringLiteral("/.wine"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(data + QStringLiteral("/wineprefixes/bnet"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(data + QStringLiteral("/bottles/bottles/Wow"))));
  QVERIFY(prefixes.contains(QDir::cleanPath(home + QStringLiteral("/Games/battlenet"))));
  QVERIFY(prefixes.contains(
      QDir::cleanPath(steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx"))));

  const BattleNetScanResult bottles =
      BattleNetScanner::scan({data + QStringLiteral("/bottles/bottles/Wow")});
  QCOMPARE(bottles.games.at(0).runner, QStringLiteral("bottles"));
  const BattleNetScanResult proton =
      BattleNetScanner::scan({steamRoot + QStringLiteral("/steamapps/compatdata/4242/pfx")});
  QCOMPARE(proton.games.at(0).runner, QStringLiteral("proton"));
  const BattleNetScanResult omarchy =
      BattleNetScanner::scan({home + QStringLiteral("/Games/battlenet")});
  QCOMPARE(omarchy.games.at(0).runner, QStringLiteral("proton"));
}

void CoreTests::battleNetScannerKeepsInstallsFromSeparatePrefixes() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString first = directory.path() + QStringLiteral("/wine");
  const QString second = directory.path() + QStringLiteral("/proton/pfx");
  createBattleNetFixture(first);
  createBattleNetFixture(second);
  writeFile(directory.path() + QStringLiteral("/proton/version"), "9.0\n");

  const BattleNetScanResult result = BattleNetScanner::scan({first, second});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 6);
  QStringList wowIds;
  for (const BattleNetGameRecord& game : result.games) {
    if (game.productId == QStringLiteral("wow")) {
      wowIds.append(game.gameId);
    }
  }
  QCOMPARE(wowIds.size(), 2);
  QVERIFY(wowIds.at(0) != wowIds.at(1));
  QCOMPARE(BattleNetScanner::productCodeFromId(wowIds.at(0)), QStringLiteral("wow"));
}

void CoreTests::battleNetModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createBattleNetFixture(prefix);

  BattleNetGameModel model(database);
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QCOMPARE(model.detectedPaths(), QStringList({QDir::cleanPath(prefix)}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(),
           QStringLiteral("Battle.net"));
  QCOMPARE(model.data(model.index(0), GameRoles::Runner).toString(), QStringLiteral("wine"));
  QCOMPARE(model.data(model.index(0), GameRoles::LaunchTarget).toString(),
           QDir::cleanPath(prefix));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  BattleNetGameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({QDir::cleanPath(prefix)}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());

  // Issue #27: a rescan must repair the runner cached by 1.5.0 without losing
  // the user's state or duplicating games in an Omarchy-style Proton prefix.
  writeFile(prefix + QStringLiteral("/version"), "GE-Proton11-6\n");
  QVERIFY(QFile::link(prefix, prefix + QStringLiteral("/pfx")));
  reloaded.refreshFromPrefixes({prefix});
  QCOMPARE(reloaded.rowCount(), 3);
  for (int row = 0; row < reloaded.rowCount(); ++row) {
    QCOMPARE(reloaded.data(reloaded.index(row), GameRoles::Runner).toString(),
             QStringLiteral("proton"));
  }
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Hidden).toBool());
  BattleNetGameModel repaired(database);
  QCOMPARE(repaired.data(repaired.index(0), GameRoles::Runner).toString(),
           QStringLiteral("proton"));
}

void CoreTests::battleNetModelMigratesLegacyRowsSafely() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString databasePath = directory.path() + QStringLiteral("/omakade.sqlite3");
  const QString connection = QStringLiteral("battlenet-legacy-migration");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE battlenet_games (product_id TEXT, name TEXT NOT NULL, launch_code TEXT, "
        "install_path TEXT, wine_prefix TEXT, runner TEXT, cover_path TEXT, hero_path TEXT, "
        "last_played INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite "
        "INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT "
        "NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO battlenet_games VALUES "
        "('wow', 'World of Warcraft', 'WoW', '/games/wow', '', 'wine', '', '', 0, 0, 1, 0, 1), "
        "('wow', 'Duplicate', 'WoW', '/games/duplicate', '', 'wine', '', '', 0, 0, 0, 1, 1), "
        "('hero', 'Heroes of the Storm', 'Hero', '/games/hero', '', 'wine', '', '', 0, 0, 0, 0, 1)")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  BattleNetGameModel model(databasePath);
  QCOMPARE(model.rowCount(), 2);
  bool favoritePreserved = false;
  for (int row = 0; row < model.rowCount(); ++row) {
    favoritePreserved = favoritePreserved ||
                        model.data(model.index(row), GameRoles::Favorite).toBool();
  }
  QVERIFY(favoritePreserved);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = "
        "'battlenet_games_legacy'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::malformedBattleNetDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);
  BattleNetGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  const QStringList detectedPaths = model.detectedPaths();
  writeFile(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"),
            "not protobuf");
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.battleNetDetected());
  QCOMPARE(model.detectedPaths(), detectedPaths);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Battle.net scan interrupted")));
}

void CoreTests::oversizedBattleNetDatabaseDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto restoreCacheHome = redirectCacheHome(directory.path() + QStringLiteral("/cache"));
  Q_UNUSED(restoreCacheHome);
  const QString prefix = directory.path() + QStringLiteral("/wine");
  createBattleNetFixture(prefix);
  BattleNetGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  const QStringList detectedPaths = model.detectedPaths();
  QFile productDb(prefix + QStringLiteral("/drive_c/ProgramData/Battle.net/Agent/product.db"));
  QVERIFY(productDb.resize(16LL * 1024 * 1024 + 1));
  model.refreshFromPrefixes({prefix});
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.battleNetDetected());
  QCOMPARE(model.detectedPaths(), detectedPaths);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Battle.net scan interrupted")));
}

void CoreTests::battleNetLauncherBuildsSafeCommands() {
  const QString prefix = QStringLiteral("/tmp/omakade-bnet");
  const LaunchCommand wine =
      GameLauncher::battleNetCommand(QStringLiteral("wow"), prefix, QStringLiteral("wine"), false);
  QCOMPARE(wine.program, QStringLiteral("wine"));
  QCOMPARE(wine.arguments.constLast(), QStringLiteral("--exec=launch WoW"));
  QVERIFY(wine.arguments.constFirst().contains(QStringLiteral("Battle.net.exe")));
  QTemporaryDir bottlesDir;
  QVERIFY(bottlesDir.isValid());
  const QString bottlesPrefix = bottlesDir.path() + QStringLiteral("/folder-name");
  writeFile(bottlesPrefix + QStringLiteral("/bottle.yml"), "Name: Actual Bottle\n");
  const LaunchCommand bottles = GameLauncher::battleNetCommand(
      QStringLiteral("pro"), bottlesPrefix, QStringLiteral("bottles"), false);
  QCOMPARE(bottles.program, QStringLiteral("bottles-cli"));
  const int bottleFlag = bottles.arguments.indexOf(QStringLiteral("-b"));
  QVERIFY(bottleFlag >= 0);
  QCOMPARE(bottles.arguments.at(bottleFlag + 1), QStringLiteral("Actual Bottle"));
  QCOMPARE(bottles.arguments.constLast(), QStringLiteral("--exec=launch Pro"));
  const QString scopedId =
      BattleNetScanner::gameIdFor(QStringLiteral("wow"), prefix);
  const LaunchCommand scoped =
      GameLauncher::battleNetCommand(scopedId, prefix, QStringLiteral("wine"), false);
  QVERIFY(scoped.isValid());
  QCOMPARE(scoped.arguments.constLast(), QStringLiteral("--exec=launch WoW"));
  const LaunchCommand proton = GameLauncher::battleNetCommand(
      QStringLiteral("s2"), prefix, QStringLiteral("proton"), false);
  QCOMPARE(proton.program, QStringLiteral("env"));
  QCOMPARE(proton.arguments.at(0), QStringLiteral("WINEPREFIX=%1").arg(prefix));
  QCOMPARE(proton.arguments.at(1), QStringLiteral("umu-run"));
  QVERIFY(proton.arguments.at(2).contains(QStringLiteral("Battle.net.exe")));
  QCOMPARE(proton.arguments.constLast(), QStringLiteral("--exec=launch S2"));
  QTemporaryDir omarchyPrefixDir;
  QVERIFY(omarchyPrefixDir.isValid());
  const QString omarchyPrefix = omarchyPrefixDir.path() + QStringLiteral("/battlenet");
  createBattleNetFixture(omarchyPrefix);
  writeFile(omarchyPrefix + QStringLiteral("/version"), "GE-Proton11-6\n");
  const LaunchCommand omarchy = GameLauncher::battleNetCommand(
      QStringLiteral("wow"), omarchyPrefix, QStringLiteral("proton"), false);
  QCOMPARE(omarchy.program, QStringLiteral("env"));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("GAMEID=umu-battlenet")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("STORE=battlenet")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("PROTONPATH=GE-Proton")));
  QVERIFY(omarchy.arguments.contains(QStringLiteral("PROTON_VERB=run")));
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("bad;id"), prefix, QStringLiteral("wine"),
                                          false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("wow"), QStringLiteral("relative"),
                                          QStringLiteral("wine"), false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("../escape"), prefix,
                                          QStringLiteral("wine"), false)
               .isValid());
  QVERIFY(!GameLauncher::battleNetCommand(QStringLiteral("wow"), prefix,
                                          QStringLiteral("unknown"), false)
               .isValid());
}

void CoreTests::launcherReportsInvalidAndStaleTargets() {
  GameLauncher launcher;
  QVERIFY(!launcher.launch(QStringLiteral("Lutris"), QStringLiteral("bad")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Lutris ID."));
  QVERIFY(!launcher.launch(QStringLiteral("Heroic"), QStringLiteral("valid"), false,
                           QStringLiteral("unknown")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Heroic target."));
  QVERIFY(!launcher.launch(QStringLiteral("Steam"), QStringLiteral("440"), false, {},
                           QStringLiteral("/path/that/does/not/exist")));
  QVERIFY(launcher.lastError().startsWith(QStringLiteral("The installed files are missing.")));
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow;rm"), false,
                           QStringLiteral("wine"), {}, QStringLiteral("/tmp/omakade-bnet")));
  QCOMPARE(launcher.lastError(), QStringLiteral("This game has an invalid Battle.net target."));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString prefix = directory.path() + QStringLiteral("/prefix");
  createBattleNetFixture(prefix);
  const QByteArray previousPath = qgetenv("PATH");
  const bool pathWasSet = qEnvironmentVariableIsSet("PATH");
  const auto restorePath = qScopeGuard([previousPath, pathWasSet] {
    if (pathWasSet) {
      qputenv("PATH", previousPath);
    } else {
      qunsetenv("PATH");
    }
  });
  qputenv("PATH", directory.path().toUtf8());
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow"), false,
                           QStringLiteral("bottles"), {}, prefix));
  QCOMPARE(launcher.lastError(), QStringLiteral("Bottles is not installed."));
  QVERIFY(!launcher.launch(QStringLiteral("Battle.net"), QStringLiteral("wow"), false,
                           QStringLiteral("proton"), {}, prefix));
  QCOMPARE(launcher.lastError(),
           QStringLiteral("umu-launcher is not installed. Install it to launch Battle.net games "
                          "from Proton."));
}

void CoreTests::igdbApiBuildsSafeQueriesAndParsesInsights() {
  const QByteArray mappingQuery = IgdbApi::steamMappingQuery(QStringLiteral("1245620"));
  QVERIFY(mappingQuery.contains("uid = \"1245620\""));
  QVERIFY(mappingQuery.contains("external_game_source.name = \"Steam\""));
  QVERIFY(IgdbApi::steamMappingQuery(QStringLiteral("1; limit 500")).isEmpty());
  QVERIFY(IgdbApi::gameQuery(0).isEmpty());
  QVERIFY(IgdbApi::timeToBeatQuery(-1).isEmpty());

  qint64 gameId = 0;
  QString error;
  QVERIFY(IgdbApi::parseSteamMapping(R"([{"id":9,"game":1942}])", &gameId, &error));
  QCOMPARE(gameId, 1942);

  IgdbGameInsight insight;
  QVERIFY(IgdbApi::parseGame(
      R"([{"id":1942,"name":"The Witcher 3","aggregated_rating":92.6,"aggregated_rating_count":47}])",
      &insight, &error));
  QCOMPARE(insight.gameId, 1942);
  QCOMPARE(insight.title, QStringLiteral("The Witcher 3"));
  QCOMPARE(insight.criticScore, 93);
  QCOMPARE(insight.criticReviewCount, 47);

  QVERIFY(IgdbApi::parseTimeToBeat(
      R"([{"game_id":1942,"hastily":184200,"normally":370800,"completely":624600,"count":382}])",
      &insight, &error));
  QCOMPARE(insight.rushedSeconds, 184200);
  QCOMPARE(insight.normalSeconds, 370800);
  QCOMPARE(insight.completeSeconds, 624600);
  QCOMPARE(insight.timeSampleCount, 382);
  QVERIFY(!IgdbApi::parseTimeToBeat(R"([{"game_id":7,"normally":20}])", &insight, &error));
  QVERIFY(!IgdbApi::parseGame("not json", &insight, &error));
}

void CoreTests::igdbInsightsLoadFromOfflineCache() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("igdb-cache-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE game_insights (source TEXT NOT NULL, app_id TEXT NOT NULL, provider TEXT "
        "NOT NULL, provider_game_id INTEGER NOT NULL, title TEXT NOT NULL, critic_score INTEGER "
        "NOT NULL, critic_review_count INTEGER NOT NULL, rushed_seconds INTEGER NOT NULL, "
        "normal_seconds INTEGER NOT NULL, complete_seconds INTEGER NOT NULL, time_sample_count "
        "INTEGER NOT NULL, updated_at INTEGER NOT NULL, PRIMARY KEY(source, app_id, provider))")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO game_insights VALUES('Steam', '10', 'igdb', 1942, 'Cached Game', 88, 31, "
        "7200, 14400, 28800, 99, 1700000000)")));
    // A remembered miss: IGDB had no entry, so provider_game_id is 0.
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO game_insights VALUES('Steam', '20', 'igdb', 0, '', -1, 0, 0, 0, 0, 0, "
        "1700000000)")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  GameInsightsService insights(databasePath, &settings);
  QTRY_VERIFY_WITH_TIMEOUT(!insights.busy(), 2000);
  insights.loadSteam(QStringLiteral("10"));
  QVERIFY(insights.available());
  QCOMPARE(insights.criticScore(), 88);
  QCOMPARE(insights.criticReviewCount(), 31);
  QCOMPARE(insights.rushedHours(), 2);
  QCOMPARE(insights.normalHours(), 4);
  QCOMPARE(insights.completeHours(), 8);
  QCOMPARE(insights.timeSampleCount(), 99);
  QCOMPARE(insights.statusText(), QStringLiteral("Cached IGDB data"));
  insights.loadSteam(QStringLiteral("20"));
  QVERIFY(!insights.available());
  QCOMPARE(insights.statusText(), QStringLiteral("IGDB has no entry for this game"));
}

void CoreTests::retroAchievementsHasherAppliesHeaderStripRules() {
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Nintendo - Game Boy")).rule,
           RetroAchievementsHashRule::WholeFileMd5);
  QCOMPARE(RetroAchievementsHasher::consoleFor(
               QStringLiteral("Nintendo - Nintendo Entertainment System"))
               .rule,
           RetroAchievementsHashRule::NesHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(
               QStringLiteral("Nintendo - Super Nintendo Entertainment System"))
               .rule,
           RetroAchievementsHashRule::SnesHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Sega - Mega Drive - Genesis")).rule,
           RetroAchievementsHashRule::WholeFileMd5);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Atari - 7800")).rule,
           RetroAchievementsHashRule::Atari7800HeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Atari - Lynx")).rule,
           RetroAchievementsHashRule::AtariLynxHeaderStrip);
  QCOMPARE(RetroAchievementsHasher::consoleFor(QStringLiteral("Sony - PlayStation")).rule,
           RetroAchievementsHashRule::Unsupported);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray payload = "PAYLOAD-BYTES";
  const QByteArray payloadMd5 = QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex();

  const QString nesHeadered = directory.path() + QStringLiteral("/game.nes");
  {
    QFile file(nesHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("NES\x1A", 4) + QByteArray(12, '\0') + payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::NesHeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  const QString nesHeaderless = directory.path() + QStringLiteral("/plain.nes");
  {
    QFile file(nesHeaderless);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeaderless, RetroAchievementsHashRule::NesHeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  // SNES headers are detected at 8KB (0x2000) granularity, not 32KB — use a payload size that's
  // a multiple of 8KB but not of 32KB so the test actually distinguishes the two.
  const QString snesHeadered = directory.path() + QStringLiteral("/game.sfc");
  const QByteArray snesPayload(0x2000, 'A');
  const QByteArray snesPayloadMd5 =
      QCryptographicHash::hash(snesPayload, QCryptographicHash::Md5).toHex();
  {
    QFile file(snesHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(512, 'H') + snesPayload);
  }
  QCOMPARE(
      RetroAchievementsHasher::hashFile(snesHeadered, RetroAchievementsHashRule::SnesHeaderStrip)
          .value_or(QByteArray()),
      snesPayloadMd5);

  // PC Engine uses the same header-strip rule as SNES but at 128KB (0x20000) granularity.
  const QString pcEngineHeadered = directory.path() + QStringLiteral("/game.pce");
  const QByteArray pcEnginePayload(0x20000, 'B');
  const QByteArray pcEnginePayloadMd5 =
      QCryptographicHash::hash(pcEnginePayload, QCryptographicHash::Md5).toHex();
  {
    QFile file(pcEngineHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(512, 'H') + pcEnginePayload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(pcEngineHeadered,
                                             RetroAchievementsHashRule::PcEngineHeaderStrip)
               .value_or(QByteArray()),
           pcEnginePayloadMd5);
  // A payload that isn't 512 bytes past a 128KB boundary must not be stripped.
  QCOMPARE(RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::PcEngineHeaderStrip)
               .value_or(QByteArray()),
           QCryptographicHash::hash(QByteArray("NES\x1A", 4) + QByteArray(12, '\0') + payload,
                                    QCryptographicHash::Md5)
               .toHex());

  const QString atari7800Headered = directory.path() + QStringLiteral("/game.a78");
  {
    QFile file(atari7800Headered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(1, '\x01') + QByteArray("ATARI7800", 9) + QByteArray(118, '\0') +
              payload);
  }
  QCOMPARE(RetroAchievementsHasher::hashFile(atari7800Headered,
                                             RetroAchievementsHashRule::Atari7800HeaderStrip)
               .value_or(QByteArray()),
           payloadMd5);

  const QString lynxHeadered = directory.path() + QStringLiteral("/game.lnx");
  {
    QFile file(lynxHeadered);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("LYNX", 4) + QByteArray(1, '\0') + QByteArray(59, '\0') + payload);
  }
  QCOMPARE(
      RetroAchievementsHasher::hashFile(lynxHeadered, RetroAchievementsHashRule::AtariLynxHeaderStrip)
          .value_or(QByteArray()),
      payloadMd5);

  QVERIFY(!RetroAchievementsHasher::hashFile(nesHeadered, RetroAchievementsHashRule::Unsupported)
               .has_value());
  QVERIFY(!RetroAchievementsHasher::hashFile(directory.path() + QStringLiteral("/missing.nes"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
}

void CoreTests::retroAchievementsHasherReadsZipArchivedRoms() {
  // RetroArch stores archived content as "archive.zip#inner/path.rom" (see
  // RetroArchScanner::runtimeFileName), which is how the overwhelming majority of real RetroArch
  // libraries store ROMs. The hasher must read the specific zip entry, not the literal path.
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QByteArray payload = "ZIPPED-ROM-BYTES";
  const QByteArray payloadMd5 = QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex();
  const QString archivePath = directory.path() + QStringLiteral("/game.zip");
  const QString innerName = QStringLiteral("game.gb");

  int errorCode = 0;
  zip_t* archive = zip_open(archivePath.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
  QVERIFY(archive != nullptr);
  zip_source_t* source = zip_source_buffer(archive, payload.constData(), payload.size(), 0);
  QVERIFY(source != nullptr);
  QVERIFY(zip_file_add(archive, innerName.toUtf8().constData(), source, ZIP_FL_OVERWRITE) >= 0);
  QCOMPARE(zip_close(archive), 0);

  const QString contentPath = archivePath + QLatin1Char('#') + innerName;
  QCOMPARE(RetroAchievementsHasher::hashFile(contentPath, RetroAchievementsHashRule::WholeFileMd5)
               .value_or(QByteArray()),
           payloadMd5);

  QVERIFY(!RetroAchievementsHasher::hashFile(archivePath + QStringLiteral("#missing-entry.gb"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
  QVERIFY(!RetroAchievementsHasher::hashFile(directory.path() +
                                                 QStringLiteral("/missing.zip#game.gb"),
                                             RetroAchievementsHashRule::WholeFileMd5)
               .has_value());
}

void CoreTests::retroAchievementsApiBuildsUrlsAndParsesResponses() {
  const QUrl gameInfoUrl =
      RetroAchievementsApi::gameInfoAndProgressUrl(QStringLiteral("KEY123"), 1942,
                                                   QStringLiteral("someuser"));
  QCOMPARE(gameInfoUrl.host(), QStringLiteral("retroachievements.org"));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("g=1942")));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("u=someuser")));
  QVERIFY(gameInfoUrl.query().contains(QStringLiteral("y=KEY123")));

  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(0, true), RetroAchievementsApiState::Offline);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(403, false),
           RetroAchievementsApiState::InvalidKey);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(429, false),
           RetroAchievementsApiState::RateLimited);
  QCOMPARE(RetroAchievementsApi::classifyHttpResponse(200, false), RetroAchievementsApiState::Ready);

  QVector<RetroAchievementsConsoleRecord> consoles;
  QVERIFY(RetroAchievementsApi::parseConsoleIds(
      R"([{"ID":7,"Name":"NES/Famicom"},{"ID":1,"Name":"Genesis/Mega Drive"}])", &consoles));
  QCOMPARE(consoles.size(), 2);
  QCOMPARE(consoles.at(0).id, 7);
  QCOMPARE(consoles.at(0).name, QStringLiteral("NES/Famicom"));

  // Regression: "Game Boy" is a substring of "Game Boy Color", so naive substring matching must
  // not let the shorter, unrelated system win just because it's listed first.
  const QVector<RetroAchievementsConsoleRecord> gameBoyFamily = {
      {.id = 4, .name = QStringLiteral("Game Boy")},
      {.id = 5, .name = QStringLiteral("Game Boy Advance")},
      {.id = 6, .name = QStringLiteral("Game Boy Color")},
  };
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("Game Boy Color")),
           6);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("Game Boy")), 4);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(gameBoyFamily, QStringLiteral("PlayStation")), 0);
  const QVector<RetroAchievementsConsoleRecord> headlineSystems = {
      {.id = 7, .name = QStringLiteral("NES/Famicom")},
      {.id = 3, .name = QStringLiteral("SNES/Super Famicom")},
      {.id = 1, .name = QStringLiteral("Mega Drive")},
      {.id = 8, .name = QStringLiteral("Famicom Disk System")},
  };
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Nintendo Entertainment System")),
           7);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Super Nintendo Entertainment System")),
           3);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(headlineSystems,
                                                   QStringLiteral("Genesis/Mega Drive")),
           1);
  QCOMPARE(RetroAchievementsApi::bestConsoleMatch(
               headlineSystems, QStringLiteral("Nintendo - Famicom Disk System")),
           8);

  QVector<RetroAchievementsHashRecord> games;
  QVERIFY(RetroAchievementsApi::parseGameList(
      R"([{"ID":1942,"Title":"Some Game","Hashes":["ABCDEF","abc123"]}])", &games));
  QCOMPARE(games.size(), 1);
  QCOMPARE(games.at(0).gameId, qint64(1942));
  QCOMPARE(games.at(0).md5Hashes.size(), 2);
  QCOMPARE(games.at(0).md5Hashes.at(0), QStringLiteral("abcdef"));

  const QByteArray progressJson =
      R"({"ID":1942,"Title":"Some Game","Achievements":{)"
      R"("111":{"ID":111,"Title":"First","Description":"Do the thing","BadgeName":"012345",)"
      R"("DateEarned":"2021-01-02 03:04:05"},)"
      R"("112":{"ID":112,"Title":"Second","Description":"","BadgeName":"012346"}}})";
  RetroAchievementsProgressResult progress;
  QString error;
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress(progressJson, &progress, &error),
           RetroAchievementsApiState::Ready);
  QVERIFY(error.isEmpty());
  QCOMPARE(progress.total, 2);
  QCOMPARE(progress.unlocked, 1);
  bool foundUnlocked = false;
  bool foundLocked = false;
  for (const RetroAchievementsAchievementRecord& achievement : progress.achievements) {
    if (achievement.apiName == QStringLiteral("111")) {
      QVERIFY(achievement.unlocked);
      QCOMPARE(achievement.unlockTime, qint64(1609556645));
      QVERIFY(achievement.iconUrl.endsWith(QStringLiteral("012345.png")));
      foundUnlocked = true;
    } else if (achievement.apiName == QStringLiteral("112")) {
      QVERIFY(!achievement.unlocked);
      QVERIFY(achievement.iconUrl.endsWith(QStringLiteral("012346_lock.png")));
      foundLocked = true;
    }
  }
  QVERIFY(foundUnlocked);
  QVERIFY(foundLocked);

  RetroAchievementsProgressResult errorResult;
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress(R"({"Error":"Game not found"})",
                                                          &errorResult, &error),
           RetroAchievementsApiState::RemoteError);
  QCOMPARE(error, QStringLiteral("Game not found"));
  QCOMPARE(RetroAchievementsApi::parseGameInfoAndProgress("not json", &errorResult, &error),
           RetroAchievementsApiState::RemoteError);
}

void CoreTests::retroArchModelReadsCachedRetroAchievementsSummary() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("retroachievements-cache-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE retroarch_games (game_id TEXT PRIMARY KEY, name TEXT NOT NULL, "
        "content_path TEXT NOT NULL, core_path TEXT, core_name TEXT, cover_path TEXT, hero_path "
        "TEXT, system TEXT NOT NULL DEFAULT '', playtime_seconds INTEGER NOT NULL DEFAULT 0, "
        "last_played INTEGER NOT NULL DEFAULT 0, flatpak INTEGER NOT NULL DEFAULT 0, favorite "
        "INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0, observed_at INTEGER NOT "
        "NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO retroarch_games(game_id, name, content_path, system, observed_at) VALUES("
        "'rg-1', 'Test Game', '/tmp/test.gb', 'Nintendo - Game Boy', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievement_summary (app_id TEXT PRIMARY KEY, unlocked INTEGER NOT NULL, "
        "total INTEGER NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO achievement_summary VALUES('rg-1', 3, 10, "
                                      "'retroachievements', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievements (app_id TEXT NOT NULL, api_name TEXT NOT NULL, title TEXT "
        "NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked INTEGER NOT NULL, "
        "unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER NOT NULL, "
        "current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT NOT NULL, "
        "PRIMARY KEY(app_id, api_name))")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  RetroArchGameModel model(databasePath);
  QCOMPARE(model.rowCount(), 1);
  const QModelIndex row = model.index(0);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 3);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 10);
  QCOMPARE(model.data(row, GameRoles::Progress).toInt(), 30);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "UPDATE achievement_summary SET unlocked = 10, total = 10 WHERE app_id = 'rg-1'")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  QSignalSpy dataChangedSpy(&model, &RetroArchGameModel::dataChanged);
  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(dataChangedSpy.count(), 1);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 10);
  QCOMPARE(model.data(row, GameRoles::Progress).toInt(), 100);

  model.clearAchievementSummaries();
  QCOMPARE(dataChangedSpy.count(), 2);
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 0);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 0);

  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 10);
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("DELETE FROM achievement_summary WHERE app_id = 'rg-1'")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
  model.reloadAchievementSummary(QStringLiteral("rg-1"));
  QCOMPARE(model.data(row, GameRoles::AchievementsUnlocked).toInt(), 0);
  QCOMPARE(model.data(row, GameRoles::AchievementsTotal).toInt(), 0);
}

void CoreTests::retroAchievementsServiceClearsCacheOnAccountSwitch() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString databasePath = directory.path() + QStringLiteral("/library.sqlite3");
  const QString connection = QStringLiteral("retroachievements-account-switch-fixture");
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievement_summary (app_id TEXT PRIMARY KEY, unlocked INTEGER NOT NULL, "
        "total INTEGER NOT NULL, source TEXT NOT NULL, updated_at INTEGER NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE achievements (app_id TEXT NOT NULL, api_name TEXT NOT NULL, title TEXT "
        "NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked INTEGER NOT NULL, "
        "unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER NOT NULL, "
        "current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT NOT NULL, "
        "PRIMARY KEY(app_id, api_name))")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  RetroAchievementsService service(databasePath, &settings);
  QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 2000);

  service.setUsername(QStringLiteral("accountA"));
  QCOMPARE(service.username(), QStringLiteral("accountA"));

  // Seed progress as if account A had already refreshed this game.
  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO achievement_summary VALUES('rg-1', 3, 10, "
                                      "'retroachievements', 1700000000)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO achievements VALUES('rg-1', 'ach-1', 'Title', '', '', '', 1, 1700000000, 0, "
        "0, 1, 1, 'retroachievements')")));
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);

  // Switching to a different account must not leave account A's cached progress behind for
  // account B to silently reuse.
  QSignalSpy achievementsClearedSpy(&service, &RetroAchievementsService::achievementsCleared);
  service.setUsername(QStringLiteral("accountB"));
  QCOMPARE(achievementsClearedSpy.count(), 1);

  {
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    QVERIFY(database.open());
    QSqlQuery verify(database);
    QVERIFY(verify.exec(QStringLiteral(
        "SELECT COUNT(*) FROM achievement_summary WHERE source = 'retroachievements'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 0);
    QVERIFY(verify.exec(
        QStringLiteral("SELECT COUNT(*) FROM achievements WHERE source = 'retroachievements'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toInt(), 0);
    database.close();
  }
  QSqlDatabase::removeDatabase(connection);
}

void CoreTests::retroAchievementsServiceBlocksAccountSwitchWhileBusy() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  settings.setRetroAchievementsUsername(QStringLiteral("accountA"));
  RetroAchievementsService service(directory.path() + QStringLiteral("/library.sqlite3"),
                                   &settings);
  QVERIFY(service.busy());

  service.setUsername(QStringLiteral("accountB"));
  QCOMPARE(service.username(), QStringLiteral("accountA"));
  QVERIFY(service.statusText().contains(QStringLiteral("still busy")));
  QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 2000);
}

void CoreTests::stressLibraryContainsOneThousandGames() {
  MockGameModel games(nullptr, 1000);
  QCOMPARE(games.rowCount(), 1000);
  QCOMPARE(games.get(999).value(QStringLiteral("title")).toString(),
           QStringLiteral("Wild Orbit 40"));
}

void CoreTests::settingsPersistReducedMotionAndCacheLimit() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = directory.path() + QStringLiteral("/config.toml");
  {
    AppSettings settings(path);
    QVERIFY(!settings.closeAfterLaunch());
    QVERIFY(!settings.couchModeEnabled());
    QCOMPARE(settings.couchLibraryView(), QStringLiteral("detail"));
    QCOMPARE(settings.librarySortMode(), 0);
    settings.setReducedMotion(true);
    settings.setArtworkCacheLimitMb(512);
    settings.setSteamId(QStringLiteral("76561198000000000"));
    settings.setIgdbClientId(QStringLiteral("publicclient123"));
    settings.setSteamEnabled(false);
    settings.setLutrisEnabled(false);
    settings.setGogEnabled(false);
    settings.setFaugusEnabled(false);
    settings.setRetroArchEnabled(false);
    QVERIFY(settings.pcsx2AutoEnabled());
    QVERIFY(settings.ryujinxAutoEnabled());
    QVERIFY(settings.shadps4AutoEnabled());
    QVERIFY(settings.cemuAutoEnabled());
    QVERIFY(settings.consolePortalsEnabled());
    settings.setPcsx2Enabled(false);  // explicit: clears the auto flag
    settings.setRyujinxEnabled(false);
    settings.setShadps4Enabled(false);
    settings.setCemuEnabled(false);
    settings.setConsolePortalsEnabled(false);
    settings.setPreferStandaloneEmulators(true);
    settings.setRomFolders({QStringLiteral("/roms/snes|snes")});
    settings.setBattleNetEnabled(false);
    settings.setCloseAfterLaunch(true);
    settings.setCouchModeEnabled(true);
    settings.setCouchLibraryView(QStringLiteral("grid"));
    settings.setLibrarySortMode(1);
  }
  AppSettings reloaded(path);
  QVERIFY(reloaded.reducedMotion());
  QCOMPARE(reloaded.artworkCacheLimitMb(), 512);
  QCOMPARE(reloaded.steamId(), QStringLiteral("76561198000000000"));
  QCOMPARE(reloaded.igdbClientId(), QStringLiteral("publicclient123"));
  QVERIFY(!reloaded.steamEnabled());
  QVERIFY(!reloaded.lutrisEnabled());
  QVERIFY(reloaded.heroicEnabled());
  QVERIFY(!reloaded.gogEnabled());
  QVERIFY(!reloaded.faugusEnabled());
  QVERIFY(!reloaded.retroArchEnabled());
  QVERIFY(!reloaded.pcsx2Enabled());
  QVERIFY(!reloaded.ryujinxEnabled());
  QVERIFY(!reloaded.shadps4Enabled());
  QVERIFY(!reloaded.cemuEnabled());
  QVERIFY(!reloaded.pcsx2AutoEnabled());  // explicit write cleared auto-detection
  QVERIFY(!reloaded.ryujinxAutoEnabled());
  QVERIFY(!reloaded.shadps4AutoEnabled());
  QVERIFY(!reloaded.cemuAutoEnabled());
  QVERIFY(!reloaded.consolePortalsEnabled());
  QVERIFY(reloaded.preferStandaloneEmulators());
  QCOMPARE(reloaded.romFolders(), QStringList({QStringLiteral("/roms/snes|snes")}));
  QVERIFY(!reloaded.battleNetEnabled());
  QVERIFY(reloaded.closeAfterLaunch());
  QVERIFY(reloaded.couchModeEnabled());
  QCOMPARE(reloaded.couchLibraryView(), QStringLiteral("grid"));
  QCOMPARE(reloaded.librarySortMode(), 1);
  reloaded.setLibrarySortMode(7);  // out of range falls back to title
  QCOMPARE(reloaded.librarySortMode(), 0);

  // A config without emulator keys keeps auto-detection pending and the keys absent
  // even after unrelated settings change.
  const QString autoPath = directory.path() + QStringLiteral("/auto.toml");
  {
    AppSettings settings(autoPath);
    settings.setReducedMotion(true);
  }
  QFile autoConfig(autoPath);
  QVERIFY(autoConfig.open(QIODevice::ReadOnly));
  const QString autoContents = QString::fromUtf8(autoConfig.readAll());
  autoConfig.close();
  QVERIFY(!autoContents.contains(QStringLiteral("pcsx2_enabled")));
  QVERIFY(!autoContents.contains(QStringLiteral("ryujinx_enabled")));
  QVERIFY(!autoContents.contains(QStringLiteral("shadps4_enabled")));
  QVERIFY(!autoContents.contains(QStringLiteral("cemu_enabled")));
  AppSettings autoReloaded(autoPath);
  QVERIFY(autoReloaded.pcsx2AutoEnabled());
  QVERIFY(autoReloaded.ryujinxAutoEnabled());
  QVERIFY(autoReloaded.shadps4AutoEnabled());
  QVERIFY(autoReloaded.cemuAutoEnabled());
  QVERIFY(autoReloaded.consolePortalsEnabled());
  QVERIFY(!autoReloaded.pcsx2Enabled());
}

void CoreTests::launchKeysRoundTripAndResolveInstallations() {
  const LaunchKey heroic = LaunchKey::parse(QStringLiteral("Heroic:legendary:Sugar"));
  QVERIFY(heroic.isValid());
  QCOMPARE(heroic.source, QStringLiteral("Heroic"));
  QCOMPARE(heroic.runner, QStringLiteral("legendary"));
  QCOMPARE(heroic.appId, QStringLiteral("Sugar"));
  QCOMPARE(heroic.toString(), QStringLiteral("Heroic:legendary:Sugar"));
  // RetroArch ids are content paths, so everything after the second colon belongs to the id.
  const LaunchKey retro = LaunchKey::parse(QStringLiteral("RetroArch::/roms/Game: Two (USA).zip"));
  QCOMPARE(retro.runner, QString{});
  QCOMPARE(retro.appId, QStringLiteral("/roms/Game: Two (USA).zip"));
  QVERIFY(!LaunchKey::parse(QStringLiteral("Steam")).isValid());
  QVERIFY(!LaunchKey::parse(QStringLiteral("Steam:620")).isValid());
  QVERIFY(!LaunchKey::parse(QStringLiteral("::620")).isValid());

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  MockGameModel games(nullptr, 3, true);
  UnifiedGameModel unified(directory.path() + QStringLiteral("/library.sqlite3"));
  unified.addSourceModel(&games);
  int row = -1;
  const QVariantMap found =
      PlayRequest::findInstallation(unified, LaunchKey::parse(QStringLiteral("demo::demo-1")), &row);
  QCOMPARE(row, 1);
  QCOMPARE(found.value(QStringLiteral("appId")).toString(), QStringLiteral("demo-1"));
  QCOMPARE(found.value(QStringLiteral("title")).toString(),
           unified.data(unified.index(1, 0), GameRoles::Title).toString());
  QVERIFY(PlayRequest::findInstallation(unified, LaunchKey::parse(QStringLiteral("Demo::nope")),
                                        &row)
              .isEmpty());
  QCOMPARE(row, -1);

  DelayedSteamModel delayedSource;
  UnifiedGameModel delayedUnified(QStringLiteral(":memory:"));
  delayedUnified.addSourceModel(&delayedSource);
  const LaunchKey delayedKey = LaunchKey::parse(QStringLiteral("Steam::620"));
  QTimer::singleShot(20, &delayedSource, [&delayedSource] { delayedSource.publish(); });
  QVERIFY(PlayRequest::waitForInstallation(delayedUnified, delayedKey, 1000));
  QCOMPARE(PlayRequest::findInstallation(delayedUnified, delayedKey, &row)
               .value(QStringLiteral("title"))
               .toString(),
           QStringLiteral("Portal 2"));

  GameLauncher launcher;
  QString error;
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("Steam::demo-0")),
                                &error));
  QVERIFY(error.contains(QStringLiteral("not installed")));
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("Demo::demo-1")),
                                &error));
  QCOMPARE(error, QStringLiteral("Demo games cannot be launched yet."));
  QVERIFY(!PlayRequest::perform(unified, launcher, LaunchKey::parse(QStringLiteral("bad")), &error));
  QVERIFY(error.contains(QStringLiteral("Steam::620")));
}

void CoreTests::singleInstanceForwardsPlayAndQuitCommands() {
  const QString name = QStringLiteral("omakade-test-") + QUuid::createUuid().toString();
  QVERIFY(!SingleInstance::sendCommand(name, "quit"));
  SingleInstance primary(name);
  QVERIFY(primary.claimOrNotify());
  QSignalSpy plays(&primary, &SingleInstance::playRequested);
  QSignalSpy quits(&primary, &SingleInstance::quitRequested);
  QSignalSpy activations(&primary, &SingleInstance::activationRequested);

  QVERIFY(SingleInstance::sendCommand(name, "play Steam::620"));
  QTRY_COMPARE_WITH_TIMEOUT(plays.size(), 1, 1000);
  QCOMPARE(plays.takeFirst().at(0).toString(), QStringLiteral("Steam::620"));
  SingleInstance secondary(name);
  QVERIFY(!secondary.claimOrNotify("play RetroArch::/roms/a b.zip"));
  QTRY_COMPARE_WITH_TIMEOUT(plays.size(), 1, 1000);
  QCOMPARE(plays.takeFirst().at(0).toString(), QStringLiteral("RetroArch::/roms/a b.zip"));
  QVERIFY(SingleInstance::sendCommand(name, "quit"));
  QTRY_COMPARE_WITH_TIMEOUT(quits.size(), 1, 1000);
  QCOMPARE(activations.size(), 0);
}

void CoreTests::sunshineIntegrationWritesOnlyItsOwnEntries() {
  QCOMPARE(SunshineIntegration::shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
  QCOMPARE(SunshineIntegration::commandPrefix(true),
           QStringLiteral("flatpak-spawn --host omakade"));
  const QString nativePrefix = SunshineIntegration::commandPrefix(false);
  QVERIFY(nativePrefix == QStringLiteral("omakade") ||
          nativePrefix.endsWith(QStringLiteral("/omakade'")));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString sunshineConfig = directory.path() + QStringLiteral("/sunshine.conf");
  writeFile(sunshineConfig, "output_name = 'DP-2' # streamed monitor\n");
  QCOMPARE(SunshineIntegration::configuredOutputName(sunshineConfig), QStringLiteral("DP-2"));
  const QStringList screens{QStringLiteral("HDMI-A-1"), QStringLiteral("DP-2")};
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("DP-2"), screens), 1);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("0"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QString{}, screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("missing"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QStringLiteral("9"), screens), 0);
  QCOMPARE(SunshineIntegration::outputScreenIndex(QString{}, {}), -1);
  const QString appsPath = directory.path() + QStringLiteral("/apps.json");
  const QByteArray stock = R"({
    "env": {"PATH": "$(PATH):$(HOME)/.local/bin"},
    "apps": [
        {"name": "Desktop", "image-path": "desktop.png"},
        {"name": "Steam Big Picture", "cmd": "", "detached": ["setsid steam steam://open/bigpicture"],
         "image-path": "steam.png", "prep-cmd": [{"do": "", "undo": "setsid steam steam://close/bigpicture"}]}
    ]
})";
  writeFile(appsPath, stock);
  const auto readApps = [&appsPath]() -> QJsonObject {
    QFile file(appsPath);
    if (!file.open(QIODevice::ReadOnly)) {
      return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
  };

  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  MockGameModel games(nullptr, 3, true);
  UnifiedGameModel unified(directory.path() + QStringLiteral("/library.sqlite3"));
  unified.addSourceModel(&games);
  // Same title as the second demo game, from a source without an Installed role.
  const QString sharedTitle = unified.data(unified.index(1, 0), GameRoles::Title).toString();
  const QString coverPath = directory.path() + QStringLiteral("/celeste.jpg");
  QImage cover(300, 450, QImage::Format_RGB32);
  cover.fill(Qt::red);
  QVERIFY(cover.save(coverPath, "JPEG"));
  LauncherOnlyModel lutris(sharedTitle, coverPath);
  unified.addSourceModel(&lutris);
  const QString imageRoot = directory.path() + QStringLiteral("/images");
  SunshineIntegration sunshine(&unified, &settings, appsPath, imageRoot);
  QVERIFY(sunshine.detected());
  QVERIFY(!sunshine.restartNeeded());
  QCOMPARE(readApps().value(QStringLiteral("apps")).toArray().size(), 2);
  // The export runs on a worker thread; wait for it before reading the file.
  const auto settle = [&sunshine] { QTRY_VERIFY_WITH_TIMEOUT(!sunshine.busy(), 10000); };

  // The uninstalled first demo game stays out; the two installed demo games and the
  // launcher game (no Installed role) become apps.
  settings.setSunshineGameApps(true);
  settle();
  QJsonObject document = readApps();
  QJsonArray apps = document.value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 5);
  QCOMPARE(apps.at(0).toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("Desktop"));
  QCOMPARE(apps.at(1).toObject().value(QStringLiteral("prep-cmd")).toArray().size(), 1);
  QCOMPARE(document.value(QStringLiteral("env")).toObject().value(QStringLiteral("PATH")).toString(),
           QStringLiteral("$(PATH):$(HOME)/.local/bin"));
  const QJsonObject firstGame = apps.at(2).toObject();
  QCOMPARE(firstGame.value(QStringLiteral("omakade")).toString(), QStringLiteral("Demo::demo-1"));
  QCOMPARE(firstGame.value(QStringLiteral("cmd")).toString(), QString{});
  QCOMPARE(firstGame.value(QStringLiteral("detached")).toArray().at(0).toString(),
           SunshineIntegration::commandPrefix(false) + QStringLiteral(" --play 'Demo::demo-1'"));
  QVERIFY(!firstGame.contains(QStringLiteral("image-path")));
  // Two stores share a title, so both names carry their source.
  QCOMPARE(firstGame.value(QStringLiteral("name")).toString(), sharedTitle + QStringLiteral(" (Demo)"));
  const QJsonObject lutrisGame = apps.at(4).toObject();
  QCOMPARE(lutrisGame.value(QStringLiteral("name")).toString(),
           sharedTitle + QStringLiteral(" (Lutris)"));
  QCOMPARE(lutrisGame.value(QStringLiteral("omakade")).toString(), QStringLiteral("Lutris::celeste"));
  const QString boxArt = lutrisGame.value(QStringLiteral("image-path")).toString();
  QVERIFY(boxArt.startsWith(imageRoot));
  QCOMPARE(QImage(boxArt).size(), QSize(600, 800));
  QCOMPARE(apps.at(3).toObject().value(QStringLiteral("name")).toString(),
           unified.data(unified.index(2, 0), GameRoles::Title).toString());
  QCOMPARE(sunshine.exportedGames(), 3);
  QVERIFY(sunshine.restartNeeded());
  QVERIFY(QFileInfo::exists(appsPath + QStringLiteral(".omakade-backup")));

  settings.setSunshineOmakadeApp(true);
  settle();
  apps = readApps().value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 6);
  const QJsonObject omakade = apps.at(2).toObject();
  QCOMPARE(omakade.value(QStringLiteral("name")).toString(), QStringLiteral("Omakade"));
  QCOMPARE(omakade.value(QStringLiteral("detached")).toArray().at(0).toString(),
           nativePrefix);
  QCOMPARE(omakade.value(QStringLiteral("prep-cmd"))
               .toArray()
               .at(0)
               .toObject()
               .value(QStringLiteral("undo"))
               .toString(),
           nativePrefix + QStringLiteral(" --quit"));

  // A second sync with nothing changed leaves the file alone, even though Sunshine's own
  // formatting differs from Qt's.
  writeFile(appsPath, QJsonDocument(readApps()).toJson(QJsonDocument::Compact));
  const QDateTime before = QFileInfo(appsPath).lastModified();
  QVERIFY(sunshine.sync());
  settle();
  QCOMPARE(readApps().value(QStringLiteral("apps")).toArray().size(), 6);
  QCOMPARE(QFileInfo(appsPath).lastModified(), before);
  QCOMPARE(sunshine.statusText(), QStringLiteral("Sunshine app list is up to date"));

  settings.setSunshineGameApps(false);
  settings.setSunshineOmakadeApp(false);
  settle();
  apps = readApps().value(QStringLiteral("apps")).toArray();
  QCOMPARE(apps.size(), 2);
  QCOMPARE(apps.at(1).toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("Steam Big Picture"));
  QCOMPARE(sunshine.exportedGames(), 0);
  QVERIFY(QDir(imageRoot).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty());
}

void CoreTests::secondInstanceRequestsActivation() {
  const QString name = QStringLiteral("omakade-test-") + QUuid::createUuid().toString();
  SingleInstance primary(name);
  QVERIFY(primary.claimOrNotify());
  QSignalSpy activation(&primary, &SingleInstance::activationRequested);

  SingleInstance secondary(name);
  QVERIFY(!secondary.claimOrNotify());
  QTRY_COMPARE_WITH_TIMEOUT(activation.size(), 1, 1000);
}

void CoreTests::couchCursorFollowsInputMode() {
  QWindow window;
  window.setProperty("couchMode", false);
  CouchCursorManager cursor(&window, 30);

  QVERIFY(!cursor.cursorHidden());
  window.setProperty("couchMode", true);
  cursor.syncCouchMode();
  QVERIFY(!cursor.cursorHidden());
  QTRY_VERIFY_WITH_TIMEOUT(cursor.cursorHidden(), 250);

  QMouseEvent move(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &move);
  QVERIFY(!cursor.cursorHidden());

  cursor.navigationActivity();
  QVERIFY(cursor.cursorHidden());

  QMouseEvent stationaryMove(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &stationaryMove);
  QVERIFY(cursor.cursorHidden());

  QMouseEvent secondMove(QEvent::MouseMove, QPointF(24, 20), QPointF(24, 20), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &secondMove);
  QVERIFY(!cursor.cursorHidden());

  QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &keyPress);
  QVERIFY(cursor.cursorHidden());

  window.setProperty("couchMode", false);
  cursor.syncCouchMode();
  QVERIFY(!cursor.cursorHidden());
  QTest::qWait(60);
  QVERIFY(!cursor.cursorHidden());
}

void CoreTests::virtualControllerConnectsAndMapsPrimaryButton() {
  QVERIFY(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc description;
  SDL_INIT_INTERFACE(&description);
  description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  description.naxes = SDL_GAMEPAD_AXIS_COUNT;
  description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  description.button_mask = (1U << SDL_GAMEPAD_BUTTON_SOUTH) | (1U << SDL_GAMEPAD_BUTTON_WEST) |
                            (1U << SDL_GAMEPAD_BUTTON_NORTH) | (1U << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                            (1U << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  description.axis_mask = (1U << SDL_GAMEPAD_AXIS_LEFTX) | (1U << SDL_GAMEPAD_AXIS_LEFTY);
  description.name = "Omakade test controller";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&description);
  QVERIFY2(id != 0, SDL_GetError());

  // A real controller may be in use while the offscreen suite runs.
  std::atomic<SDL_JoystickID> testControllerId{id};
  SDL_SetEventFilter([](void* context, SDL_Event* event) {
    const auto allowed = static_cast<std::atomic<SDL_JoystickID>*>(context)->load();
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
      return event->gbutton.which == allowed;
    }
    if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
      return event->gaxis.which == allowed;
    }
    return true;
  }, &testControllerId);
  const auto clearEventFilter = qScopeGuard([] { SDL_SetEventFilter(nullptr, nullptr); });

  ControllerInput controller;
  controller.start();
  QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 1000);
  const int connectedCount = controller.controllerCount();
  QSignalSpy keys(&controller, &ControllerInput::keyRequested);
  QSignalSpy focusDirections(&controller, &ControllerInput::focusDirectionRequested);
  QSignalSpy favorites(&controller, &ControllerInput::favoriteRequested);
  QSignalSpy toolbar(&controller, &ControllerInput::toolbarRequested);
  SDL_Joystick* joystick = SDL_OpenJoystick(id);
  QVERIFY(joystick != nullptr);
  QVERIFY(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Return));

  SDL_Event favorite{};
  favorite.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  favorite.gbutton.which = id;
  favorite.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;
  QVERIFY(SDL_PushEvent(&favorite));
  QTRY_COMPARE_WITH_TIMEOUT(favorites.size(), 1, 1000);

  SDL_Event controls{};
  controls.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  controls.gbutton.which = id;
  controls.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
  QVERIFY(SDL_PushEvent(&controls));
  QTRY_COMPARE_WITH_TIMEOUT(toolbar.size(), 1, 1000);

  keys.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Right));

  keys.clear();
  controller.setFocusNavigation(true);
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
  SDL_UpdateJoysticks();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, -20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Left));
  QVERIFY(keys.isEmpty());

  focusDirections.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0));
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Up));

  focusDirections.clear();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0));
  SDL_UpdateJoysticks();
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 20000));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!focusDirections.isEmpty(), 1000);
  QCOMPARE(focusDirections.first().at(0).toInt(), static_cast<int>(Qt::Key_Down));

  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0));
  SDL_UpdateJoysticks();
  QTest::qWait(30);
  focusDirections.clear();
  SDL_Event dpadDown{};
  dpadDown.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  dpadDown.gbutton.which = id;
  dpadDown.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() >= 3, 700);
  for (const QList<QVariant>& direction : std::as_const(focusDirections)) {
    QCOMPARE(direction.at(0).toInt(), static_cast<int>(Qt::Key_Right));
  }

  SDL_Event dpadUp{};
  dpadUp.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
  dpadUp.gbutton.which = id;
  dpadUp.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  QVERIFY(SDL_PushEvent(&dpadUp));
  QTest::qWait(50);
  const qsizetype directionsAfterRelease = focusDirections.size();
  QTest::qWait(300);
  QCOMPARE(focusDirections.size(), directionsAfterRelease);

  focusDirections.clear();
  dpadDown.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() >= 3, 700);
  for (const QList<QVariant>& direction : std::as_const(focusDirections)) {
    QCOMPARE(direction.at(0).toInt(), static_cast<int>(Qt::Key_Left));
  }
  dpadUp.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  QVERIFY(SDL_PushEvent(&dpadUp));
  QTest::qWait(50);
  const qsizetype leftDirectionsAfterRelease = focusDirections.size();
  QTest::qWait(300);
  QCOMPARE(focusDirections.size(), leftDirectionsAfterRelease);

  // Losing focus must cancel held navigation and suppress every controller action.
  QVERIFY(SDL_PushEvent(&dpadDown));
  QTRY_VERIFY_WITH_TIMEOUT(focusDirections.size() > leftDirectionsAfterRelease, 700);
  controller.setInputEnabled(false);
  keys.clear();
  focusDirections.clear();
  favorites.clear();
  toolbar.clear();
  for (int button : {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                     SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_WEST,
                     SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}) {
    SDL_Event backgroundButton{};
    backgroundButton.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    backgroundButton.gbutton.which = id;
    backgroundButton.gbutton.button = button;
    QVERIFY(SDL_PushEvent(&backgroundButton));
  }
  QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 20000));
  SDL_UpdateJoysticks();
  QTest::qWait(400);
  QVERIFY(keys.isEmpty());
  QVERIFY(focusDirections.isEmpty());
  QVERIFY(favorites.isEmpty());
  QVERIFY(toolbar.isEmpty());

  // Input already queued on return must not be replayed, nor may an old hold resume.
  QVERIFY(SDL_PushEvent(&favorite));
  controller.setInputEnabled(true);
  QTest::qWait(350);
  QVERIFY(keys.isEmpty());
  QVERIFY(focusDirections.isEmpty());
  QVERIFY(favorites.isEmpty());
  QVERIFY(toolbar.isEmpty());
  QVERIFY(SDL_PushEvent(&favorite));
  QTRY_COMPARE_WITH_TIMEOUT(favorites.size(), 1, 1000);

  SDL_CloseJoystick(joystick);
  SDL_Event removed{};
  removed.type = SDL_EVENT_GAMEPAD_REMOVED;
  removed.gdevice.which = id;
  QVERIFY(SDL_PushEvent(&removed));
  QVERIFY(SDL_DetachVirtualJoystick(id));
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount - 1, 1000);
  keys.clear();
  QTest::qWait(400);
  QVERIFY(keys.isEmpty());

  const SDL_JoystickID reconnectedId = SDL_AttachVirtualJoystick(&description);
  QVERIFY2(reconnectedId != 0, SDL_GetError());
  testControllerId.store(reconnectedId);
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount, 1000);
  SDL_Joystick* reconnectedJoystick = SDL_OpenJoystick(reconnectedId);
  QVERIFY(reconnectedJoystick != nullptr);
  QVERIFY(SDL_SetJoystickVirtualButton(reconnectedJoystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
  SDL_UpdateJoysticks();
  QTRY_VERIFY_WITH_TIMEOUT(!keys.isEmpty(), 1000);
  QCOMPARE(keys.first().at(0).toInt(), static_cast<int>(Qt::Key_Return));
  SDL_CloseJoystick(reconnectedJoystick);
  QVERIFY(SDL_DetachVirtualJoystick(reconnectedId));
  QTRY_COMPARE_WITH_TIMEOUT(controller.controllerCount(), connectedCount - 1, 1000);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void CoreTests::thousandGameSearchStaysResponsive() {
  MockGameModel games(nullptr, 1000);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QElapsedTimer timer;
  timer.start();
  for (int index = 0; index < 100; ++index) {
    library.setSearchText(QString::number(index));
    (void)library.rowCount();
  }
  QVERIFY2(timer.elapsed() < 1000,
           qPrintable(QStringLiteral("100 searches took %1 ms").arg(timer.elapsed())));
}

void CoreTests::openingAConsoleDoesNotHangTheLibrary() {
  MockGameModel games(nullptr, 1500);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  QCOMPARE(library.rowCount(), 1500);
  QElapsedTimer timer;
  timer.start();
  library.setConsoleFilter(QStringLiteral("snes"));
  QVERIFY(library.consoleFilter() == QStringLiteral("snes"));
  QCOMPARE(library.rowCount(), 0);
  QVERIFY2(timer.elapsed() < 250,
           qPrintable(QStringLiteral("opening SNES took %1 ms").arg(timer.elapsed())));
  timer.restart();
  library.setConsoleFilter({});
  QCOMPARE(library.rowCount(), 1500);
  QVERIFY2(timer.elapsed() < 250,
           qPrintable(QStringLiteral("leaving SNES took %1 ms").arg(timer.elapsed())));
}


void CoreTests::pcsx2ScannerImportsCacheGamesAndPlaytime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  const QString flatpakRoot =
      directory.path() + QStringLiteral("/.var/app/net.pcsx2.PCSX2/config/PCSX2");
  createPcsx2Fixture(root);
  createPcsx2Fixture(flatpakRoot, 0);

  const Pcsx2ScanResult result = Pcsx2Scanner::scan({root, flatpakRoot});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root, flatpakRoot}));
  // The same serial appearing in both roots dedupes to the first discovery.
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Crash Twinsanity"));
  QCOMPARE(result.games.constFirst().serial, QStringLiteral("SLUS-20909"));
  QCOMPARE(result.games.constFirst().region, QStringLiteral("NTSC-U"));
  QCOMPARE(result.games.constFirst().playtimeSeconds, 14);
  QCOMPARE(result.games.constFirst().lastPlayed, 1700000500);
  QVERIFY(result.games.constFirst().path.endsWith(QStringLiteral(".iso")));
  QVERIFY(!result.games.constFirst().isElf);
  QVERIFY(!result.games.constFirst().flatpak);

  // A second ROM without a cache entry (never scanned by PCSX2) must not appear.
  writeFile(root + QStringLiteral("/roms/Unscanned (USA).chd"), "rom");
  const Pcsx2ScanResult dropped = Pcsx2Scanner::scan({root});
  QCOMPARE(dropped.games.size(), 1);
}

void CoreTests::pcsx2ModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createPcsx2Fixture(root);

  Pcsx2GameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("PCSX2"));
  QCOMPARE(model.data(model.index(0), GameRoles::Hours).toInt(), 0);
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());

  Pcsx2GameModel reloaded(database);
  QCOMPARE(reloaded.detectedPaths(), QStringList({root}));
  QCOMPARE(reloaded.lastScan(), model.lastScan());
}

void CoreTests::malformedPcsx2DataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  createPcsx2Fixture(root);
  Pcsx2GameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), "not a cache");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("PCSX2 scan interrupted")));

  // A 0xFFFFFFFF length must be rejected before int narrowing.
  createPcsx2Fixture(root);
  QFile cacheFile(root + QStringLiteral("/cache/gamelist.cache"));
  QVERIFY(cacheFile.open(QIODevice::ReadOnly));
  QByteArray malformedCache = cacheFile.readAll();
  cacheFile.close();
  QVERIFY(malformedCache.size() > 12);
  // Overwrite the first string length (path) with 0xFFFFFFFF.
  malformedCache[8] = '\xff';
  malformedCache[9] = '\xff';
  malformedCache[10] = '\xff';
  malformedCache[11] = '\xff';
  writeFile(root + QStringLiteral("/cache/gamelist.cache"), malformedCache);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("PCSX2 scan interrupted")));
}

void CoreTests::pcsx2UnifiedFilterShowsGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/pcsx2");
  createPcsx2Fixture(root);
  Pcsx2GameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  UnifiedGameModel games;
  games.addSourceModel(&model);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  QCOMPARE(library.rowCount(), 1);
  library.setSourceFilter(QStringLiteral("PCSX2"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(),
           QStringLiteral("Crash Twinsanity"));
  library.setSourceFilter(QStringLiteral("Ryujinx"));
  QCOMPARE(library.rowCount(), 0);
}

void CoreTests::pcsx2LauncherBuildsSafeCommands() {
  // Serial ids are not launchable: PCSX2 boots disc images by path.
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("SLUS-20909"), false, false).isValid());
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("SCUS-97399"), false, false).isValid());
  const LaunchCommand native =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/Crash.iso"), false, false);
  QCOMPARE(native.program, QStringLiteral("pcsx2-qt"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("/games/Crash.iso")}));
  const LaunchCommand flatpak =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/Crash.iso"), false, true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("net.pcsx2.PCSX2"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/games/Crash.iso"));
  // ELF entries must receive -elf <file>.
  const LaunchCommand elf =
      GameLauncher::pcsx2Command(QStringLiteral("path:/games/homebrew.elf"), true, false);
  QCOMPARE(elf.arguments,
           QStringList({QStringLiteral("-elf"), QStringLiteral("/games/homebrew.elf")}));
  QVERIFY(!GameLauncher::pcsx2Command(QStringLiteral("bad;id"), false, false).isValid());
}

void CoreTests::ryujinxScannerImportsRomsMetadataAndPlaytime() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  createRyujinxFixture(root, roms);

  const RyujinxScanResult result = RyujinxScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().titleId, QStringLiteral("0100ABCD12345678"));
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Custom Title"));
  QVERIFY(result.games.constFirst().coverPath.endsWith(QStringLiteral("box.jpg")));
  QCOMPARE(result.games.constFirst().playtimeSeconds, 3600);
  QVERIFY(result.games.constFirst().lastPlayed > 0);
  QVERIFY(!result.games.constFirst().flatpak);
}

void CoreTests::ryujinxScannerReadsNspTitleIdAndLocalCovers() {
  // Keep the scanner away from this machine's real icon cache and keys, so
  // the fixture's local cover files decide the result.
  QStandardPaths::setTestModeEnabled(true);
  const auto restoreCache = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1\"]}").arg(roms).toUtf8());
  writeFile(roms + QStringLiteral("/Mystery.nsp"), pfs0WithTicket("0100FEED00000000"));
  writeFile(root + QStringLiteral("/games/0100FEED00000000/covers/icon.png"), "icon");
  writeFile(root + QStringLiteral("/games/0100FEED00000000/gui/metadata.json"),
            "{\"title\":\"Mystery Castle\"}");
  writeFile(roms + QStringLiteral("/Super Mario Odyssey.xci"), "xci");
  writeFile(root + QStringLiteral("/games/0100000000010000/gui/metadata.json"),
            "{\"title\":\"SUPER MARIO ODYSSEY\"}");
  writeFile(root + QStringLiteral("/games/0100000000010000/covers/box.jpg"), "odyssey");
  writeFile(roms + QStringLiteral("/Metroid Prime Remastered/data/games/MPR.nsp"), "mpr");
  writeFile(root + QStringLiteral("/games/010012101468C000/gui/metadata.json"),
            "{\"title\":\"Metroid Prime Remastered\"}");
  writeFile(root + QStringLiteral("/games/010012101468C000/covers/box.jpg"), "mpr-art");

  const RyujinxScanResult result = RyujinxScanner::scan({root});
  QCOMPARE(result.games.size(), 3);
  const RyujinxGameRecord* mystery = nullptr;
  const RyujinxGameRecord* odyssey = nullptr;
  const RyujinxGameRecord* metroid = nullptr;
  for (const RyujinxGameRecord& game : result.games) {
    if (game.titleId == QStringLiteral("0100FEED00000000")) {
      mystery = &game;
    } else if (game.titleId == QStringLiteral("0100000000010000")) {
      odyssey = &game;
    } else if (game.titleId == QStringLiteral("010012101468C000")) {
      metroid = &game;
    }
  }
  QVERIFY(mystery);
  QVERIFY(odyssey);
  QVERIFY(metroid);
  QCOMPARE(mystery->title, QStringLiteral("Mystery Castle"));
  QVERIFY(mystery->coverPath.endsWith(QStringLiteral("icon.png")));
  QCOMPARE(odyssey->title, QStringLiteral("SUPER MARIO ODYSSEY"));
  QVERIFY(odyssey->coverPath.endsWith(QStringLiteral("box.jpg")));
  QCOMPARE(metroid->title, QStringLiteral("Metroid Prime Remastered"));
  QVERIFY(metroid->coverPath.endsWith(QStringLiteral("box.jpg")));
}

void CoreTests::ryujinxScannerSkipsConfiguredAddOnsAndUpdates() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  const QString base = roms + QStringLiteral("/Game [0100ABCD12346000].nsp");
  const QString configuredUpdate =
      roms + QStringLiteral("/Game Update [0100ABCD12346000][v65536].nsp");
  const QString configuredDlc = roms + QStringLiteral("/Game DLC [0100ABCD12347001].nsp");
  const QString titleIdUpdate = roms + QStringLiteral("/Game Patch [0100ABCD12346800].nsp");
  const QString combinedCartridge = roms + QStringLiteral("/Game Bundle [0100ABCD12347800].xci");
  const QString unclassifiedPackage =
      roms + QStringLiteral("/Bonus Pack [0100ABCD12347002].nsp");
  const QString addOnDir = directory.path() + QStringLiteral("/DLCs-Updates");
  const QString namedDlc =
      addOnDir + QStringLiteral("/Pokemon Legends Z-A [DLC Mega Dimensions].nsp");
  const QString versionedCopy =
      addOnDir + QStringLiteral("/Pokemon Brilliant Diamond v1.3.0.nsp");
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1\",\"%2\"]}")
                .arg(roms, addOnDir)
                .toUtf8());
  writeFile(base, "base");
  writeFile(configuredUpdate, "update");
  writeFile(configuredDlc, "dlc");
  writeFile(titleIdUpdate, "update");
  writeFile(combinedCartridge, "combined");
  writeFile(unclassifiedPackage, "ambiguous");
  writeFile(namedDlc, "dlc-file");
  writeFile(versionedCopy, "version-file");
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/updates.json"),
            QStringLiteral("{\"paths\":[\"%1\"],\"selected\":\"%1\"}")
                .arg(configuredUpdate)
                .toUtf8());
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/dlc.json"),
            QStringLiteral("[{\"path\":\"%1\"}]").arg(configuredDlc).toUtf8());
  writeFile(root + QStringLiteral("/games/0100ABCD12346000/gui/metadata.json"),
            "{\"timespan_played\":\"1.06:00:00.5000000\"}");
  writeFile(root + QStringLiteral("/games/0100ABCD12347002/gui/metadata.json"),
            "{\"time_played\":42}");

  const RyujinxScanResult result = RyujinxScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 3);
  QStringList paths;
  for (const RyujinxGameRecord& game : result.games) {
    paths.append(game.path);
  }
  QVERIFY(paths.contains(base));
  QVERIFY(paths.contains(combinedCartridge));
  QVERIFY(paths.contains(unclassifiedPackage));
  QVERIFY(!paths.contains(configuredUpdate));
  QVERIFY(!paths.contains(configuredDlc));
  QVERIFY(!paths.contains(titleIdUpdate));
  QVERIFY(!paths.contains(namedDlc));
  QVERIFY(!paths.contains(versionedCopy));
  for (const RyujinxGameRecord& game : result.games) {
    if (game.path == base) {
      QCOMPARE(game.playtimeSeconds, qint64(108000));
    } else if (game.path == unclassifiedPackage) {
      QCOMPARE(game.playtimeSeconds, qint64(42));
    }
  }
}

void CoreTests::ryujinxModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root =
      directory.path() + QStringLiteral("/.var/app/org.ryujinx.Ryujinx/config/Ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createRyujinxFixture(root, roms);

  RyujinxGameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.detectedPaths(), QStringList({root}));
  QVERIFY(model.lastScan() > 0);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Ryujinx"));
  QCOMPARE(model.data(model.index(0), GameRoles::Runner).toString(),
           QStringLiteral("org.ryujinx.Ryujinx"));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
  RyujinxGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 1);
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
  QCOMPARE(reloaded.data(reloaded.index(0), GameRoles::Runner).toString(),
           QStringLiteral("org.ryujinx.Ryujinx"));
}

void CoreTests::malformedRyujinxDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/ryujinx");
  const QString roms = directory.path() + QStringLiteral("/switch-roms");
  createRyujinxFixture(root, roms);
  RyujinxGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QVERIFY2(model.rowCount() == 1,
           qPrintable(model.statusText() + QStringLiteral(": ") + model.errorText()));
  writeFile(root + QStringLiteral("/Config.json"), "not json");
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Ryujinx scan interrupted")));

  // A configured directory that disappeared must keep the cached library.
  writeFile(root + QStringLiteral("/Config.json"),
            QStringLiteral("{\"version\":70,\"game_dirs\":[\"%1/missing\"]}")
                .arg(roms)
                .toUtf8());
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Ryujinx scan interrupted")));
}

void CoreTests::ryujinxLauncherBuildsSafeCommands() {
  // Title ids are display metadata only: Ryujinx launches ROM file paths.
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("0100ABCD12345678"),
                                        QStringLiteral("ryujinx-wrapper"))
               .isValid());
  const LaunchCommand wrapper =
      GameLauncher::ryujinxCommand(QStringLiteral("/roms/Zelda.nsp"), QStringLiteral("ryujinx-wrapper"));
  QCOMPARE(wrapper.program, QStringLiteral("ryujinx-wrapper"));
  QCOMPARE(wrapper.arguments, QStringList({QStringLiteral("/roms/Zelda.nsp")}));
  // Native builds may ship the binary under different names.
  const LaunchCommand native =
      GameLauncher::ryujinxCommand(QStringLiteral("/roms/Zelda.nsp"), QStringLiteral("Ryujinx"));
  QCOMPARE(native.program, QStringLiteral("Ryujinx"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("/roms/Zelda.nsp")}));
  const LaunchCommand flatpak =
      GameLauncher::ryujinxCommand(QStringLiteral("path:/roms/Zelda.nsp"), QString{});
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("io.github.ryubing.Ryujinx"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/roms/Zelda.nsp"));
  const LaunchCommand legacyFlatpak = GameLauncher::ryujinxCommand(
      QStringLiteral("path:/roms/Zelda.nsp"), QString{}, QStringLiteral("org.ryujinx.Ryujinx"));
  QCOMPARE(legacyFlatpak.arguments.at(1), QStringLiteral("org.ryujinx.Ryujinx"));
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("bad;id"), QStringLiteral("Ryujinx")).isValid());
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("0100abcd12345678"), QStringLiteral("Ryujinx"))
               .isValid());
  // Paths with commas, plus signs, and hashes stay launchable.
  QVERIFY(GameLauncher::ryujinxCommand(
              QStringLiteral("/roms/Zelda, Part 2 + [dlc #3].nsp"), QStringLiteral("Ryujinx"))
              .isValid());
  QVERIFY(GameLauncher::ryujinxCommand(
              QStringLiteral("/roms/Zelda: Tears @ Midnight [0100ABCD12345678].xci"),
              QStringLiteral("Ryujinx"))
              .isValid());
  QVERIFY(!GameLauncher::ryujinxCommand(QStringLiteral("/roms/totally-not-a-rom.txt"),
                                        QStringLiteral("Ryujinx"))
               .isValid());
}

void CoreTests::shadps4ScannerImportsGamesAndArtwork() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/shadps4");
  const QString games = directory.path() + QStringLiteral("/ps4");
  createShadps4Fixture(root, games);

  const Shadps4ScanResult result = Shadps4Scanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().titleId, QStringLiteral("CUSA00001"));
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Bloodborne"));
  QVERIFY(result.games.constFirst().path.endsWith(QStringLiteral("/eboot.bin")));
  QVERIFY(result.games.constFirst().coverPath.endsWith(QStringLiteral("icon0.png")));
  QVERIFY(result.games.constFirst().heroPath.endsWith(QStringLiteral("pic0.png")));
}

void CoreTests::shadps4ScannerSkipsPatches() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/shadps4");
  const QString games = directory.path() + QStringLiteral("/ps4");
  createShadps4Fixture(root, games);
  const QString patch = games + QStringLiteral("/CUSA00001_patch");
  writeFile(patch + QStringLiteral("/eboot.bin"), "patch");
  writeFile(patch + QStringLiteral("/sce_sys/param.sfo"),
            paramSfo(QStringLiteral("Bloodborne Update"), QStringLiteral("CUSA00001"),
                     QStringLiteral("gp")));

  const Shadps4ScanResult result = Shadps4Scanner::scan({root});
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Bloodborne"));
}

void CoreTests::shadps4ScannerKeepsMergedPatchDumps() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/shadps4");
  const QString games = directory.path() + QStringLiteral("/ps4");
  writeFile(root + QStringLiteral("/config.toml"),
            QStringLiteral("[GUI]\ninstallDirs = [\"%1\"]\n").arg(games).toUtf8());
  writeFile(games + QStringLiteral("/CUSA00900/eboot.bin"), "elf");
  writeFile(games + QStringLiteral("/CUSA00900/sce_sys/param.sfo"),
            paramSfo(QStringLiteral("Bloodborne"), QStringLiteral("CUSA00900"),
                     QStringLiteral("gp")));
  writeFile(games + QStringLiteral("/CUSA00900/sce_sys/icon0.png"), "icon");

  const Shadps4ScanResult result = Shadps4Scanner::scan({root});
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Bloodborne"));
  QCOMPARE(result.games.constFirst().titleId, QStringLiteral("CUSA00900"));
}

void CoreTests::shadps4ModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/shadps4");
  const QString games = directory.path() + QStringLiteral("/ps4");
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  createShadps4Fixture(root, games);

  Shadps4GameModel model(database);
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("shadPS4"));
  model.toggleFavorite(0);
  model.toggleHidden(0);
  model.refreshFromRoots({root});
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
  QVERIFY(model.data(model.index(0), GameRoles::Hidden).toBool());
}

void CoreTests::malformedShadps4DataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/shadps4");
  const QString games = directory.path() + QStringLiteral("/ps4");
  createShadps4Fixture(root, games);
  Shadps4GameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  writeFile(root + QStringLiteral("/config.toml"),
            QStringLiteral("[GUI]\ninstallDirs = [\"%1/missing\"]\n").arg(games).toUtf8());
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("shadPS4 scan interrupted")));
}

void CoreTests::shadps4LauncherBuildsSafeCommands() {
  const LaunchCommand native = GameLauncher::shadps4Command(
      QStringLiteral("/games/CUSA00001/eboot.bin"), QStringLiteral("shadps4"));
  QCOMPARE(native.program, QStringLiteral("shadps4"));
  QCOMPARE(native.arguments, QStringList({QStringLiteral("-g"),
                                          QStringLiteral("/games/CUSA00001/eboot.bin")}));
  const LaunchCommand flatpak = GameLauncher::shadps4Command(
      QStringLiteral("/games/CUSA00001/eboot.bin"), {}, QStringLiteral("net.shadps4.shadPS4"));
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("net.shadps4.shadPS4"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/games/CUSA00001/eboot.bin"));
  QVERIFY(!GameLauncher::shadps4Command(QStringLiteral("bad;id"), QStringLiteral("shadps4"))
               .isValid());
  QVERIFY(!GameLauncher::shadps4Command(QStringLiteral("/games/notes.txt"),
                                        QStringLiteral("shadps4"))
               .isValid());
}

void CoreTests::cemuScannerImportsTitlesAndPackages() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/cemu");
  const QString games = directory.path() + QStringLiteral("/wiiu");
  createCemuFixture(root, games);
  writeFile(games + QStringLiteral("/Wind Waker (USA).wua"), "wua");
  writeFile(games + QStringLiteral("/Wind Waker (USA).png"), "cover");

  const CemuScanResult result = CemuScanner::scan({root});
  QVERIFY(!result.incomplete);
  QCOMPARE(result.games.size(), 2);
  const CemuGameRecord* mario = nullptr;
  const CemuGameRecord* windWaker = nullptr;
  for (const CemuGameRecord& game : result.games) {
    if (game.titleId == QStringLiteral("0005000010101D00")) {
      mario = &game;
    } else if (game.path.endsWith(QStringLiteral(".wua"))) {
      windWaker = &game;
    }
  }
  QVERIFY(mario);
  QVERIFY(windWaker);
  QCOMPARE(mario->title, QStringLiteral("Super Mario 3D World"));
  QVERIFY(mario->coverPath.endsWith(QStringLiteral("iconTex.png")));
  QCOMPARE(windWaker->title, QStringLiteral("Wind Waker"));
  QVERIFY(windWaker->coverPath.endsWith(QStringLiteral(".png")));
}

void CoreTests::cemuScannerUsesTitleListCacheForPackages() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/cemu");
  const QString games = directory.path() + QStringLiteral("/wiiu");
  createCemuFixture(root, games);
  const QString package = games + QStringLiteral("/Breath of the Wild (USA) (DLC) (v208).wua");
  writeFile(package, "wua");
  writeFile(root + QStringLiteral("/mlc01/usr/save/00050000/101c9400/meta/iconTex.png"), "icon");
  writeFile(root + QStringLiteral("/title_list_cache.xml"),
            QStringLiteral("<?xml version=\"1.0\"?>\n<title_list>\n"
                           "<title titleId=\"0005000e101c9400\"><name>Update</name>"
                           "<path>%1</path></title>\n"
                           "<title titleId=\"00050000101c9400\"><name>Breath of the Wild</name>"
                           "<path>%1</path></title>\n"
                           "</title_list>\n")
                .arg(package)
                .toUtf8());

  const CemuScanResult result = CemuScanner::scan({root});
  const CemuGameRecord* botw = nullptr;
  for (const CemuGameRecord& game : result.games) {
    if (game.titleId == QStringLiteral("00050000101C9400")) {
      botw = &game;
    }
  }
  QVERIFY(botw);
  QCOMPARE(botw->gameId, QStringLiteral("00050000101C9400"));
  QCOMPARE(botw->title, QStringLiteral("Breath of the Wild"));
  QVERIFY(botw->coverPath.endsWith(QStringLiteral("iconTex.png")));
}

void CoreTests::cemuScannerSkipsUpdatesAndDlc() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/cemu");
  const QString games = directory.path() + QStringLiteral("/wiiu");
  createCemuFixture(root, games);
  const QString update = games + QStringLiteral("/MarioUpdate");
  writeFile(update + QStringLiteral("/code/app.xml"),
            "<title_id>0005000E10101D00</title_id>");
  writeFile(update + QStringLiteral("/code/game.rpx"), "rpx");
  writeFile(update + QStringLiteral("/meta/meta.xml"),
            "<menu><title_id>0005000E10101D00</title_id>"
            "<longname_en>Mario Update</longname_en></menu>");

  const CemuScanResult result = CemuScanner::scan({root});
  QCOMPARE(result.games.size(), 1);
  QCOMPARE(result.games.constFirst().title, QStringLiteral("Super Mario 3D World"));
}

void CoreTests::cemuModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/cemu");
  const QString games = directory.path() + QStringLiteral("/wiiu");
  createCemuFixture(root, games);
  CemuGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Cemu"));
  model.toggleFavorite(0);
  model.refreshFromRoots({root});
  QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
}

void CoreTests::malformedCemuDataDoesNotReplaceCachedGames() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/cemu");
  const QString games = directory.path() + QStringLiteral("/wiiu");
  createCemuFixture(root, games);
  CemuGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  writeFile(root + QStringLiteral("/settings.xml"),
            QStringLiteral("<content><GamePaths><string>%1/missing</string></GamePaths></content>")
                .arg(games)
                .toUtf8());
  model.refreshFromRoots({root});
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(model.statusText().startsWith(QStringLiteral("Cemu scan interrupted")));
}

void CoreTests::consolePortalsGroupRetroArchRomsAndCanFlatten() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString snes = directory.path() + QStringLiteral("/roms/Chrono Trigger.sfc");
  const QString nes = directory.path() + QStringLiteral("/roms/Metroid.nes");
  const QString switchRom = directory.path() + QStringLiteral("/roms/Zelda.nsp");
  writeFile(snes, "sfc");
  writeFile(nes, "nes");
  writeFile(switchRom, "nsp");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n").arg(root).toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Chrono Trigger","db_name":"Nintendo - SNES.lpl"}]})json")
                .arg(snes)
                .toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - NES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Metroid","db_name":"Nintendo - NES.lpl"}]})json")
                .arg(nes)
                .toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - Nintendo Switch.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Zelda","db_name":"Nintendo - Nintendo Switch.lpl"}]})json")
                .arg(switchRom)
                .toUtf8());
  const QString cemuRoot = directory.path() + QStringLiteral("/cemu");
  createCemuFixture(cemuRoot, directory.path() + QStringLiteral("/wiiu"));

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  RetroArchGameModel roms(database);
  roms.refreshFromRoots({root});
  QCOMPARE(roms.rowCount(), 3);
  CemuGameModel cemu(database);
  cemu.refreshFromRoots({cemuRoot});
  QCOMPARE(cemu.rowCount(), 1);

  ConsolePortalModel portals;
  portals.addRomModel(&roms);
  QCOMPARE(portals.rowCount(), 3);

  UnifiedGameModel games;
  games.addSourceModel(&roms);
  games.addSourceModel(&cemu);
  games.addSourceModel(&portals);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  QCOMPARE(library.rowCount(), 4);
  QStringList titles;
  int portalCount = 0;
  for (int row = 0; row < library.rowCount(); ++row) {
    const QVariantMap game = library.get(row);
    titles.append(game.value(QStringLiteral("title")).toString());
    if (game.value(QStringLiteral("isPortal")).toBool()) {
      ++portalCount;
    }
  }
  QCOMPARE(portalCount, 3);
  QVERIFY(titles.contains(QStringLiteral("Nintendo Switch")));
  QVERIFY(titles.contains(QStringLiteral("Super Mario 3D World")));
  QVERIFY(!titles.contains(QStringLiteral("Chrono Trigger")));

  library.setConsoleFilter(QStringLiteral("snes"));
  QCOMPARE(library.rowCount(), 1);
  QCOMPARE(library.get(0).value(QStringLiteral("title")).toString(),
           QStringLiteral("Chrono Trigger"));
  QVERIFY(!library.get(0).value(QStringLiteral("isPortal")).toBool());

  library.setConsoleFilter({});
  library.setConsolePortalsEnabled(false);
  QCOMPARE(library.rowCount(), 4);
  for (int row = 0; row < library.rowCount(); ++row) {
    QVERIFY(!library.get(row).value(QStringLiteral("isPortal")).toBool());
  }
}

void CoreTests::consolePortalsDoNotRebuildTheLibraryWhenCoversChange() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString snes = directory.path() + QStringLiteral("/roms/Chrono Trigger.sfc");
  writeFile(snes, "sfc");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n").arg(root).toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Chrono Trigger","db_name":"Nintendo - SNES.lpl"}]})json")
                .arg(snes)
                .toUtf8());

  RetroArchGameModel roms(directory.path() + QStringLiteral("/omakade.sqlite3"));
  roms.refreshFromRoots({root});
  QCOMPARE(roms.rowCount(), 1);

  ConsolePortalModel portals;
  portals.addRomModel(&roms);
  UnifiedGameModel games;
  games.addSourceModel(&roms);
  games.addSourceModel(&portals);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  library.setConsoleFilter(QStringLiteral("snes"));
  QCOMPARE(library.rowCount(), 1);

  QSignalSpy portalResets(&portals, &QAbstractItemModel::modelReset);
  QSignalSpy libraryResets(&library, &QAbstractItemModel::modelReset);
  roms.toggleFavorite(0);
  QCOMPARE(portalResets.count(), 0);
  QCOMPARE(libraryResets.count(), 0);
  QCOMPARE(library.rowCount(), 1);
}

void CoreTests::consolePortalsDoNotMergeDifferentFiles() {
  QCOMPARE(ConsoleCatalog::idFor(QStringLiteral("Nintendo - SNES")), QStringLiteral("snes"));
  QCOMPARE(ConsoleCatalog::displayNameFor(QStringLiteral("snes")),
           QStringLiteral("Super Nintendo"));
  QVERIFY(ConsoleCatalog::isDedicatedSource(QStringLiteral("Sony - PlayStation 2")));
  QVERIFY(ConsoleCatalog::isDedicatedSource(QStringLiteral("Nintendo Switch")));
  QVERIFY(ConsoleCatalog::isDedicatedSource(QStringLiteral("Wii U")));
  QVERIFY(!ConsoleCatalog::isDedicatedSource(QStringLiteral("Nintendo - SNES")));
}

void CoreTests::romFoldersMergeWithPlaylistsByCanonicalPath() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString rom = directory.path() + QStringLiteral("/roms/snes/Chrono Trigger (USA).sfc");
  const QString sidecar = directory.path() + QStringLiteral("/roms/snes/Chrono Trigger (USA).png");
  writeFile(rom, "sfc");
  writeFile(sidecar, "art");
  const QString root = directory.path() + QStringLiteral("/retroarch");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n").arg(root).toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Chrono Trigger","core_path":"/cores/snes9x_libretro.so","db_name":"Nintendo - SNES.lpl"}]})json")
                .arg(rom)
                .toUtf8());

  RetroArchGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromSources({root}, {RomFolderScanner::encode(
                                       directory.path() + QStringLiteral("/roms/snes"),
                                       QStringLiteral("snes"))});
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), GameRoles::Title).toString(),
           QStringLiteral("Chrono Trigger"));
  QCOMPARE(model.data(model.index(0), GameRoles::LaunchTarget).toString(),
           QStringLiteral("/cores/snes9x_libretro.so"));
  QVERIFY(model.data(model.index(0), GameRoles::CoverPath).toString().contains(
      QStringLiteral("Chrono Trigger (USA).png")));
}

void CoreTests::romFoldersKeepSeparateCopies() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString play = directory.path() + QStringLiteral("/play/Mario.sfc");
  const QString backup = directory.path() + QStringLiteral("/backup/Mario.sfc");
  writeFile(play, "play");
  writeFile(backup, "backup");
  RetroArchGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromSources(
      {}, {RomFolderScanner::encode(directory.path() + QStringLiteral("/play"),
                                    QStringLiteral("snes")),
           RomFolderScanner::encode(directory.path() + QStringLiteral("/backup"),
                                    QStringLiteral("snes"))});
  QCOMPARE(model.rowCount(), 2);
}

void CoreTests::cartridgeLaunchResolverPrefersPlaylistCoreThenStandalone() {
  const QString rom = QStringLiteral("/roms/Chrono Trigger.sfc");
  const LaunchCommand playlist = GameLauncher::resolvedCartridgeCommand(
      rom, QStringLiteral("/cores/snes9x_libretro.so"), false, true, QStringLiteral("snes9x"),
      QStringLiteral("/usr/lib/libretro/snes9x_libretro.so"));
  QCOMPARE(playlist.program, QStringLiteral("retroarch"));
  QCOMPARE(playlist.arguments,
           QStringList({QStringLiteral("-L"), QStringLiteral("/cores/snes9x_libretro.so"), rom}));

  const LaunchCommand standalone = GameLauncher::resolvedCartridgeCommand(
      rom, {}, false, true, QStringLiteral("snes9x"),
      QStringLiteral("/usr/lib/libretro/snes9x_libretro.so"));
  QCOMPARE(standalone.program, QStringLiteral("snes9x"));
  QCOMPARE(standalone.arguments, QStringList({rom}));

  const LaunchCommand mapped = GameLauncher::resolvedCartridgeCommand(
      rom, {}, false, false, QStringLiteral("snes9x"),
      QStringLiteral("/usr/lib/libretro/snes9x_libretro.so"));
  QCOMPARE(mapped.program, QStringLiteral("retroarch"));
  QCOMPARE(mapped.arguments,
           QStringList({QStringLiteral("-L"),
                        QStringLiteral("/usr/lib/libretro/snes9x_libretro.so"), rom}));

  const LaunchCommand fallback = GameLauncher::resolvedCartridgeCommand(
      rom, {}, false, false, QStringLiteral("snes9x"), {});
  QCOMPARE(fallback.program, QStringLiteral("snes9x"));
  QVERIFY(!GameLauncher::resolvedCartridgeCommand(rom, {}, false, false, {}, {}).isValid());
}

void CoreTests::libretroCoverUrlsAndCachePathsAreStable() {
  QCOMPARE(RetroArchGameModel::libretroCoverUrl(
               QStringLiteral("Nintendo - Super Nintendo Entertainment System"),
               QStringLiteral("Chrono Trigger")),
           QStringLiteral("https://thumbnails.libretro.com/"
                          "Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/"
                          "Named_Boxarts/Chrono%20Trigger.png"));
  const QString gameId = QStringLiteral("abc123");
  const QString cached = RetroArchGameModel::libretroCoverCachePath(gameId);
  QVERIFY(cached.endsWith(QStringLiteral("/omakade/covers/libretro/abc123.png")));
  QVERIFY(RetroArchGameModel::libretroCoverCachePath(QStringLiteral("../x")).isEmpty());
  const QStringList naLabels = RetroArchGameModel::coverLabelCandidates(
      QStringLiteral("Aladdin (NA)"), QStringLiteral("Aladdin (NA)"));
  QCOMPARE(naLabels.constFirst(), QStringLiteral("Aladdin (USA)"));
  QVERIFY(naLabels.contains(QStringLiteral("Aladdin (NA)")));
  QVERIFY(naLabels.contains(QStringLiteral("Aladdin")));
  const QStringList jpLabels = RetroArchGameModel::coverLabelCandidates(
      QStringLiteral("Chrono Trigger (JP)"), QStringLiteral("Chrono Trigger (Japan)"));
  QVERIFY(jpLabels.contains(QStringLiteral("Chrono Trigger (Japan)")));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString rom = directory.path() + QStringLiteral("/Chrono Trigger.sfc");
  const QString sidecar = directory.path() + QStringLiteral("/Chrono Trigger.png");
  writeFile(rom, "sfc");
  writeFile(sidecar, "art");
  RetroArchGameModel model(directory.path() + QStringLiteral("/omakade.sqlite3"));
  model.refreshFromSources(
      {}, {RomFolderScanner::encode(directory.path(), QStringLiteral("snes"))});
  int row = -1;
  for (int index = 0; index < model.rowCount(); ++index) {
    if (model.data(model.index(index), GameRoles::Title).toString() ==
        QStringLiteral("Chrono Trigger")) {
      row = index;
      break;
    }
  }
  QVERIFY(row >= 0);
  const QString appId = model.data(model.index(row), GameRoles::AppId).toString();
  const QString before = model.data(model.index(row), GameRoles::CoverPath).toString();
  QVERIFY(before.contains(QStringLiteral("Chrono Trigger.png")));
  model.requestCover(appId);
  QCOMPARE(model.data(model.index(row), GameRoles::CoverPath).toString(), before);
}

void CoreTests::cemuLauncherBuildsSafeCommands() {
  const LaunchCommand native =
      GameLauncher::cemuCommand(QStringLiteral("/games/mario.rpx"), false);
  QCOMPARE(native.program, QStringLiteral("cemu"));
  QCOMPARE(native.arguments,
           QStringList({QStringLiteral("-g"), QStringLiteral("/games/mario.rpx")}));
  const LaunchCommand flatpak =
      GameLauncher::cemuCommand(QStringLiteral("/games/mario.wua"), true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.at(1), QStringLiteral("info.cemu.Cemu"));
  QCOMPARE(flatpak.arguments.constLast(), QStringLiteral("/games/mario.wua"));
  QVERIFY(!GameLauncher::cemuCommand(QStringLiteral("bad;id"), false).isValid());
  QVERIFY(!GameLauncher::cemuCommand(QStringLiteral("/games/notes.txt"), false).isValid());
}

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"

void CoreTests::consoleFilterKeepsOtherSourcesOut() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/retroarch");
  const QString chrono = directory.path() + QStringLiteral("/roms/Chrono Trigger.sfc");
  const QString mario = directory.path() + QStringLiteral("/roms/Super Mario World.sfc");
  const QString metroid = directory.path() + QStringLiteral("/roms/Metroid.nes");
  writeFile(chrono, "sfc");
  writeFile(mario, "sfc");
  writeFile(metroid, "nes");
  writeFile(root + QStringLiteral("/retroarch.cfg"),
            QStringLiteral("playlist_directory = \"%1/playlists\"\n").arg(root).toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - SNES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Super Mario World","db_name":"Nintendo - SNES.lpl"},{"path":"%2","label":"Chrono Trigger","db_name":"Nintendo - SNES.lpl"}]})json")
                .arg(mario, chrono)
                .toUtf8());
  writeFile(root + QStringLiteral("/playlists/Nintendo - NES.lpl"),
            QStringLiteral(R"json({"version":"1.5","items":[{"path":"%1","label":"Metroid","db_name":"Nintendo - NES.lpl"}]})json")
                .arg(metroid)
                .toUtf8());

  // A large non-RetroArch source stands in for Steam, Ryujinx, and friends.
  MockGameModel others(nullptr, 300);
  RetroArchGameModel roms(directory.path() + QStringLiteral("/omakade.sqlite3"));
  roms.refreshFromRoots({root});
  QCOMPARE(roms.rowCount(), 3);
  ConsolePortalModel portals;
  portals.addRomModel(&roms);
  QCOMPARE(portals.rowCount(), 2);

  UnifiedGameModel games;
  games.addSourceModel(&others);
  games.addSourceModel(&roms);
  games.addSourceModel(&portals);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  QCOMPARE(library.rowCount(), 302);

  const auto onlyRomsFor = [&library](const QString& system, int expected) {
    QCOMPARE(library.rowCount(), expected);
    QStringList titles;
    for (int row = 0; row < library.rowCount(); ++row) {
      const QVariantMap game = library.get(row);
      QCOMPARE(game.value(QStringLiteral("source")).toString(), QStringLiteral("RetroArch"));
      QCOMPARE(game.value(QStringLiteral("system")).toString(), system);
      QVERIFY(!game.value(QStringLiteral("isPortal")).toBool());
      titles.append(game.value(QStringLiteral("title")).toString());
    }
    QStringList sorted = titles;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
      return a.localeAwareCompare(b) < 0;
    });
    QCOMPARE(titles, sorted);
  };

  library.setConsoleFilter(QStringLiteral("snes"));
  onlyRomsFor(QStringLiteral("snes"), 2);
  library.setConsoleFilter(QStringLiteral("nes"));
  onlyRomsFor(QStringLiteral("nes"), 1);
  library.setConsoleFilter({});
  QCOMPARE(library.rowCount(), 302);
  library.setConsoleFilter(QStringLiteral("snes"));
  onlyRomsFor(QStringLiteral("snes"), 2);

  // Picking a source chip leaves the console and never mixes the two views.
  library.setSourceFilter(QStringLiteral("Demo"));
  QVERIFY(library.consoleFilter().isEmpty());
  QCOMPARE(library.rowCount(), 300);
  library.setSourceFilter({});
  library.setConsoleFilter(QStringLiteral("snes"));
  onlyRomsFor(QStringLiteral("snes"), 2);

  // A search inside a console stays inside the console.
  library.setSearchText(QStringLiteral("Mario"));
  onlyRomsFor(QStringLiteral("snes"), 1);
  library.setSearchText({});
  onlyRomsFor(QStringLiteral("snes"), 2);

  // Scans and cover updates finishing while a console is open must not leak
  // other sources back in: the unified model resets, the proxy re-filters.
  roms.refreshFromRoots({root});
  onlyRomsFor(QStringLiteral("snes"), 2);
  others.toggleFavorite(0);
  onlyRomsFor(QStringLiteral("snes"), 2);
  games.setSourceEnabled(QStringLiteral("Demo"), false);
  onlyRomsFor(QStringLiteral("snes"), 2);
  games.setSourceEnabled(QStringLiteral("Demo"), true);
  onlyRomsFor(QStringLiteral("snes"), 2);

  // Flattening portals shows everything as first-class tiles again.
  library.setConsolePortalsEnabled(false);
  QVERIFY(library.consoleFilter().isEmpty());
  QCOMPARE(library.rowCount(), 303);
  library.setConsolePortalsEnabled(true);
  QCOMPARE(library.rowCount(), 302);
}

// Diagnostics against real dumps on this machine. Skipped unless the
// OMAKADE_PROBE_SWITCH (NSP/XCI) or OMAKADE_PROBE_WUA (.wua) variables are set.
void CoreTests::probeEmbeddedArtwork() {
  const QString rom = qEnvironmentVariable("OMAKADE_PROBE_SWITCH");
  const QString wua = qEnvironmentVariable("OMAKADE_PROBE_WUA");
  if (qEnvironmentVariableIsSet("OMAKADE_PROBE_SCANNERS")) {
    for (const RyujinxGameRecord& game : RyujinxScanner::scan(RyujinxScanner::discoverRoots()).games)
      qWarning().noquote() << QStringLiteral("RYUJINX %1 | %2 | %3").arg(game.titleId, game.title, game.coverPath);
    for (const CemuGameRecord& game : CemuScanner::scan(CemuScanner::discoverRoots()).games)
      qWarning().noquote() << QStringLiteral("CEMU %1 | %2 | %3").arg(game.titleId, game.title, game.coverPath);
    const DolphinScanResult dolphin = DolphinScanner::scan(DolphinScanner::discoverRoots(), {}, true);
    qWarning().noquote() << QStringLiteral("DOLPHIN installed=%1 roots=%2 folders=%3").arg(DolphinScanner::dolphinInstalled()).arg(dolphin.roots.join(",")).arg(dolphin.folders.join(","));
    for (const DolphinGameRecord& game : dolphin.games)
      qWarning().noquote() << QStringLiteral("DOLPHIN %1 | %2 | %3 | %4 | cover=%5").arg(game.discId, game.title, game.platform, game.path, game.coverPath);
    for (const RomFolder& folder : RomFolderScanner::discoverAutoFolders())
      qWarning().noquote() << QStringLiteral("ROMFOLDER %1 -> %2").arg(folder.path, folder.system);
    return;
  }
  if (rom.isEmpty() && wua.isEmpty()) QSKIP("set OMAKADE_PROBE_SWITCH or OMAKADE_PROBE_WUA");
  for (const QString& path : rom.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
    QElapsedTimer timer; timer.start();
    const SwitchTitleInfo info = SwitchTitleReader::read(path);
    qWarning().noquote() << QStringLiteral("SWITCH %1 | id=%2 | title=%3 | icon=%4 bytes | %5 | %6 ms")
        .arg(QFileInfo(path).fileName(), info.titleId, info.title).arg(info.icon.size()).arg(info.failure).arg(timer.elapsed());
    for (const QString& note : info.notes) qWarning().noquote() << "   " << note;
    if (info.hasIcon()) {
      QImage image = QImage::fromData(info.icon);
      qWarning().noquote() << QStringLiteral("   decoded %1x%2").arg(image.width()).arg(image.height());
    }
  }
  if (!wua.isEmpty()) {
    QElapsedTimer timer; timer.start();
    auto reader = ZArchiveReader::open(wua);
    if (!reader) { qWarning() << "WUA: could not open"; return; }
    for (const QString& name : reader->list(QString{})) {
      qWarning().noquote() << QStringLiteral("WUA root: %1 (dir=%2)").arg(name).arg(reader->isDirectory(name));
      const QByteArray icon = reader->readFile(name + QStringLiteral("/meta/iconTex.tga"), 4 * 1024 * 1024);
      const QByteArray meta = reader->readFile(name + QStringLiteral("/meta/meta.xml"), 1024 * 1024);
      const QImage image = decodeTgaImage(icon);
      qWarning().noquote() << QStringLiteral("   tga header %1").arg(QString::fromLatin1(icon.left(18).toHex(' ')));
      qWarning().noquote() << QStringLiteral("   iconTex=%1 bytes decoded %2x%3, meta.xml=%4 bytes, longname=%5")
          .arg(icon.size()).arg(image.width()).arg(image.height()).arg(meta.size())
          .arg(QString::fromUtf8(meta).section("<longname_en", 1, 1).section('>', 1, 1).section('<', 0, 0));
    }
    qWarning().noquote() << QStringLiteral("   %1 ms").arg(timer.elapsed());
  }
}

namespace {
QByteArray le32(quint32 value) { QByteArray b(4, '\0'); qToLittleEndian(value, b.data()); return b; }
QByteArray le64(quint64 value) { QByteArray b(8, '\0'); qToLittleEndian(value, b.data()); return b; }
QByteArray be32(quint32 value) { QByteArray b(4, '\0'); qToBigEndian(value, b.data()); return b; }
QByteArray be64(quint64 value) { QByteArray b(8, '\0'); qToBigEndian(value, b.data()); return b; }
QByteArray padded(QByteArray bytes, qsizetype alignment) {
  while (bytes.size() % alignment != 0) bytes.append('\0');
  return bytes;
}
QByteArray randomBytes(int count) {
  QByteArray bytes(count, '\0');
  for (char& byte : bytes) byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
  return bytes;
}
QByteArray smallJpeg() {
  QImage image(48, 48, QImage::Format_RGB32);
  image.fill(Qt::magenta);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "JPEG");
  return bytes;
}
}  // namespace

// Builds an NSP the way the console tools lay one out: a PFS0 holding one
// control NCA whose header is XTS-encrypted with a header key and whose RomFS
// section is CTR-encrypted with a key-area key, all generated for this test.
void CoreTests::switchTitleReaderReadsSyntheticDump() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QByteArray headerKey = randomBytes(32);
  const QByteArray keyAreaKey = randomBytes(16);
  const QByteArray ctrKey = randomBytes(16);
  const QByteArray sectionCtr = randomBytes(8);
  const QString keysPath = temp.filePath("prod.keys");
  writeFile(keysPath, QStringLiteral("header_key = %1\nkey_area_key_application_00 = %2\n")
                          .arg(QString::fromLatin1(headerKey.toHex()), QString::fromLatin1(keyAreaKey.toHex()))
                          .toUtf8());

  // RomFS with control.nacp and one icon in the root directory.
  const QByteArray icon = smallJpeg();
  QByteArray nacp(0x4000, '\0');
  const QByteArray name = QStringLiteral("Synthetic Adventure").toUtf8();
  nacp.replace(0, name.size(), name);
  nacp.replace(0x3038, 8, le64(0x0100ABCDEF123000ULL));
  QByteArray fileData = padded(nacp, 0x10) + padded(icon, 0x10);
  QByteArray fileTable;
  const auto fileEntry = [&](const QByteArray& entryName, quint64 offset, quint64 size, quint32 sibling) {
    QByteArray entry = le32(0) + le32(sibling) + le64(offset) + le64(size) + le32(0xFFFFFFFF) + le32(entryName.size());
    entry += padded(entryName, 4);
    return entry;
  };
  const QByteArray first = fileEntry("control.nacp", 0, nacp.size(), 0);
  fileTable += fileEntry("control.nacp", 0, nacp.size(), first.size());
  fileTable += fileEntry("icon_AmericanEnglish.dat", padded(nacp, 0x10).size(), icon.size(), 0xFFFFFFFF);
  const QByteArray dirTable = le32(0) + le32(0xFFFFFFFF) + le32(0) + le32(0xFFFFFFFF) + le32(0xFFFFFFFF) + le32(0);
  const quint64 dirHashOffset = 0x50;
  const quint64 dirTableOffset = dirHashOffset + 0x10;
  const quint64 fileHashOffset = dirTableOffset + dirTable.size();
  const quint64 fileTableOffset = fileHashOffset + 0x10;
  const quint64 dataOffset = (fileTableOffset + fileTable.size() + 0xF) & ~quint64(0xF);
  QByteArray romfs = le64(0x50) + le64(dirHashOffset) + le64(0x10) + le64(dirTableOffset) + le64(dirTable.size()) +
                     le64(fileHashOffset) + le64(0x10) + le64(fileTableOffset) + le64(fileTable.size()) + le64(dataOffset);
  romfs += QByteArray(0x10, '\xFF') + dirTable + QByteArray(0x10, '\xFF') + fileTable;
  romfs = padded(romfs, 0x10) + fileData;
  QCOMPARE(static_cast<quint64>(romfs.indexOf(fileData)), dataOffset);
  romfs = padded(romfs, 0x200);

  // Plain NCA header, then encrypt the key area and the header itself.
  QByteArray header(0xC00, '\0');
  header.replace(0x200, 4, "NCA3");
  header[0x205] = 2;  // control
  header.replace(0x240, 4, le32(6));
  header.replace(0x244, 4, le32(6 + romfs.size() / 0x200));
  QByteArray keyArea(0x40, '\0');
  keyArea.replace(0x20, 16, ctrKey);
  header.replace(0x300, 0x40, SwitchCrypto::ecb(keyAreaKey, keyArea, true));
  header[0x400 + 2] = 0;  // RomFS
  header[0x400 + 3] = 3;
  header[0x400 + 4] = 3;  // CTR
  header.replace(0x408, 4, "IVFC");
  header.replace(0x400 + 0x90, 8, le64(0));
  header.replace(0x540, 8, sectionCtr);
  const QByteArray encryptedHeader = SwitchCrypto::xtsSectors(headerKey, header, 0, true);
  QCOMPARE(encryptedHeader.size(), 0xC00);
  QCOMPARE(SwitchCrypto::xtsSectors(headerKey, encryptedHeader, 0, false), header);
  QByteArray iv(16, '\0');
  for (int index = 0; index < 8; ++index) iv[index] = sectionCtr.at(7 - index);
  qToBigEndian<quint64>(0xC00 >> 4, iv.data() + 8);
  const QByteArray nca = encryptedHeader + SwitchCrypto::ctr(ctrKey, iv, romfs);

  // PFS0 container.
  const QByteArray ncaName = QByteArray("control.nca") + '\0';
  const QByteArray strings = padded(ncaName, 0x20);
  const QByteArray nsp = QByteArray("PFS0") + le32(1) + le32(strings.size()) + le32(0) + le64(0) + le64(nca.size()) +
                         le32(0) + le32(0) + strings + nca;
  const QString nspPath = temp.filePath("game.nsp");
  writeFile(nspPath, nsp);

  const SwitchTitleInfo info = SwitchTitleReader::read(nspPath, {keysPath}, {});
  QVERIFY2(info.failure.isEmpty(), qPrintable(info.failure + " " + info.notes.join(" | ")));
  QCOMPARE(info.title, QStringLiteral("Synthetic Adventure"));
  QCOMPARE(info.titleId, QStringLiteral("0100ABCDEF123000"));
  QCOMPARE(info.icon, icon);

  // The cache writes the icon once and reuses it.
  const QString cacheRoot = temp.filePath("cache");
  QVERIFY(QFileInfo::exists(SwitchTitleReader::cachedIcon(nspPath, info.titleId, cacheRoot, nullptr)) == false);
  const SwitchTitleInfo unreadable = SwitchTitleReader::read(nspPath, {temp.filePath("missing.keys")}, {});
  QVERIFY(!unreadable.hasIcon());
  QVERIFY(unreadable.failure.contains(QStringLiteral("prod.keys")));
  const SwitchTitleInfo wrongKeys = SwitchTitleReader::read(
      nspPath, {[&] {
        const QString path = temp.filePath("wrong.keys");
        writeFile(path, QStringLiteral("header_key = %1\n").arg(QString::fromLatin1(randomBytes(32).toHex())).toUtf8());
        return path;
      }()}, {});
  QVERIFY(!wrongKeys.hasIcon());
}

void CoreTests::zarchiveReaderAndTgaDecodeSyntheticArchive() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  // A 2x2 bottom-left origin 32-bit TGA: red, green / blue, white.
  QByteArray tga(18, '\0');
  tga[2] = 2;
  tga[12] = 2;
  tga[14] = 2;
  tga[16] = 32;
  tga[17] = 8;
  const auto bgra = [](int r, int g, int b, int a) { return QByteArray().append(char(b)).append(char(g)).append(char(r)).append(char(a)); };
  tga += bgra(0, 0, 255, 255) + bgra(255, 255, 255, 255);  // bottom row first
  tga += bgra(255, 0, 0, 255) + bgra(0, 255, 0, 255);
  tga += QByteArray("TRUEVISION-XFILE.\0", 18);
  const QImage decoded = decodeTgaImage(tga);
  QCOMPARE(decoded.size(), QSize(2, 2));
  QCOMPARE(decoded.pixel(0, 0), qRgb(255, 0, 0));
  QCOMPARE(decoded.pixel(1, 0), qRgb(0, 255, 0));
  QCOMPARE(decoded.pixel(0, 1), qRgb(0, 0, 255));
  QCOMPARE(decoded.pixel(1, 1), qRgb(255, 255, 255));

  // Archive: /00050000ABCDEF12_v0/meta/{iconTex.tga, meta.xml} stored in raw
  // 64 KiB blocks, with the footer the ZArchive tools write.
  const QByteArray metaXml = QByteArrayLiteral("<menu><longname_en>Synthetic\nTitle</longname_en></menu>");
  QByteArray stream = tga + metaXml;
  const qsizetype block = 64 * 1024;
  stream = padded(stream, block);
  const int blockCount = stream.size() / block;
  QByteArray records = be64(0);
  for (int index = 0; index < 16; ++index) records += QByteArray(2, index < blockCount ? '\xFF' : '\0');
  QByteArray names;
  const auto addName = [&names](const QByteArray& value) {
    const quint32 offset = names.size();
    names.append(static_cast<char>(value.size()));
    names += value;
    return offset;
  };
  const quint32 rootName = addName("");
  const quint32 titleName = addName("00050000ABCDEF12_v0");
  const quint32 metaName = addName("meta");
  const quint32 iconName = addName("iconTex.tga");
  const quint32 xmlName = addName("meta.xml");
  const auto dirNode = [](quint32 nameOffset, quint32 first, quint32 count) {
    return be32(nameOffset) + be32(first) + be32(count) + be32(0);
  };
  const auto fileNode = [](quint32 nameOffset, quint64 offset, quint64 size) {
    return be32(0x80000000u | nameOffset) + be32(quint32(offset)) + be32(quint32(size)) +
           be32(quint32((offset >> 32) & 0xFFFF) | quint32((size >> 16) & 0xFFFF0000));
  };
  QByteArray tree = dirNode(rootName, 1, 1) + dirNode(titleName, 2, 1) + dirNode(metaName, 3, 2) +
                    fileNode(iconName, 0, tga.size()) + fileNode(xmlName, tga.size(), metaXml.size());
  QByteArray archive = stream + records + names + tree;
  const quint64 recordsOffset = stream.size();
  const quint64 namesOffset = recordsOffset + records.size();
  const quint64 treeOffset = namesOffset + names.size();
  QByteArray footer = be64(0) + be64(stream.size()) + be64(recordsOffset) + be64(records.size()) + be64(namesOffset) +
                      be64(names.size()) + be64(treeOffset) + be64(tree.size()) + be64(0) + be64(0) + be64(0) + be64(0);
  footer += QByteArray(32, '\0');
  footer += be64(archive.size() + footer.size() + 8 + 4 + 4 + 32 - 32);
  footer += be32(0x61bf3a01) + be32(0x169f52d6);
  archive += footer;
  // Fix the total size now that the footer length is known.
  archive.replace(archive.size() - 16, 8, be64(archive.size()));
  const QString path = temp.filePath("game.wua");
  writeFile(path, archive);

  auto reader = ZArchiveReader::open(path);
  QVERIFY(reader != nullptr);
  QCOMPARE(reader->list(QString{}), QStringList{QStringLiteral("00050000ABCDEF12_v0")});
  QVERIFY(reader->isDirectory(QStringLiteral("00050000abcdef12_v0/META")));
  QCOMPARE(reader->readFile(QStringLiteral("00050000ABCDEF12_v0/meta/iconTex.tga"), 1024 * 1024), tga);
  QCOMPARE(reader->readFile(QStringLiteral("00050000ABCDEF12_v0/meta/meta.xml"), 1024 * 1024), metaXml);
  QVERIFY(reader->readFile(QStringLiteral("00050000ABCDEF12_v0/meta/missing"), 1024).isEmpty());
  QVERIFY(reader->readFile(QStringLiteral("00050000ABCDEF12_v0/meta/meta.xml"), 4).isEmpty());
  QVERIFY(ZArchiveReader::open(temp.filePath("prod.keys")) == nullptr);

  // The Cemu scanner reads the same archive: title id and name from the
  // archive, icon converted to PNG in the cache. Test mode keeps that cache
  // out of the real one.
  QStandardPaths::setTestModeEnabled(true);
  const auto restoreCache = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
  const QString cemuRoot = temp.filePath("cemu");
  writeFile(cemuRoot + QStringLiteral("/settings.xml"),
            QStringLiteral("<content><GamePaths><Entry>%1</Entry></GamePaths></content>").arg(temp.path()).toUtf8());
  const CemuScanResult scan = CemuScanner::scan({cemuRoot});
  QCOMPARE(scan.games.size(), 1);
  QCOMPARE(scan.games.first().titleId, QStringLiteral("00050000ABCDEF12"));
  QCOMPARE(scan.games.first().title, QStringLiteral("Synthetic - Title"));
  QVERIFY2(scan.games.first().coverPath.endsWith(QStringLiteral("/wiiu/00050000ABCDEF12.png")), qPrintable(scan.games.first().coverPath));
  QCOMPARE(QImage(scan.games.first().coverPath).size(), QSize(2, 2));
}

namespace {
QByteArray discHeaderBytes(const QByteArray& id, const QByteArray& internalName, bool wii) {
  QByteArray header(0x80, '\0');
  header.replace(0, id.size(), id);
  if (wii) {
    header.replace(0x18, 4, QByteArray("\x5D\x1C\x9E\xA3", 4));
  } else {
    header.replace(0x1C, 4, QByteArray("\xC2\x33\x9F\x3D", 4));
  }
  header.replace(0x20, internalName.size(), internalName);
  return header;
}

QByteArray rvzBytes(const QByteArray& id, const QByteArray& internalName, bool wii) {
  QByteArray file(0x48, '\0');
  file.replace(0, 4, "RVZ\x01");
  QByteArray header2(0x10, '\0');
  header2[3] = wii ? 2 : 1;
  file += header2 + discHeaderBytes(id, internalName, wii);
  file += QByteArray(0x100, '\0');
  return file;
}

void createDolphinFixture(const QString& root, const QString& games) {
  writeFile(root + QStringLiteral("/Dolphin.ini"),
            QStringLiteral("[General]\nISOPaths = 1\nISOPath0 = %1\nRecursiveISOPaths = True\n").arg(games).toUtf8());
  writeFile(games + QStringLiteral("/Legend of Zelda, The - The Wind Waker (USA).rvz"),
            rvzBytes("GZLE01", "ZELDA WIND WAKER", false));
  writeFile(games + QStringLiteral("/Legend of Zelda, The - The Wind Waker (USA).png"), "cover");
  writeFile(games + QStringLiteral("/nested/Mario Kart Wii (USA).wbfs"),
            QByteArray("WBFS") + QByteArray(0x1FC, '\0') + discHeaderBytes("RMCE01", "MARIO KART WII", true));
  writeFile(games + QStringLiteral("/Plain Disc (USA).iso"), discHeaderBytes("GPLE01", "PLAIN DISC", false) + QByteArray(0x100, '\0'));
  writeFile(games + QStringLiteral("/notes.txt"), "not a game");
}
}  // namespace

void CoreTests::dolphinScannerReadsDiscHeadersAndLaunches() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/dolphin-emu");
  const QString games = directory.path() + QStringLiteral("/GameCube");
  createDolphinFixture(root, games);

  const DolphinScanResult result = DolphinScanner::scan({root});
  QVERIFY2(!result.incomplete, qPrintable(result.warnings.join(" | ")));
  QCOMPARE(result.roots, QStringList({root}));
  QCOMPARE(result.games.size(), 3);
  QMap<QString, DolphinGameRecord> byId;
  for (const DolphinGameRecord& game : result.games) byId.insert(game.discId, game);
  QCOMPARE(byId.value("GZLE01").title, QStringLiteral("The Legend of Zelda - The Wind Waker"));
  QCOMPARE(byId.value("GZLE01").platform, QStringLiteral("GameCube"));
  QVERIFY(byId.value("GZLE01").coverPath.endsWith(QStringLiteral(".png")));
  QCOMPARE(byId.value("RMCE01").title, QStringLiteral("Mario Kart Wii"));
  QCOMPARE(byId.value("RMCE01").platform, QStringLiteral("Wii"));
  QCOMPARE(byId.value("GPLE01").platform, QStringLiteral("GameCube"));
  QVERIFY(byId.value("GPLE01").coverPath.isEmpty());

  // Explicit folders work without any Dolphin configuration.
  const DolphinScanResult direct = DolphinScanner::scan({}, {games});
  QCOMPARE(direct.games.size(), 3);
  QVERIFY(direct.roots.isEmpty());

  QCOMPARE(DolphinGameModel::gameTdbCoverUrl(QStringLiteral("GZLE01")), QStringLiteral("https://art.gametdb.com/wii/cover/US/GZLE01.png"));
  QCOMPARE(DolphinGameModel::gameTdbCoverUrl(QStringLiteral("GZLP01")), QStringLiteral("https://art.gametdb.com/wii/cover/EN/GZLP01.png"));
  QCOMPARE(DolphinGameModel::gameTdbCoverUrl(QStringLiteral("GZLJ01")), QStringLiteral("https://art.gametdb.com/wii/cover/JA/GZLJ01.png"));
  QVERIFY(DolphinGameModel::gameTdbCoverUrl(QStringLiteral("../x")).isEmpty());

  const LaunchCommand native = GameLauncher::dolphinCommand(byId.value("GZLE01").path, QStringLiteral("dolphin-emu"), false);
  QCOMPARE(native.program, QStringLiteral("dolphin-emu"));
  QCOMPARE(native.arguments, (QStringList{QStringLiteral("-b"), QStringLiteral("-e"), byId.value("GZLE01").path}));
  const LaunchCommand flatpak = GameLauncher::dolphinCommand(QStringLiteral("path:") + byId.value("RMCE01").path, QString{}, true);
  QCOMPARE(flatpak.program, QStringLiteral("flatpak"));
  QCOMPARE(flatpak.arguments.first(), QStringLiteral("run"));
  QCOMPARE(flatpak.arguments.last(), byId.value("RMCE01").path);
  QVERIFY(!GameLauncher::dolphinCommand(games + QStringLiteral("/notes.txt"), QStringLiteral("dolphin-emu"), false).isValid());
  QVERIFY(!GameLauncher::dolphinCommand(QStringLiteral("relative.rvz"), QStringLiteral("dolphin-emu"), false).isValid());
}

void CoreTests::dolphinModelIsRepeatableAndPreservesLocalState() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path() + QStringLiteral("/dolphin-emu");
  const QString games = directory.path() + QStringLiteral("/GameCube");
  createDolphinFixture(root, games);
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  {
    DolphinGameModel model(database);
    model.refreshFromRoots({root});
    QCOMPARE(model.rowCount(), 3);
    model.toggleFavorite(0);
    model.refreshFromRoots({root});
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.data(model.index(0), GameRoles::Favorite).toBool());
    QCOMPARE(model.data(model.index(0), GameRoles::Source).toString(), QStringLiteral("Dolphin"));
    QVERIFY(model.data(model.index(0), GameRoles::Subtitle).toString().startsWith(QStringLiteral("Dolphin · ")));
    QVERIFY(!model.data(model.index(0), GameRoles::LaunchTarget).toString().isEmpty());
  }
  DolphinGameModel reloaded(database);
  QCOMPARE(reloaded.rowCount(), 3);
  QVERIFY(reloaded.data(reloaded.index(0), GameRoles::Favorite).toBool());
  // A scan that cannot read its folder keeps the cached library.
  QVERIFY(QDir(games).removeRecursively());
  reloaded.refreshFromRoots({root});
  QCOMPARE(reloaded.rowCount(), 3);
  QVERIFY(reloaded.statusText().contains(QStringLiteral("interrupted")));
}

void CoreTests::dreamcastFoldersBecomeAPortal() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString folder = directory.path() + QStringLiteral("/Dreamcast");
  writeFile(folder + QStringLiteral("/Crazy Taxi (USA)/Crazy Taxi (USA).gdi"), "gdi");
  writeFile(folder + QStringLiteral("/Crazy Taxi (USA)/track01.bin"), "bin");
  writeFile(folder + QStringLiteral("/Crazy Taxi (USA)/track02.raw"), "raw");
  writeFile(folder + QStringLiteral("/Jet Grind Radio (USA)/Jet Grind Radio (USA).gdi"), "gdi");
  writeFile(folder + QStringLiteral("/Jet Grind Radio (USA)/track03.bin"), "bin");
  RetroArchGameModel roms(directory.path() + QStringLiteral("/omakade.sqlite3"));
  roms.refreshFromSources({}, {folder + QStringLiteral("|dreamcast")});
  QCOMPARE(roms.rowCount(), 2);
  QCOMPARE(roms.data(roms.index(0), GameRoles::System).toString(), QStringLiteral("dreamcast"));
  QCOMPARE(roms.data(roms.index(0), GameRoles::Title).toString(), QStringLiteral("Crazy Taxi"));
  ConsolePortalModel portals;
  portals.addRomModel(&roms);
  QCOMPARE(portals.rowCount(), 1);
  QCOMPARE(portals.data(portals.index(0), GameRoles::Title).toString(), QStringLiteral("Sega Dreamcast"));
  QVERIFY(!ConsoleCatalog::isDedicatedSource(QStringLiteral("dreamcast")));
  QVERIFY(ConsoleCatalog::isDedicatedSource(QStringLiteral("gamecube")));
  // No playlist core: the Flycast core hosts the game, with RetroArch's menu never shown.
  QVERIFY(roms.data(roms.index(0), GameRoles::LaunchTarget).toString().isEmpty());  // no playlist core
  const LaunchCommand command = GameLauncher::resolvedCartridgeCommand(
      roms.data(roms.index(0), GameRoles::InstallPath).toString(), QString{}, false, false, QString{},
      QStringLiteral("/usr/lib/libretro/flycast_libretro.so"));
  QCOMPARE(command.program, QStringLiteral("retroarch"));
  QCOMPARE(command.arguments.at(1), QStringLiteral("/usr/lib/libretro/flycast_libretro.so"));
}

void CoreTests::consoleLayoutsPinAndExpand() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  // Three SNES carts, one N64 cart, two GameCube discs, and a large "PC" source.
  const QString roms = directory.path() + QStringLiteral("/roms");
  for (const QString& name : {QStringLiteral("snes/A.sfc"), QStringLiteral("snes/B.sfc"), QStringLiteral("snes/C.sfc"),
                              QStringLiteral("n64/Mario.z64")}) {
    writeFile(roms + QLatin1Char('/') + name, "rom");
  }
  const QString dolphinRoot = directory.path() + QStringLiteral("/dolphin-emu");
  const QString discs = directory.path() + QStringLiteral("/GameCube");
  createDolphinFixture(dolphinRoot, discs);
  QVERIFY(QFile::remove(discs + QStringLiteral("/nested/Mario Kart Wii (USA).wbfs")));
  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  MockGameModel pc(nullptr, 50);
  RetroArchGameModel carts(database);
  carts.refreshFromSources({}, {roms + QStringLiteral("/snes|snes"), roms + QStringLiteral("/n64|n64")});
  QCOMPARE(carts.rowCount(), 4);
  DolphinGameModel cubes(database);
  cubes.refreshFromRoots({dolphinRoot});
  QCOMPARE(cubes.rowCount(), 2);

  ConsolePortalModel portals;
  portals.addRomModel(&carts);
  portals.addRomModel(&cubes);
  AppSettings settings(directory.path() + QStringLiteral("/config.toml"));
  QCOMPARE(settings.consoleLayout(QStringLiteral("snes")), QStringLiteral("follow"));
  QCOMPARE(settings.consoleLayout(QStringLiteral("gamecube")), QStringLiteral("follow"));
  portals.setCardSystems(settings.cardSystems());
  QCOMPARE(portals.rowCount(), 3);  // snes, n64, gamecube

  UnifiedGameModel games(database);
  games.addSourceModel(&pc);
  games.addSourceModel(&carts);
  games.addSourceModel(&cubes);
  games.addSourceModel(&portals);
  LibraryFilterModel library;
  library.setSourceModel(&games);
  library.setConsolePortalsEnabled(true);
  library.setCardSystems(settings.cardSystems());
  const auto count = [&library](const QString& source) {
    int total = 0;
    for (int row = 0; row < library.rowCount(); ++row)
      if (library.get(row).value("source").toString() == source && !library.get(row).value("isPortal").toBool()) ++total;
    return total;
  };
  const auto portalCount = [&library] {
    int total = 0;
    for (int row = 0; row < library.rowCount(); ++row) total += library.get(row).value("isPortal").toBool();
    return total;
  };
  QCOMPARE(library.rowCount(), 50 + 3);  // PC games and all three consoles
  QCOMPARE(count("Dolphin"), 0);

  // GameCube moves behind a card; the card is built from Dolphin games and carries that source.
  settings.setConsoleLayout(QStringLiteral("gamecube"), QStringLiteral("card"));
  portals.setCardSystems(settings.cardSystems());
  library.setCardSystems(settings.cardSystems());
  QCOMPARE(portals.rowCount(), 3);
  QCOMPARE(count("Dolphin"), 0);
  QCOMPARE(portalCount(), 3);
  AppSettings reloaded(directory.path() + QStringLiteral("/config.toml"));
  QCOMPARE(reloaded.consoleLayout(QStringLiteral("gamecube")), QStringLiteral("card"));
  QCOMPARE(reloaded.consoleLayouts(), QStringList{QStringLiteral("gamecube=card")});
  settings.setConsoleLayout(QStringLiteral("gamecube"), QStringLiteral("library"));
  QCOMPARE(settings.consoleLayouts(), QStringList{QStringLiteral("gamecube=library")});
  settings.setConsoleLayout(QStringLiteral("gamecube"), QStringLiteral("card"));

  // A single pinned game holds a library spot while its system stays a card.
  library.setConsoleFilter(QStringLiteral("gamecube"));
  QCOMPARE(library.rowCount(), 2);
  const int zelda = library.indexOf(QStringLiteral("Dolphin"), QString{}, QStringLiteral("GZLE01"));
  QVERIFY(zelda >= 0);
  QVERIFY(library.setPinned(zelda, true));
  library.setConsoleFilter({});
  QCOMPARE(count("Dolphin"), 1);
  QCOMPARE(portalCount(), 3);
  QVERIFY(library.get(library.indexOf(QStringLiteral("Dolphin"), QString{}, QStringLiteral("GZLE01"))).value("pinned").toBool());
  UnifiedGameModel reopened(database);
  reopened.addSourceModel(&cubes);
  QVERIFY(reopened.data(reopened.index(0), GameRoles::Pinned).toBool() ||
          reopened.data(reopened.index(1), GameRoles::Pinned).toBool());

  // Source filters combine, and the console card follows its games' source.
  library.setSourceFilters({QStringLiteral("Dolphin")});
  QCOMPARE(count("Dolphin"), 1);
  QCOMPARE(portalCount(), 1);  // the GameCube card
  library.toggleSource(QStringLiteral("Demo"));
  QCOMPARE(library.rowCount(), 50 + 1 + 1);
  QVERIFY(library.sourceSelected(QStringLiteral("demo")));
  library.setSourceFilter(QStringLiteral("Emulated"));
  QCOMPARE(library.sourceFilter(), QStringLiteral("Emulated"));
  QVERIFY(library.sourcesSelected(LibraryFilterModel::emulatorSources()));
  QCOMPARE(count("Demo"), 0);
  QCOMPARE(portalCount(), 3);
  library.toggleSources(LibraryFilterModel::emulatorSources());
  QVERIFY(library.sourceFilters().isEmpty());

  // Games view ignores the old size cap. Only explicit overrides keep cards.
  QVERIFY(library.hasConsoleCards());
  library.setConsoleExpandLimit(2);
  library.setFixedCardSystems(settings.fixedCardSystems());
  library.setExpandConsoles(true);
  QCOMPARE(count("Dolphin"), 1);  // explicit card override, plus pinned Zelda
  QCOMPARE(count("RetroArch"), 4);
  QCOMPARE(portalCount(), 1);
  settings.setConsoleLayout(QStringLiteral("gamecube"), QStringLiteral("follow"));
  library.setFixedCardSystems(settings.fixedCardSystems());
  QVERIFY(settings.consoleLayouts().isEmpty());
  QCOMPARE(count("Dolphin"), 2);
  QCOMPARE(portalCount(), 0);
  QCOMPARE(count("RetroArch"), 4);
  library.setExpandConsoles(false);
  QCOMPARE(portalCount(), 3);
  QCOMPARE(count("RetroArch"), 0);

  // Inside a console, every game of that system shows, pinned or not, and search stays inside.
  library.setConsoleFilter(QStringLiteral("snes"));
  QCOMPARE(library.rowCount(), 3);
  library.setSearchText(QStringLiteral("B"));
  QCOMPARE(library.rowCount(), 1);
  library.setSearchText({});
  library.setSourceFilters({QStringLiteral("Demo")});
  QVERIFY(library.consoleFilter().isEmpty());
  QCOMPARE(library.rowCount(), 50);
  // Favorites and search count the games inside cards, not the portal's own flags.
  library.setSourceFilters({});
  library.setConsoleFilter(QStringLiteral("snes"));
  library.toggleFavorite(0);
  library.setConsoleFilter({});
  library.setMode(LibraryFilterModel::Mode::Favorites);
  QCOMPARE(portalCount(),1);
  for (int row = 0; row < library.rowCount(); ++row)
    if (library.get(row).value("isPortal").toBool()) QCOMPARE(library.get(row).value("subtitle").toString(),QStringLiteral("1 game"));
  library.setMode(LibraryFilterModel::Mode::All);
  settings.setConsoleLayout(QStringLiteral("snes"),QStringLiteral("library"));
  portals.setCardSystems(settings.cardSystems());
  library.setCardSystems(settings.cardSystems());
  QCOMPARE(count("RetroArch"),3);
  QCOMPARE(portalCount(),2);
  library.setExpandConsoles(true);
  QCOMPARE(count("RetroArch"),4);
  QCOMPARE(portalCount(),0);

}

void CoreTests::metadataMatchingKeepsPlatformsAndEditions() {
  QCOMPARE(GameMetadata::normalizedTitle("Chrono Trigger (USA)"), QStringLiteral("chrono trigger"));
  QVERIFY(GameMetadata::normalizedTitle("Metroid Prime") != GameMetadata::normalizedTitle("Metroid Prime Remastered"));
  QVERIFY(GameMetadata::normalizedTitle("Final Fantasy VII") != GameMetadata::normalizedTitle("Final Fantasy VIII"));
  QVERIFY(GameMetadata::normalizedTitle("Super Mario World") != GameMetadata::normalizedTitle("Super \"Mario\" World"));
  QVERIFY(!GameMetadata::searchQuery("Super Mario World (USA)", "snes").contains("USA"));
  QVERIFY(GameMetadata::searchQuery("Mario", "unknown-console").isEmpty());
  QVERIFY(GameMetadata::searchQuery("Mario", "gamecube").contains("platforms = (21)"));
  const auto matches = GameMetadata::parseMatches(R"json([
    {"id":1,"name":"Metroid Prime","platforms":[21],"total_rating":89.5,"total_rating_count":300},
    {"id":2,"name":"Metroid Prime Remastered","platforms":[130],"total_rating":94,"total_rating_count":90},
    {"id":3,"name":"Unrated","platforms":[21]},
    {"id":4,"name":"Bad rating","platforms":[21],"total_rating":999,"total_rating_count":1},
    {"id":0,"name":"Invalid","platforms":[21]}])json",21);
  QCOMPARE(matches.size(),3);
  QCOMPARE(matches.at(0).toMap().value("rating").toInt(),90);
  QCOMPARE(matches.at(1).toMap().value("rating").toInt(),-1);
  QCOMPARE(matches.at(2).toMap().value("rating").toInt(),-1);
  QVERIFY(GameMetadata::parseMatches("{broken",21).isEmpty());
  const auto covers = GameMetadata::parseCovers(R"json({"success":true,"data":[
    {"id":1,"width":600,"height":900,"url":"https://cdn2.steamgriddb.com/grid/good.png"},
    {"id":2,"width":512,"height":512,"url":"https://cdn2.steamgriddb.com/grid/square.png"},
    {"id":3,"width":600,"height":900,"nsfw":true,"url":"https://cdn2.steamgriddb.com/grid/flagged.png"},
    {"id":4,"width":600,"height":900,"url":"https://untrusted.example/image.png"}]})json");
  QCOMPARE(covers.size(),1);
  QVERIFY(!GameMetadata::trustedImageUrl(QUrl("http://cdn2.steamgriddb.com/grid/image.png")));
  QVERIFY(!GameMetadata::trustedImageUrl(QUrl("https://cdn2.steamgriddb.com.evil.example/image.png")));
}

void CoreTests::metadataPersistsRatingsAndPreservesCustomArt() {
  QTemporaryDir temp;
  const QString database = temp.filePath("library.sqlite3");
  MockGameModel source(nullptr, 3);
  UnifiedGameModel games(database);
  games.addSourceModel(&source);
  const QString key = games.data(games.index(1),GameRoles::MetadataKey).toString();
  QVERIFY(!key.isEmpty());
  const QString portrait = temp.filePath("portrait.png");
  const QString custom = temp.filePath("custom.png");
  QImage image(600,900,QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(portrait));
  image.fill(Qt::red); QVERIFY(image.save(custom));
  { GameMetadata metadata(database,nullptr); }
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE","metadata-test"); db.setDatabaseName(database); QVERIFY(db.open());
    QSqlQuery query(db); query.prepare("INSERT INTO game_metadata(game_key,payload) VALUES(?,?)");
    query.addBindValue(key);
    query.addBindValue(QJsonDocument(QJsonObject{{"igdbId",1},{"rating",92},{"ratingCount",20},{"portrait",portrait}}).toJson());
    QVERIFY(query.exec()); db.close();
  }
  QSqlDatabase::removeDatabase("metadata-test");
  {
    GameMetadata metadata(database,nullptr); games.setMetadata(&metadata);
    QCOMPARE(games.data(games.index(1),GameRoles::Rating).toInt(),92);
    QCOMPARE(games.data(games.index(0),GameRoles::Rating).toInt(),-1);
    QCOMPARE(games.data(games.index(1),GameRoles::CoverPath).toString(),QUrl::fromLocalFile(portrait).toString());
    LibraryFilterModel library; library.setSourceModel(&games); library.setSortMode(LibraryFilterModel::SortMode::Rating);
    QCOMPARE(library.get(0).value("metadataKey").toString(),key);
    QVERIFY(games.setCustomCover(1,QUrl::fromLocalFile(custom)));
    const QVariant chosen = games.data(games.index(1),GameRoles::CoverPath);
    QVERIFY(chosen.toString() != QUrl::fromLocalFile(portrait).toString());
    metadata.inspect({{"metadataKey",key}}); metadata.rejectMatch();
    QCOMPARE(games.data(games.index(1),GameRoles::CoverPath),chosen);
    QCOMPARE(games.data(games.index(1),GameRoles::Rating).toInt(),-1);
    games.setMetadata(nullptr);
  }
  GameMetadata reopened(database,nullptr);
  QVERIFY(reopened.entry(key).value("rejected").toBool());
  QVERIFY(!reopened.entry(key).contains("igdbId"));
}

void CoreTests::coverSizesPersistIndependently() {
  QTemporaryDir temp;
  const QString path = temp.filePath("config.toml");
  AppSettings settings(path);
  QCOMPARE(settings.coverSize(),100);
  QCOMPARE(settings.couchCoverSize(),100);
  settings.setCoverSize(70); settings.setCouchCoverSize(140);
  AppSettings restored(path);
  QCOMPARE(restored.coverSize(),70); QCOMPARE(restored.couchCoverSize(),140);
  restored.setCoverSize(-100); restored.setCouchCoverSize(10000);
  QCOMPARE(restored.coverSize(),60); QCOMPARE(restored.couchCoverSize(),160);
  AppSettings clamped(path);
  QCOMPARE(clamped.coverSize(),60); QCOMPARE(clamped.couchCoverSize(),160);
  clamped.setCoverSize(100);
  QCOMPARE(clamped.couchCoverSize(),160);
}

void CoreTests::controllerNavigationFollowsWindowFocus() {
  ControllerInput controller;
  controller.setWindowFocused(true);
  QVERIFY(controller.inputEnabled());
  controller.setWindowFocused(false);
  QVERIFY(!controller.inputEnabled());
  controller.setWindowFocused(true);
  QVERIFY(controller.inputEnabled());
  controller.setWindowFocused(false);
  QVERIFY(!controller.inputEnabled());
  controller.setWindowFocused(true);
  QVERIFY(controller.inputEnabled());
  QWindow root, dialog, other;
  dialog.setTransientParent(&root);
  QVERIFY(ControllerFocusGuard::ownsWindow(&root,&root));
  QVERIFY(ControllerFocusGuard::ownsWindow(&root,&dialog));
  QVERIFY(!ControllerFocusGuard::ownsWindow(&root,&other));
  QVERIFY(!ControllerFocusGuard::ownsWindow(&root,nullptr));
}


namespace {
class PortraitFixtureReply final : public QNetworkReply {
public:
  PortraitFixtureReply(const QNetworkRequest& request, QByteArray body, QObject* parent)
      : QNetworkReply(parent), m_body(std::move(body)) {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this] {
      emit readyRead();
      setFinished(true);
      emit finished();
    });
  }
  void abort() override {}
  qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
  qint64 readData(char* data, qint64 maximum) override {
    const qint64 length = qMin(maximum, m_body.size() - m_offset);
    if (length <= 0) return -1;
    memcpy(data, m_body.constData() + m_offset, length);
    m_offset += length;
    return length;
  }
private:
  QByteArray m_body;
  qint64 m_offset = 0;
};
class PortraitFixtureNetwork final : public QNetworkAccessManager {
public:
  QList<QNetworkRequest> requests;
  QByteArray png;
protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
    requests.append(request);
    QByteArray body = png;
    if (request.url().host() == "www.steamgriddb.com") {
      const QString id = request.url().path().section('/', -1);
      body = QString(R"({"success":true,"data":[{"id":%1,"width":600,"height":900,"url":"https://cdn2.steamgriddb.com/grid/%1.png"}]})").arg(id).toUtf8();
    }
    return new PortraitFixtureReply(request, body, this);
  }
};
}

void CoreTests::portraitBatchContinuesAndKeepsRatingTimestamp() {
  QTemporaryDir temp;
  PortraitFixtureNetwork network;
  QImage image(600, 900, QImage::Format_RGB32);
  image.fill(Qt::blue);
  QBuffer buffer(&network.png);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "PNG"));
  MockGameModel source(nullptr, 2);
  UnifiedGameModel games;
  games.addSourceModel(&source);
  GameMetadata metadata(temp.filePath("metadata.sqlite3"), nullptr, nullptr, &network);
  metadata.setLibrary(&games);
  metadata.m_gridKey = "offline-fixture-key";
  const qint64 ratingUpdated = QDateTime::currentSecsSinceEpoch() - 3600;
  QStringList keys;
  for (int row = 0; row < games.rowCount(); ++row) {
    const QString key = games.data(games.index(row), GameRoles::MetadataKey).toString();
    keys.append(key);
    metadata.persist(key, {{"igdbId", row + 1}, {"gridId", row + 11},
                           {"updated", ratingUpdated}, {"rating", 90}});
  }
  QCOMPARE(keys.size(), 2);
  bool idleWithPendingGames = false;
  connect(&metadata, &GameMetadata::changed, &metadata, [&] {
    if (metadata.pending() > 0 && !metadata.busy())
      idleWithPendingGames = true;
  });
  metadata.refreshLibrary();
  QTRY_VERIFY_WITH_TIMEOUT(!metadata.busy() && metadata.pending() == 0, 5000);
  QVERIFY(!idleWithPendingGames);
  QCOMPARE(network.requests.size(), 4);
  for (const auto& key : keys) {
    const auto entry = metadata.entry(key);
    QVERIFY(QFileInfo::exists(entry.value("portrait").toString()));
    QCOMPARE(entry.value("updated").toLongLong(), ratingUpdated);
    QCOMPARE(entry.value("rating").toInt(), 90);
    QVERIFY(entry.value("portraitUpdated").toLongLong() >= ratingUpdated);
  }
  for (const auto& request : network.requests) {
    if (request.url().host() == "www.steamgriddb.com")
      QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer offline-fixture-key"));
    else
      QVERIFY(request.rawHeader("Authorization").isEmpty());
  }

  // A queued game still owns the updater between requests. Credential actions
  // must not start in that gap, and a scheduled next() must respect Stop.
  metadata.m_queue.enqueue({{"metadataKey", keys.first()}});
  QVERIFY(metadata.busy());
  metadata.storeGridKey("invalid");
  QVERIFY(metadata.status() != "That SteamGridDB API key is invalid");
  metadata.cancel();
  QVERIFY(!metadata.busy());
  QCOMPARE(metadata.pending(), 0);
  metadata.next();
  QCOMPARE(metadata.status(), QString("Metadata update stopped"));
  QCOMPARE(network.requests.size(), 4);
}

void CoreTests::portraitSelectionCompletesOnlyAfterSuccessfulSave() {
  QTemporaryDir temp;
  PortraitFixtureNetwork network;
  QImage image(600, 900, QImage::Format_RGB32);
  image.fill(Qt::blue);
  QBuffer buffer(&network.png);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "PNG"));
  MockGameModel source(nullptr, 1);
  UnifiedGameModel games(temp.filePath("library.sqlite3"));
  games.addSourceModel(&source);
  GameMetadata metadata(temp.filePath("metadata.sqlite3"), nullptr, nullptr, &network);
  metadata.setLibrary(&games);
  metadata.m_gridKey = "offline-fixture-key";
  const QString key = games.data(games.index(0), GameRoles::MetadataKey).toString();
  metadata.persist(key, {{"igdbId", 1}, {"gridId", 11}});
  metadata.inspect({{"metadataKey", key}});
  QSignalSpy selected(&metadata, &GameMetadata::portraitSelected);
  metadata.findCovers();
  QTRY_VERIFY(!metadata.busy());
  QCOMPARE(metadata.covers().size(), 1);
  QSignalSpy stateChanges(&metadata, &GameMetadata::changed);
  metadata.chooseCover(0);
  QVERIFY(metadata.busy());
  QVERIFY(!stateChanges.isEmpty());
  QCOMPARE(selected.count(), 0);
  QTRY_VERIFY(!metadata.busy());
  QCOMPARE(selected.count(), 1);
  QCOMPARE(selected.first().first().toString(), key);
  QVERIFY(metadata.covers().isEmpty());
  QVERIFY(QFileInfo::exists(metadata.current().value("portrait").toString()));

  metadata.findCovers();
  QTRY_VERIFY(!metadata.busy());
  QCOMPARE(metadata.covers().size(), 1);
  network.png = "invalid image";
  metadata.chooseCover(0);
  QTRY_VERIFY(!metadata.busy());
  QCOMPARE(selected.count(), 1);
  QCOMPARE(metadata.covers().size(), 1);
  QCOMPARE(metadata.status(), QString("Portrait has unexpected dimensions"));
}

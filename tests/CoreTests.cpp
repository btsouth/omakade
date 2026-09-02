#include "achievements/AchievementModel.h"
#include "achievements/RetroAchievementsApi.h"
#include "achievements/RetroAchievementsHasher.h"

#include <zip.h>
#include "achievements/SteamAchievementApi.h"
#include "app/AppSettings.h"
#include "app/SingleInstance.h"
#include "input/ControllerInput.h"
#include "launch/GameLauncher.h"
#include "launch/SteamLauncher.h"
#include "library/FaugusGameModel.h"
#include "library/GameRoles.h"
#include "library/HeroicGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/LutrisGameModel.h"
#include "library/MockGameModel.h"
#include "library/RetroArchGameModel.h"
#include "library/SteamGameModel.h"
#include "library/SteamOwnedGamesApi.h"
#include "library/UnifiedGameModel.h"
#include "metadata/GameInsightsService.h"
#include "metadata/IgdbApi.h"
#include "sources/faugus/FaugusScanner.h"
#include "sources/heroic/HeroicScanner.h"
#include "sources/lutris/LutrisScanner.h"
#include "sources/retroarch/RetroArchScanner.h"
#include "sources/steam/SteamScanner.h"
#include "sources/steam/ValveKeyValues.h"
#include "theme/OmarchyTheme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <SDL3/SDL.h>

namespace {
void writeFile(const QString& path, const QByteArray& contents) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
  QCOMPARE(file.write(contents), contents.size());
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
  writeFile(root + QStringLiteral("/gog-game/goggame-12345.info"), R"({"name":"GOG Quest"})");
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
} // namespace

class CoreTests final : public QObject {
  Q_OBJECT

private slots:
  void mockLibraryIsDeterministic();
  void libraryFiltersByModeAndSearch();
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
  void steamScannerRejectsLandscapeCoverFallbackAndImportsAchievements();
  void steamScannerSurvivesMissingLibrariesAndBrokenManifests();
  void steamModelPersistsFavoritesAndHiddenState();
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
  void heroicModelIsRepeatableAndPreservesLocalState();
  void malformedHeroicDataDoesNotReplaceCachedGames();
  void heroicLauncherBuildsSafeCommands();
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
  void launcherReportsInvalidAndStaleTargets();
  void igdbApiBuildsSafeQueriesAndParsesInsights();
  void igdbInsightsLoadFromOfflineCache();
  void retroAchievementsHasherAppliesHeaderStripRules();
  void retroAchievementsHasherReadsZipArchivedRoms();
  void retroAchievementsApiBuildsUrlsAndParsesResponses();
  void retroArchModelReadsCachedRetroAchievementsSummary();
  void stressLibraryContainsOneThousandGames();
  void settingsPersistReducedMotionAndCacheLimit();
  void secondInstanceRequestsActivation();
  void virtualControllerConnectsAndMapsPrimaryButton();
  void thousandGameSearchStaysResponsive();
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
  QVERIFY(SteamLauncher::launchUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());
  QVERIFY(SteamLauncher::installUrl(QStringLiteral("440;touch /tmp/nope")).isEmpty());
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
  QVERIFY(model.statusText().startsWith(QStringLiteral("Heroic scan interrupted")));
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
  createLutrisFixture(dataHome + QStringLiteral("/lutris"));
  createHeroicFixture(configHome + QStringLiteral("/heroic"));
  createFaugusFixture(dataHome + QStringLiteral("/faugus-launcher"));

  const QString database = directory.path() + QStringLiteral("/omakade.sqlite3");
  LutrisGameModel lutris(database);
  HeroicGameModel heroic(database);
  FaugusGameModel faugus(database);
  lutris.refresh();
  heroic.refresh();
  faugus.refresh();
  QCOMPARE(lutris.statusText(), QStringLiteral("Scanning Lutris library"));
  QCOMPARE(heroic.statusText(), QStringLiteral("Scanning Heroic library"));
  QCOMPARE(faugus.statusText(), QStringLiteral("Scanning Faugus library"));
  QTRY_COMPARE_WITH_TIMEOUT(lutris.rowCount(), 1, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(heroic.rowCount(), 4, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(faugus.rowCount(), 3, 3000);
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

  const QString connection = QStringLiteral("empty-source-paths-") + QUuid::createUuid().toString();
  {
    QSqlDatabase stored = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    stored.setDatabaseName(database);
    QVERIFY(stored.open());
    QSqlQuery query(stored);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT COUNT(*) FROM source_state WHERE source IN "
        "('lutris', 'heroic', 'faugus', 'retroarch') AND paths = '' AND paths IS NOT NULL")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 4);
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

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString archive = directory.path() + QStringLiteral("/games.zip");
  writeFile(archive, "archive");
  GameLauncher launcher;
  QVERIFY(!launcher.launch(QStringLiteral("RetroArch"), QStringLiteral("id"), false, {},
                           archive + QStringLiteral("#Sonic.bin"), {}));
  QVERIFY(!launcher.lastError().startsWith(QStringLiteral("The installed files are missing.")));
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

  const QString snesHeadered = directory.path() + QStringLiteral("/game.sfc");
  const QByteArray snesPayload(0x8000, 'A');
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
      QVERIFY(achievement.unlockTime > 0);
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
    settings.setReducedMotion(true);
    settings.setArtworkCacheLimitMb(512);
    settings.setSteamId(QStringLiteral("76561198000000000"));
    settings.setIgdbClientId(QStringLiteral("publicclient123"));
    settings.setSteamEnabled(false);
    settings.setLutrisEnabled(false);
    settings.setFaugusEnabled(false);
    settings.setRetroArchEnabled(false);
    settings.setCloseAfterLaunch(true);
  }
  AppSettings reloaded(path);
  QVERIFY(reloaded.reducedMotion());
  QCOMPARE(reloaded.artworkCacheLimitMb(), 512);
  QCOMPARE(reloaded.steamId(), QStringLiteral("76561198000000000"));
  QCOMPARE(reloaded.igdbClientId(), QStringLiteral("publicclient123"));
  QVERIFY(!reloaded.steamEnabled());
  QVERIFY(!reloaded.lutrisEnabled());
  QVERIFY(reloaded.heroicEnabled());
  QVERIFY(!reloaded.faugusEnabled());
  QVERIFY(!reloaded.retroArchEnabled());
  QVERIFY(reloaded.closeAfterLaunch());
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

void CoreTests::virtualControllerConnectsAndMapsPrimaryButton() {
  QVERIFY(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_VirtualJoystickDesc description;
  SDL_INIT_INTERFACE(&description);
  description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  description.naxes = SDL_GAMEPAD_AXIS_COUNT;
  description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  description.button_mask = (1U << SDL_GAMEPAD_BUTTON_SOUTH) | (1U << SDL_GAMEPAD_BUTTON_WEST) |
                            (1U << SDL_GAMEPAD_BUTTON_NORTH);
  description.axis_mask = (1U << SDL_GAMEPAD_AXIS_LEFTX) | (1U << SDL_GAMEPAD_AXIS_LEFTY);
  description.name = "Omakade test controller";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&description);
  QVERIFY2(id != 0, SDL_GetError());

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

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"

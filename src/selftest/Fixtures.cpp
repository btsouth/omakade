#include "selftest/SelfTest.h"

#include "app/AppSettings.h"
#include "app/CardExport.h"
#include "app/CommandLine.h"
#include "backup/BackupSnapshot.h"
#include "gamemode/LaunchHold.h"
#include "highlights/ClipLibrary.h"
#include "highlights/HighlightClips.h"
#include "library/HeroicGameModel.h"
#include "library/LauncherGameModel.h"
#include "library/LibraryFilterModel.h"
#include "library/ManualGameModel.h"
#include "library/UnifiedGameModel.h"
#include "saves/SaveBackups.h"
#include "tracking/PlaySessionStore.h"
#include "tracking/ProcFs.h"
#include "tracking/SessionDatabase.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <memory>

namespace SelfTest {

std::optional<int> prepareBeforeSources(Harness& harness, AppContext& app) {
  AppSettings& preferences = *app.preferences;
  std::unique_ptr<LauncherGameModel>& kytyGames = *app.kytyGames;
  std::unique_ptr<LauncherGameModel>& nativePortGames = *app.nativePortGames;
  LauncherGameModel*& kytyLibrary = *app.kytyLibrary;
  LauncherGameModel*& nativePortLibrary = *app.nativePortLibrary;
  const bool& sourceRescanFixture = harness.options.sourceRescanFixture;
  std::shared_ptr<std::atomic<int>>& kytyRefreshes = harness.fixtures.kytyRefreshes;
  std::shared_ptr<std::atomic<int>>& portRefreshes = harness.fixtures.portRefreshes;
  if (harness.options.startupNavigationTest &&
      app.application->arguments().contains("--startup-grid")) {
    preferences.setCouchLibraryView(QStringLiteral("grid"));
  }
  if (sourceRescanFixture) {
    preferences.setKytyEnabled(false);
    preferences.setNativePortsEnabled(false);
    kytyGames = std::make_unique<LauncherGameModel>(
        LauncherGameModel::Definition{"Kyty", "kyty", {}, [kytyRefreshes] {
          ++*kytyRefreshes;
          return LauncherScanResult{};
        }}, ":memory:");
    nativePortGames = std::make_unique<LauncherGameModel>(
        LauncherGameModel::Definition{"Ports", "native_port", {}, [portRefreshes] {
          ++*portRefreshes;
          return LauncherScanResult{};
        }}, ":memory:");
    kytyLibrary = kytyGames.get();
    nativePortLibrary = nativePortGames.get();
  }
  return std::nullopt;
}

std::optional<int> prepareSourcesBuilt(Harness& harness, AppContext& app) {
  AppSettings& preferences = *app.preferences;
  std::unique_ptr<QAbstractItemModel>& games = *app.games;
  HeroicGameModel*& heroicLibrary = *app.heroicLibrary;
  QString& libraryDatabasePath = *app.libraryDatabasePath;
  const QString& renderOverlay = harness.options.renderOverlay;
  const bool& statsFixture = harness.options.statsFixture;
  const bool& heroicOwnedFixture = harness.options.heroicOwnedFixture;
  const bool& gogSettingsFixture = harness.options.gogSettingsFixture;
  const bool& linkedPreferenceFixture = harness.options.linkedPreferenceFixture;
  const bool& backupFixture = harness.options.backupFixture;
  const bool& bulkEditorTest = harness.options.bulkEditorTest;
  const bool& savedFilterTest = harness.options.savedFilterTest;
  const bool& artworkEditorTest = harness.options.artworkEditorTest;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  const QString& heroicOwnedDispatchLog = harness.fixtures.heroicOwnedDispatchLog;
  if (renderOverlay.startsWith("library-reflow")) {
    libraryDatabasePath = QStringLiteral(":memory:");
  }
  if (gogSettingsFixture || linkedPreferenceFixture || backupFixture || artworkEditorTest ||
      savedFilterTest || bulkEditorTest || renderOverlay == QStringLiteral("saved-filters") ||
      renderOverlay == QStringLiteral("bulk-editor") ||
      renderOverlay.startsWith(QStringLiteral("session-history")) ||
      renderOverlay == QStringLiteral("game-clips") ||
      statsFixture || heroicOwnedFixture ||
      renderOverlay == QStringLiteral("now-playing") || renderOverlay == "library-repair-controls") {
    if (!artworkFixture.isValid()) return EXIT_FAILURE;
    libraryDatabasePath = artworkFixture.filePath(QStringLiteral("library.sqlite"));
  }
  if (heroicOwnedFixture) {
    // Use a real owned row and launcher, but confine external dispatch to a stub.
    const QString root = artworkFixture.filePath("heroic");
    const QString bin = artworkFixture.filePath("bin");
    if (!QDir().mkpath(root + "/store_cache") || !QDir().mkpath(bin)) return EXIT_FAILURE;
    QFile cache(root + "/store_cache/legendary_library.json");
    if (!cache.open(QIODevice::WriteOnly) || cache.write(
        R"({"library":[{"app_name":"owned-fixture","title":"Heroic Owned Fixture"}]})") < 0)
      return EXIT_FAILURE;
    cache.close();
    QFile stub(bin + "/heroic");
    if (!stub.open(QIODevice::WriteOnly) || stub.write(
        "#!/bin/sh\nprintf '%s\\n' \"$#\" \"$@\" >> \"$OMAKADE_HEROIC_DISPATCH_LOG\"\n/bin/sleep 1\n") < 0)
      return EXIT_FAILURE;
    stub.close();
    if (!stub.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
      return EXIT_FAILURE;
    qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
    qputenv("OMAKADE_HEROIC_DISPATCH_LOG", heroicOwnedDispatchLog.toUtf8());
    auto owned = std::make_unique<HeroicGameModel>(libraryDatabasePath);
    owned->refreshFromRoots({root});
    if (owned->rowCount() != 1) return EXIT_FAILURE;
    heroicLibrary = owned.get();
    games = std::move(owned);
    preferences.setHeroicEnabled(true);
    preferences.setCloseAfterLaunch(true);
  }
  if (statsFixture) {
    // Completion state and achievements have to exist before the library model is built, because
    // the model reads the organization table once when it loads. Without them the sections that
    // report progress would draw their empty states in the render check and prove nothing.
    QSqlDatabase fixture;
    if (!SessionDatabase::open(fixture, libraryDatabasePath,
                               QStringLiteral("omakade-stats-state")))
      return EXIT_FAILURE;
    QSqlQuery query(fixture);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS game_organization (source TEXT NOT NULL, runner TEXT NOT "
            "NULL, app_id TEXT NOT NULL, completion_status TEXT NOT NULL DEFAULT '', tags_json "
            "TEXT NOT NULL DEFAULT '[]', pinned INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(source, "
            "runner, app_id))")) ||
        !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS achievements (app_id TEXT NOT NULL, api_name TEXT NOT "
            "NULL, title TEXT NOT NULL, description TEXT, icon_url TEXT, icon_path TEXT, unlocked "
            "INTEGER NOT NULL, unlock_time INTEGER NOT NULL, rarity REAL NOT NULL, hidden INTEGER "
            "NOT NULL, current_progress REAL NOT NULL, maximum_progress REAL NOT NULL, source TEXT "
            "NOT NULL, PRIMARY KEY(app_id, api_name))")))
      return EXIT_FAILURE;
    for (const auto& completion : {std::make_tuple("Demo", "demo-1", "completed"),
                                   std::make_tuple("Demo", "demo-2", "playing"),
                                   std::make_tuple("Demo", "demo-3", "abandoned"),
                                   std::make_tuple("Steam", "demo-0", "completed")}) {
      if (!query.exec(QStringLiteral("INSERT OR REPLACE INTO game_organization"
                                     "(source, runner, app_id, completion_status) VALUES('%1', '', "
                                     "'%2', '%3')")
                          .arg(QString::fromUtf8(std::get<0>(completion)),
                               QString::fromUtf8(std::get<1>(completion)),
                               QString::fromUtf8(std::get<2>(completion)))))
        return EXIT_FAILURE;
    }
    // Achievements carry their own unlock times, so the period figures have something to select.
    const qint64 unlockedAt =
        QDateTime(QDate::currentDate().addDays(-2), QTime(20, 0)).toSecsSinceEpoch();
    for (const auto& achievement :
         {std::make_tuple("demo-1", "ACH_FIRST", "First Steps", 1, 4.7),
          std::make_tuple("demo-2", "ACH_LONG", "Long Haul", 1, 11.2),
          std::make_tuple("demo-3", "ACH_OPEN", "Still Untouched", 0, 0.0)}) {
      const QString statement =
          QStringLiteral("INSERT OR REPLACE INTO achievements(app_id, api_name, title, "
                         "description, icon_url, icon_path, unlocked, unlock_time, rarity, hidden, "
                         "current_progress, maximum_progress, source) VALUES('%1', '%2', '%3', '', "
                         "'', '', %4, %5, %6, 0, 0, 1, 'steam-local')")
              .arg(QString::fromUtf8(std::get<0>(achievement)),
                   QString::fromUtf8(std::get<1>(achievement)),
                   QString::fromUtf8(std::get<2>(achievement)))
              .arg(std::get<3>(achievement) != 0 ? 1 : 0)
              .arg(std::get<3>(achievement) != 0 ? unlockedAt : 0)
              .arg(std::get<4>(achievement));
      if (!query.exec(statement)) return EXIT_FAILURE;
    }
    // Genres and ratings reach the library through the metadata layer rather than from a source
    // model, so the sections that report them read these entries. The key is the source, a NUL,
    // the runner, a NUL, and the app id.
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS game_metadata (game_key TEXT PRIMARY KEY, payload TEXT "
            "NOT NULL)")))
      return EXIT_FAILURE;
    const auto metadataKey = [](const QString& source, const QString& appId) {
      return source + QChar::Null + QChar::Null + appId;
    };
    for (const auto& entry :
         {std::make_tuple("Steam", "demo-0",
                          R"({"genres":["Platformer"],"rating":90,"ratingCount":210})"),
          std::make_tuple("Demo", "demo-1",
                          R"({"genres":["Action","Adventure"],"rating":86,"ratingCount":120})"),
          std::make_tuple("Demo", "demo-2",
                          R"({"genres":["Role Playing"],"rating":92,"ratingCount":340})"),
          std::make_tuple("Demo", "demo-3",
                          R"({"genres":["Shooter"],"rating":74,"ratingCount":58})")}) {
      QSqlQuery metadata(fixture);
      metadata.prepare(QStringLiteral(
          "INSERT OR REPLACE INTO game_metadata(game_key, payload) VALUES(?, ?)"));
      metadata.addBindValue(metadataKey(QString::fromUtf8(std::get<0>(entry)),
                                        QString::fromUtf8(std::get<1>(entry))));
      metadata.addBindValue(QString::fromUtf8(std::get<2>(entry)));
      if (!metadata.exec()) return EXIT_FAILURE;
    }
    fixture.close();
    fixture = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("omakade-stats-state"));
  }
  return std::nullopt;
}

std::optional<int> prepareLibraryFiltered(Harness& harness, AppContext& app) {
  LibraryFilterModel& library = *app.library;
  const bool& heroicOwnedFixture = harness.options.heroicOwnedFixture;
  if (heroicOwnedFixture) {
    library.setAvailability(LibraryFilterModel::Availability::AllGames);
  }
  return std::nullopt;
}

std::optional<int> prepareSaveBackups(Harness& harness, AppContext& app) {
  std::unique_ptr<SaveBackups>& saveBackupsOwner = *app.saveBackupsOwner;
  const QString& renderOverlay = harness.options.renderOverlay;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  const QString& saveFixtureGame = harness.fixtures.saveFixtureGame;
  if (renderOverlay.startsWith("save-backups")) {
    const QString folder = artworkFixture.filePath("save-protection");
    const auto fixtureWrite = [](const QString& path, const QByteArray& data) {
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile file(path);
      return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QString config = folder + "/retroarch.cfg";
    const QString save = folder + "/saves/Snes9x/Test Game.srm";
    if (!fixtureWrite(saveFixtureGame, "fixture ROM") || !fixtureWrite(save, "older progress") ||
        !fixtureWrite(config, "savefile_directory = \"~/saves\"\nsavefiles_in_content_dir = \"false\"\nsort_savefiles_enable = \"true\"\nsort_savefiles_by_content_enable = \"false\"\nauto_overrides_enable = \"false\"\n")) return EXIT_FAILURE;
    saveBackupsOwner = std::make_unique<SaveBackups>(folder, config, folder + "/backups", [] { return false; });
    const bool sharedFixture=renderOverlay.contains("shared");
    if(sharedFixture) {
      const QJsonObject rule{{"source","RetroArch"},{"game",saveFixtureGame},
        {"trees",QJsonArray{folder+"/saves/Snes9x"}},{"shared",true},{"description","Shared memory-card saves"}};
      if(!fixtureWrite(folder+"/.config/omakade/save-layouts.json",
          QJsonDocument(QJsonObject{{"format",1},{"layouts",QJsonArray{rule}}}).toJson()))return EXIT_FAILURE;
    }
    const auto protectFixture=[&] {
      return sharedFixture ? saveBackupsOwner->protectLaunch("RetroArch",saveFixtureGame,"snes9x_libretro.so")
                           : saveBackupsOwner->protect(saveFixtureGame,"snes9x_libretro.so");
    };
    if (!protectFixture() ||
        !fixtureWrite(save, "newer progress") ||
        !protectFixture() ||
        !fixtureWrite(save, "current progress")) return EXIT_FAILURE;
  }
  return std::nullopt;
}

std::optional<int> prepareBackups(Harness& harness, AppContext& app) {
  const QString& settingsPath = *app.settingsPath;
  AppSettings& preferences = *app.preferences;
  QString& libraryDatabasePath = *app.libraryDatabasePath;
  ManualGameModel& manualGames = *app.manualGames;
  UnifiedGameModel& unifiedGames = *app.unifiedGames;
  LibraryFilterModel& library = *app.library;
  BackupRecovery::Paths& managerPaths = *app.managerPaths;
  const QString& renderOverlay = harness.options.renderOverlay;
  const bool& renderMode = harness.options.renderMode;
  const bool& gogSettingsFixture = harness.options.gogSettingsFixture;
  const bool& linkedPreferenceFixture = harness.options.linkedPreferenceFixture;
  const bool& backupFixture = harness.options.backupFixture;
  QTemporaryDir& artworkFixture = harness.fixtures.artworkFixture;
  const QString& gogAvailableFolder = harness.fixtures.gogAvailableFolder;
  const QString& gogMissingFolder = harness.fixtures.gogMissingFolder;
  QString& linkedManualId = harness.fixtures.linkedManualId;
  const QString& linkedExecutable = harness.fixtures.linkedExecutable;
  QString& backupFixturePath = harness.fixtures.backupFixturePath;
  if (gogSettingsFixture) {
    if (!QDir().mkpath(gogAvailableFolder)) return EXIT_FAILURE;
    QFile sentinel(gogAvailableFolder + "/keep-game.txt");
    if (!sentinel.open(QIODevice::WriteOnly) || sentinel.write("game files stay") < 0) return EXIT_FAILURE;
    if (renderMode) {
      if (!preferences.addGogLibraryPath(gogAvailableFolder) || !preferences.addGogLibraryPath(gogMissingFolder)) return EXIT_FAILURE;
    }
  }
  if (linkedPreferenceFixture) {
    QFile executable(linkedExecutable);
    if (!executable.open(QIODevice::WriteOnly) || executable.write("#!/bin/sh\nprintf unexpected > launched.txt\n") < 0) return EXIT_FAILURE;
    executable.close();
    if (!QFile::setPermissions(linkedExecutable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return EXIT_FAILURE;
    linkedManualId = manualGames.saveEntry({{"title", "Aster Vale"}, {"executable", linkedExecutable},
        {"directory", artworkFixture.path()}, {"arguments", QStringList{}}});
    if (linkedManualId.isEmpty() || !unifiedGames.linkGames(0, "Manual", "", linkedManualId)) return EXIT_FAILURE;
    if (renderMode) {
      if (!unifiedGames.setPreferredInstallation(0, "Manual", "", linkedManualId)) return EXIT_FAILURE;
      if (renderOverlay == "linked-preference-missing" && !QFile::rename(linkedExecutable, linkedExecutable + ".disconnected")) return EXIT_FAILURE;
    }
  }
  if (backupFixture) {
    managerPaths = {libraryDatabasePath, settingsPath, artworkFixture.filePath("recovery")};
    library.saveCurrentFilter("Weekend games");
    BackupPayload sample;
    QString error;
    if (!BackupSnapshot::capture(libraryDatabasePath, preferences.backupSettings(), &sample, &error)) {
      qCritical().noquote() << error; return EXIT_FAILURE;
    }
    sample.settings.insert("gog_library_paths", QJsonArray{artworkFixture.filePath("Offline/GOG Games")});
    // A command the library does not have yet, so the preview must list and flag it.
    const QJsonObject shellEntry{{"id", "restored-shell"}, {"title", "Restored shell game"},
        {"executable", "/bin/sh"}, {"directory", artworkFixture.path()},
        {"arguments", QJsonArray{"-c", "echo restored"}}};
    auto manualRows = sample.library.value("manual_games").toArray();
    manualRows.append(QJsonObject{{"id", "restored-shell"},
        {"entry", QString::fromUtf8(QJsonDocument(shellEntry).toJson(QJsonDocument::Compact))},
        {"favorite", false}, {"hidden", false}, {"active", true}});
    sample.library.insert("manual_games", manualRows);
    backupFixturePath = artworkFixture.filePath("sample.omakade-backup");
    if (!BackupArchive::write(backupFixturePath, sample, &error)) {
      qCritical().noquote() << error; return EXIT_FAILURE;
    }
  }
  return std::nullopt;
}

std::optional<int> prepareCardExport(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  CardExport& cardExport = *app.cardExport;
  const QString& cardExportPath = harness.options.cardExportPath;
  if (!cardExportPath.isEmpty()) {
    QObject::connect(&cardExport, &CardExport::exportReported, &application,
                     [&application](bool written) {
                       if (!written)
                         qCritical() << "Card export: the card was not written";
                       application.exit(written ? EXIT_SUCCESS : EXIT_FAILURE);
                     }, Qt::QueuedConnection);
  }
  return std::nullopt;
}

std::optional<int> prepareSessions(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  AppSettings& preferences = *app.preferences;
  std::unique_ptr<PlaySessionStore>& playSessionStore = *app.playSessionStore;
  QString& libraryDatabasePath = *app.libraryDatabasePath;
  const QString& renderOverlay = harness.options.renderOverlay;
  const bool& statsFixture = harness.options.statsFixture;
  qint64& nowPlayingFixturePid = harness.fixtures.nowPlayingFixturePid;
  if (renderOverlay.startsWith("settings-recorder-")) {
    playSessionStore = std::make_unique<PlaySessionStore>(QStringLiteral(":memory:"));
    preferences.setTrackPlaySessions(renderOverlay.endsWith("on"));
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  if (renderOverlay.startsWith(QStringLiteral("session-history"))) {
    const QString connection = QStringLiteral("omakade-session-history-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 older = SessionDatabase::beginSession(
        database, QStringLiteral("/games/demo-0.nes"), QStringLiteral("RetroArch"),
        now - 7200, 10, 10);
    const qint64 recent = SessionDatabase::beginSession(
        database, QStringLiteral("/games/demo-0.nes"), QStringLiteral("RetroArch"),
        now - 1800, 11, 11);
    if (older <= 0 || recent <= 0 ||
        !SessionDatabase::endSession(database, older, now - 5400, 1800) ||
        !SessionDatabase::endSession(database, recent, now - 600, 1200))
      return EXIT_FAILURE;
    if (renderOverlay == QStringLiteral("session-history-pages")) {
      for (int i = 0; i < 10; ++i) {
        const auto start = now - 86400 * (i + 1);
        const auto id = SessionDatabase::beginSession(database, "/games/demo-0.nes", "RetroArch", start, 12, 12);
        if (id <= 0 || !SessionDatabase::endSession(database, id, start + 60, 60)) return EXIT_FAILURE;
      }
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  if (statsFixture) {
    // A spread of recorded sessions so every section of the stats screen has something real to
    // draw in a render check: more than one game and source, days apart, different hours, one
    // long session and one short one, and a return after a gap.
    const QString connection = QStringLiteral("omakade-stats-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    struct StatsFixture {
      const char* path;
      const char* source;
      int daysAgo;
      int hour;
      qint64 seconds;
    };
    const StatsFixture fixtures[] = {
        {"/games/demo-0.nes", "RetroArch", 0, 21, 5400},
        {"/games/demo-1.sfc", "RetroArch", 0, 22, 1800},
        {"/games/demo-0.nes", "RetroArch", 1, 20, 2700},
        {"/games/demo-2.iso", "PCSX2", 2, 19, 7200},
        {"/games/demo-0.nes", "RetroArch", 3, 23, 900},
        {"/games/demo-2.iso", "PCSX2", 12, 18, 3600},
    };
    for (const StatsFixture& fixture : fixtures) {
      if (renderOverlay.endsWith(QStringLiteral("empty"))) break;
      const qint64 start =
          QDateTime(QDate::currentDate().addDays(-fixture.daysAgo), QTime(fixture.hour, 0))
              .toSecsSinceEpoch();
      const qint64 id = SessionDatabase::beginSession(
          database, QString::fromLatin1(fixture.path), QString::fromLatin1(fixture.source), start,
          10, 10);
      if (id <= 0 ||
          !SessionDatabase::endSession(database, id, start + fixture.seconds +
                                    (renderOverlay.endsWith("paused") ? 7200 : 0), fixture.seconds))
        return EXIT_FAILURE;
    }
    if (renderOverlay.endsWith(QStringLiteral("error"))) {
      QSqlQuery broken(database);
      if (!broken.exec(QStringLiteral("DROP TABLE play_sessions"))) return EXIT_FAILURE;
    }
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    // The error fixture must retain its missing required table.
    if (!renderOverlay.endsWith(QStringLiteral("error"))) {
      playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
      playSessionStore->setEnabled(preferences.trackPlaySessions());
    }
  }
  if (renderOverlay == QStringLiteral("now-playing")) {
    // A live session for the Now Playing render, backed by a real process so the
    // view exercises the same liveness check it uses in normal runs. The stand-in
    // ignores SIGTERM, exactly like an emulator that only closes when it is forced.
    const QString connection = QStringLiteral("omakade-now-playing-render");
    QSqlDatabase database;
    if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
    auto* fixtureGame = new QProcess(&application);
    fixtureGame->start(QStringLiteral("/bin/sh"),
                       {QStringLiteral("-c"), QStringLiteral("trap '' TERM; sleep 300")});
    if (!fixtureGame->waitForStarted(5000)) return EXIT_FAILURE;
    // waitForStarted can return while exec is still finishing. Until it does, /proc
    // shows an empty command line and listProcesses skips the process.
    qint64 procStart = -1;
    QElapsedTimer fixtureWait;
    fixtureWait.start();
    while (procStart < 0 && fixtureWait.elapsed() < 2000) {
      for (const ProcessSnapshot& snapshot : ProcFs::listProcesses()) {
        if (snapshot.pid == fixtureGame->processId()) {
          procStart = snapshot.procStart;
          break;
        }
      }
      if (procStart < 0) QThread::msleep(10);
    }
    const bool recorded =
        procStart >= 0 &&
        SessionDatabase::beginSession(
            database, QStringLiteral("/data/Emulation/Games/Xbox/Dante's Inferno (USA)/default.xex"),
            QStringLiteral("Xenia"), QDateTime::currentSecsSinceEpoch() - 754,
            fixtureGame->processId(), procStart) > 0;
    nowPlayingFixturePid = fixtureGame->processId();
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connection);
    if (!recorded) return EXIT_FAILURE;
    playSessionStore = std::make_unique<PlaySessionStore>(libraryDatabasePath);
    playSessionStore->setEnabled(preferences.trackPlaySessions());
  }
  return std::nullopt;
}

std::optional<int> prepareClips(Harness& harness, AppContext& app) {
  QString& libraryDatabasePath = *app.libraryDatabasePath;
  HighlightClips& highlightClips = *app.highlightClips;
  const QString& renderOverlay = harness.options.renderOverlay;
  if (renderOverlay == QStringLiteral("game-clips")) {
    // Three saved clips for the first fixture game, with stills drawn here so no ffmpeg runs.
    const QString fixture = QFileInfo(libraryDatabasePath).absolutePath() + QStringLiteral("/clip-fixture");
    const QString thumbnails = fixture + QStringLiteral("/thumbnails");
    QDir().mkpath(thumbnails);
    const QString connection = QStringLiteral("omakade-game-clips-render");
    {
      QSqlDatabase database;
      if (!SessionDatabase::open(database, libraryDatabasePath, connection)) return EXIT_FAILURE;
      const qint64 now = QDateTime::currentSecsSinceEpoch();
      const struct { const char* label; const char* trigger; int seconds; int age; QColor from, to; } seeds[] = {
          {"Falsehood, Lost Kin", "achievement", 30, 600, QColor("#1d3b53"), QColor("#c97b2a")},
          {"", "button", 30, 3600, QColor("#2b1d4f"), QColor("#4fa3a5")},
          {"", "button", 90, 86400 * 3, QColor("#3f1f1f"), QColor("#d9c36a")},
      };
      for (const auto& seed : seeds) {
        const QString staged = fixture + QStringLiteral("/Replay.mp4");
        QFile file(staged);
        if (!file.open(QIODevice::WriteOnly) || file.write("video") != 5) return EXIT_FAILURE;
        file.close();
        ClipLibrary::Clip clip;
        clip.source = QStringLiteral("RetroArch");
        clip.gameKey = QStringLiteral("/games/demo-0.nes");
        clip.title = QStringLiteral("Demo Game");
        clip.createdAt = now - seed.age;
        clip.seconds = seed.seconds;
        clip.trigger = QString::fromLatin1(seed.trigger);
        clip.label = QString::fromLatin1(seed.label);
        const ClipLibrary::Clip filed = ClipLibrary::file(database, staged, fixture + QStringLiteral("/Videos"), clip);
        if (filed.id <= 0) return EXIT_FAILURE;
        QImage still(640, 360, QImage::Format_RGB32);
        QPainter painter(&still);
        QLinearGradient gradient(0, 0, 640, 360);
        gradient.setColorAt(0, seed.from);
        gradient.setColorAt(1, seed.to);
        painter.fillRect(still.rect(), gradient);
        painter.end();
        if (!still.save(ClipLibrary::thumbnailPath(filed.path, thumbnails))) return EXIT_FAILURE;
      }
      database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    highlightClips.setThumbnailRoot(thumbnails);
  }
  return std::nullopt;
}

std::optional<int> prepareBeforeLoad(Harness& harness, AppContext& app) {
  QGuiApplication& application = *app.application;
  LaunchHold& launchHold = *app.launchHold;
  QQmlApplicationEngine& engine = *app.engine;
  const QString& renderOverlay = harness.options.renderOverlay;
  const bool& renderMode = harness.options.renderMode;
  const bool& heroicOwnedFixture = harness.options.heroicOwnedFixture;
  const bool& gogSettingsFixture = harness.options.gogSettingsFixture;
  if (harness.options.startupNavigationTest &&
      (application.arguments().contains("--startup-empty") ||
       application.arguments().contains("--startup-delayed"))) {
    app.library->setSearchText(QStringLiteral("no-matching-startup-game"));
  }
  // The game a launching render shows, and the windows it holds, from
  // --render-game-info-file when one is given.
  QVariantMap launchingPreview;
  QVector<GameModeHeldWindow> launchingWindows{
      {QStringLiteral("0x1"), 0, QStringLiteral("heroic"), QStringLiteral("Heroic Games Launcher")},
      {QStringLiteral("0x2"), 0, QStringLiteral("upc.exe"), QStringLiteral("Ubisoft Connect")},
      {QStringLiteral("0x3"), 0, QStringLiteral("upc.exe"), QStringLiteral("Sign in to Ubisoft Connect")}};
  if (renderMode && renderOverlay == QStringLiteral("launching") &&
      optionSupplied(application.arguments(), QStringLiteral("--render-game-info-file"))) {
    QFile fixture(optionValue(application.arguments(), QStringLiteral("--render-game-info-file")));
    if (!fixture.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
    const QJsonObject data = QJsonDocument::fromJson(fixture.readAll()).object();
    launchingPreview = data.value("game").toObject().toVariantMap();
    if (data.contains("windows")) {
      launchingWindows.clear();
      for (const QJsonValue& value : data.value("windows").toArray()) {
        const QJsonObject window = value.toObject();
        launchingWindows.append({window.value("address").toString(), 0,
                                 window.value("class").toString(), window.value("title").toString()});
      }
    }
  }
  engine.rootContext()->setContextProperty(QStringLiteral("LaunchingPreview"), launchingPreview);
  if (renderMode && renderOverlay == QStringLiteral("launching")) {
    launchHold.preview(launchingPreview.value(QStringLiteral("title"),
                                              QStringLiteral("Assassin's Creed Valhalla")).toString(),
                       launchingWindows);
  }
  engine.rootContext()->setContextProperty("GogSettingsFixture", gogSettingsFixture);
  if (heroicOwnedFixture || renderOverlay == "game-info-native-port") {
    engine.rootContext()->setContextProperty(QStringLiteral("DemoMode"), false);
  }
  engine.rootContext()->setContextProperty(
      QStringLiteral("StatsFixtureView"),
      renderOverlay == QStringLiteral("stats-patterns") ? 1
      : renderOverlay == QStringLiteral("stats-library") ? 2 : 0);
  engine.rootContext()->setContextProperty(
      QStringLiteral("CouchLibraryViewOverride"),
      renderOverlay.startsWith(QStringLiteral("couch-grid")) ? QStringLiteral("grid") : QString{});
  return std::nullopt;
}

} // namespace SelfTest

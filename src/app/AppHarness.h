#pragma once

#include "backup/BackupRecovery.h"

#include <QString>

#include <memory>
#include <optional>

class AchievementModel;
class AppSettings;
class BackupManager;
class CardExport;
class ControllerInput;
class GameLauncher;
class GameMetadata;
class GameModeCompositor;
class GameModeSession;
class GameStopService;
class HeroicGameModel;
class HighlightClips;
class HomeModel;
class LaunchHold;
class LauncherGameModel;
class LibraryFilterModel;
class LibraryRepair;
class ManualGameModel;
class OmarchyTheme;
class PlaySessionStore;
class PlayStats;
class QAbstractItemModel;
class QElapsedTimer;
class QGuiApplication;
class QQmlApplicationEngine;
class QQuickWindow;
class QWindow;
class SaveBackups;
class SaveProtection;
class UnifiedGameModel;

// What the harness asks of the app for this run. The shipped binary has no harness, so every
// mode keeps its default.
struct AppHarnessModes {
  // A render path draws the window, saves a screenshot or a card, and ends the run.
  bool renderMode = false;
  // A harness self-test drives the run. It starts like --smoke-test, on the demo library.
  bool selfTest = false;
  // The demo library carries the play history and completion the stats and repair fixtures
  // check.
  bool demoStatsFixture = false;
  // The demo metadata lives in the library database, where the fixture seeded it.
  bool metadataInLibraryDatabase = false;
  // A backup fixture is in place, so Backups works without a Steam library.
  bool backupFixture = false;
  // A one-shot card export ends the run once the card is written.
  bool cardExport = false;
  // Game Mode drives this compositor instead of the desktop.
  GameModeCompositor* gameModeCompositor = nullptr;
};

// The app's own objects, for the harness to prepare and drive. runOmakade sets each pointer
// where it creates the object, so a stage sees what exists by then and nothing later. The
// pointers are to the app's objects themselves: a harness lambda that keeps a reference sees
// later changes, as the code did when it ran inside runOmakade.
struct AppContext {
  AppHarnessModes modes;

  QGuiApplication* application = nullptr;
  OmarchyTheme* theme = nullptr;
  QElapsedTimer* startupTimer = nullptr;
  const bool* isolatedTest = nullptr;
  const QString* settingsPath = nullptr;
  AppSettings* preferences = nullptr;
  ControllerInput* controller = nullptr;
  std::unique_ptr<QAbstractItemModel>* games = nullptr;
  std::unique_ptr<LauncherGameModel>* kytyGames = nullptr;
  std::unique_ptr<LauncherGameModel>* nativePortGames = nullptr;
  std::unique_ptr<PlaySessionStore>* playSessionStore = nullptr;
  HeroicGameModel** heroicLibrary = nullptr;
  LauncherGameModel** kytyLibrary = nullptr;
  LauncherGameModel** nativePortLibrary = nullptr;
  QString* libraryDatabasePath = nullptr;
  ManualGameModel* manualGames = nullptr;
  UnifiedGameModel* unifiedGames = nullptr;
  LibraryFilterModel* library = nullptr;
  AchievementModel* achievements = nullptr;
  std::unique_ptr<GameMetadata>* gameMetadata = nullptr;
  std::unique_ptr<SaveBackups>* saveBackupsOwner = nullptr;
  SaveBackups* saveBackups = nullptr;
  GameLauncher* launcher = nullptr;
  SaveProtection* saveProtection = nullptr;
  LibraryRepair* libraryRepair = nullptr;
  GameStopService* gameStop = nullptr;
  BackupRecovery::Paths* managerPaths = nullptr;
  BackupManager* backups = nullptr;
  HomeModel* home = nullptr;
  PlayStats* stats = nullptr;
  CardExport* cardExport = nullptr;
  GameModeSession* gameMode = nullptr;
  LaunchHold* launchHold = nullptr;
  QQmlApplicationEngine* engine = nullptr;
  HighlightClips* highlightClips = nullptr;
  QWindow** rootWindow = nullptr;
  // The root window, set just before windowReady and kept for the rest of the run.
  QQuickWindow* quickWindow = nullptr;
};

// The UI test harness, omakade-uitest's half of the app. runOmakade calls it at the points
// where a fixture, render path or self-test used to run inline.
class AppHarness {
public:
  // The points in startup where a fixture can change what the app builds, in the order
  // runOmakade reaches them.
  enum class Stage {
    // AppSettings exists; the library models do not yet.
    BeforeSources,
    // The source models exist; the unified library does not yet.
    SourcesBuilt,
    // The filtered library exists.
    LibraryFiltered,
    // Before the app makes its own SaveBackups, which it skips when the harness made one.
    SaveBackups,
    // Before BackupManager is made from managerPaths.
    Backups,
    // CardExport exists.
    CardExport,
    // Before the session recorder's status reaches QML.
    Sessions,
    // HighlightClips exists.
    Clips,
    // Every context property is set; the QML is not loaded yet.
    BeforeLoad,
  };

  virtual ~AppHarness() = default;

  // Reads the harness's own options, before the app reads its own, and fills app.modes. A
  // value ends the run with that exit code, before any window or library exists.
  virtual std::optional<int> start(AppContext& app) = 0;
  // Runs the fixtures for one stage. A value ends the run with that exit code.
  virtual std::optional<int> prepare(Stage stage, AppContext& app) = 0;
  // Sizes app.quickWindow for the run and starts any render path, before its first frame. A
  // value ends the run with that exit code.
  virtual std::optional<int> windowReady(AppContext& app) = 0;
  // Starts the selected self-test once the window is up. False when none was asked for and
  // no render path runs, so the app runs its own --smoke-test or stays open.
  virtual bool drive(AppContext& app) = 0;
  // Checks what the run left behind once the event loop ends, and returns the exit code.
  virtual int finish(AppContext& app, int result) = 0;
};

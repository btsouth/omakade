#pragma once

#include "app/AppHarness.h"
#include "selftest/TestDoubles.h"

#include <QSize>
#include <QStandardItemModel>
#include <QString>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace SelfTest {

// The options omakade-uitest reads on top of the app's own.
struct Options {
  // --render-screenshot, --render-size, --render-overlay and --export-card.
  QString screenshotPath;
  QString renderSize;
  QString renderOverlay;
  QString cardExportPath;
  QSize requestedRenderSize;
  // Both the stats screen and the card read the same fixture, which is why they share a flag.
  bool statsFixture = false;
  bool repairNavigationFixture = false;
  bool renderMode = false;
  bool heroicOwnedFixture = false;
  bool gogSettingsFixture = false;
  bool linkedPreferenceFixture = false;
  bool backupFixture = false;
  bool sourceRescanFixture = false;
  // The self-tests, one per entry in cases().
  bool gogSettingsTest = false;
  bool linkedPreferenceTest = false;
  bool backupEditorTest = false;
  bool bulkEditorTest = false;
  bool savedFilterTest = false;
  bool randomSelectionTest = false;
  bool staleSelectionTest = false;
  bool filterBackTest = false;
  bool gameModeTest = false;
  bool artworkEditorTest = false;
  bool manualEditorTest = false;
  bool launchingNavigationTest = false;
};

// What the fixtures write and keep for the render paths and self-tests to check. It outlives
// the app, so the files are still there while the app's models close them.
struct Fixtures {
  QTemporaryDir artworkFixture;
  const QString heroicOwnedDispatchLog = artworkFixture.filePath("heroic-dispatch.log");
  const QString saveFixtureGame = artworkFixture.filePath("save-protection/roms/Test Game.sfc");
  const QString gogAvailableFolder = artworkFixture.filePath("GOG Games");
  const QString gogMissingFolder = artworkFixture.filePath("Disconnected GOG Games");
  QString linkedManualId;
  const QString linkedExecutable = artworkFixture.filePath("native-game.sh");
  QString backupFixturePath;
  // The Now Playing render fixture keeps the pid of its stand-in game so the check can
  // find that exact row's stop control.
  qint64 nowPlayingFixturePid = 0;
  bool heroicOwnedDispatchComplete = false;
  std::shared_ptr<std::atomic<int>> kytyRefreshes = std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<std::atomic<int>> portRefreshes = std::make_shared<std::atomic<int>>(0);
  std::unique_ptr<QTemporaryDir> relocationFixtureDirectory;
  std::unique_ptr<QStandardItemModel> relocationFixture;
  LibraryGameModeTestCompositor gameModeTestCompositor;
};

class Harness final : public AppHarness {
public:
  std::optional<int> start(AppContext& app) override;
  std::optional<int> prepare(Stage stage, AppContext& app) override;
  std::optional<int> windowReady(AppContext& app) override;
  bool drive(AppContext& app) override;
  int finish(AppContext& app, int result) override;

  Options options;
  Fixtures fixtures;
};

// A self-test the harness can run, chosen by its flag. When several flags are given, the
// first case in cases() runs.
struct Case {
  const char* flag;
  bool Options::*selected;
  void (*drive)(Harness& harness, AppContext& app);
};
const std::vector<Case>& cases();

// --restore-startup-test: a library restore an earlier run left part way, recovered in its own
// window before the library opens.
int testRestoreStartup(QGuiApplication& application, OmarchyTheme& theme, const QString& mode,
                       bool couch, const QSize& size, const QString& screenshot);
bool parseRenderSize(const QString& value, QSize* result);

// The fixtures for each AppHarness::Stage, in Fixtures.cpp.
std::optional<int> prepareBeforeSources(Harness& harness, AppContext& app);
std::optional<int> prepareSourcesBuilt(Harness& harness, AppContext& app);
std::optional<int> prepareLibraryFiltered(Harness& harness, AppContext& app);
std::optional<int> prepareSaveBackups(Harness& harness, AppContext& app);
std::optional<int> prepareBackups(Harness& harness, AppContext& app);
std::optional<int> prepareCardExport(Harness& harness, AppContext& app);
std::optional<int> prepareSessions(Harness& harness, AppContext& app);
std::optional<int> prepareClips(Harness& harness, AppContext& app);
std::optional<int> prepareBeforeLoad(Harness& harness, AppContext& app);

// --render-screenshot and --export-card: sets up the overlay, checks it and saves the image.
std::optional<int> renderWindow(Harness& harness, AppContext& app);

// The self-tests in cases().
void driveLibraryPreferences(Harness& harness, AppContext& app);
void driveBackupEditor(Harness& harness, AppContext& app);
void driveBulkEditor(Harness& harness, AppContext& app);
void driveSavedFilter(Harness& harness, AppContext& app);
void driveRandomSelection(Harness& harness, AppContext& app);
void driveLaunchingNavigation(Harness& harness, AppContext& app);
void driveStaleSelection(Harness& harness, AppContext& app);
void driveGameMode(Harness& harness, AppContext& app);
void driveFilterBack(Harness& harness, AppContext& app);
void driveArtworkEditor(Harness& harness, AppContext& app);
void driveManualEditor(Harness& harness, AppContext& app);

} // namespace SelfTest

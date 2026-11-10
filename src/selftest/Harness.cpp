#include "selftest/SelfTest.h"

#include "app/CommandLine.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQuickWindow>

#include <cstdlib>

namespace SelfTest {

const std::vector<Case>& cases() {
  static const std::vector<Case> all{
      {"--gog-settings-test", &Options::gogSettingsTest, driveLibraryPreferences},
      {"--linked-preference-test", &Options::linkedPreferenceTest, driveLibraryPreferences},
      {"--backup-editor-test", &Options::backupEditorTest, driveBackupEditor},
      {"--bulk-editor-test", &Options::bulkEditorTest, driveBulkEditor},
      {"--saved-filter-test", &Options::savedFilterTest, driveSavedFilter},
      {"--random-selection-test", &Options::randomSelectionTest, driveRandomSelection},
      {"--launching-navigation-test", &Options::launchingNavigationTest, driveLaunchingNavigation},
      {"--stale-selection-test", &Options::staleSelectionTest, driveStaleSelection},
      {"--game-mode-test", &Options::gameModeTest, driveGameMode},
      {"--filter-back-test", &Options::filterBackTest, driveFilterBack},
      {"--artwork-editor-test", &Options::artworkEditorTest, driveArtworkEditor},
      {"--manual-editor-test", &Options::manualEditorTest, driveManualEditor},
  };
  return all;
}

std::optional<int> Harness::start(AppContext& app) {
  QGuiApplication& application = *app.application;
  const QStringList arguments = application.arguments();
  Options& o = options;
  o.screenshotPath = optionValue(arguments, QStringLiteral("--render-screenshot"));
  o.renderSize = optionValue(arguments, QStringLiteral("--render-size"));
  o.renderOverlay = optionValue(arguments, QStringLiteral("--render-overlay"));
  // `--export-card=<path>` renders the year-in-review card offscreen, writes it, and exits with
  // the outcome. This isolated render path uses synthetic data, so require an explicit
  // fixture flag rather than silently exporting demo statistics as someone's library.
  o.cardExportPath = optionValue(arguments, QStringLiteral("--export-card"));
  if (!o.cardExportPath.isEmpty() && !arguments.contains(QStringLiteral("--stats-fixture"))) {
    qCritical() << "Headless card checks require --stats-fixture. Export your library from Stats > Your Card.";
    return EXIT_FAILURE;
  }
  if (!o.cardExportPath.isEmpty() && o.renderOverlay.isEmpty())
    o.renderOverlay = QStringLiteral("year-in-review");
  o.statsFixture = o.renderOverlay.startsWith(QStringLiteral("stats"))
                   || o.renderOverlay.startsWith(QStringLiteral("year-in-review"));
  o.repairNavigationFixture = arguments.contains(QStringLiteral("--repair-navigation-fixture"));
  if (optionSupplied(arguments, QStringLiteral("--render-screenshot")) &&
      o.screenshotPath.isEmpty()) {
    qCritical() << "--render-screenshot requires a path";
    return EXIT_FAILURE;
  }
  if (optionSupplied(arguments, QStringLiteral("--render-size")) &&
      !parseRenderSize(o.renderSize, &o.requestedRenderSize)) {
    qCritical() << "--render-size requires WIDTHxHEIGHT between 1 and 16384";
    return EXIT_FAILURE;
  }
  const QString restoreStartupTest = optionValue(arguments, "--restore-startup-test");
  if (!restoreStartupTest.isEmpty()) {
    return testRestoreStartup(application, *app.theme, restoreStartupTest,
        arguments.contains("--couch"), o.requestedRenderSize, o.screenshotPath);
  }
  o.renderMode = !o.screenshotPath.isEmpty() || !o.cardExportPath.isEmpty();
  o.heroicOwnedFixture = o.renderMode && o.renderOverlay == QStringLiteral("heroic-owned");
  bool selfTest = false;
  for (const Case& entry : cases()) {
    o.*entry.selected = arguments.contains(QString::fromLatin1(entry.flag));
    selfTest = selfTest || o.*entry.selected;
  }
  o.couchNavigationTest = arguments.contains(QStringLiteral("--couch-navigation-test"));
  o.couchNavigationContract = arguments.contains(QStringLiteral("--couch-navigation-contract"));
  o.couchNavigationSweep = arguments.contains(QStringLiteral("--couch-navigation-sweep"));
  o.startupNavigationTest = arguments.contains(QStringLiteral("--startup-navigation-test"));
  o.controllerNavigationTest = arguments.contains(QStringLiteral("--controller-navigation-test"));
  o.ownedLayoutTest = arguments.contains(QStringLiteral("--owned-layout-test"));
  o.detailsDirectionTest = arguments.contains(QStringLiteral("--details-direction-test"));
  o.gogSettingsFixture = o.gogSettingsTest || o.renderOverlay == "gog-folders";
  o.linkedPreferenceFixture = o.linkedPreferenceTest || o.renderOverlay == "linked-preference" || o.renderOverlay == "linked-preference-missing";
  o.backupFixture = o.backupEditorTest || o.renderOverlay == QStringLiteral("backup-editor");
  o.sourceRescanFixture = o.renderMode && o.renderOverlay == "source-rescan";

  AppHarnessModes& modes = app.modes;
  modes.renderMode = o.renderMode;
  modes.selfTest = selfTest;
  modes.demoStatsFixture = o.statsFixture || o.repairNavigationFixture ||
                           o.couchNavigationContract || o.couchNavigationSweep;
  modes.navigationTest = o.couchNavigationTest || o.couchNavigationContract ||
                         o.couchNavigationSweep || o.startupNavigationTest ||
                         o.controllerNavigationTest;
  modes.startupNavigationTest = o.startupNavigationTest;
  modes.metadataInLibraryDatabase = o.renderOverlay == "library-repair-controls" || o.statsFixture;
  modes.backupFixture = o.backupFixture;
  modes.cardExport = !o.cardExportPath.isEmpty();
  if (o.gameModeTest) modes.gameModeCompositor = &fixtures.gameModeTestCompositor;
  return std::nullopt;
}

std::optional<int> Harness::prepare(Stage stage, AppContext& app) {
  switch (stage) {
  case Stage::BeforeSources: return prepareBeforeSources(*this, app);
  case Stage::SourcesBuilt: return prepareSourcesBuilt(*this, app);
  case Stage::LibraryFiltered: return prepareLibraryFiltered(*this, app);
  case Stage::SaveBackups: return prepareSaveBackups(*this, app);
  case Stage::Backups: return prepareBackups(*this, app);
  case Stage::CardExport: return prepareCardExport(*this, app);
  case Stage::Sessions: return prepareSessions(*this, app);
  case Stage::Clips: return prepareClips(*this, app);
  case Stage::BeforeLoad: return prepareBeforeLoad(*this, app);
  }
  return std::nullopt;
}

std::optional<int> Harness::windowReady(AppContext& app) {
  if (*app.isolatedTest && options.requestedRenderSize.isValid()) {
    app.quickWindow->resize(options.requestedRenderSize);
    app.quickWindow->setProperty("testRenderSize", options.requestedRenderSize);
  }
  if (options.renderMode) {
    if (auto exit = renderWindow(*this, app)) return exit;
  }
  startNavigation(*this, app);
  return std::nullopt;
}

bool Harness::drive(AppContext& app) {
  for (const Case& entry : cases()) {
    if (options.*entry.selected) {
      entry.drive(*this, app);
      return true;
    }
  }
  return false;
}

int Harness::finish(AppContext&, int result) {
  if (options.heroicOwnedFixture && !fixtures.heroicOwnedDispatchComplete) {
    qCritical() << "Owned Heroic action quit before dispatch verification completed";
    return EXIT_FAILURE;
  }
  return result;
}

} // namespace SelfTest

#include "gamemode/GameModeController.h"
#include "gamemode/GameModeDesktop.h"
#include "gamemode/GameModeSession.h"
#include "gamemode/GameModeShortcut.h"
#include "gamemode/LaunchHold.h"
#include "tracking/ProcFs.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <atomic>
#include <functional>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

GameModeOutput output(int id, const QString& name, const QString& description, bool enabled,
                      bool focused, const QString& workspace) {
  GameModeOutput result;
  result.id = id;
  result.name = name;
  result.description = description;
  result.enabled = enabled;
  result.focused = focused;
  result.workspace = enabled ? workspace : QString{};
  return result;
}

const QString kPlaceholderAddress = QStringLiteral("0x55d0c0ffee99");

// A compositor that behaves the way the real one was observed to: enabling an output
// takes a few polls to go live and gives it a workspace, placing a window creates the
// target workspace on the output that was focused first.
class FakeCompositor final : public GameModeCompositor {
public:
  // A window owned by another process, such as a game or its launcher.
  struct OtherWindow {
    QString address;
    QString workspace;
  };

  std::function<void()> beforeOutputs;
  std::function<void()> beforePlace;
  std::function<void()> beforeGameScan;
  std::function<void()> beforeWindowFocus;
  std::function<void()> beforeWorkspaceCheck;
  bool usable = true;
  bool enableFails = false;
  int disableFailuresRemaining = 0;
  // -1 never goes live.
  int livePolls = 2;
  bool placeFails = false;
  bool holdFails = false;
  bool tradeFails = false;
  bool returnFails = false;
  bool gameScanFails = false;
  int gameScanFailuresRemaining = 0;
  bool moveWorkspaceFails = false;
  bool focusScanFails = false;
  bool windowMapped = true;
  QVector<GameModeGameWindow> games;
  QVector<GameModeProcess> alive;
  GameModeDesktopFocus currentFocus;
  QString gameWorkspaceOutput;
  QVector<qint64> gameFocusOwners;

  // Set by the controller's placeholder callback.
  bool placeholderShown = false;
  int placeholderPolls = 2;
  GameModeWindow placeholder;
  QVector<GameModeOutput> list;
  GameModeWindow window;
  QVector<OtherWindow> others;
  QStringList log;
  int pendingPolls = -1;
  QString pendingOutput;

  void tick() {
    if (pendingPolls > 0 && --pendingPolls == 0) {
      for (GameModeOutput& entry : list) {
        if (entry.name == pendingOutput) {
          entry.enabled = true;
          entry.workspace = QStringLiteral("9");
        }
      }
    }
  }

  bool available() override { return usable; }
  QVector<GameModeOutput> outputs(QString*) override {
    if (beforeOutputs)
      beforeOutputs();
    return list;
  }
  bool setOutputEnabled(const QString& name, bool enabled, QString*) override {
    log.append(QStringLiteral("%1 %2").arg(enabled ? "enable" : "disable", name));
    if (!enabled && disableFailuresRemaining > 0) {
      --disableFailuresRemaining;
      return false;
    }
    if (enabled && enableFails) {
      return false;
    }
    if (enabled) {
      pendingOutput = name;
      pendingPolls = livePolls;
    } else {
      pendingPolls = -1;
      for (GameModeOutput& entry : list) {
        if (entry.name == name) {
          entry.enabled = false;
          entry.workspace.clear();
        }
      }
    }
    return true;
  }
  GameModeWindow windowForPid(qint64) override { return windowMapped ? window : GameModeWindow{}; }
  int otherWindowsOn(const QString& workspace, qint64) override {
    if (beforeWorkspaceCheck)
      beforeWorkspaceCheck();
    return otherWindowAddressesOn(workspace, 0).size();
  }
  QStringList otherWindowAddressesOn(const QString& workspace, qint64) override {
    QStringList addresses;
    for (const OtherWindow& entry : others) {
      if (entry.workspace == workspace) {
        addresses.append(entry.address);
      }
    }
    return addresses;
  }
  bool setWindowMode(const QString& address, int mode, int clientMode, QString*) override {
    if (address != window.address)
      return false;
    window.fullscreenMode = mode;
    window.fullscreenClient = clientMode;
    window.fullscreen = mode != 0;
    return true;
  }
  bool gameWindows(const QString&, qint64, QVector<GameModeGameWindow>* result,
                   QString* error) override {
    if (beforeGameScan)
      beforeGameScan();
    if (gameScanFails || gameScanFailuresRemaining > 0) {
      if (gameScanFailuresRemaining > 0)
        --gameScanFailuresRemaining;
      if (error)
        *error = QStringLiteral("game scan failed");
      return false;
    }
    *result = games;
    return true;
  }
  bool processAlive(const GameModeProcess& process) override {
    for (const auto& candidate : alive)
      if (candidate == process)
        return true;
    return false;
  }
  bool desktopFocus(GameModeDesktopFocus* focus, QString* error) override {
    if (focusScanFails) {
      if (error)
        *error = QStringLiteral("focus scan failed");
      return false;
    }
    if (currentFocus.output.isEmpty()) {
      for (const auto& entry : list)
        if (entry.focused)
          currentFocus = {entry.name, entry.workspace, window.address};
    }
    *focus = currentFocus;
    return !focus->output.isEmpty() && !focus->workspace.isEmpty();
  }
  bool moveWorkspace(const QString& workspace, const QString& target, QString* error) override {
    log.append(QStringLiteral("move-workspace %1 %2").arg(workspace, target));
    if (moveWorkspaceFails) {
      if (error)
        *error = QStringLiteral("workspace move failed");
      return false;
    }
    gameWorkspaceOutput = target;
    return true;
  }
  GameModeWindow placeholderForPid(qint64) override {
    // Mapping takes a poll, as it does for a real window.
    if (placeholderShown && placeholderPolls > 0 && --placeholderPolls == 0) {
      placeholder = {kPlaceholderAddress, QStringLiteral("special:omakade"), {}};
    }
    return placeholderShown ? placeholder : GameModeWindow{};
  }
  bool holdPlaceholder(QString*) override {
    log.append(QStringLiteral("hold"));
    return !holdFails;
  }
  // Off unless a test models Hyprland's open rule.
  bool openSupported = false;
  QString openWorkspace;
  bool prepareOpen(const QString& workspace, const QString& output, QString*) override {
    if (!openSupported) return false;
    openWorkspace = workspace;
    log.append(workspace.isEmpty() ? QStringLiteral("open withdrawn")
                                   : QStringLiteral("open %1 %2").arg(workspace, output));
    return true;
  }
  bool placeWindow(const QString& address, const QString& workspace, const QString& target,
                   const QString& held, QString*) override {
    if (beforePlace)
      beforePlace();
    log.append(
        held.isEmpty()
            ? QStringLiteral("place %1 %2 %3").arg(address, workspace, target)
            : QStringLiteral("place %1 %2 %3 holding %4").arg(address, workspace, target, held));
    if (placeFails) {
      return false;
    }
    if (!held.isEmpty()) {
      placeholder.workspace = window.workspace;
    }
    window.workspace = workspace;
    window.output = target;
    currentFocus = {target, workspace, address};
    gameWorkspaceOutput = target;
    for (GameModeOutput& entry : list) {
      if (entry.name == target) {
        entry.workspace = workspace;
      }
    }
    return true;
  }
  bool returnWindow(const QString& address, const QString& workspace, const QString& held,
                    QString*) override {
    if (!held.isEmpty()) {
      log.append(QStringLiteral("trade %1 %2").arg(address, held));
      if (tradeFails) {
        return false;
      }
      window.workspace = placeholder.workspace;
      return true;
    }
    log.append(QStringLiteral("return %1 %2").arg(address, workspace));
    if (returnFails)
      return false;
    for (OtherWindow& entry : others) {
      if (entry.address == address) {
        entry.workspace = workspace;
        return true;
      }
    }
    window.workspace = workspace;
    return true;
  }
  bool focusWindow(const QString& address, QString*) override {
    if (beforeWindowFocus)
      beforeWindowFocus();
    log.append(QStringLiteral("focus-window %1").arg(address));
    currentFocus.address = address;
    return true;
  }
  bool focusGameWindow(const GameModeGameWindow& game, qint64 ownerPid, QString*) override {
    gameFocusOwners.append(ownerPid);
    log.append(QStringLiteral("game-mode %1 %2 %3")
                   .arg(game.address)
                   .arg(game.fullscreen)
                   .arg(game.fullscreenClient));
    if (ownerPid > 0)
      window.fullscreen = false;
    for (auto& candidate : games)
      if (candidate.address == game.address && candidate.process == game.process) {
        candidate.fullscreen = game.fullscreen;
        candidate.fullscreenClient = game.fullscreenClient;
      }
    return focusWindow(game.address, nullptr);
  }
  bool focusWorkspace(const QString& workspace, QString*) override {
    log.append(QStringLiteral("focus-workspace %1").arg(workspace));
    currentFocus.workspace = workspace;
    currentFocus.address.clear();
    return true;
  }
  bool focusOutput(const QString& name, QString*) override {
    log.append(QStringLiteral("focus-output %1").arg(name));
    currentFocus.output = name;
    return true;
  }
  // Off unless a test holds launcher windows.
  bool canHold = false;
  bool setSteamHold(const QString& workspace, QString*) override {
    if (!canHold) return false;
    log.append(QStringLiteral("steam-hold %1").arg(workspace));
    return true;
  }
  QVector<GameModeHeldWindow> held;
  bool setLaunchHold(const QString& workspace, QString*) override {
    if (!canHold) return false;
    log.append(QStringLiteral("launch-hold %1").arg(workspace));
    return true;
  }
  bool heldWindows(QVector<GameModeHeldWindow>* windows, QString*) override {
    *windows = held;
    return canHold;
  }
  bool releaseWindow(const QString& address, const QString& workspace, QString*) override {
    held.removeIf([&](const auto& window) { return window.address == address; });
    log.append(QStringLiteral("release-held %1 %2").arg(address, workspace));
    return true;
  }
};

class FakeAudio final : public GameModeAudio {
public:
  bool usable = true;
  bool setFails = false;
  QVector<GameModeSink> list{{QStringLiteral("headset"), QStringLiteral("Headset")}};
  QString current = QStringLiteral("headset");
  QStringList log;
  // A sink that only exists once this many polls have passed, like HDMI audio.
  GameModeSink late;
  int latePolls = -1;

  QVector<GameModeStream> inputs;
  bool streamScanFails = false;
  int streamScanFailuresRemaining = 0;
  int muteWrites = 0;
  int muteFailAt = -1;
  bool unmuteFails = false;
  bool streams(QVector<GameModeStream>* result, QString* error) override {
    if (streamScanFails || streamScanFailuresRemaining > 0) {
      if (streamScanFailuresRemaining > 0)
        --streamScanFailuresRemaining;
      if (error)
        *error = QStringLiteral("audio scan failed");
      return false;
    }
    *result = inputs;
    return true;
  }
  bool setStreamMuted(const GameModeStream& expected, bool muted, QString* error) override {
    log.append(QStringLiteral("mute %1 %2").arg(expected.index).arg(muted));
    if ((muted && ++muteWrites == muteFailAt) || (!muted && unmuteFails)) {
      if (error)
        *error = QStringLiteral("mute write failed");
      return false;
    }
    for (auto& stream : inputs)
      if (stream.sameStream(expected)) {
        stream.muted = muted;
        return true;
      }
    if (error)
      *error = QStringLiteral("stream changed");
    return false;
  }

  void tick() {
    if (latePolls > 0 && --latePolls == 0) {
      list.append(late);
    }
  }
  bool available() override { return usable; }
  QVector<GameModeSink> sinks(QString*) override { return list; }
  QString defaultSink() override { return current; }
  bool setDefaultSink(const QString& name, QString*) override {
    log.append(QStringLiteral("default %1").arg(name));
    if (setFails) {
      return false;
    }
    current = name;
    return true;
  }
};

class FakeNotifications final : public GameModeNotifications {
public:
  bool usable = true;
  bool quiet = false;
  bool setFails = false;
  std::function<void()> beforeSet;
  QStringList log;
  bool available() override { return usable; }
  bool silenced(bool* silenced) override {
    *silenced = quiet;
    return true;
  }
  bool setSilenced(bool silenced) override {
    if (beforeSet)
      beforeSet();
    log.append(silenced ? QStringLiteral("silence") : QStringLiteral("unsilence"));
    if (setFails)
      return false;
    quiet = silenced;
    return true;
  }
};

const QString kDesk = QStringLiteral("DP-2");
const QString kTv = QStringLiteral("HDMI-A-2");
const QString kTvDescription = QStringLiteral("Samsung Electric Company QBQ90 0x01000E00");
const QString kTvSink = QStringLiteral("alsa_output.hdmi-stereo");
const QString kAddress = QStringLiteral("0x55d0c0ffee00");

// Only what a launch hold uses. Called from worker threads, so every access is locked.
class HoldCompositor final : public GameModeCompositor {
public:
  bool available() override { return true; }
  QVector<GameModeOutput> outputs(QString* = nullptr) override {
    return {{0, kTv, kTvDescription, true, true, 2560, 1440, GameModeController::workspace()}};
  }
  bool setOutputEnabled(const QString&, bool, QString* = nullptr) override { return true; }
  GameModeWindow windowForPid(qint64) override {
    return {kAddress, GameModeController::workspace(), kTv};
  }
  int otherWindowsOn(const QString&, qint64) override { return 0; }
  QStringList otherWindowAddressesOn(const QString&, qint64) override { return {}; }
  GameModeWindow placeholderForPid(qint64) override { return {}; }
  bool holdPlaceholder(QString* = nullptr) override { return true; }
  bool placeWindow(const QString&, const QString&, const QString&, const QString&,
                   QString* = nullptr) override {
    return true;
  }
  bool returnWindow(const QString&, const QString&, const QString&, QString* = nullptr) override {
    return true;
  }
  bool focusWindow(const QString& address, QString* = nullptr) override {
    record(QStringLiteral("focus ") + address);
    return true;
  }
  bool setWindowMode(const QString& address, int mode, int client, QString* = nullptr) override {
    record(QStringLiteral("mode %1 %2 %3").arg(address).arg(mode).arg(client));
    return true;
  }
  bool focusWorkspace(const QString&, QString* = nullptr) override { return true; }
  bool focusOutput(const QString&, QString* = nullptr) override { return true; }
  bool setLaunchHold(const QString& workspace, QString* = nullptr) override {
    record(QStringLiteral("hold ") + workspace);
    return true;
  }
  bool setSteamHold(const QString& workspace, QString* = nullptr) override {
    record(QStringLiteral("steam-hold ") + workspace);
    return true;
  }
  bool heldWindows(QVector<GameModeHeldWindow>* windows, QString*) override {
    QMutexLocker lock(&mutex);
    *windows = held;
    return true;
  }
  bool releaseWindow(const QString& address, const QString& workspace, QString* = nullptr) override {
    QMutexLocker lock(&mutex);
    held.removeIf([&](const auto& window) { return window.address == address; });
    log.append(QStringLiteral("release %1 %2").arg(address, workspace));
    return true;
  }
  bool presentWindow(const QString& address, const QString& workspace, int width, int height,
                     QString* = nullptr) override {
    QMutexLocker lock(&mutex);
    for (const auto& window : std::as_const(held))
      if (window.address == address) shown.append(window);
    held.removeIf([&](const auto& window) { return window.address == address; });
    log.append(QStringLiteral("present %1 %2 %3x%4").arg(address, workspace).arg(width).arg(height));
    return true;
  }
  bool holdWindow(const QString& address, QString* = nullptr) override {
    QMutexLocker lock(&mutex);
    for (const auto& window : std::as_const(shown))
      if (window.address == address) held.append(window);
    shown.removeIf([&](const auto& window) { return window.address == address; });
    log.append(QStringLiteral("hold-window %1").arg(address));
    return true;
  }
  void open(const GameModeHeldWindow& window) {
    QMutexLocker lock(&mutex);
    held.append(window);
  }
  void close(const QString& address) {
    QMutexLocker lock(&mutex);
    held.removeIf([&](const auto& window) { return window.address == address; });
    shown.removeIf([&](const auto& window) { return window.address == address; });
  }
  QStringList entries() {
    QMutexLocker lock(&mutex);
    return log;
  }

private:
  void record(const QString& entry) {
    QMutexLocker lock(&mutex);
    log.append(entry);
  }
  QMutex mutex;
  QVector<GameModeHeldWindow> held;
  QVector<GameModeHeldWindow> shown;
  QStringList log;
};

} // namespace

class GameModeTests final : public QObject {
  Q_OBJECT

private:
  QTemporaryDir m_directory;
  FakeCompositor m_compositor;
  FakeAudio m_audio;
  FakeNotifications m_notifications;
  int m_slept = 0;

  [[nodiscard]] QString statePath() const { return m_directory.filePath("game-mode-state.json"); }

  GameModeController controller(bool ownerAlive = false) {
    return GameModeController(
        &m_compositor, &m_audio, &m_notifications, statePath(),
        [this](int milliseconds) {
          m_slept += milliseconds;
          m_compositor.tick();
          m_audio.tick();
        },
        [ownerAlive](qint64) { return ownerAlive; });
  }

  // A desk monitor with Omakade on workspace 3, and a television that is off.
  void deskAndTv(bool tvEnabled) {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3")),
        output(1, kTv, kTvDescription, tvEnabled, false, QStringLiteral("5"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
  }

  [[nodiscard]] static GameModeSettings tvSettings(const QString& sink = {}) {
    GameModeSettings settings;
    settings.outputName = kTv;
    settings.outputDescription = kTvDescription;
    settings.sinkName = sink;
    return settings;
  }

  void retainedGame(bool alreadyMuted = false) {
    const GameModeProcess process{200, 42, QStringLiteral("367520")};
    m_compositor.games = {{QStringLiteral("0x9a01"), process}};
    m_compositor.alive = {process};
    m_audio.inputs = {{1, process, QStringLiteral("serial-1"), alreadyMuted},
                      {2, {300, 50, {}}, QStringLiteral("browser"), false}};
    m_compositor.others = {{QStringLiteral("0x9a01"), GameModeController::workspace()}};
  }

private slots:
  void init() {
    m_compositor = FakeCompositor{};
    m_audio = FakeAudio{};
    m_notifications = FakeNotifications{};
    m_slept = 0;
    QFile::remove(statePath());
  }

  void retainedParkResumeRepeatKeepsWorkspaceAndFreshFocus() {
    deskAndTv(false);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_compositor.currentFocus.address = QStringLiteral("0x9a01");
    auto parked = game.park(100);
    QVERIFY2(parked.ok, qPrintable(parked.error));
    QVERIFY(game.parked());
    QVERIFY(!game.active());
    QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
    QCOMPARE(m_compositor.others.first().workspace, GameModeController::workspace());
    QCOMPARE(m_compositor.gameWorkspaceOutput, kDesk);
    QVERIFY(m_compositor.log.indexOf("move-workspace name:omakade DP-2") <
            m_compositor.log.indexOf("disable HDMI-A-2"));
    QVERIFY(m_audio.inputs.first().muted);
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(!game.state().enabledOutput);
    QVERIFY(!game.state().windowPlaced);
    QVERIFY(!game.state().silencedNotifications);
    QVERIFY(game.recover().ok);
    QVERIFY(game.parked());

    // The owner is allowed to change both focus and sound while away.
    m_compositor.currentFocus = {kDesk, QStringLiteral("7"), QStringLiteral("0xd00d")};
    const auto resumed = game.resume(tvSettings(), 100);
    QVERIFY2(resumed.ok, qPrintable(resumed.error));
    QVERIFY(resumed.resumedGame);
    QVERIFY(game.active());
    QVERIFY(!game.parked());
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(game.focusRetainedGame());
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QCOMPARE(m_compositor.gameWorkspaceOutput, kTv);
    QVERIFY(game.park(100).ok);
    QCOMPARE(m_compositor.currentFocus.output, kDesk);
    QCOMPARE(m_compositor.currentFocus.workspace, QStringLiteral("7"));
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0xd00d"));
  }

  void retainedStreamsPreserveMutedAndMuteNewExactSteamWitness() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame(true);
    QVERIFY(game.park(100).ok);
    const GameModeProcess helper{201, 43, QStringLiteral("367520")};
    m_audio.inputs.append({3, helper, QStringLiteral("serial-3"), false});
    QVERIFY(game.refreshParked().ok);
    QVERIFY(m_audio.inputs.last().muted);
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(m_audio.inputs.first().muted); // it was muted before us
    QVERIFY(!m_audio.inputs.last().muted);
  }

  void emptyParkDiscoversLateGameAndResumesExactWindow_data() {
    QTest::addColumn<bool>("steam");
    QTest::newRow("direct-non-steam") << false;
    QTest::newRow("exact-steam-witness") << true;
  }

  void emptyParkDiscoversLateGameAndResumesExactWindow() {
    QFETCH(bool, steam);
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(game.park(100).ok);
    QVERIFY(game.state().games.isEmpty());
    retainedGame(); // Launch maps only after the library has returned to desktop.
    if (!steam) {
      m_compositor.games[0].process.steamAppId.clear();
      m_audio.inputs[0].process.steamAppId.clear();
    } else {
      m_audio.inputs.append({3, {201, 43, QStringLiteral("367520")},
                             QStringLiteral("serial-3"), false});
    }
    m_compositor.games[0].fullscreen = 1;
    m_compositor.games[0].fullscreenClient = 2;
    const auto focus = m_compositor.currentFocus.address;
    m_compositor.log.clear();
    for (int poll = 0; poll < 2; ++poll) {
      QVERIFY(game.refreshParked().ok);
      QCOMPARE(game.state().games.size(), 1);
      QCOMPARE(game.state().games.first().address, QStringLiteral("0x9a01"));
      QVERIFY(m_audio.inputs.first().muted);
      QVERIFY(!m_audio.inputs.at(1).muted);
      if (steam) QVERIFY(m_audio.inputs.last().muted);
      QCOMPARE(m_compositor.currentFocus.address, focus);
      QVERIFY(m_compositor.log.isEmpty());
    }
    const auto resumed = game.resume(tvSettings(), 100);
    QVERIFY2(resumed.ok, qPrintable(resumed.error));
    QVERIFY(resumed.resumedGame);
    QVERIFY(!m_audio.inputs.first().muted);
    if (steam) QVERIFY(!m_audio.inputs.last().muted);
    QVERIFY(game.focusRetainedGame());
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QCOMPARE(game.state().games.first().fullscreen, 1);
    QCOMPARE(game.state().games.first().fullscreenClient, 2);
    QVERIFY(game.exit(100).ok);
  }

  void emptyParkDiscoveryFailureKeepsAuthorityUntilRetry() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(game.park(100).ok);
    retainedGame();
    m_compositor.gameScanFails = true;
    m_compositor.log.clear();
    const auto failed = game.refreshParked();
    QVERIFY(!failed.ok);
    QVERIFY(failed.error.contains("scan failed"));
    QVERIFY(game.parked());
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!game.resume({}, 100).ok);
    m_compositor.gameScanFails = false;
    QVERIFY(game.refreshParked().ok);
    QVERIFY(m_audio.inputs.first().muted);
    QVERIFY(game.exit(100).ok);
    QVERIFY(!m_audio.inputs.first().muted);
  }

  void emptyParkRefusesUnverifiedArrivals_data() {
    QTest::addColumn<int>("identity");
    QTest::newRow("missing-start") << 0;
    QTest::newRow("reused-pid") << 1;
    QTest::newRow("missing-window") << 2;
    QTest::newRow("owner-window") << 3;
  }

  void emptyParkRefusesUnverifiedArrivals() {
    QFETCH(int, identity);
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(game.park(100).ok);
    retainedGame();
    if (identity == 0) m_compositor.games[0].process.procStart = -1;
    if (identity == 1) ++m_compositor.games[0].process.procStart;
    if (identity == 2) m_compositor.games[0].address.clear();
    if (identity == 3) {
      m_compositor.games[0].process.pid = game.state().ownerPid;
      m_compositor.alive = {m_compositor.games.first().process};
    }
    m_compositor.log.clear();
    QVERIFY(!game.refreshParked().ok);
    QVERIFY(game.parked());
    QVERIFY(game.state().games.isEmpty());
    QVERIFY(game.state().mutedStreams.isEmpty());
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(QFile::exists(statePath()));
    m_compositor.games.clear();
    QVERIFY(game.exit(100).ok);
  }

  void emptyParkLateGameAudioRefusalExposesWithoutScopeExpansion_data() {
    QTest::addColumn<int>("failure");
    QTest::newRow("audio-unavailable") << 0;
    QTest::newRow("unstable-stream") << 1;
    QTest::newRow("shared-wine") << 2;
    QTest::newRow("shared-flatpak") << 3;
  }

  void emptyParkLateGameAudioRefusalExposesWithoutScopeExpansion() {
    QFETCH(int, failure);
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(game.park(100).ok);
    retainedGame();
    if (failure == 0) m_audio.usable = false;
    if (failure == 1) m_audio.inputs[0].token.clear();
    if (failure == 2) {
      m_compositor.games[0].process.winePrefix = "/shared";
      m_audio.inputs[1].process.winePrefix = "/shared";
    }
    if (failure == 3) {
      m_compositor.games[0].process.flatpakAppId = "shared.app";
      m_audio.inputs[1].process.flatpakAppId = "shared.app";
    }
    QVERIFY(!game.refreshParked().ok);
    QVERIFY(!game.parked());
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedStreamReuseAndExitNeverUnmuteNewOwners() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_audio.inputs[0].process = {999, 999, {}};
    m_audio.inputs[0].token = QStringLiteral("replacement");
    m_audio.inputs[0].muted = true;
    const auto focus = m_compositor.currentFocus.address;
    m_compositor.alive.clear(); // launcher window may remain, game process has ended
    m_compositor.games.clear();
    m_compositor.log.clear();
    QVERIFY(game.refreshParked().ok);
    QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
    QVERIFY(game.state().games.isEmpty());
    QVERIFY(game.state().mutedStreams.isEmpty());
    QVERIFY(game.state().lastGameWindow.isEmpty());
    QVERIFY(m_audio.inputs.first().muted);
    QCOMPARE(m_compositor.currentFocus.address, focus);
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(QFile::exists(statePath()));
    const auto resumed = game.resume(tvSettings(), 100);
    QVERIFY(resumed.ok);
    QVERIFY(!resumed.resumedGame);
    QVERIFY(game.exit(100).ok);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedRefreshIsFocusNeutralAndReleasesGoneStreams() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_audio.inputs.removeFirst();
    m_compositor.log.clear();
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.parked());
    QVERIFY(game.state().mutedStreams.isEmpty());
    QVERIFY(m_compositor.log.isEmpty());
  }

  void retainedGameExitDoesNotNeedAudioToKeepLibrary() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_compositor.alive.clear();
    m_compositor.games.clear();
    m_audio.usable = false;
    m_audio.log.clear();
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.parked());
    QVERIFY(game.state().games.isEmpty());
    QVERIFY(game.state().mutedStreams.isEmpty());
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedParkRefusesUnknownPortsAndAmbiguousAudio() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_compositor.gameScanFails = true;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active());
    m_compositor.gameScanFails = false;
    m_audio.streamScanFails = true;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active());
    m_audio.streamScanFails = false;
    m_compositor.games[0].process.winePrefix = QStringLiteral("/shared");
    m_audio.inputs[1].process.winePrefix = QStringLiteral("/shared");
    const auto refused = game.park(100);
    QVERIFY(!refused.ok);
    QVERIFY(refused.error.contains("attributed"));
    QVERIFY(!m_audio.inputs.first().muted); // rollback of the preceding direct mute
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(game.active());
  }

  void retainedParkMuteFailureRollsBackWithoutHidingGame() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_audio.inputs.append(
        {3, {201, 43, QStringLiteral("367520")}, QStringLiteral("serial-3"), false});
    m_audio.muteFailAt = 2;
    const auto failed = game.park(100);
    QVERIFY(!failed.ok);
    QVERIFY(game.active());
    QVERIFY(!game.parked());
    QVERIFY(!m_audio.inputs.first().muted);
    QCOMPARE(m_compositor.window.workspace, GameModeController::workspace());
    QVERIFY(QFile::exists(statePath()));
  }

  void retainedParkRelocationFailureKeepsTvOnAndRollsBack() {
    deskAndTv(false);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_compositor.moveWorkspaceFails = true;
    const auto failed = game.park(100);
    QVERIFY(!failed.ok);
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_compositor.list.at(1).enabled);
    QVERIFY(!m_compositor.log.contains("disable HDMI-A-2"));
    QVERIFY(game.active() || game.parked());
  }

  void retainedRollbackPreservesSessionSettings() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    auto settings = tvSettings(kTvSink);
    settings.silenceNotifications = false;
    auto game = controller();
    QVERIFY(game.enter(settings, 100).ok);
    retainedGame();
    m_compositor.disableFailuresRemaining = 1;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active());
    QCOMPARE(m_audio.current, kTvSink);
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(game.exit(100).ok);
  }

  void parkWithoutGamesRequestsUiLeaveBeforeRetainingLibrary() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter({}, 100).ok);
    bool leaving = false;
    game.setBeforeParkRestore([&] {
      leaving = true;
      QCOMPARE(m_compositor.window.workspace, GameModeController::workspace());
    });
    QVERIFY(game.park(100).ok);
    QVERIFY(leaving);
    QVERIFY(!game.active());
    QVERIFY(game.parked());
    QVERIFY(game.state().games.isEmpty());
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.parked());
    QVERIFY(game.exit(100).ok);
    QVERIFY(!QFile::exists(statePath()));
  }

  void libraryOnlyColdAndWarmRetention_data() {
    QTest::addColumn<bool>("cold");
    QTest::addColumn<int>("mode");
    QTest::newRow("cold") << true << 0;
    QTest::newRow("warm-tiled") << false << 0;
    QTest::newRow("warm-maximized") << false << 1;
    QTest::newRow("warm-couch") << false << 2;
  }

  void libraryOnlyColdAndWarmRetention() {
    QFETCH(bool, cold);
    QFETCH(int, mode);
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("TV audio")});
    m_compositor.window.fullscreenMode = mode;
    m_compositor.window.fullscreenClient = mode;
    auto game = controller();
    game.setTemporaryWindow(cold);
    game.setPlaceholder([&](bool shown) { m_compositor.placeholderShown = shown; });
    QStringList visibility;
    game.setWindowVisibility([&](bool shown) {
      visibility.append(shown ? "show" : "hide");
      m_compositor.windowMapped = shown;
    });
    const auto settings = tvSettings(kTvSink);
    QVERIFY(game.enter(settings, 100).ok);
    const auto ownerStart = game.state().ownerStart;
    for (int cycle = 0; cycle < 3; ++cycle) {
      QVERIFY(game.park(100).ok);
      QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
      QCOMPARE(game.state().ownerStart, ownerStart);
      QVERIFY(game.state().games.isEmpty());
      QVERIFY(game.state().mutedStreams.isEmpty());
      QVERIFY(!m_notifications.quiet);
      QCOMPARE(m_audio.current, QStringLiteral("headset"));
      QVERIFY(!m_compositor.list.at(1).enabled);
      if (cold)
        QVERIFY(!m_compositor.windowMapped);
      else {
        QCOMPARE(m_compositor.window.fullscreenMode, mode);
        QCOMPARE(m_compositor.window.fullscreenClient, mode);
      }
      m_compositor.log.clear();
      QVERIFY(game.refreshParked().ok);
      QVERIFY(game.refreshParked().ok);
      QVERIFY(game.parked());
      QVERIFY(m_compositor.log.isEmpty());
      m_compositor.currentFocus = {kDesk, QString::number(7 + cycle), QStringLiteral("0xcafe")};
      if (!cold)
        m_compositor.window.workspace = QString::number(7 + cycle);
      const auto resumed = game.resume(settings, 100);
      QVERIFY2(resumed.ok, qPrintable(resumed.error));
      QVERIFY(!resumed.resumedGame);
      QVERIFY(game.active());
      QCOMPARE(m_audio.current, kTvSink);
      QVERIFY(m_notifications.quiet);
      QVERIFY(m_compositor.windowMapped);
      QVERIFY(QFile::exists(statePath()));
    }
    QVERIFY(game.park(100).ok);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(game.phase(), GameModePhase::Ended);
    QVERIFY(!QFile::exists(statePath()));
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_notifications.quiet);
    if (cold)
      QCOMPARE(visibility.count("hide"), 4);
    else
      QCOMPARE(m_compositor.window.workspace, QStringLiteral("9"));
  }

  void libraryOnlyParkDoesNotNeedAudioButRequiresSafeDiscovery() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter({}, 100).ok);
    m_audio.usable = false;
    QVERIFY(game.park(100).ok);
    m_compositor.usable = false;
    QVERIFY(!game.refreshParked().ok); // Unavailable discovery is unknown, not empty.
    QVERIFY(game.parked());
    QVERIFY(QFile::exists(statePath()));
    const auto unavailable = game.resume({}, 100);
    QVERIFY(!unavailable.ok);
    QVERIFY(game.parked());
    m_compositor.usable = true;
    QVERIFY(game.resume({}, 100).ok);
    m_compositor.gameScanFails = true;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active());
    m_compositor.gameScanFails = false;
    m_compositor.usable = false;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active());
    m_compositor.usable = true;
    QVERIFY(game.exit(100).ok);
  }

  void libraryOnlyFailedParkAndResumeKeepRecoveryOwnership() {
    deskAndTv(false);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_compositor.disableFailuresRemaining = 1;
    QVERIFY(!game.park(100).ok);
    QVERIFY(game.active()); // The failed return rolls back the library too.
    QVERIFY(game.park(100).ok);
    m_compositor.enableFails = true;
    QVERIFY(!game.resume(tvSettings(), 100).ok);
    QVERIFY(game.parked());
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(!game.state().retentionEnding);
    m_compositor.enableFails = false;
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
  }

  void sessionToggleRetainsLibraryAndShutdownEndsIt() {
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy entering(&session, &GameModeSession::entering);
    QSignalSpy parking(&session, &GameModeSession::parking);
    QSignalSpy parked(&session, &GameModeSession::parkedOnDesktop);
    QSignalSpy resumed(&session, &GameModeSession::resumed);
    QSignalSpy ended(&session, &GameModeSession::exited);
    session.toggle();
    QTRY_VERIFY(session.active() && !session.busy());
    for (int cycle = 0; cycle < 2; ++cycle) {
      session.toggle();
      QTRY_VERIFY(session.parked() && !session.busy());
      QVERIFY(session.hasSession());
      QCOMPARE(ended.size(), 0);
      session.selectDisplay(2);
      session.selectSound(1);
      session.setSilenceNotifications(false);
      QVERIFY(session.settings().outputName.isEmpty());
      QVERIFY(session.settings().sinkName.isEmpty());
      QVERIFY(session.silenceNotifications());
      session.toggle();
      QTRY_VERIFY(session.active() && !session.busy());
      session.focusGame(); // Without a game, focus the retained library window.
      QTRY_COMPARE(m_compositor.currentFocus.address, kAddress);
    }
    QCOMPARE(entering.size(), 1);
    QCOMPARE(parking.size(), 2);
    QCOMPARE(parked.size(), 2);
    QCOMPARE(resumed.size(), 2);
    session.park();
    QTRY_VERIFY(session.parked() && !session.busy());
    session.shutdown();
    QVERIFY(!session.hasSession());
    QVERIFY(!QFile::exists(statePath()));
  }

  void sessionPreparesBeforeDesktopEffectsAndCancelsFailedResume() {
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    bool prepared = false;
    bool retainNavigation = false;
    bool capturedBeforePrepare = false;
    connect(&session, &GameModeSession::entering, this,
            [&] { capturedBeforePrepare = !prepared; });
    connect(&session, &GameModeSession::preparing, this, [&](bool retained) {
      prepared = true;
      retainNavigation = retained;
    });
    // A worker effect observes completed GUI preparation, rather than depending
    // on a queued completion signal after the window has already been exposed.
    bool preparedBeforePlace = false;
    m_compositor.beforePlace = [&] { preparedBeforePlace = prepared; };
    QSignalSpy cancelled(&session, &GameModeSession::preparationCancelled);
    m_compositor.placeFails = true;
    session.enter();
    QTRY_VERIFY(!session.busy());
    QVERIFY(!session.hasSession());
    QCOMPARE(cancelled.size(), 1);
    prepared = false;
    m_compositor.placeFails = false;
    session.enter();
    QVERIFY(prepared);
    QVERIFY(capturedBeforePrepare);
    QVERIFY(!retainNavigation);
    QTRY_VERIFY(session.active() && !session.busy());
    QVERIFY(preparedBeforePlace);
    session.park();
    QTRY_VERIFY(session.parked() && !session.busy());
    prepared = false;
    preparedBeforePlace = false;
    m_compositor.placeFails = true;
    session.enter();
    QVERIFY(prepared);
    QVERIFY(retainNavigation);
    QTRY_VERIFY(!session.busy());
    QVERIFY(preparedBeforePlace);
    QVERIFY(session.parked());
    QCOMPARE(cancelled.size(), 2);
    m_compositor.placeFails = false;
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    QCOMPARE(cancelled.size(), 2);
    session.exit();
    QTRY_VERIFY(!session.hasSession() && !session.busy());
  }

  void rejectedSessionParkPreservesEntrySnapshot() {
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy entered(&session, &GameModeSession::entered);
    QSignalSpy restored(&session, &GameModeSession::resumed);
    QSignalSpy leaving(&session, &GameModeSession::leaving);
    session.enter();
    QTRY_COMPARE(entered.size(), 1);
    m_compositor.gameScanFails = true;
    session.park();
    QTRY_VERIFY(!session.busy());
    QCOMPARE(entered.size(), 1);
    QCOMPARE(restored.size(), 0);
    QCOMPARE(leaving.size(), 0);
    QVERIFY(session.active());
    m_compositor.gameScanFails = false;
    session.exit();
    QTRY_VERIFY(!session.busy());
  }

  void explicitEndQueuesAcrossBusySessionChanges_data() {
    QTest::addColumn<int>("change");
    QTest::newRow("enter") << 0;
    QTest::newRow("park") << 1;
    QTest::newRow("resume") << 2;
  }

  void explicitEndQueuesAcrossBusySessionChanges() {
    QFETCH(int, change);
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy ended(&session, &GameModeSession::exited);
    if (change != 0) {
      session.enter();
      QTRY_VERIFY(session.active() && !session.busy());
      if (change == 2) {
        session.park();
        QTRY_VERIFY(session.parked() && !session.busy());
      }
    }
    // Workers cannot deliver finishChange while this GUI stack is running.
    // Request End immediately after dispatch, before processing that handoff.
    if (change == 1) session.park();
    else session.enter();
    QVERIFY(session.busy());
    session.exit();
    session.exit(); // Repeated End requests collapse into a single queued exit.
    QTRY_VERIFY(!session.busy() && !session.hasSession());
    QCOMPARE(ended.size(), 1);
    QVERIFY(!QFile::exists(statePath()));
  }

  void sessionParkWaitsForFocusAndWorkspaceCopies() {
    deskAndTv(true);
    QSemaphore focusStarted, releaseFocus, workspaceStarted, releaseWorkspace;
    std::atomic_bool pauseFocus{false}, pauseWorkspace{false}, timedOut{false};
    std::atomic_int focusCalls{0}, gameScans{0};
    m_compositor.beforeWindowFocus = [&] {
      ++focusCalls;
      if (pauseFocus.exchange(false)) {
        focusStarted.release();
        if (!releaseFocus.tryAcquire(1, 3000)) timedOut = true;
      }
    };
    m_compositor.beforeWorkspaceCheck = [&] {
      if (pauseWorkspace.exchange(false)) {
        workspaceStarted.release();
        if (!releaseWorkspace.tryAcquire(1, 3000)) timedOut = true;
      }
    };
    m_compositor.beforeGameScan = [&] { ++gameScans; };
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy devices(&session, &GameModeSession::devicesChanged);
    session.enter();
    QTRY_COMPARE(devices.size(), 2); // Transition plus its device refresh have finished.
    const int initialFocusCalls = focusCalls;
    pauseFocus = true;
    pauseWorkspace = true;
    session.focusWindow();
    QVERIFY(focusStarted.tryAcquire(1, 1000));
    session.checkWorkspace();
    QVERIFY(workspaceStarted.tryAcquire(1, 1000));
    session.park();
    QVERIFY(session.busy());
    QTest::qWait(50);
    QCOMPARE(gameScans.load(), 0);
    releaseFocus.release();
    QTest::qWait(50);
    session.focusWindow(); // Even after the earlier focus completes, internal busy rejects it.
    QTest::qWait(50);
    QCOMPARE(focusCalls.load(), initialFocusCalls + 1);
    QCOMPARE(gameScans.load(), 0); // The captured workspace query must also finish.
    releaseWorkspace.release();
    QTRY_VERIFY(session.parked() && !session.busy());
    QCOMPARE(gameScans.load(), 1);
    QVERIFY(!timedOut);
    session.shutdown();
  }

  void parkedScanDefersRequestsWithoutPublicBusy_data() {
    QTest::addColumn<int>("request");
    QTest::newRow("resume") << 0;
    QTest::newRow("end") << 1;
    QTest::newRow("end-wins-over-resume") << 2;
  }

  void parkedScanDefersRequestsWithoutPublicBusy() {
    QFETCH(int, request);
    deskAndTv(true);
    QSemaphore scanStarted, releaseScan;
    std::atomic_bool pauseScan{false}, timedOut{false};
    std::atomic_int focusCalls{0};
    m_compositor.beforeGameScan = [&] {
      if (pauseScan.exchange(false)) {
        scanStarted.release();
        if (!releaseScan.tryAcquire(1, 3000)) timedOut = true;
      }
    };
    m_compositor.beforeWindowFocus = [&] { ++focusCalls; };
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy devices(&session, &GameModeSession::devicesChanged);
    session.enter();
    QTRY_COMPARE(devices.size(), 2);
    devices.clear();
    session.park();
    QTRY_COMPARE(devices.size(), 2);
    QSignalSpy state(&session, &GameModeSession::stateChanged);
    QSignalSpy preparing(&session, &GameModeSession::preparing);
    QSignalSpy resumed(&session, &GameModeSession::resumed);
    QSignalSpy ended(&session, &GameModeSession::exited);
    devices.clear();
    const int initialFocusCalls = focusCalls;
    pauseScan = true;
    QTRY_VERIFY(scanStarted.available() > 0);
    QVERIFY(scanStarted.tryAcquire());
    QVERIFY(session.parked() && !session.busy());
    session.focusWindow(); // Public idle must not permit a conflicting focus operation.
    if (request != 1) session.enter();
    if (request != 0) {
      session.exit();
      session.exit();
    }
    QTest::qWait(50);
    QCOMPARE(focusCalls.load(), initialFocusCalls);
    QCOMPARE(preparing.size(), 0); // Resume is accepted but waits for the scan.
    QCOMPARE(state.size(), 0);
    QCOMPARE(devices.size(), 0);
    releaseScan.release();
    if (request == 0) {
      QTRY_VERIFY(session.active() && !session.busy());
      QCOMPARE(resumed.size(), 1);
      QCOMPARE(preparing.size(), 1);
      QCOMPARE(ended.size(), 0);
    } else {
      QTRY_VERIFY(!session.hasSession() && !session.busy());
      QCOMPARE(ended.size(), 1);
      QCOMPARE(preparing.size(), 0);
      QVERIFY(!QFile::exists(statePath()));
    }
    QVERIFY(!timedOut);
    session.shutdown();
  }

  void parkedScansPreserveStatusAndSuppressRepeatedSignals() {
    deskAndTv(true);
    QSemaphore scanStarted, releaseScan;
    std::atomic_bool pauseScan{false}, timedOut{false};
    std::atomic_int focusCalls{0};
    m_compositor.beforeGameScan = [&] {
      if (pauseScan.exchange(false)) {
        scanStarted.release();
        if (!releaseScan.tryAcquire(1, 3000)) timedOut = true;
      }
    };
    m_compositor.beforeWindowFocus = [&] { ++focusCalls; };
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy devices(&session, &GameModeSession::devicesChanged);
    session.enter();
    QTRY_COMPARE(devices.size(), 2);
    devices.clear();
    session.park();
    QTRY_COMPARE(devices.size(), 2);
    QSignalSpy state(&session, &GameModeSession::stateChanged);
    QSignalSpy failed(&session, &GameModeSession::failed);
    devices.clear();
    m_compositor.gameScanFails = true; // An unknown empty library remains retained.
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(state.size(), 1);
    QCOMPARE(devices.size(), 1);
    QVERIFY(session.parked() && !session.busy());
    const QString status = session.statusText();
    QVERIFY(!status.isEmpty());
    state.clear();
    devices.clear();
    failed.clear();
    for (bool succeeds : {false, true}) {
      m_compositor.gameScanFails = !succeeds;
      pauseScan = true;
      QTRY_VERIFY(scanStarted.available() > 0);
      QVERIFY(scanStarted.tryAcquire());
      QVERIFY(!session.busy());
      releaseScan.release();
      const int previousFocusCalls = focusCalls;
      // Focus becomes admissible only after finishChange has cleared internal busy.
      QTRY_VERIFY(([&] {
        session.focusWindow();
        return focusCalls.load() > previousFocusCalls;
      })());
      QCOMPARE(state.size(), 0);
      QCOMPARE(devices.size(), 0);
      QCOMPARE(failed.size(), 0);
      QCOMPARE(session.statusText(), status);
      QVERIFY(session.parked());
    }
    QVERIFY(!timedOut);
    session.shutdown();
  }

  void parkedScanPublishesFatalCleanup() {
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy devices(&session, &GameModeSession::devicesChanged);
    session.enter();
    QTRY_COMPARE(devices.size(), 2);
    retainedGame();
    devices.clear();
    session.park();
    QTRY_COMPARE(devices.size(), 2);
    QSignalSpy state(&session, &GameModeSession::stateChanged);
    QSignalSpy failed(&session, &GameModeSession::failed);
    QSignalSpy ended(&session, &GameModeSession::exited);
    devices.clear();
    m_audio.streamScanFailuresRemaining = 1;
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(!session.hasSession() && !session.busy());
    QCOMPARE(state.size(), 1);
    QCOMPARE(devices.size(), 1);
    QCOMPARE(ended.size(), 1);
    QVERIFY(!session.statusText().isEmpty());
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedResumeFailurePreservesQuietParkedGame() {
    deskAndTv(false);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_compositor.enableFails = true;
    const auto failed = game.resume(tvSettings(), 100);
    QVERIFY(!failed.ok);
    QVERIFY(game.parked());
    QVERIFY(m_audio.inputs.first().muted);
    QCOMPARE(m_compositor.others.first().workspace, GameModeController::workspace());
    QVERIFY(QFile::exists(statePath()));
  }

  void retainedResumeUnmuteFailureRestoresQuietDesktopAndJournal() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_audio.unmuteFails = true;
    const auto result = game.resume(tvSettings(), 100);
    QVERIFY(!result.ok);
    QVERIFY(game.parked());
    QVERIFY(m_audio.inputs.first().muted);
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(!game.state().mutedStreams.isEmpty());
    QVERIFY(QFile::exists(statePath()));
  }

  void activeExitRestoresDesktopDespitePendingGameAudio() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("TV")});
    auto game = controller();
    QVERIFY(game.enter(tvSettings(kTvSink), 100).ok);
    retainedGame();
    m_audio.inputs.append({3, m_compositor.games.first().process, QStringLiteral("serial-3"), false});
    m_audio.muteFailAt = 2;
    m_audio.unmuteFails = true;
    // A failed park leaves an active session with a partially muted game.
    QVERIFY(!game.park(100).ok);
    QCOMPARE(game.phase(), GameModePhase::Active);
    QVERIFY(m_audio.inputs.first().muted);
    QVERIFY(game.state().windowPlaced);
    QVERIFY(m_notifications.quiet);

    const auto left = game.exit(100);
    QVERIFY(!left.ok);
    QVERIFY(left.error.contains("recovery remains recorded"));
    QCOMPARE(game.phase(), GameModePhase::Active); // Cleanup is still owned and retryable.
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QCOMPARE(m_compositor.currentFocus.output, kDesk);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    QVERIFY(!m_notifications.quiet);
    QVERIFY(m_audio.inputs.first().muted);
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    GameModeState saved;
    QVERIFY(GameModeState::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &saved));
    QVERIFY(!saved.windowPlaced && !saved.enabledOutput && !saved.silencedNotifications);
    QVERIFY(saved.sessionSink.isEmpty());
    QCOMPARE(saved.mutedStreams.size(), 1);
    file.close();

    const auto desktopEffects = m_compositor.log;
    m_audio.unmuteFails = false;
    QVERIFY(game.exit(100).ok);
    QCOMPARE(game.phase(), GameModePhase::Ended);
    QCOMPARE(m_compositor.log, desktopEffects); // Consumed desktop effects are not replayed.
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!m_audio.inputs.at(1).muted);
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedSessionExitDoesNotReplayFullscreenAfterWindowReturn() {
    deskAndTv(true);
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, {}, statePath());
    QSignalSpy devices(&session, &GameModeSession::devicesChanged);
    QSignalSpy failed(&session, &GameModeSession::failed);
    QSignalSpy resumed(&session, &GameModeSession::resumed);
    session.enter();
    QTRY_COMPARE(devices.size(), 2);
    QVERIFY(m_compositor.setWindowMode(kAddress, 2, 2, nullptr)); // Couch UI after its snapshot.
    retainedGame();
    m_audio.inputs.append({3, m_compositor.games.first().process, QStringLiteral("serial-3"), false});
    m_audio.muteFailAt = 2;
    m_audio.unmuteFails = true;
    devices.clear();
    session.park(); // Audio rollback fails before any desktop or UI restoration.
    QTRY_COMPARE(devices.size(), 2);
    QCOMPARE(failed.size(), 1);
    QVERIFY(session.active() && !session.busy());

    bool couchFullscreen = true;
    connect(&session, &GameModeSession::leaving, this, [&](bool) {
      couchFullscreen = false;
    });
    connect(&session, &GameModeSession::resumed, this, [&] {
      // Mirror QML's fullscreen restoration and deferred compositor/game focus.
      couchFullscreen = true;
      m_compositor.setWindowMode(kAddress, 2, 2, nullptr);
      session.focusGame();
    });
    devices.clear();
    session.exit();
    QTRY_COMPARE(devices.size(), 2);
    QCOMPARE(failed.size(), 2);
    QCOMPARE(resumed.size(), 0);
    QVERIFY(session.active() && !session.busy()); // Recovery ownership still allows End.
    QVERIFY(!couchFullscreen);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QCOMPARE(m_compositor.window.fullscreenMode, 0);
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    GameModeState saved;
    QVERIFY(GameModeState::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &saved));
    QVERIFY(!saved.windowPlaced);
    QVERIFY(!saved.mutedStreams.isEmpty());
    file.close();

    m_audio.unmuteFails = false;
    session.exit();
    QTRY_VERIFY(!session.hasSession() && !session.busy());
    QCOMPARE(resumed.size(), 0);
    QVERIFY(!couchFullscreen);
    QCOMPARE(m_compositor.window.fullscreenMode, 0);
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedSameProcessStreamIndexReuseIsNotUnmuted() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_audio.inputs[0].token = QStringLiteral("new-stream-same-process");
    m_audio.inputs[0].muted = true;
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(m_audio.inputs.first().muted); // newly observed stream was already muted
    QVERIFY(!m_audio.log.contains("mute 1 0"));
  }

  void retainedColdWindowSnapshotPrecedesShowAndHidesOnEveryReturn() {
    deskAndTv(true);
    auto game = controller();
    game.setTemporaryWindow(true);
    QStringList visibility;
    game.setWindowVisibility([&](bool shown) {
      visibility.append(shown ? "show" : "hide");
      m_compositor.windowMapped = shown;
      if (shown)
        m_compositor.currentFocus = {kTv, GameModeController::workspace(), kAddress};
    });
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    QCOMPARE(visibility, (QStringList{"show", "hide"}));
    m_compositor.currentFocus = {kDesk, QStringLiteral("8"), QStringLiteral("0xcafe")};
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(game.park(100).ok);
    QCOMPARE(m_compositor.currentFocus.workspace, QStringLiteral("8"));
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0xcafe"));
    QCOMPARE(visibility, (QStringList{"show", "hide", "show", "hide"}));
  }

  void coldRootOpensDirectlyInGameMode() {
    deskAndTv(true);
    auto game = controller();
    game.setTemporaryWindow(true);
    game.setPlaceholder([&](bool shown) { m_compositor.placeholderShown = shown; });
    m_compositor.openSupported = true;
    QStringList opened;
    game.setOpenOutput([&](const QString& output) { opened.append(output); });
    game.setWindowVisibility([&](bool shown) {
      m_compositor.windowMapped = shown;
      // Hyprland applies the rule as the window maps: it never tiles on the desktop.
      if (shown && !m_compositor.openWorkspace.isEmpty())
        m_compositor.window.workspace = m_compositor.openWorkspace;
    });
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(m_compositor.log.contains(QStringLiteral("open name:omakade %1").arg(kTv)));
    for (const auto& entry : m_compositor.log) {
      QVERIFY2(!entry.startsWith("place") && entry != "hold", qPrintable(entry));
    }
    // Restoring the desktop still knows Game Mode moved focus.
    QVERIFY(game.state().windowPlaced);
    QVERIFY(game.state().desktopPending);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_compositor.log.clear();
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(m_compositor.log.contains(QStringLiteral("open name:omakade %1").arg(kTv)));
    for (const auto& entry : m_compositor.log) QVERIFY2(!entry.startsWith("place"), qPrintable(entry));
    // The window is sized for the display it opens on, before it is shown.
    QCOMPARE(opened, (QStringList{kTv, kTv}));
    const QString open = HyprlandGameModeCompositor::openScript("name:omakade", kTv);
    QVERIFY(open.contains("initial_title = \"^Omakade Game Mode Opening.*\""));
    // Lua rejects "\." in a string: the regex's backslashes must be escaped.
    QVERIFY(open.contains(QStringLiteral("class = \"^io\\\\.github\\\\.tsouth89\\\\.Omakade$\"")));
    QVERIFY(!open.contains(QStringLiteral("\"^io\\.github")));
    QVERIFY(open.contains("workspace = \"name:omakade\", float = false, fullscreen = true, no_anim = true"));
    QVERIFY(open.endsWith(QStringLiteral("hl.dispatch(hl.dsp.focus({ monitor = \"%1\" }))").arg(kTv)));
    QVERIFY(HyprlandGameModeCompositor::openScript({}, {}).contains("enabled = false"));
  }

  void retainedWarmWindowTradesPlaceholderEachCycle() {
    deskAndTv(true);
    auto game = controller();
    game.setPlaceholder([&](bool shown) { m_compositor.placeholderShown = shown; });
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!m_compositor.placeholderShown);
    m_compositor.window.workspace = QStringLiteral("7");
    m_compositor.currentFocus = {kDesk, QStringLiteral("7"), kAddress};
    QVERIFY(game.resume(tvSettings(), 100).ok);
    QVERIFY(game.park(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("7"));
    QCOMPARE(
        m_compositor.log.count(QStringLiteral("trade %1 %2").arg(kAddress, kPlaceholderAddress)),
        2);
  }

  void retainedWarmWindowRestoresCompositorMode_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("windowed") << 0;
    QTest::newRow("maximized") << 1;
    QTest::newRow("fullscreen") << 2;
  }
  void retainedWarmWindowRestoresCompositorMode() {
    QFETCH(int, mode);
    deskAndTv(true);
    m_compositor.window.fullscreen = mode != 0;
    m_compositor.window.fullscreenMode = mode;
    m_compositor.window.fullscreenClient = mode;
    auto game = controller();
    game.setPlaceholder([&](bool shown) { m_compositor.placeholderShown = shown; });
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    for (int cycle = 0; cycle < 2; ++cycle) {
      m_compositor.window.fullscreenMode = 0;
      m_compositor.window.fullscreenClient = 0;
      m_compositor.window.fullscreen = false;
      QVERIFY(game.park(100).ok);
      QCOMPARE(m_compositor.window.fullscreenMode, mode);
      QCOMPARE(m_compositor.window.fullscreenClient, mode);
      QVERIFY(game.resume(tvSettings(), 100).ok);
    }
    QVERIFY(game.exit(100).ok);
  }

  void retainedRecoveryWithDeadGameDoesNotRequireWorkspace() {
    deskAndTv(true);
    {
      auto game = controller();
      QVERIFY(game.enter(tvSettings(), 100).ok);
      retainedGame();
      QVERIFY(game.park(100).ok);
      auto state = game.state();
      state.ownerPid = 424242;
      state.ownerStart = 123;
      QFile file(statePath());
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(QJsonDocument(state.toJson()).toJson());
    }
    m_compositor.alive.clear();
    m_compositor.games.clear();
    m_compositor.moveWorkspaceFails = true; // workspace has been removed
    m_audio.inputs.clear();
    m_compositor.log.clear();
    auto game = controller();
    QVERIFY(game.recover().ok);
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(m_compositor.log.isEmpty());
  }

  void retainedColdUnmapTimeoutKeepsJournalAndReportsFailure() {
    deskAndTv(true);
    auto game = controller();
    game.setTemporaryWindow(true);
    game.setWindowVisibility([](bool) {}); // GUI callback never unmaps the root
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    const auto result = game.park(100);
    QVERIFY(!result.ok);
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_slept >= 3000);
  }

  void retainedCrashRecoveryExposesWithoutReplayingOldDesktopEffects() {
    deskAndTv(true);
    {
      auto game = controller();
      QVERIFY(game.enter(tvSettings(), 100).ok);
      retainedGame();
      QVERIFY(game.park(100).ok);
      GameModeState abandoned = game.state();
      abandoned.ownerPid = 424242;
      abandoned.ownerStart = 123;
      QFile file(statePath());
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(QJsonDocument(abandoned.toJson()).toJson());
    }
    m_compositor.currentFocus = {kDesk, QStringLiteral("8"), QStringLiteral("0xcafe")};
    m_audio.current = QStringLiteral("user-choice");
    m_notifications.quiet = true; // a later user override
    m_compositor.log.clear();
    auto recovered = controller();
    const auto result = recovered.recover();
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!m_audio.inputs.first().muted);
    QCOMPARE(m_audio.current, QStringLiteral("user-choice"));
    QVERIFY(m_notifications.quiet);
    QVERIFY(!m_compositor.log.contains("focus-workspace 3"));
    QVERIFY(!m_compositor.log.contains("return 0x9a01 3"));
    QVERIFY(m_compositor.log.contains("focus-window 0x9a01"));
  }

  void retainedFatalScannerFailureExposesAndReportsActualPhase() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_compositor.gameScanFailuresRemaining = 1;
    const auto result = game.refreshParked();
    QVERIFY(!result.ok);
    QCOMPARE(m_compositor.gameScanFailuresRemaining, 0);
    QCOMPARE(game.phase(), GameModePhase::Ended);
    QVERIFY(!game.active());
    QVERIFY(!game.parked());
    QVERIFY(!m_audio.inputs.first().muted);
    QCOMPARE(m_compositor.gameWorkspaceOutput, kDesk);
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QVERIFY(!QFile::exists(statePath()));
    const auto log = m_compositor.log;
    QVERIFY(!game.park(100).ok);
    QVERIFY(!game.resume(tvSettings(), 100).ok);
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.log, log);

    // A new presentation must capture fresh effects before another park can succeed.
    m_compositor.currentFocus = {kDesk, QStringLiteral("8"), QStringLiteral("0xcafe")};
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(game.state().windowPlaced);
    QVERIFY(game.state().desktopPending);
    QVERIFY(game.park(100).ok);
    QCOMPARE(m_compositor.currentFocus.workspace, QStringLiteral("8"));
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0xcafe"));
  }

  void retainedFatalAudioFailureFinishesOrKeepsCleanupJournal_data() {
    QTest::addColumn<bool>("unmuteFails");
    QTest::newRow("one-scan-failure") << false;
    QTest::newRow("pending-unmute") << true;
  }

  void retainedFatalAudioFailureFinishesOrKeepsCleanupJournal() {
    QFETCH(bool, unmuteFails);
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    m_audio.streamScanFailuresRemaining = 1;
    m_audio.unmuteFails = unmuteFails;
    const auto result = game.refreshParked();
    QVERIFY(!result.ok);
    QCOMPARE(m_audio.streamScanFailuresRemaining, 0);
    QVERIFY(!game.active());
    QCOMPARE(m_compositor.gameWorkspaceOutput, kDesk);
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QVERIFY(!m_audio.inputs.at(1).muted);
    if (unmuteFails) {
      QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
      QVERIFY(game.state().retentionEnding);
      QVERIFY(m_audio.inputs.first().muted);
      QFile file(statePath());
      QVERIFY(file.open(QIODevice::ReadOnly));
      GameModeState saved;
      QVERIFY(GameModeState::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &saved));
      QVERIFY(saved.retentionEnding);
      QVERIFY(!saved.mutedStreams.isEmpty());
      file.close();
      QVERIFY(!game.park(100).ok);
      QVERIFY(!game.resume(tvSettings(), 100).ok);
      QVERIFY(!game.exit(100).ok);
      QVERIFY(game.parked());
      QVERIFY(QFile::exists(statePath()));
      m_audio.unmuteFails = false;
      const auto retry = game.resume(tvSettings(), 100);
      QVERIFY(retry.ok);
      QVERIFY(!retry.resumedGame);
    }
    QCOMPARE(game.phase(), GameModePhase::Ended);
    QVERIFY(!m_audio.inputs.first().muted);
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(!game.park(100).ok);
    QVERIFY(!game.resume(tvSettings(), 100).ok);
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.exit(100).ok);
  }

  void retainedEndedGameFinishesPartialParkCleanup_data() {
    QTest::addColumn<bool>("desktopAvailable");
    QTest::newRow("restore-tv-power") << true;
    QTest::newRow("preserve-pending-effects") << false;
  }

  void retainedEndedGameFinishesPartialParkCleanup() {
    QFETCH(bool, desktopAvailable);
    deskAndTv(false);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_compositor.moveWorkspaceFails = true;
    QVERIFY(!game.park(100).ok); // relocation and rollback both fail
    QVERIFY(game.parked());
    QVERIFY(game.state().enabledOutput);
    QVERIFY(!game.state()
                 .desktopPending); // Focus restoration already completed; TV power remains pending.
    QVERIFY(m_compositor.list.at(1).enabled);
    m_compositor.alive.clear();
    m_compositor.games.clear();
    m_audio.inputs.removeFirst();
    m_compositor.usable = desktopAvailable;
    const auto result = game.refreshParked();
    QCOMPARE(result.ok, desktopAvailable);
    if (!desktopAvailable) {
      QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
      QVERIFY(!game.state().retentionEnding);
      QVERIFY(game.state().enabledOutput);
      QVERIFY(
          !game.state()
               .desktopPending); // Focus restoration already completed; TV power remains pending.
      QVERIFY(QFile::exists(statePath()));
      QVERIFY(m_compositor.list.at(1).enabled);
      m_compositor.usable = true;
      QVERIFY(game.refreshParked().ok);
    }
    QCOMPARE(game.phase(), GameModePhase::DesktopRetained);
    QVERIFY(!m_compositor.list.at(1).enabled);
    QVERIFY(m_compositor.log.contains("disable HDMI-A-2"));
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(game.state().games.isEmpty());
    QVERIFY(game.refreshParked().ok);
    QVERIFY(game.exit(100).ok);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedStaleSelfPidCannotHideOrRefocusNewWindow_data() {
    QTest::addColumn<bool>("parked");
    QTest::newRow("active-journal") << false;
    QTest::newRow("parked-journal") << true;
  }

  void retainedStaleSelfPidCannotHideOrRefocusNewWindow() {
    QFETCH(bool, parked);
    deskAndTv(true);
    GameModeState state;
    state.ownerPid = QCoreApplication::applicationPid();
    qint64 currentStart = -1;
    for (const auto& process : ProcFs::listProcesses())
      if (process.pid == state.ownerPid)
        currentStart = process.procStart;
    QVERIFY(currentStart >= 0);
    state.ownerStart = currentStart + 1;
    QVERIFY(!ProcFs::processAlive(state.ownerPid, state.ownerStart));
    state.phase = parked ? GameModePhase::DesktopRetained : GameModePhase::Active;
    state.temporaryWindow = true;
    state.windowPlaced = true;
    state.desktopPending = true;
    state.output = kTv;
    state.outputWorkspace = QStringLiteral("5");
    state.focusedOutput = kDesk;
    state.focusedWorkspace = QStringLiteral("3");
    state.focusedWindow = QStringLiteral("0xold");
    state.silencedNotifications = true;
    m_notifications.quiet = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();
    m_compositor.currentFocus = {kDesk, QStringLiteral("8"), kAddress};
    auto game = controller(true);
    QStringList visibility;
    game.setWindowVisibility([&](bool shown) {
      visibility.append(shown ? "show" : "hide");
      m_compositor.windowMapped = shown;
    });
    QVERIFY(game.recover().ok);
    QVERIFY(visibility.isEmpty());
    QVERIFY(m_compositor.windowMapped);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QCOMPARE(m_compositor.currentFocus.workspace, QStringLiteral("8"));
    QCOMPARE(m_compositor.currentFocus.address, kAddress);
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(game.recover().ok);
  }

  void retainedFocusRestoresRecordedGameModesAfterColdUiSettles_data() {
    QTest::addColumn<int>("fullscreen");
    QTest::addColumn<int>("fullscreenClient");
    QTest::newRow("fullscreen") << 2 << 2;
    QTest::newRow("maximized") << 1 << 0;
    QTest::newRow("windowed") << 0 << 0;
  }

  void retainedFocusRestoresRecordedGameModesAfterColdUiSettles() {
    QFETCH(int, fullscreen);
    QFETCH(int, fullscreenClient);
    deskAndTv(true);
    auto game = controller();
    game.setTemporaryWindow(true);
    game.setWindowVisibility([&](bool shown) { m_compositor.windowMapped = shown; });
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    m_compositor.games[0].fullscreen = fullscreen;
    m_compositor.games[0].fullscreenClient = fullscreenClient;
    game.setBeforeParkRestore([&] {
      m_compositor.games[0].fullscreen = 0;
      m_compositor.games[0].fullscreenClient = 0;
    });
    QVERIFY(game.park(100).ok);
    for (int cycle = 0; cycle < 2; ++cycle) {
      // The fresh parked scan must not replace the modes originally captured at park.
      m_compositor.games[0].fullscreen = 0;
      m_compositor.games[0].fullscreenClient = 0;
      QVERIFY(game.refreshParked().ok);
      QCOMPARE(game.state().games.first().fullscreen, fullscreen);
      QCOMPARE(game.state().games.first().fullscreenClient, fullscreenClient);
      QFile file(statePath());
      QVERIFY(file.open(QIODevice::ReadOnly));
      GameModeState saved;
      QVERIFY(GameModeState::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &saved));
      QCOMPARE(saved.games.first().fullscreen, fullscreen);
      QCOMPARE(saved.games.first().fullscreenClient, fullscreenClient);
      file.close();
      const auto resumed = game.resume(tvSettings(), 100);
      QVERIFY(resumed.ok && resumed.resumedGame);
      // Simulate the remapped library's settled fullscreen replacing game fullscreen.
      m_compositor.window.fullscreen = true;
      m_compositor.currentFocus.address = kAddress;
      m_compositor.games[0].fullscreen = 0;
      m_compositor.games[0].fullscreenClient = 0;
      QVERIFY(game.focusRetainedGame());
      QVERIFY(!m_compositor.window.fullscreen);
      QCOMPARE(m_compositor.games.first().fullscreen, fullscreen);
      QCOMPARE(m_compositor.games.first().fullscreenClient, fullscreenClient);
      QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
      QVERIFY(game.park(100).ok);
    }
  }

  void retainedFocusRejectsChangedWindowIdentity() {
    deskAndTv(true);
    auto game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    retainedGame();
    QVERIFY(game.park(100).ok);
    QVERIFY(game.resume(tvSettings(), 100).ok);
    const auto original = m_compositor.games.first();
    m_compositor.log.clear();
    m_compositor.games[0].address = QStringLiteral("0xother");
    QVERIFY(!game.focusRetainedGame()); // same process, another unrecorded window
    m_compositor.games[0] = original;
    m_compositor.games[0].process.procStart += 1;
    m_compositor.alive = {m_compositor.games.first().process};
    QVERIFY(!game.focusRetainedGame()); // same address, reused pid
    m_compositor.games[0] = original;
    m_compositor.alive.clear();
    QVERIFY(!game.focusRetainedGame()); // recorded process exited
    QVERIFY(m_compositor.log.isEmpty());
  }

  void retainedRecoveryGameFocusNeverClearsOldOwnerWindow_data() {
    QTest::addColumn<bool>("reusedSelfPid");
    QTest::newRow("old-owner") << false;
    QTest::newRow("reused-self-pid") << true;
  }

  void retainedRecoveryGameFocusNeverClearsOldOwnerWindow() {
    QFETCH(bool, reusedSelfPid);
    deskAndTv(true);
    retainedGame();
    m_compositor.games[0].fullscreen = 2;
    m_compositor.games[0].fullscreenClient = 2;
    GameModeState abandoned;
    abandoned.phase = GameModePhase::DesktopRetained;
    abandoned.ownerPid = reusedSelfPid ? QCoreApplication::applicationPid() : 424242;
    abandoned.ownerStart = 0; // neither is the recorded live owner
    QVERIFY(!ProcFs::processAlive(abandoned.ownerPid, abandoned.ownerStart));
    abandoned.games = m_compositor.games;
    abandoned.lastGameWindow = abandoned.games.first().address;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(abandoned.toJson()).toJson());
    file.close();
    // This root belongs to the new warm session, not the abandoned journal.
    m_compositor.window.fullscreen = true;
    auto game = controller();
    QVERIFY(game.recover().ok);
    QCOMPARE(m_compositor.gameFocusOwners, (QVector<qint64>{0}));
    QVERIFY(m_compositor.window.fullscreen);
    QCOMPARE(m_compositor.currentFocus.address, QStringLiteral("0x9a01"));
    QCOMPARE(m_compositor.games.first().fullscreen, 2);
    QCOMPARE(m_compositor.games.first().fullscreenClient, 2);
    QVERIFY(!QFile::exists(statePath()));
  }

  void retainedGameFocusScriptOnlyChangesOwnedWindows() {
    using Compositor = HyprlandGameModeCompositor;
    const GameModeGameWindow game{QStringLiteral("0x9a01"), {200, 42, {}}, 2, 1};
    const QString script = Compositor::focusGameScript(game, kAddress);
    QCOMPARE(
        script,
        QStringLiteral(
            "hl.dispatch(hl.dsp.window.fullscreen_state({ window = \"address:0x55d0c0ffee00\", "
            "internal = 0, client = 0 }))\n"
            "hl.dispatch(hl.dsp.window.fullscreen_state({ window = \"address:0x9a01\", "
            "internal = 2, client = 1 }))\n"
            "hl.dispatch(hl.dsp.focus({ window = \"address:0x9a01\" }))"));
    auto windowed = game;
    windowed.fullscreen = 0;
    windowed.fullscreenClient = 0;
    const auto windowedScript = Compositor::focusGameScript(windowed, {});
    QVERIFY(windowedScript.contains("internal = 0, client = 0"));
    QVERIFY(!windowedScript.contains(kAddress));
    QVERIFY(Compositor::focusGameScript(game, game.address).isEmpty());
    QVERIFY(Compositor::focusGameScript(game, QStringLiteral("invalid")).isEmpty());
    windowed.address = QStringLiteral("invalid");
    QVERIFY(Compositor::focusGameScript(windowed, kAddress).isEmpty());
    windowed = game;
    windowed.fullscreen = 4;
    QVERIFY(Compositor::focusGameScript(windowed, kAddress).isEmpty());
  }

  void retainedAudioParsingRequiresUniqueTokensAndNumericIndices() {
    QVector<GameModeStream> streams;
    QVERIFY(PactlGameModeAudio::parseStreams(R"([
      {"index":7,"mute":true,"properties":{"application.process.id":"123","object.serial":"88"}},
      {"index":8,"mute":false,"properties":{"application.process.id":"124","module-stream-restore.id":"reused-name"}}
    ])",
                                             &streams));
    QCOMPARE(streams.size(), 2);
    QCOMPARE(streams.first().index, quint32(7));
    QCOMPARE(streams.first().token, QStringLiteral("88"));
    QVERIFY(streams.first().muted);
    QVERIFY(streams.last().token.isEmpty());
    QVERIFY(!PactlGameModeAudio::parseStreams("invalid", &streams));
    QVERIFY(!PactlGameModeAudio::parseStreams(R"([{"index":-1}])", &streams));
    QVERIFY(!PactlGameModeAudio::parseStreams(R"([{"index":"oops"}])", &streams));
    QCOMPARE(HyprlandGameModeCompositor::moveWorkspaceScript("name:omakade", "HDMI-A-2"),
             QStringLiteral("hl.dispatch(hl.dsp.workspace.move({ workspace = \"name:omakade\", "
                            "monitor = \"HDMI-A-2\" }))"));
  }

  void currentDisplayEntersAndRestores() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    GameModeController game = controller();

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QCOMPARE(entered.output, kDesk);
    QVERIFY(game.active());
    QCOMPARE(m_compositor.window.workspace, GameModeController::workspace());
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_audio.log.isEmpty());
    QCOMPARE(m_notifications.log, QStringList{"silence"});

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QVERIFY(!game.active());
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains("focus-workspace 3"));
    QVERIFY(!m_compositor.log.join(' ').contains("able "));
    QCOMPARE(m_notifications.log, (QStringList{"silence", "unsilence"}));
    QVERIFY(!QFile::exists(statePath()));
  }

  // With other windows tiled beside it, Omakade has to come back to the same place.
  void placeholderKeepsTheWindowsPlaceInTheLayout() {
    deskAndTv(true);
    GameModeController game = controller();
    QList<bool> shown;
    game.setPlaceholder([&](bool visible) {
      shown.append(visible);
      m_compositor.placeholderShown = visible;
    });

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(game.state().placeholder);
    QVERIFY(m_compositor.log.contains(QStringLiteral("place %1 name:omakade %2 holding %3")
                                          .arg(kAddress, kDesk, kPlaceholderAddress)));
    // The placeholder now sits where Omakade was.
    QCOMPARE(m_compositor.placeholder.workspace, QStringLiteral("3"));

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(
        QStringLiteral("trade %1 %2").arg(kAddress, kPlaceholderAddress)));
    QVERIFY(m_compositor.log.contains(QStringLiteral("focus-window %1").arg(kAddress)));
    QVERIFY(!m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
    QCOMPARE(shown, (QList<bool>{true, false}));
  }

  void placeholderThatNeverMapsFallsBackToMovingTheWindow() {
    deskAndTv(true);
    GameModeController game = controller();
    QList<bool> shown;
    game.setPlaceholder([&](bool visible) { shown.append(visible); });

    const auto entered = game.enter({}, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(!game.state().placeholder);
    QVERIFY(
        m_compositor.log.contains(QStringLiteral("place %1 name:omakade %2").arg(kAddress, kDesk)));
    QCOMPARE(shown, (QList<bool>{true, false}));

    QVERIFY(game.exit(100).ok);
    QVERIFY(m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
  }

  void closedPlaceholderOrFailedTradeFallsBackToMovingTheWindow() {
    deskAndTv(true);
    GameModeController game = controller();
    game.setPlaceholder([&](bool visible) { m_compositor.placeholderShown = visible; });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(game.state().placeholder);
    m_compositor.tradeFails = true;

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return %1 3").arg(kAddress)));
  }

  void floatingWindowNeedsNoPlaceholder() {
    deskAndTv(true);
    m_compositor.window.floating = true;
    GameModeController game = controller();
    bool asked = false;
    game.setPlaceholder([&](bool) { asked = true; });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(!asked);
    QVERIFY(!game.state().placeholder);
  }

  // Fullscreen tiled Couch Mode still needs its place in the layout preserved.
  void fullscreenWindowPreservesPlaceholder() {
    deskAndTv(true);
    m_compositor.window.fullscreen = true;
    GameModeController game = controller();
    bool asked = false;
    game.setPlaceholder([&](bool shown) {
      asked = true;
      m_compositor.placeholderShown = shown;
    });
    QVERIFY(game.enter({}, 100).ok);
    QVERIFY(asked);
    QVERIFY(game.state().placeholder);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
  }

  void televisionIsTurnedOnAndOffAgain() {
    deskAndTv(false);
    m_audio.late = {kTvSink, QStringLiteral("QBQ90 HDMI")};
    m_audio.latePolls = 4;
    GameModeController game = controller();

    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QCOMPARE(entered.output, kTv);
    QCOMPARE(m_compositor.window.output, kTv);
    QCOMPARE(m_audio.current, kTvSink);
    QVERIFY(game.state().enabledOutput);
    QCOMPARE(game.state().previousSink, QStringLiteral("headset"));

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    // The window comes back and focus returns to the desk before the television goes off.
    const int returned = m_compositor.log.indexOf(QStringLiteral("return %1 3").arg(kAddress));
    const int focused =
        m_compositor.log.lastIndexOf(QStringLiteral("focus-window %1").arg(kAddress));
    const int disabled = m_compositor.log.indexOf(QStringLiteral("disable %1").arg(kTv));
    QVERIFY(returned >= 0 && focused > returned && disabled > focused);
    QVERIFY(!QFile::exists(statePath()));
  }

  void televisionThatWasOnStaysOn() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(!game.state().enabledOutput);
    QVERIFY(game.exit(100).ok);
    QVERIFY(m_compositor.list.at(1).enabled);
    QVERIFY(!m_compositor.log.contains(QStringLiteral("disable %1").arg(kTv)));
    // It shows the workspace it had before, and focus goes back to the desk.
    QVERIFY(m_compositor.log.contains("focus-workspace 5"));
    QCOMPARE(m_compositor.currentFocus.output, kDesk);
    QCOMPARE(m_compositor.currentFocus.workspace, QStringLiteral("3"));
    QCOMPARE(m_compositor.currentFocus.address, kAddress);
  }

  void televisionThatNeverTurnsOnIsUndone() {
    deskAndTv(false);
    m_compositor.livePolls = -1;
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("did not turn on"));
    QVERIFY(!game.active());
    QCOMPARE(m_compositor.log, (QStringList{QStringLiteral("enable %1").arg(kTv),
                                            QStringLiteral("disable %1").arg(kTv)}));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(m_slept >= 10000);
  }

  void missingSoundOutputTurnsTheTelevisionBackOff() {
    deskAndTv(false);
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("sound output"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedSoundSwitchLeavesTheDefaultAlone() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.setFails = true;
    GameModeController game = controller();
    QVERIFY(!game.enter(tvSettings(kTvSink), 100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_audio.log, QStringList{QStringLiteral("default %1").arg(kTvSink)});
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedWindowMoveUndoesSoundAndDisplay() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_compositor.placeFails = true;
    GameModeController game = controller();
    QVERIFY(!game.enter(tvSettings(kTvSink), 100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_compositor.list.at(1).enabled);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
  }

  void disconnectedDisplayChangesNothing() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("not connected"));
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
  }

  void soundOutputChosenDuringTheSessionIsKept() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.list.append({QStringLiteral("speakers"), QStringLiteral("Speakers")});
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(kTvSink), 100).ok);
    m_audio.current = QStringLiteral("speakers");
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("speakers"));
  }

  void vanishedSessionSinkFallsBackToThePreviousOne() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.list.append({QStringLiteral("speakers"), QStringLiteral("Speakers")});
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(kTvSink), 100).ok);
    // The television went to standby: its sink is gone and the sound server fell back.
    m_audio.list.removeAt(1);
    m_audio.current = QStringLiteral("speakers");
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
  }

  void unavailableDesktopKeepsRecoveryStateForRetry() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_compositor.usable = false;
    QVERIFY(!game.exit(100).ok);
    QVERIFY(QFile::exists(statePath()));
    m_compositor.usable = true;
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void failedWindowReturnKeepsRecoveryStateForRetry() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_compositor.returnFails = true;
    const auto left = game.exit(100);
    QVERIFY(!left.ok);
    QVERIFY(!left.notes.isEmpty());
    QCOMPARE(m_compositor.window.workspace, GameModeController::workspace());
    QVERIFY(QFile::exists(statePath()));
    m_compositor.returnFails = false;
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void chosenDisplayRequiresMappedWindow() {
    deskAndTv(false);
    m_compositor.window = {};
    GameModeController game = controller();
    const auto entered = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!entered.ok);
    QVERIFY(entered.error.contains("window"));
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(m_audio.log.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!QFile::exists(statePath()));
    QVERIFY(m_slept >= 3000);
  }

  void windowAndNotificationChangesAreJournaledFirst() {
    deskAndTv(true);
    m_compositor.beforePlace = [this] {
      QFile record(statePath());
      QVERIFY(record.open(QIODevice::ReadOnly));
      const auto state = QJsonDocument::fromJson(record.readAll()).object();
      QVERIFY(state.value("window_placed").toBool());
      QCOMPARE(state.value("window_workspace").toString(), QStringLiteral("3"));
    };
    m_notifications.beforeSet = [this] {
      QFile record(statePath());
      QVERIFY(record.open(QIODevice::ReadOnly));
      QVERIFY(QJsonDocument::fromJson(record.readAll())
                  .object()
                  .value("silenced_notifications")
                  .toBool());
    };
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_notifications.beforeSet = {};
    QVERIFY(game.exit(100).ok);
  }

  void notificationsStayOnWithoutRecoveryStorage() {
    const QString blocked = m_directory.filePath("blocked-notification-state");
    QFile file(blocked);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    GameModeController game(nullptr, nullptr, &m_notifications, blocked + "/state.json");
    const auto entered = game.enter({}, 100);
    QVERIFY(entered.ok);
    QVERIFY(!entered.notes.isEmpty());
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(!m_notifications.quiet);
    QVERIFY(game.exit(100).ok);
    QFile::remove(blocked);
  }

  void failedNotificationChangeClearsRecoveryFlag() {
    deskAndTv(true);
    m_notifications.setFails = true;
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QFile record(statePath());
    QVERIFY(record.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(record.readAll())
                 .object()
                 .value("silenced_notifications")
                 .toBool());
    record.close();
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_notifications.log, QStringList{"silence"});
  }

  void notificationsAlreadySilencedStaySilenced() {
    deskAndTv(true);
    m_notifications.quiet = true;
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(m_notifications.quiet);
  }

  void notificationsCanBeLeftAlone() {
    deskAndTv(true);
    GameModeSettings settings = tvSettings();
    settings.silenceNotifications = false;
    GameModeController game = controller();
    QVERIFY(game.enter(settings, 100).ok);
    QVERIFY(m_notifications.log.isEmpty());
    QVERIFY(game.exit(100).ok);
  }

  void interruptedSessionIsUndoneWithoutTouchingWindows() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    m_audio.current = kTvSink;
    m_notifications.quiet = true;
    GameModeState state;
    state.ownerPid = 4242;
    state.output = kTv;
    state.enabledOutput = true;
    state.focusedOutput = kDesk;
    state.windowWorkspace = QStringLiteral("3");
    state.previousSink = QStringLiteral("headset");
    state.sessionSink = kTvSink;
    state.silencedNotifications = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(false);
    const auto recovered = game.recover();
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.output, kTv);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(!m_notifications.quiet);
    QCOMPARE(m_compositor.log, QStringList{QStringLiteral("disable %1").arg(kTv)});
    QVERIFY(!QFile::exists(statePath()));
    // Nothing left to undo the second time.
    QVERIFY(game.recover().ok);
    QCOMPARE(m_compositor.log.size(), 1);
  }

  void exitedOwnerDoesNotBlockRecoveryBeforeItsParentReapsIt() {
    const pid_t child = fork();
    QVERIFY(child >= 0);
    if (child == 0) {
      prctl(PR_SET_NAME, "omakade");
      _exit(0);
    }
    struct ReapChild {
      pid_t pid;
      ~ReapChild() { waitpid(pid, nullptr, 0); }
    } reap{child};
    siginfo_t info{};
    QCOMPARE(waitid(P_PID, child, &info, WEXITED | WNOWAIT), 0);
    QFile comm(QStringLiteral("/proc/%1/comm").arg(child));
    QVERIFY(comm.open(QIODevice::ReadOnly));
    QCOMPARE(comm.readAll().trimmed(), QByteArray("omakade"));
    GameModeState state;
    state.ownerPid = child;
    state.silencedNotifications = true;
    m_notifications.quiet = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();
    GameModeController game(&m_compositor, &m_audio, &m_notifications, statePath());
    QVERIFY(game.recover().ok);
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!QFile::exists(statePath()));
  }

  void sessionOwnedByARunningOmakadeIsLeftAlone() {
    deskAndTv(true);
    GameModeState state;
    state.ownerPid = 4242;
    state.output = kTv;
    state.enabledOutput = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(true);
    QVERIFY(!game.recover().ok);
    QVERIFY(!game.enter(tvSettings(), 100).ok);
    QVERIFY(m_compositor.log.isEmpty());
    QVERIFY(QFile::exists(statePath()));
  }

  void unreadableStateIsPreservedForSafeRecovery() {
    deskAndTv(true);
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    GameModeController game = controller();
    QVERIFY(!game.enter(tvSettings(), 100).ok);
    QVERIFY(QFile::exists(statePath()));
    QVERIFY(m_compositor.log.isEmpty());
  }

  void withoutACompositorOnlySoundAndNotificationsChange() {
    m_compositor.usable = false;
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    GameModeController game = controller();

    const auto chosen = game.enter(tvSettings(kTvSink), 100);
    QVERIFY(!chosen.ok);
    QVERIFY(chosen.error.contains("Hyprland"));
    QVERIFY(m_audio.log.isEmpty());

    GameModeSettings settings;
    settings.sinkName = kTvSink;
    const auto entered = game.enter(settings, 100);
    QVERIFY2(entered.ok, qPrintable(entered.error));
    QVERIFY(entered.output.isEmpty());
    QCOMPARE(m_audio.current, kTvSink);
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QVERIFY(m_compositor.log.isEmpty());
  }

  void endingGameModeWithdrawsALaunchHold() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    m_compositor.canHold = true;
    m_compositor.held = {{QStringLiteral("0x70"), 70, QStringLiteral("heroic"), QStringLiteral("Sign in")}};
    m_compositor.log.clear();
    QVERIFY(game.exit(100).ok);
    QCOMPARE(m_compositor.log.at(0), QStringLiteral("steam-hold "));
    QCOMPARE(m_compositor.log.at(1), QStringLiteral("launch-hold "));
    QCOMPARE(m_compositor.log.at(2), QStringLiteral("release-held 0x70 name:omakade"));
    QVERIFY(m_compositor.held.isEmpty());

    // A crashed Omakade's hold is withdrawn by the next one's recovery too.
    deskAndTv(true);
    m_compositor.canHold = false;
    QVERIFY(controller(false).enter(tvSettings(), 100).ok);
    m_compositor.canHold = true;
    m_compositor.held = {{QStringLiteral("0x71"), 71, QStringLiteral("steam"), QStringLiteral("Steam")}};
    m_compositor.log.clear();
    QVERIFY(controller(false).recover().ok);
    QVERIFY(m_compositor.log.contains(QStringLiteral("release-held 0x71 name:omakade")));
  }

  void enteringTwiceIsRefused() {
    deskAndTv(true);
    GameModeController game = controller();
    QVERIFY(game.enter(tvSettings(), 100).ok);
    QVERIFY(!game.enter(tvSettings(), 100).ok);
    QVERIFY(game.exit(100).ok);
  }

  void displayIsFoundByDescriptionBeforeConnector() {
    const QVector<GameModeOutput> outputs{
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, "1"),
        output(1, QStringLiteral("HDMI-A-1"), kTvDescription, false, false, {})};
    // The television moved to another port.
    QCOMPARE(GameModeController::findOutput(outputs, kTv, kTvDescription), 1);
    // A connector that now carries a different display is not the chosen one.
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, kTvDescription), 1);
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, QStringLiteral("LG OLED")), -1);
    // A display with no description is matched by connector alone.
    QCOMPARE(GameModeController::findOutput(outputs, kDesk, {}), 0);
    QCOMPARE(GameModeController::findOutput(outputs, QStringLiteral("DP-9"), {}), -1);
    // Two identical displays are told apart by connector.
    const QVector<GameModeOutput> twins{output(0, "DP-1", "Twin", true, true, "1"),
                                        output(1, "DP-2", "Twin", true, false, "2")};
    QCOMPARE(GameModeController::findOutput(twins, "DP-2", "Twin"), 1);
    QCOMPARE(GameModeController::findOutput(twins, "DP-7", "Twin"), 0);
  }

  void monitorsAreParsed() {
    const QByteArray json = R"([
      {"id":0,"name":"DP-2","description":"Dell S2721DGF","width":2560,"height":1440,
       "disabled":false,"focused":true,"activeWorkspace":{"id":3,"name":"3"}},
      {"id":1,"name":"HDMI-A-2","description":"Samsung QBQ90","width":0,"height":0,
       "disabled":true,"focused":false,"activeWorkspace":{"id":0,"name":""}},
      {"id":2,"name":"DP-3","description":"","width":1920,"height":1080,
       "disabled":false,"focused":false,"activeWorkspace":{"id":-1337,"name":"omakade"}},
      {"description":"nameless"}])";
    const auto outputs = HyprlandGameModeCompositor::parseOutputs(json);
    QCOMPARE(outputs.size(), 3);
    QCOMPARE(outputs.at(0).workspace, QStringLiteral("3"));
    QVERIFY(outputs.at(0).enabled && outputs.at(0).focused);
    QVERIFY(!outputs.at(1).enabled);
    QVERIFY(outputs.at(1).workspace.isEmpty());
    QCOMPARE(outputs.at(2).workspace, GameModeController::workspace());

    QString error;
    QVERIFY(HyprlandGameModeCompositor::parseOutputs("{", &error).isEmpty());
    QVERIFY(!error.isEmpty());
  }

  void ownWindowIsFoundAmongClients() {
    const auto outputs = HyprlandGameModeCompositor::parseOutputs(
        R"([{"id":0,"name":"DP-2","disabled":false,"activeWorkspace":{"id":3,"name":"3"}},
            {"id":1,"name":"HDMI-A-2","disabled":false,"activeWorkspace":{"id":5,"name":"5"}}])");
    const QByteArray clients = R"([
      {"address":"0xaaa1","mapped":true,"pid":77,"class":"steam","monitor":0,
       "workspace":{"id":3,"name":"3"}},
      {"address":"0xbbb2","mapped":true,"pid":100,"class":"omakade-file-dialog","monitor":0,
       "workspace":{"id":3,"name":"3"}},
      {"address":"0xccc3","mapped":false,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":0,"workspace":{"id":3,"name":"3"}},
      {"address":"0xddd4","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":1,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"not-an-address","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "monitor":1,"workspace":{"id":5,"name":"5"}}])";
    const GameModeWindow window = HyprlandGameModeCompositor::parseWindow(clients, outputs, 100);
    QCOMPARE(window.address, QStringLiteral("0xddd4"));
    QCOMPARE(window.output, kTv);
    QCOMPARE(window.workspace, GameModeController::workspace());
    QVERIFY(!HyprlandGameModeCompositor::parseWindow(clients, outputs, 5).valid());
    QVERIFY(!HyprlandGameModeCompositor::parseWindow("nope", outputs, 100).valid());

    // The placeholder shares the window class and is told apart by its title.
    const QByteArray withPlaceholder = R"([
      {"address":"0xeee5","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "title":"Omakade Game Mode Placeholder — Omakade","fullscreen":2,"monitor":0,
       "workspace":{"id":-98,"name":"special:omakade"}},
      {"address":"0xddd4","mapped":true,"pid":100,"class":"io.github.tsouth89.Omakade",
       "title":"Omakade","floating":true,"monitor":0,"workspace":{"id":3,"name":"3"}}])";
    const GameModeWindow main =
        HyprlandGameModeCompositor::parseWindow(withPlaceholder, outputs, 100);
    QCOMPARE(main.address, QStringLiteral("0xddd4"));
    QVERIFY(main.floating);
    QVERIFY(!main.fullscreen);
    const GameModeWindow held =
        HyprlandGameModeCompositor::parseWindow(withPlaceholder, outputs, 100, true);
    QCOMPARE(held.address, QStringLiteral("0xeee5"));
    QCOMPARE(held.workspace, QStringLiteral("special:omakade"));
    QVERIFY(held.fullscreen);
    QVERIFY(!HyprlandGameModeCompositor::parseWindow(clients, outputs, 100, true).valid());
  }

  void scriptsQuoteEveryName() {
    QCOMPARE(HyprlandGameModeCompositor::luaString("HDMI-A-2"), QStringLiteral("\"HDMI-A-2\""));
    QCOMPARE(HyprlandGameModeCompositor::luaString("a\"b\\c\nd"),
             QStringLiteral("\"a\\\"b\\\\c\\010d\""));
    QCOMPARE(HyprlandGameModeCompositor::outputScript("HDMI-A-2", true),
             QStringLiteral("hl.monitor({ output = \"HDMI-A-2\", disabled = false })"));
    QCOMPARE(HyprlandGameModeCompositor::outputScript("x\" }) os.exit() --", false),
             QStringLiteral("hl.monitor({ output = \"x\\\" }) os.exit() --\", disabled = true })"));
    // Hyprland matches a rule against the whole title, which Qt ends with " — Omakade".
    QVERIFY(HyprlandGameModeCompositor::holdScript().contains(
        QStringLiteral("title = \"^Omakade Game Mode Placeholder.*\"")));
    const QString place =
        HyprlandGameModeCompositor::placeScript("0xddd4", "name:omakade", "HDMI-A-2");
    const QString heldPlace = HyprlandGameModeCompositor::placeScript(
        "0xddd4", "name:omakade", "HDMI-A-2", kPlaceholderAddress);
    QVERIFY(heldPlace.indexOf("fullscreen_state") < heldPlace.indexOf("window.swap"));
    QVERIFY(heldPlace.contains("internal = 0, client = 0"));
    QVERIFY(place.startsWith("hl.dispatch(hl.dsp.focus({ monitor = \"HDMI-A-2\" }))"));
    QVERIFY(place.contains(
        "hl.dsp.window.move({ window = \"address:0xddd4\", workspace = \"name:omakade\", follow = false })"));
    QVERIFY(place.endsWith("hl.dispatch(hl.dsp.focus({ window = \"address:0xddd4\" }))"));
    QVERIFY(HyprlandGameModeCompositor::returnScript("0xddd4", "3").contains("follow = false"));
    QVERIFY(HyprlandGameModeCompositor::validAddress("0x55d0c0ffee00"));
    QVERIFY(!HyprlandGameModeCompositor::validAddress("0x55\" })"));
    QVERIFY(!HyprlandGameModeCompositor::validAddress(""));
  }

  void placementMakesFullscreenAfterTradeBeforeExposure_data() {
    QTest::addColumn<QString>("placeholder");
    QTest::newRow("cold-or-floating") << QString{};
    QTest::newRow("warm-tile") << kPlaceholderAddress;
  }

  void placementMakesFullscreenAfterTradeBeforeExposure() {
    QFETCH(QString, placeholder);
    const QString script = HyprlandGameModeCompositor::placeScript(
        "0xddd4", "name:omakade", "HDMI-A-2", placeholder);
    const auto move = script.indexOf("window.move");
    const auto fullscreen = script.indexOf("internal = 2, client = 2");
    const auto expose = script.indexOf("focus({ window");
    QVERIFY(move >= 0);
    QVERIFY(fullscreen > move);
    QVERIFY(expose > fullscreen);
    QVERIFY(script.contains("follow = false"));
    if (!placeholder.isEmpty()) {
      const auto clear = script.indexOf("internal = 0, client = 0");
      const auto trade = script.indexOf("window.swap");
      QVERIFY(clear >= 0);
      QVERIFY(trade > clear);
      QVERIFY(move > trade);
    } else {
      QVERIFY(!script.contains("internal = 0"));
      QVERIFY(!script.contains("window.swap"));
    }
  }

  void shortcutIsOneLineThatCanBeAddedAndRemoved() {
    const QString stock = QStringLiteral(
        "-- Add a new binding.\n"
        "-- o.bind(\"SUPER + CTRL + G\", \"Game Mode\", \"omakade --game-mode-toggle\")\n"
        "o.bind(\"SUPER + H\", nil, \"voxtype record toggle\")\n");
    // A commented example is not a binding.
    QVERIFY(GameModeShortcut::boundKey(stock).isEmpty());

    const QString added = GameModeShortcut::withBinding(stock);
    QVERIFY(added.startsWith(stock));
    QVERIFY(added.endsWith(GameModeShortcut::bindingLine() + QLatin1Char('\n')));
    QCOMPARE(GameModeShortcut::boundKey(added), QStringLiteral("SUPER + CTRL + G"));
    // Adding twice changes nothing, and removing gives back the file as it was.
    QCOMPARE(GameModeShortcut::withBinding(added), added);
    QCOMPARE(GameModeShortcut::withoutBinding(added), stock);

    // A binding the user wrote on another key counts, and only that line is removed.
    const QString own =
        stock +
        QStringLiteral("  o.bind(\"SUPER + F9\", \"Couch\", \"omakade --game-mode\")\n"
                       "o.bind(\"SUPER + F10\", \"Leave\", \"omakade --game-mode-exit\")\n");
    QCOMPARE(GameModeShortcut::boundKey(own), QStringLiteral("SUPER + F9"));
    QCOMPARE(GameModeShortcut::withBinding(own), own);
    const QString removed = GameModeShortcut::withoutBinding(own);
    QVERIFY(!removed.contains("SUPER + F9"));
    QVERIFY(removed.contains("SUPER + F10"));
    QVERIFY(removed.contains("SUPER + H"));
    const QString multiple =
        own +
        QStringLiteral("o.bind(\"SUPER + F8\", \"Game Mode\", \"omakade --game-mode-toggle\")\n");
    QCOMPARE(GameModeShortcut::removalScript(multiple),
             QStringLiteral("hl.unbind(\"SUPER + F9\")\nhl.unbind(\"SUPER + F8\")"));
    QVERIFY(GameModeShortcut::removalScript(stock).isEmpty());

    QCOMPARE(GameModeShortcut::withBinding({}),
             QStringLiteral("-- Omakade Game Mode. Press it again to leave. Added by Omakade.\n") +
                 GameModeShortcut::bindingLine() + QLatin1Char('\n'));
    // Hyprland may also reload the edited file on its own; clearing the key first keeps
    // that from leaving two bindings that each toggle Game Mode.
    QCOMPARE(GameModeShortcut::liveBindingScript(),
             QStringLiteral("hl.unbind(\"SUPER + CTRL + G\")\n") + GameModeShortcut::bindingLine());
    QCOMPARE(GameModeShortcut::displayKey("SUPER + CTRL + G"), QStringLiteral("Super + Ctrl + G"));
    QCOMPARE(GameModeShortcut::displayKey("SUPER+F9"), QStringLiteral("Super + F9"));
  }

  void shortcutCommandMustBeTheActualThirdArgument() {
    const QString unrelated = QStringLiteral(
        "o.bind(\"SUPER + F9\", \"Steam\", \"steam -gamepadui\") -- alternative: \"omakade "
        "--game-mode-toggle\"\n"
        "o.bind(\"SUPER + F8\", \"omakade --game-mode-toggle\", \"steam -gamepadui\")\n"
        "o.bind(\"SUPER + F7\", \"Mode\", \"echo omakade --game-mode-toggle\")\n"
        "o.bind(\"SUPER + F6\", \"Mode\", \"omakade --game-mode-toggle\"); o.bind(\"SUPER + F5\", "
        "nil, \"steam\")\n");
    QVERIFY(GameModeShortcut::boundKey(unrelated).isEmpty());
    QCOMPARE(GameModeShortcut::withoutBinding(unrelated), unrelated);
    QVERIFY(GameModeShortcut::removalScript(unrelated).isEmpty());
    const QString own = QStringLiteral("o.rebind(\"SUPER + F4\", \"A \\\"quoted\\\" mode\", "
                                       "\"omakade --game-mode-toggle\") -- own binding\n"
                                       "o.bind(\"SUPER + F3\", nil, \"omakade --game-mode\")\n");
    QCOMPARE(GameModeShortcut::boundKey(unrelated + own), QStringLiteral("SUPER + F4"));
    QCOMPARE(GameModeShortcut::withoutBinding(unrelated + own), unrelated);
    QCOMPARE(GameModeShortcut::removalScript(unrelated + own),
             QStringLiteral("hl.unbind(\"SUPER + F4\")\nhl.unbind(\"SUPER + F3\")"));
  }

  void shortcutKeyInUseIsReported() {
    const QByteArray binds = R"([
      {"modmask":65,"key":"G","description":"Signal"},
      {"modmask":64,"key":"G","description":"Toggle window grouping"}])";
    QVERIFY(GameModeShortcut::takenBy(binds).isEmpty());
    QCOMPARE(GameModeShortcut::takenBy(R"([{"modmask":68,"key":"g","description":"Herdr"}])"),
             QStringLiteral("Herdr"));
    QCOMPARE(GameModeShortcut::takenBy(R"([{"modmask":68,"key":"G","description":""}])"),
             QStringLiteral("another shortcut"));
    // A matching description is not evidence that Omakade owns the binding.
    QCOMPARE(
        GameModeShortcut::takenBy(
            R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"exec","arg":"steam -gamepadui"}])"),
        QStringLiteral("Game Mode"));
    QCOMPARE(
        GameModeShortcut::takenBy(
            R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"__lua","arg":"210"}])"),
        QStringLiteral("Game Mode"));
    QVERIFY(
        GameModeShortcut::takenBy(
            R"([{"modmask":68,"key":"G","description":"Game Mode","dispatcher":"exec","arg":"omakade --game-mode-toggle"}])")
            .isEmpty());
    QVERIFY(GameModeShortcut::takenBy("nope").isEmpty());
  }

  // A Steam game is started by Steam, not Omakade, so the window is what shows it is there.
  void otherWindowsOnTheGameModeWorkspaceAreCounted() {
    const QByteArray clients = R"([
      {"address":"0xa1","mapped":true,"pid":100,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa2","mapped":true,"pid":200,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa3","mapped":false,"pid":300,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa4","mapped":true,"pid":400,"workspace":{"id":1,"name":"1"}}])";
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows(clients, GameModeController::workspace(),
                                                           100),
             1);
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows(clients, QStringLiteral("1"), 100), 1);
    QCOMPARE(HyprlandGameModeCompositor::countOtherWindows("nope", QStringLiteral("1"), 100), 0);

    // The addresses are the ones the count comes from, and only mapped, other-pid,
    // valid-address windows on that workspace qualify.
    const QByteArray withJunk = R"([
      {"address":"0xa1","mapped":true,"pid":100,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa2","mapped":true,"pid":200,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa3","mapped":false,"pid":300,"workspace":{"id":-1337,"name":"omakade"}},
      {"address":"0xa4","mapped":true,"pid":400,"workspace":{"id":1,"name":"1"}},
      {"address":"nope","mapped":true,"pid":500,"workspace":{"id":-1337,"name":"omakade"}}])";
    QCOMPARE(HyprlandGameModeCompositor::otherWindowAddresses(withJunk,
                                                              GameModeController::workspace(), 100),
             QStringList{QStringLiteral("0xa2")});
    QCOMPARE(HyprlandGameModeCompositor::otherWindowAddresses(withJunk, QStringLiteral("1"), 100),
             QStringList{QStringLiteral("0xa4")});
    QVERIFY(HyprlandGameModeCompositor::otherWindowAddresses("nope",
                                                             GameModeController::workspace(), 100)
                .isEmpty());
  }

  // A game left running when Game Mode is left must come home with Omakade instead of
  // being stranded on the workspace that goes away.
  void leavingGameModeBringsLeftBehindWindowsToTheDesktop() {
    deskAndTv(true);
    m_compositor.others = {{QStringLiteral("0x9a01"), GameModeController::workspace()}};
    GameModeController game = controller();
    QVERIFY(game.enter({}, 100).ok);

    const auto left = game.exit(100);
    QVERIFY2(left.ok, qPrintable(left.error));
    QCOMPARE(m_compositor.others.at(0).workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return 0x9a01 3")));
  }

  // An interrupted session leaves the same windows behind, and recovery brings them home.
  void recoveryBringsLeftBehindWindowsToTheDesktop() {
    deskAndTv(true);
    m_compositor.others = {{QStringLiteral("0x9a01"), GameModeController::workspace()}};
    GameModeState state;
    state.ownerPid = 4242;
    state.windowWorkspace = QStringLiteral("3");
    state.windowPlaced = true;
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(state.toJson()).toJson());
    file.close();

    GameModeController game = controller(false);
    const auto recovered = game.recover();
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(m_compositor.others.at(0).workspace, QStringLiteral("3"));
    QVERIFY(m_compositor.log.contains(QStringLiteral("return 0x9a01 3")));
  }

  void workspaceSelectorsMatchDispatchers() {
    using Compositor = HyprlandGameModeCompositor;
    QCOMPARE(Compositor::workspaceSelector({{"id", 3}, {"name", "3"}}), QStringLiteral("3"));
    QCOMPARE(Compositor::workspaceSelector({{"id", -1337}, {"name", "omakade"}}),
             QStringLiteral("name:omakade"));
    QCOMPARE(Compositor::workspaceSelector({{"id", -98}, {"name", "special:scratchpad"}}),
             QStringLiteral("special:scratchpad"));
    QVERIFY(Compositor::workspaceSelector({{"id", 0}, {"name", ""}}).isEmpty());
  }

  void launchHoldScriptsKeepOmakadeAndOtherWorkspacesOut() {
    using Compositor = HyprlandGameModeCompositor;
    const QString hold = Compositor::launchHoldScript(GameModeController::workspace());
    const QString guard = QStringLiteral("name = \"omakade-held-focus\", "
                                        "match = { workspace = \"special:omakade-launch\" }, "
                                        "focus_on_activate = false");
    QVERIFY(hold.contains(guard));
    QVERIFY(hold.contains(QStringLiteral("name = \"omakade-launch-hold\", enabled = false")));
    QVERIFY(hold.contains(QStringLiteral("name = \"omakade-steam-hold\", enabled = false")));
    QVERIFY(hold.contains(QStringLiteral("name = \"omakade-launch-route\", enabled = true")));
    QVERIFY(!hold.contains(QStringLiteral("suppress_event")));
    QVERIFY(hold.contains(
        QStringLiteral("class = \"negative:^(io\\\\.github\\\\.tsouth89\\\\.Omakade)$\"")));
    QVERIFY(hold.contains(QStringLiteral("workspace = \"name:omakade\" }")));
    QVERIFY(hold.contains(
        QStringLiteral("workspace = \"special:omakade-launch silent\", no_initial_focus = false })")));
    QVERIFY(Compositor::launchHoldScript({}).contains(
        QStringLiteral("name = \"omakade-launch-route\", enabled = false")));
    // Publisher windows kept after the game opens still cannot activate from hiding.
    QVERIFY(Compositor::launchHoldScript({}).contains(guard));

    const QString steam = Compositor::steamHoldScript(GameModeController::workspace());
    QVERIFY(steam.contains(guard));
    QVERIFY(steam.contains(QStringLiteral("name = \"omakade-steam-route\", enabled = true")));
    QVERIFY(!steam.contains(QStringLiteral("suppress_event")));
    QVERIFY(steam.contains(QStringLiteral("name = \"omakade-steam-hold\", enabled = true")));
    QVERIFY(steam.contains(
        QStringLiteral("match = { class = \"^([Ss]team)$\", workspace = \"name:omakade\" }")));
    QVERIFY(steam.contains(
        QStringLiteral("workspace = \"special:omakade-launch silent\", no_initial_focus = false })")));
    QVERIFY(Compositor::steamHoldScript({}).contains(
        QStringLiteral("name = \"omakade-steam-route\", enabled = false")));

    const QString present = Compositor::presentScript(kAddress, QStringLiteral("name:omakade"), 1440, 810);
    QVERIFY(present.contains(QStringLiteral("workspace = \"name:omakade\", follow = false")));
    QVERIFY(present.contains(QStringLiteral("x = 1440, y = 810, relative = false")));
    QVERIFY(present.endsWith(QStringLiteral("hl.dispatch(hl.dsp.focus({ window = \"address:%1\" }))")
                                 .arg(kAddress)));
    QVERIFY(Compositor::presentScript(QStringLiteral("nope"), QStringLiteral("name:omakade"), 1, 1)
                .isEmpty());
    QVERIFY(Compositor::presentScript(kAddress, {}, 1, 1).isEmpty());
    QVERIFY(Compositor::presentScript(kAddress, QStringLiteral("1"), 0, 1).isEmpty());

    const auto held = Compositor::parseHeldWindows(R"([
        {"address":"0x10","mapped":true,"pid":41,"class":"steam","title":"Steam",
         "workspace":{"id":-98,"name":"special:omakade-launch"}},
        {"address":"0x11","mapped":true,"pid":42,"class":"game","title":"Game",
         "workspace":{"id":-1337,"name":"omakade"}},
        {"address":"0x12","mapped":false,"pid":43,"class":"hidden","title":"",
         "workspace":{"id":-98,"name":"special:omakade-launch"}},
        {"address":"bad","mapped":true,"pid":44,"class":"bad","title":"",
         "workspace":{"id":-98,"name":"special:omakade-launch"}}])");
    QCOMPARE(held.size(), 1);
    QCOMPARE(held.at(0).address, QStringLiteral("0x10"));
    QCOMPARE(held.at(0).pid, 41);
    QCOMPARE(held.at(0).windowClass, QStringLiteral("steam"));
    QCOMPARE(held.at(0).title, QStringLiteral("Steam"));
    QVERIFY(Compositor::parseHeldWindows("[").isEmpty());
  }

  void launchHoldKnowsTheGameWindow() {
    // pid -> process; 50 is the Steam client, 60 Steam's reaper, 61 the game under it.
    const QHash<qint64, LaunchHoldProcess> table{
        {50, {1, QStringLiteral("/usr/lib/steam/steam"), {}, {}}},
        {60, {50, QStringLiteral("/usr/lib/steam/reaper"), {}, QStringLiteral("367520")}},
        {61, {60, QStringLiteral("/usr/bin/wine64-preloader"), {}, QStringLiteral("367520")}},
        {70, {1, QStringLiteral("/opt/heroic/heroic"), {}, {}}},
        {71, {70, QStringLiteral("/usr/bin/wine-preloader"),
              {QStringLiteral("wine"), QStringLiteral("Z:\\games\\Celeste\\Celeste.exe")}, {}}},
        {80, {1, QStringLiteral("/usr/bin/dolphin-emu"), {}, {}}},
        {81, {80, QStringLiteral("/usr/bin/dolphin-emu"), {}, {}}},
        {90, {90, QStringLiteral("/usr/bin/loop"), {}, {}}}};
    const LaunchHold::ProcessLookup lookup = [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
      if (!table.contains(pid)) return std::nullopt;
      return table.value(pid);
    };
    const auto window = [](qint64 pid, const QString& windowClass = QStringLiteral("app")) {
      return GameModeHeldWindow{QStringLiteral("0x1"), pid, windowClass, {}};
    };

    const auto steam = LaunchHold::targetFor(
        {{"source", "Steam"}, {"appId", "367520"}, {"title", "Hollow Knight"}});
    QCOMPARE(steam.steamAppId, QStringLiteral("367520"));
    QVERIFY(!steam.directProcess);
    QVERIFY(!LaunchHold::isGameWindow(window(50, QStringLiteral("steam")), steam, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(61), steam, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(0, QStringLiteral("steam_app_367520")), steam, lookup));
    QVERIFY(!LaunchHold::isGameWindow(window(0, QStringLiteral("steam_app_10")), steam, lookup));

    // A launcher's own window comes from the launched process too; only the install
    // folder tells its game apart.
    QTemporaryDir root;
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("games/Celeste")));
    QHash<qint64, LaunchHoldProcess> heroicTable = table;
    heroicTable[71].arguments = {QStringLiteral("wine"),
                                 QStringLiteral("Z:") + QDir::toNativeSeparators(
                                     root.filePath(QStringLiteral("games/Celeste/Celeste.exe")))
                                     .replace(QLatin1Char('/'), QLatin1Char('\\'))};
    const LaunchHold::ProcessLookup heroicLookup = [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
      if (!heroicTable.contains(pid)) return std::nullopt;
      return heroicTable.value(pid);
    };
    auto heroic = LaunchHold::targetFor({{"source", "Heroic"},
                                         {"appId", "Salt"},
                                         {"installPath", root.filePath(QStringLiteral("games/Celeste"))}});
    heroic.pid = 70;
    QVERIFY(!heroic.directProcess);
    QVERIFY(!LaunchHold::isGameWindow(window(70), heroic, heroicLookup));
    QVERIFY(LaunchHold::isGameWindow(window(71), heroic, heroicLookup));
    // A folder whose name only starts the same is a different game.
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("games/Celes")));
    heroic.installPath = root.filePath(QStringLiteral("games/Celes"));
    QVERIFY(!LaunchHold::isGameWindow(window(71), heroic, heroicLookup));

    // Battle.net installs inside its prefix, and Wine shows the game as C:\...
    const QString prefix = root.filePath(QStringLiteral("battlenet"));
    QVERIFY(QDir(prefix).mkpath(QStringLiteral("drive_c/Program Files (x86)/World of Warcraft")));
    QHash<qint64, LaunchHoldProcess> prefixTable = table;
    prefixTable[71].arguments = {QStringLiteral("C:\\Program Files (x86)\\World of Warcraft\\Wow.exe")};
    prefixTable[71].winePrefix = prefix;
    const LaunchHold::ProcessLookup prefixLookup = [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
      if (!prefixTable.contains(pid)) return std::nullopt;
      return prefixTable.value(pid);
    };
    const auto warcraft = LaunchHold::targetFor(
        {{"source", "Battle.net"},
         {"appId", "WoW"},
         {"installPath", prefix + QStringLiteral("/drive_c/Program Files (x86)/World of Warcraft")}});
    QVERIFY(LaunchHold::isGameWindow(window(71), warcraft, prefixLookup));
    // A prefix reached through a link, with the game named in another case.
    const QString linkedPrefix = root.filePath(QStringLiteral("linked-battlenet"));
    QVERIFY(QFile::link(prefix, linkedPrefix));
    prefixTable[71].winePrefix = linkedPrefix;
    prefixTable[71].arguments = {QStringLiteral("C:\\PROGRAM FILES (X86)\\World of Warcraft\\Wow.exe")};
    QVERIFY(LaunchHold::isGameWindow(window(71), warcraft, prefixLookup));
    prefixTable[71].winePrefix.clear();
    QVERIFY(!LaunchHold::isGameWindow(window(71), warcraft, prefixLookup));

    // An emulator or native game is the launched process itself.
    auto dolphin = LaunchHold::targetFor({{"source", "Dolphin"}, {"appId", "path:/roms/a.iso"}});
    QVERIFY(dolphin.directProcess);
    QVERIFY(!LaunchHold::isGameWindow(window(81), dolphin, lookup));
    dolphin.pid = 80;
    QVERIFY(LaunchHold::isGameWindow(window(81), dolphin, lookup));
    QVERIFY(!LaunchHold::isGameWindow(window(70), dolphin, lookup));
    // A process that is its own parent ends the walk.
    QVERIFY(!LaunchHold::isGameWindow(window(90), dolphin, lookup));
  }

  void launchHoldKeepsPublisherLaunchersOfSteamGames() {
    // Grand Theft Auto V through Steam: PlayGTAV starts Rockstar's launcher, which asks
    // for a sign-in, and then the game. All of them carry the app's Steam name and id.
    QTemporaryDir root;
    const QString library = root.filePath(QStringLiteral("SteamLibrary"));
    const QString install = library + QStringLiteral("/steamapps/common/Grand Theft Auto V");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/271590/pfx");
    QVERIFY(QDir().mkpath(install));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/drive_c/Program Files/Rockstar Games/Launcher")));
    // Proton's prefix names its drives with links; S: here stands for any of them.
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/dosdevices")));
    QVERIFY(QFile::link(QStringLiteral("../drive_c"), prefix + QStringLiteral("/dosdevices/c:")));
    QVERIFY(QFile::link(library + QStringLiteral("/steamapps"), prefix + QStringLiteral("/dosdevices/s:")));
    const QString steamClass = QStringLiteral("steam_app_271590");
    const auto wine = [&](qint64 parent, const QString& program) {
      return LaunchHoldProcess{parent, QStringLiteral("/proton/files/bin/wine64-preloader"),
                               {program}, QStringLiteral("271590"), prefix};
    };
    QHash<qint64, LaunchHoldProcess> table{
        {50, {1, QStringLiteral("/home/player/.steam/steam/ubuntu12_32/steam"), {}, {}, {}}},
        {60, {50, QStringLiteral("/home/player/.steam/steam/ubuntu12_32/reaper"),
              {QStringLiteral("reaper"), QStringLiteral("SteamLaunch")}, QStringLiteral("271590"), {}}},
        {61, wine(60, QStringLiteral("C:\\windows\\system32\\steam.exe"))},
        {62, wine(61, QStringLiteral("Z:") + QString(install).replace(QLatin1Char('/'), QLatin1Char('\\'))
                          + QStringLiteral("\\PlayGTAV.exe"))},
        {63, wine(62, QStringLiteral("C:\\Program Files\\Rockstar Games\\Launcher\\Launcher.exe"))},
        {64, wine(62, QStringLiteral("S:\\common\\Grand Theft Auto V\\GTA5.exe"))},
        {65, wine(61, QStringLiteral("C:\\windows\\system32\\explorer.exe"))}};
    const LaunchHold::ProcessLookup lookup = [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
      if (!table.contains(pid)) return std::nullopt;
      return table.value(pid);
    };
    const auto window = [&](qint64 pid) { return GameModeHeldWindow{QStringLiteral("0x1"), pid, steamClass, {}}; };
    const auto gta = LaunchHold::targetFor(
        {{"source", "Steam"}, {"appId", "271590"}, {"installPath", install}});

    QVERIFY(!LaunchHold::isGameWindow(window(63), gta, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(64), gta, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(62), gta, lookup));
    // Wine's own windows are never the game, wherever Wine started them: Red Dead
    // Redemption's explorer.exe opened an untitled window under PlayRDR.exe that ended the
    // hold before Rockstar's sign-in. Without an install to place it, the same.
    QVERIFY(!LaunchHold::isGameWindow(window(65), gta, lookup));
    table[68] = wine(62, QStringLiteral("C:\\windows\\system32\\explorer.exe"));
    table[68].arguments.append(QStringLiteral("/desktop"));
    QVERIFY(!LaunchHold::isGameWindow(window(68), gta, lookup));
    auto unplaced = gta;
    unplaced.installPath.clear();
    QVERIFY(!LaunchHold::isGameWindow(window(68), unplaced, lookup));
    // A game started through one of them is still the game.
    QVERIFY(LaunchHold::isGameWindow(window(64), unplaced, lookup));
    QVERIFY(!LaunchHold::isGameWindow({QStringLiteral("0x2"), 50, QStringLiteral("steam"), {}}, gta, lookup));
    // Wine names paths in any case.
    table[64].arguments = {QStringLiteral("s:\\COMMON\\grand theft auto v\\GTA5.exe")};
    QVERIFY(LaunchHold::isGameWindow(window(64), gta, lookup));
    // A library reached through a link, as Flatpak Steam's is, is the same library.
    const QString linked = root.filePath(QStringLiteral("linked"));
    QVERIFY(QFile::link(library, linked));
    table[64].arguments = {QStringLiteral("Z:") +
                           QString(linked).replace(QLatin1Char('/'), QLatin1Char('\\')) +
                           QStringLiteral("\\steamapps\\common\\Grand Theft Auto V\\GTA5.exe")};
    QVERIFY(LaunchHold::isGameWindow(window(64), gta, lookup));
    auto viaLink = gta;
    viaLink.installPath = linked + QStringLiteral("/steamapps/common/Grand Theft Auto V");
    QVERIFY(!LaunchHold::isGameWindow(window(63), viaLink, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(62), viaLink, lookup));
    // A path outside the prefix that does not match is no verdict, so it can never hold
    // the game: Steam's word for the app decides, as before.
    table[66] = wine(61, QStringLiteral("S:\\common\\Somewhere Else\\GTA5.exe"));
    QVERIFY(LaunchHold::isGameWindow(window(66), gta, lookup));
    QFile::remove(prefix + QStringLiteral("/dosdevices/s:"));
    table[64].arguments = {QStringLiteral("S:\\common\\Grand Theft Auto V\\GTA5.exe")};
    QVERIFY(LaunchHold::isGameWindow(window(64), gta, lookup));
    // A launcher in the prefix stays held however it was started.
    table[67] = wine(60, QStringLiteral("C:\\Program Files\\Electronic Arts\\EA Desktop\\EALauncher.exe"));
    QVERIFY(!LaunchHold::isGameWindow(window(67), gta, lookup));
    // Without a known install, only Steam's word for the app is left, as before.
    auto bare = gta;
    bare.installPath.clear();
    QVERIFY(LaunchHold::isGameWindow(window(63), bare, lookup));
  }

  void launchHoldKeepsFirstRunSetupHeld() {
    // A first launch through Steam: Steam's install script evaluator runs the game's
    // redistributables under the game's app id, as Steam's own log shows it:
    // reaper SteamLaunch AppId=N Install=1 -- ... proton run .../iscriptevaluator.exe ...
    QTemporaryDir root;
    const QString library = root.filePath(QStringLiteral("SteamLibrary"));
    const QString install = library + QStringLiteral("/steamapps/common/Grand Theft Auto V");
    const QString prefix = library + QStringLiteral("/steamapps/compatdata/271590/pfx");
    QVERIFY(QDir().mkpath(install + QStringLiteral("/_CommonRedist/vcredist/2019")));
    QVERIFY(QDir().mkpath(install + QStringLiteral("/__Installer")));
    QVERIFY(QDir().mkpath(prefix));
    const auto zPath = [](QString path) { return QStringLiteral("Z:") + path.replace(QLatin1Char('/'), QLatin1Char('\\')); };
    const auto wine = [&](qint64 parent, const QString& program) {
      return LaunchHoldProcess{parent, QStringLiteral("/proton/files/bin/wine64-preloader"),
                               {program}, QStringLiteral("271590"), prefix};
    };
    const QString steam = QStringLiteral("/home/player/.steam/steam");
    QHash<qint64, LaunchHoldProcess> table{
        {50, {1, steam + QStringLiteral("/ubuntu12_32/steam"), {}, {}, {}}},
        {60, {50, steam + QStringLiteral("/ubuntu12_32/reaper"),
              {steam + QStringLiteral("/ubuntu12_32/reaper"), QStringLiteral("SteamLaunch"),
               QStringLiteral("AppId=271590"), QStringLiteral("Install=1"), QStringLiteral("--"),
               QStringLiteral("/steamapps/common/Proton - Experimental/proton"), QStringLiteral("run"),
               steam + QStringLiteral("/legacycompat/iscriptevaluator.exe"),
               QStringLiteral("legacycompat\\evaluatorscript_271590.vdf")},
              QStringLiteral("271590"), {}}},
        {61, wine(60, zPath(steam + QStringLiteral("/legacycompat/iscriptevaluator.exe")))},
        {62, wine(61, zPath(install + QStringLiteral("/_CommonRedist/vcredist/2019/VC_redist.x64.exe")))},
        {63, wine(61, zPath(library + QStringLiteral("/steamapps/common/Steamworks Shared/_CommonRedist/DirectX/Jun2010/DXSETUP.exe")))},
        {70, {50, steam + QStringLiteral("/ubuntu12_32/reaper"),
              {QStringLiteral("reaper"), QStringLiteral("SteamLaunch"), QStringLiteral("AppId=271590"),
               QStringLiteral("--"), install + QStringLiteral("/GTA5.exe"), QStringLiteral("Install=1")},
              QStringLiteral("271590"), {}}},
        {71, wine(70, zPath(install + QStringLiteral("/GTA5.exe")))},
        {72, wine(70, zPath(install + QStringLiteral("/__INSTALLER/Touchup.exe")))},
        {73, wine(70, zPath(install + QStringLiteral("/_commonredist/vcredist/2019/VC_redist.x64.exe")))}};
    const LaunchHold::ProcessLookup lookup = [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
      if (!table.contains(pid)) return std::nullopt;
      return table.value(pid);
    };
    const auto window = [](qint64 pid) {
      return GameModeHeldWindow{QStringLiteral("0x1"), pid, QStringLiteral("steam_app_271590"), {}};
    };
    const auto gta = LaunchHold::targetFor(
        {{"source", "Steam"}, {"appId", "271590"}, {"installPath", install}});

    // Setup carries the game's app id and window class, from inside its folder or not.
    QVERIFY(!LaunchHold::isGameWindow(window(61), gta, lookup));
    QVERIFY(!LaunchHold::isGameWindow(window(62), gta, lookup));
    QVERIFY(!LaunchHold::isGameWindow(window(63), gta, lookup));
    // Even with no install known.
    auto bare = gta;
    bare.installPath.clear();
    QVERIFY(!LaunchHold::isGameWindow(window(63), bare, lookup));
    // The game's own arguments after reaper's -- decide nothing about setup.
    QVERIFY(LaunchHold::isGameWindow(window(71), gta, lookup));
    QVERIFY(LaunchHold::isGameWindow(window(71), bare, lookup));
    // Setup a publisher runs from the game's folder is not the game either.
    QVERIFY(!LaunchHold::isGameWindow(window(72), gta, lookup));
    QVERIFY(!LaunchHold::isGameWindow(window(73), gta, lookup));
  }

  void launchHoldListsSteamsWindowsAndWaitsWhileLaunchersWork() {
    HoldCompositor compositor;
    LaunchHold hold(&compositor, GameModeController::workspace(),
                    [](qint64) -> std::optional<LaunchHoldProcess> { return std::nullopt; });
    hold.setPollInterval(10);
    hold.setTimeout(1000);
    // Held while Steam started in the background, before any launch.
    compositor.open({QStringLiteral("0x50"), 50, QStringLiteral("steam"), QStringLiteral("Sign in to Steam")});
    QSignalSpy released(&hold, &LaunchHold::released);
    QVERIFY(hold.begin({{"source", "Heroic"}, {"appId", "Salt"}, {"title", "Celeste"}}));
    QTRY_COMPARE(hold.windows().size(), 1);
    QCOMPARE(hold.windows().first().toMap().value("title").toString(), QStringLiteral("Sign in to Steam"));

    // Each new launcher window gives the hold its full time again.
    QElapsedTimer clock;
    clock.start();
    for (int window = 0; window < 3; ++window) {
      QTest::qWait(600);
      compositor.open({QStringLiteral("0x6%1").arg(window), 60 + window, QStringLiteral("heroic"),
                       QStringLiteral("Update %1").arg(window)});
      QTRY_COMPARE(hold.windows().size(), 2 + window);
      QVERIFY(released.isEmpty());
    }
    // Listed windows wait, however long a first-run update takes.
    QTest::qWait(1500);
    QVERIFY(released.isEmpty());
    QVERIFY(hold.active());

    // With nothing listed, the time runs from the last window closing.
    for (const auto& address : {QStringLiteral("0x50"), QStringLiteral("0x60"), QStringLiteral("0x61"),
                                QStringLiteral("0x62")})
      compositor.close(address);
    QTRY_VERIFY(hold.windows().isEmpty());
    clock.restart();
    QTRY_COMPARE(released.size(), 1);
    QVERIFY(clock.elapsed() >= 800);
  }

  void launchHoldGivesUpOnlyWithNothingListed() {
    HoldCompositor compositor;
    LaunchHold hold(&compositor, GameModeController::workspace(),
                    [](qint64) -> std::optional<LaunchHoldProcess> { return std::nullopt; });
    hold.setPollInterval(10);
    hold.setTimeout(300);
    QSignalSpy released(&hold, &LaunchHold::released);
    // A launch that opens nothing at all ends on its own.
    QVERIFY(hold.begin({{"source", "Heroic"}, {"appId", "Salt"}, {"title", "Celeste"}}));
    QTRY_COMPARE(released.size(), 1);

    // A window being answered is not given up on, though nothing else is listed.
    QVERIFY(hold.begin({{"source", "Heroic"}, {"appId", "Salt"}, {"title", "Celeste"}}));
    compositor.open({QStringLiteral("0x80"), 80, QStringLiteral("heroic"), QStringLiteral("Sign in")});
    QTRY_COMPARE(hold.windows().size(), 1);
    hold.present(QStringLiteral("0x80"));
    QTRY_VERIFY(hold.windows().isEmpty());
    QTest::qWait(800);
    QCOMPARE(released.size(), 1);
    QVERIFY(hold.active());
    // Signed in and closed: the time starts again from the return to the list.
    compositor.close(QStringLiteral("0x80"));
    QElapsedTimer clock;
    clock.start();
    hold.returnToLibrary();
    QTRY_COMPARE(released.size(), 2);
    QVERIFY(clock.elapsed() >= 250);
  }

  void launchHoldShowsTheGameAndReturnsTheRest() {
    HoldCompositor compositor;
    const QHash<qint64, LaunchHoldProcess> table{
        {60, {1, QStringLiteral("/usr/lib/steam/steam"), {}, {}}},
        {61, {1, QStringLiteral("/usr/bin/wine64-preloader"), {}, QStringLiteral("367520")}}};
    LaunchHold hold(&compositor, GameModeController::workspace(),
                    [&](qint64 pid) -> std::optional<LaunchHoldProcess> {
                      if (!table.contains(pid)) return std::nullopt;
                      return table.value(pid);
                    });
    hold.setPollInterval(10);
    QSignalSpy shown(&hold, &LaunchHold::gameShown);
    QSignalSpy released(&hold, &LaunchHold::released);
    QVERIFY(hold.begin({{"source", "Steam"}, {"appId", "367520"}, {"title", "Hollow Knight"}}));
    QVERIFY(hold.active());
    QCOMPARE(hold.title(), QStringLiteral("Hollow Knight"));
    // A launch takes over from holding Steam's windows alone.
    QCOMPARE(compositor.entries(),
             (QStringList{QStringLiteral("hold name:omakade"), QStringLiteral("steam-hold ")}));

    compositor.open({QStringLiteral("0x60"), 60, QStringLiteral("steam"), QStringLiteral("Steam")});
    QTRY_COMPARE(hold.windows().size(), 1);
    QCOMPARE(hold.windows().first().toMap().value("title").toString(), QStringLiteral("Steam"));
    QVERIFY(shown.isEmpty());

    compositor.open({QStringLiteral("0x61"), 61, QStringLiteral("hollow_knight"), QStringLiteral("Hollow Knight")});
    QTRY_COMPARE(shown.size(), 1);
    QVERIFY(!hold.active());
    QVERIFY(hold.windows().isEmpty());
    QVERIFY(released.isEmpty());
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("focus 0x61")));
    // The Steam client stays out of sight; the game takes over fullscreen.
    QCOMPARE(compositor.entries(),
             (QStringList{QStringLiteral("hold name:omakade"), QStringLiteral("steam-hold "),
                          QStringLiteral("hold "), QStringLiteral("mode %1 0 0").arg(kAddress),
                          QStringLiteral("release 0x61 name:omakade"),
                          QStringLiteral("mode 0x61 2 2"), QStringLiteral("focus 0x61")}));

    // The next launch neither lists nor lets go of the Steam client this one left held.
    QVERIFY(hold.begin({{"source", "Steam"}, {"appId", "10"}, {"title", "Next"}}));
    compositor.open({QStringLiteral("0x62"), 62, QStringLiteral("splash"), QStringLiteral("Splash")});
    QTRY_COMPARE(hold.windows().size(), 1);
    QCOMPARE(hold.windows().first().toMap().value("address").toString(), QStringLiteral("0x62"));
    hold.cancel();
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("release 0x62 name:omakade")));
    QVERIFY(!compositor.entries().contains(QStringLiteral("release 0x60 name:omakade")));

    // A kept window that closed is forgotten: a new window given its address is listed.
    compositor.close(QStringLiteral("0x60"));
    QVERIFY(hold.begin({{"source", "Steam"}, {"appId", "10"}, {"title", "Next"}}));
    compositor.open({QStringLiteral("0x60"), 63, QStringLiteral("steam"), QStringLiteral("Sign in to Steam")});
    QTRY_COMPARE(hold.windows().size(), 1);
    QCOMPARE(hold.windows().first().toMap().value("title").toString(), QStringLiteral("Sign in to Steam"));
    hold.cancel();
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("release 0x60 name:omakade")));

    // Leaving Game Mode stops a hold without touching windows; Game Mode returns them.
    QVERIFY(hold.begin({{"source", "Steam"}, {"appId", "10"}, {"title", "Next"}}));
    const auto before = compositor.entries().size();
    hold.abandon();
    QVERIFY(!hold.active());
    QTest::qWait(50);
    QCOMPARE(compositor.entries().size(), before);
  }

  void launchHoldShowsAWindowAndLetsGoInTime() {
    HoldCompositor compositor;
    LaunchHold hold(&compositor, GameModeController::workspace(),
                    [](qint64) -> std::optional<LaunchHoldProcess> { return std::nullopt; });
    hold.setPollInterval(10);
    hold.setTimeout(1500);
    QSignalSpy released(&hold, &LaunchHold::released);
    QVERIFY(hold.begin({{"source", "Heroic"}, {"appId", "Salt"}, {"title", "Celeste"}}));
    compositor.open({QStringLiteral("0x70"), 70, QStringLiteral("heroic"), QStringLiteral("Sign in")});
    compositor.open({QStringLiteral("0x71"), 71, QStringLiteral("heroic"), QStringLiteral("Update")});
    QTRY_COMPARE(hold.windows().size(), 2);

    // Only a held window can be shown; the library leaves fullscreen so it is not covered.
    hold.present(QStringLiteral("0x99"));
    QVERIFY(hold.presented().isEmpty());
    hold.present(QStringLiteral("0x70"));
    QCOMPARE(hold.presented(), QStringLiteral("0x70"));
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("present 0x70 name:omakade 1920x1080")));
    QVERIFY(compositor.entries().contains(QStringLiteral("mode %1 0 0").arg(kAddress)));
    QTRY_COMPARE(hold.windows().size(), 1);
    // Showing another window first puts the shown one back with the others.
    hold.present(QStringLiteral("0x71"));
    QCOMPARE(hold.presented(), QStringLiteral("0x71"));
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("present 0x71 name:omakade 1920x1080")));
    QVERIFY(compositor.entries().indexOf(QStringLiteral("hold-window 0x70")) <
            compositor.entries().indexOf(QStringLiteral("present 0x71 name:omakade 1920x1080")));
    QTRY_VERIFY(hold.windows().size() == 1 &&
                hold.windows().first().toMap().value("address").toString() == QStringLiteral("0x70"));
    hold.returnToLibrary();
    QVERIFY(hold.presented().isEmpty());
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("mode %1 2 2").arg(kAddress)));
    QTRY_VERIFY(compositor.entries().endsWith(QStringLiteral("focus ") + kAddress));
    // A sign-in left open waits in the list again.
    QVERIFY(compositor.entries().contains(QStringLiteral("hold-window 0x71")));
    QTRY_COMPARE(hold.windows().size(), 2);

    // Listed windows are not given up on; B puts everything back, and focus stays with
    // the library.
    QTest::qWait(2000);
    QVERIFY(released.isEmpty());
    QVERIFY(hold.active());
    hold.cancel();
    QTRY_COMPARE(released.size(), 1);
    QVERIFY(!hold.active());
    QTRY_VERIFY(compositor.entries().contains(QStringLiteral("release 0x71 name:omakade")));
    const QStringList log = compositor.entries();
    // The hold is off before anything comes back, so nothing opens out of sight after.
    QVERIFY(log.lastIndexOf(QStringLiteral("hold ")) < log.indexOf(QStringLiteral("release 0x70 name:omakade")));
    QVERIFY(log.lastIndexOf(QStringLiteral("hold ")) < log.indexOf(QStringLiteral("release 0x71 name:omakade")));
    QVERIFY(!log.contains(QStringLiteral("focus 0x71")));
  }

  void launchHoldNeedsACompositorThatCanHold() {
    m_compositor.usable = true;
    LaunchHold hold(&m_compositor, GameModeController::workspace());
    QVERIFY(!hold.begin({{"source", "Steam"}, {"appId", "10"}}));
    QVERIFY(!hold.active());
    LaunchHold none(nullptr, GameModeController::workspace());
    QVERIFY(!none.begin({{"source", "Steam"}, {"appId", "10"}}));
  }

  void sinksAreParsed() {
    const auto sinks = PactlGameModeAudio::parseSinks(
        R"([{"index":51,"name":"alsa_output.hdmi-stereo","description":"QBQ90 HDMI"},
            {"index":52,"name":"headset","description":""},{"index":53}])");
    QCOMPARE(sinks.size(), 2);
    QCOMPARE(sinks.at(0).description, QStringLiteral("QBQ90 HDMI"));
    QCOMPARE(sinks.at(1).name, QStringLiteral("headset"));
    QVERIFY(PactlGameModeAudio::parseSinks("[").isEmpty());
  }

  void sessionCyclesChoicesAndKeepsThem() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("cycle/game-mode.json");
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QCOMPARE(session.displayChoices(), 1);
    session.refresh();
    QTRY_VERIFY(session.displayManaged() && session.soundManaged());
    QCOMPARE(session.displayChoices(), 3);
    QCOMPARE(session.soundChoices(), 3);
    QCOMPARE(session.displayLabel(), QStringLiteral("Current display"));
    QCOMPARE(session.soundLabel(), QStringLiteral("Current sound output"));

    session.cycleDisplay();
    QCOMPARE(session.displayLabel(), QStringLiteral("Dell S2721DGF (DP-2)"));
    session.cycleDisplay();
    QCOMPARE(session.displayLabel(),
             QStringLiteral("%1 (%2) · off until Game Mode").arg(kTvDescription, kTv));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("Headset"));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("QBQ90 HDMI"));
    const GameModeSettings saved = GameModeSession::loadSettings(settingsPath);
    QCOMPARE(saved.outputName, kTv);
    QCOMPARE(saved.outputDescription, kTvDescription);
    QCOMPARE(saved.sinkName, kTvSink);

    // An unplugged choice is shown as missing, not silently replaced.
    m_compositor.list.removeLast();
    m_audio.list.removeLast();
    session.refresh();
    QTRY_COMPARE(session.displayChoices(), 2);
    QCOMPARE(session.displayLabel(), QStringLiteral("%1 · not connected").arg(kTvDescription));
    QCOMPARE(session.soundLabel(), QStringLiteral("%1 · not available").arg(kTvSink));
    session.cycleDisplay();
    QCOMPARE(session.displayLabel(), QStringLiteral("Current display"));
    session.cycleSound();
    QCOMPARE(session.soundLabel(), QStringLiteral("Current sound output"));
  }

  void sessionListsAndSelectsDevicesWithoutLosingMissingChoices() {
    deskAndTv(false);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("select/game-mode.json");
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    session.refresh();
    QTRY_COMPARE(session.displayOptions().size(), 3);
    QCOMPARE(session.displayIndex(), 0);
    session.selectDisplay(2);
    session.selectSound(2);
    QCOMPARE(session.displayIndex(), 2);
    QCOMPARE(session.soundIndex(), 2);
    QVERIFY(session.displayOptions().at(2).toMap().value("label").toString().contains("off until"));
    session.selectDisplay(-1);
    session.selectSound(99);
    QCOMPARE(session.settings().outputName, kTv);
    QCOMPARE(session.settings().sinkName, kTvSink);

    m_compositor.list.removeLast();
    m_audio.list.removeLast();
    session.refresh();
    QTRY_VERIFY(!session.displayOptions().last().toMap().value("available").toBool());
    QCOMPARE(session.displayIndex(), 2);
    QCOMPARE(session.soundIndex(), 2);
    QVERIFY(!session.soundOptions().last().toMap().value("available").toBool());
    QCOMPARE(GameModeSession::loadSettings(settingsPath).outputName, kTv);
    session.selectDisplay(0);
    session.selectSound(0);
    QVERIFY(session.settings().outputName.isEmpty());
    QVERIFY(session.settings().sinkName.isEmpty());
  }

  void sessionStartAndLeaveDoNotBlockOnDiscovery() {
    m_compositor.list = {output(0, kDesk, "Desk", true, true, "3")};
    m_compositor.window = {kAddress, "3", kDesk};
    QSemaphore discoveryStarted, continueDiscovery;
    std::atomic_bool pauseDiscovery{false};
    m_compositor.beforeOutputs = [&] {
      if (pauseDiscovery.exchange(false)) {
        discoveryStarted.release();
        continueDiscovery.tryAcquire(1, 1000);
      }
    };
    GameModeSession session(&m_compositor, &m_audio, &m_notifications,
                            m_directory.filePath("responsive-settings.json"), statePath());
    for (bool entering : {true, false}) {
      // Wait until any refresh from the previous transition has settled.
      QSignalSpy devices(&session, &GameModeSession::devicesChanged);
      session.refresh();
      QTRY_VERIFY(!devices.isEmpty());
      pauseDiscovery = true;
      session.refresh();
      QVERIFY(discoveryStarted.tryAcquire(1, 1000));
      QElapsedTimer elapsed;
      elapsed.start();
      if (entering)
        session.enter();
      else
        session.exit();
      const auto duration = elapsed.elapsed();
      continueDiscovery.release();
      QVERIFY2(duration < 200, "Device discovery blocked the UI transition");
      QTRY_VERIFY(!session.busy());
      QCOMPARE(session.active(), entering);
    }
  }

  void sessionSignalsTheWindowInOrder() {
    deskAndTv(true);
    const QString settingsPath = m_directory.filePath("order/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings()));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QStringList order;
    connect(&session, &GameModeSession::entered, this, [&order] { order.append("entered"); });
    connect(&session, &GameModeSession::leaving, this, [&order] { order.append("leaving"); });
    connect(&session, &GameModeSession::exited, this, [&order] { order.append("exited"); });
    connect(&session, &GameModeSession::failed, this, [&order] { order.append("failed"); });

    session.enter();
    QVERIFY(session.busy());
    QTRY_VERIFY(session.active() && !session.busy());
    QCOMPARE(m_compositor.window.output, kTv);
    // Choices are fixed while a session is using them.
    session.cycleDisplay();
    session.selectDisplay(0);
    session.selectSound(0);
    session.setSilenceNotifications(false);
    QCOMPARE(session.settings().outputName, kTv);
    QVERIFY(session.silenceNotifications());

    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
    QCOMPARE(order, (QStringList{"entered", "leaving", "exited"}));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!QFile::exists(statePath()));
  }

  void steamStartsWithTheSessionWhenChosen() {
    deskAndTv(true);
    const QString settingsPath = m_directory.filePath("steam/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings()));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QVERIFY(!session.steamAvailable());
    int starts = 0;
    auto result = GameModeSession::SteamStart::Started;
    session.setSteamStarter([&] { ++starts; return result; });
    QVERIFY(session.steamAvailable());

    // Off by default: a session starts nothing.
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    QCOMPARE(starts, 0);
    // Turning it on during a session starts Steam straight away.
    session.setWarmSteam(true);
    QCOMPARE(starts, 1);
    QVERIFY(GameModeSession::loadSettings(settingsPath).warmSteam);
    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
    QCOMPARE(starts, 1);

    QSignalSpy notice(&session, &GameModeSession::notice);
    result = GameModeSession::SteamStart::Failed;
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    QCOMPARE(starts, 2);
    QVERIFY(std::any_of(notice.cbegin(), notice.cend(), [](const QList<QVariant>& args) {
      return args.first().toString().contains("Steam could not be started");
    }));
    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());

    // With launcher windows held, Steam's own windows wait too while it starts.
    m_compositor.canHold = true;
    session.setHoldLaunchers(true);
    result = GameModeSession::SteamStart::Started;
    m_compositor.log.clear();
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    QVERIFY(m_compositor.log.contains(QStringLiteral("steam-hold name:omakade")));
    QVERIFY(!m_compositor.log.contains(QStringLiteral("steam-hold ")));
    // A launch hold that takes over lists Steam's windows, so none come back here.
    m_compositor.held = {{QStringLiteral("0x41"), 41, QStringLiteral("steam"), QStringLiteral("Sign in")}};
    session.endSteamHold(false);
    QVERIFY(m_compositor.log.contains(QStringLiteral("steam-hold ")));
    QVERIFY(!m_compositor.log.join(QLatin1Char('\n')).contains(QStringLiteral("release-held")));
    // Leaving withdraws it.
    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
    QVERIFY(m_compositor.log.contains(QStringLiteral("steam-hold ")));

    // Turning holding off brings back what Steam opened, and only that.
    m_compositor.held = {{QStringLiteral("0x40"), 40, QStringLiteral("steam"), QStringLiteral("Kept")}};
    m_compositor.log.clear();
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    m_compositor.held.append({QStringLiteral("0x41"), 41, QStringLiteral("steam"), QStringLiteral("Sign in")});
    m_compositor.held.append({QStringLiteral("0x42"), 42, QStringLiteral("heroic"), QStringLiteral("Heroic")});
    session.setHoldLaunchers(false);
    QVERIFY(m_compositor.log.contains(QStringLiteral("release-held 0x41 name:omakade")));
    QVERIFY(!m_compositor.log.contains(QStringLiteral("release-held 0x40 name:omakade")));
    QVERIFY(!m_compositor.log.contains(QStringLiteral("release-held 0x42 name:omakade")));
    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
    m_compositor.held.clear();
    session.setHoldLaunchers(true);

    // A Steam that was already running is left alone: its windows open as usual.
    result = GameModeSession::SteamStart::Running;
    m_compositor.log.clear();
    session.enter();
    QTRY_VERIFY(session.active() && !session.busy());
    const auto on = m_compositor.log.indexOf(QStringLiteral("steam-hold name:omakade"));
    QVERIFY(on >= 0 && m_compositor.log.indexOf(QStringLiteral("steam-hold "), on) > on);
    session.exit();
    QTRY_VERIFY(!session.active() && !session.busy());
  }

  void sessionSaysWhyItCouldNotStart() {
    m_compositor.list = {
        output(0, kDesk, QStringLiteral("Dell S2721DGF"), true, true, QStringLiteral("3"))};
    m_compositor.window = {kAddress, QStringLiteral("3"), kDesk};
    const QString settingsPath = m_directory.filePath("missing/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings()));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    QSignalSpy failed(&session, &GameModeSession::failed);
    QSignalSpy entered(&session, &GameModeSession::entered);
    session.enter();
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains("not connected"));
    QVERIFY(!session.active() && !session.busy());
    QVERIFY(session.statusText().contains("not connected"));
    QCOMPARE(entered.size(), 0);
  }

  void closingOmakadeLeavesGameMode() {
    deskAndTv(true);
    m_audio.list.append({kTvSink, QStringLiteral("QBQ90 HDMI")});
    const QString settingsPath = m_directory.filePath("shutdown/game-mode.json");
    QVERIFY(GameModeSession::saveSettings(settingsPath, tvSettings(kTvSink)));
    GameModeSession session(&m_compositor, &m_audio, &m_notifications, settingsPath, statePath());
    session.enter();
    QTRY_VERIFY(session.active());
    QCOMPARE(m_audio.current, kTvSink);
    session.shutdown();
    QVERIFY(!session.active());
    QCOMPARE(m_audio.current, QStringLiteral("headset"));
    QCOMPARE(m_compositor.window.workspace, QStringLiteral("3"));
    QVERIFY(!m_notifications.quiet);
    QVERIFY(!QFile::exists(statePath()));
  }

  void choicesSurviveARestart() {
    const QString path = m_directory.filePath("nested/game-mode.json");
    const GameModeSettings defaults = GameModeSession::loadSettings(path);
    QVERIFY(defaults.outputName.isEmpty() && defaults.sinkName.isEmpty());
    QVERIFY(defaults.silenceNotifications);

    GameModeSettings chosen = tvSettings(kTvSink);
    chosen.silenceNotifications = false;
    QVERIFY(GameModeSession::saveSettings(path, chosen));
    const GameModeSettings loaded = GameModeSession::loadSettings(path);
    QCOMPARE(loaded.outputName, kTv);
    QCOMPARE(loaded.outputDescription, kTvDescription);
    QCOMPARE(loaded.sinkName, kTvSink);
    QVERIFY(!loaded.silenceNotifications);
    QVERIFY(!defaults.warmSteam);
    chosen.warmSteam = true;
    QVERIFY(GameModeSession::saveSettings(path, chosen));
    QVERIFY(GameModeSession::loadSettings(path).warmSteam);
    QVERIFY(!defaults.holdLaunchers);
    chosen.holdLaunchers = true;
    QVERIFY(GameModeSession::saveSettings(path, chosen));
    QVERIFY(GameModeSession::loadSettings(path).holdLaunchers);
  }
};

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  GameModeTests tests;
  // Keep the expanded suite within each CTest deadline without dropping cases.
  // Ordinary direct invocations still support QTest's individual-test arguments.
  if (argc != 2 || !QByteArray(argv[1]).startsWith("--test-shard="))
    return QTest::qExec(&tests, argc, argv);
  const QByteArray shardArgument(argv[1]);
  bool valid = false;
  const int shard = shardArgument.mid(sizeof("--test-shard=") - 1).toInt(&valid);
  if (!valid || shard < 0 || shard > 1) return 2;
  QStringList arguments{QString::fromLocal8Bit(argv[0])};
  const QMetaObject* meta = tests.metaObject();
  int testIndex = 0;
  for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
    const QMetaMethod method = meta->method(index);
    const QByteArray name = method.name();
    if (method.methodType() != QMetaMethod::Slot || method.parameterCount() != 0 ||
        name.endsWith("_data") || name == "init" || name == "cleanup" ||
        name == "initTestCase" || name == "cleanupTestCase") continue;
    if (testIndex++ % 2 == shard) arguments.append(QString::fromLatin1(name));
  }
  return QTest::qExec(&tests, arguments);
}
#include "GameModeTests.moc"

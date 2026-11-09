#pragma once

#include "gamemode/GameModePorts.h"
#include "tracking/GameStopper.h"

#include <QString>
#include <QVector>

#include <atomic>

namespace SelfTest {

// Library-only transport for --game-mode-test. It exercises the real session and
// QML lifecycle offscreen without reaching any desktop, process or audio service.
class LibraryGameModeTestCompositor final : public GameModeCompositor {
public:
  GameModeWindow window{QStringLiteral("0xaa"), QStringLiteral("1"), QStringLiteral("fixture")};
  bool placeFails = false;
  std::atomic<bool> mapped{true};
  std::atomic<bool> remapped{false};
  bool available() override { return true; }
  QVector<GameModeOutput> outputs(QString*) override {
    GameModeOutput output;
    output.name = QStringLiteral("fixture");
    output.enabled = true;
    output.focused = true;
    output.workspace = mapped.load() ? window.workspace : QStringLiteral("1");
    return {output};
  }
  bool setOutputEnabled(const QString&, bool, QString*) override { return true; }
  GameModeWindow windowForPid(qint64) override {
    if (!mapped.load()) return {};
    // A hidden Qt surface maps as a new client on the current desktop workspace.
    if (remapped.exchange(false)) window.workspace = QStringLiteral("1");
    return window;
  }
  // Windows of other processes on the Game Mode workspace, such as a game the fixture
  // pretends is running there.
  std::atomic<int> otherWindows{0};
  int otherWindowsOn(const QString&, qint64) override { return otherWindows.load(); }
  QStringList otherWindowAddressesOn(const QString&, qint64) override { return {}; }
  bool gameWindows(const QString&, qint64, QVector<GameModeGameWindow>* games, QString*) override {
    games->clear();
    return true;
  }
  bool desktopFocus(GameModeDesktopFocus* focus, QString*) override {
    *focus = mapped.load() ? GameModeDesktopFocus{window.output, window.workspace, window.address}
                          : GameModeDesktopFocus{window.output, QStringLiteral("1"), QStringLiteral("0xdd")};
    return true;
  }
  GameModeWindow placeholderForPid(qint64) override { return {}; }
  bool holdPlaceholder(QString*) override { return false; }
  bool placeWindow(const QString&, const QString& workspace, const QString& output, const QString&,
                   QString*) override {
    if (placeFails) return false;
    window.workspace = workspace;
    window.output = output;
    return true;
  }
  bool returnWindow(const QString&, const QString& workspace, const QString&, QString*) override {
    window.workspace = workspace;
    return true;
  }
  bool setWindowMode(const QString&, int mode, int client, QString*) override {
    window.fullscreenMode = mode;
    window.fullscreenClient = client;
    return true;
  }
  bool moveWorkspace(const QString&, const QString&, QString*) override { return true; }
  bool focusWindow(const QString&, QString*) override { return true; }
  bool focusWorkspace(const QString&, QString*) override { return true; }
  bool focusOutput(const QString&, QString*) override { return true; }
};

// A sink that signals nothing, for the render overlays that drive the stop
// confirmation: the overlay has to press the confirm action to prove it works,
// and this is what keeps that from touching a real process.
class NoSignalSink final : public GameStop::SignalSink {
public:
  GameStop::LeverResult terminate(qint64) override { return GameStop::LeverResult::Done; }
  GameStop::LeverResult forceTerminate(qint64) override { return GameStop::LeverResult::Done; }
  GameStop::LeverResult stopWinePrefix(const QString&, bool) override {
    return GameStop::LeverResult::Done;
  }
  GameStop::LeverResult stopFlatpakApp(const QString&) override {
    return GameStop::LeverResult::Done;
  }
};

} // namespace SelfTest

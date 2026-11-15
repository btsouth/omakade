#pragma once

#include "gamemode/GameModePorts.h"
#include "tracking/GameStopper.h"

#include <QObject>
#include <QQuickWindow>
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
  // The Steam client's own windows there, as its library left open: counted, but no game.
  std::atomic<int> steamWindows{0};
  GameModeWorkspaceWindows workspaceWindows(const QString&, qint64) override {
    return {otherWindows.load() + steamWindows.load(), otherWindows.load()};
  }
  QStringList otherWindowAddressesOn(const QString&, qint64) override { return {}; }
  // Steam's Refresh Sign In window, open on the Game Mode workspace, and how many times it
  // was brought in front.
  std::atomic<bool> steamSignIn{false};
  std::atomic<int> presented{0};
  bool openWindows(QVector<GameModeHeldWindow>* windows, QString*) override {
    windows->clear();
    if (steamSignIn.load())
      windows->append({QStringLiteral("0xee"), 4242, QStringLiteral("steam"),
                       QStringLiteral("Refresh Sign In"), 705, 470});
    return true;
  }
  bool presentWindow(const QString& address, const QString&, int, int, QString*) override {
    if (address == QStringLiteral("0xee")) ++presented;
    return true;
  }
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

// Stands in for Game Mode's layer-shell overlay, which an offscreen run cannot create. It
// counts the times the controls ask for it and declines, so they open in the window, unless
// accept is set, which leaves the overlay as an ordinary window.
class GameModeOverlaySpy final : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  int requests = 0;
  bool accept = false;
  Q_INVOKABLE bool prepare(QQuickWindow*, const QString&) {
    ++requests;
    return accept;
  }
};

} // namespace SelfTest

#include "gamemode/GameModeController.h"
#include "tracking/ProcFs.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <limits>

namespace {
constexpr int kPollStepMs = 250;
// The placeholder is Omakade's own window, so it maps within a frame or two.
constexpr int kPlaceholderWaitMs = 1500;
constexpr int kPlaceholderStepMs = 40;
// A cold start may ask for Game Mode before the compositor has mapped the window.
constexpr int kWindowWaitMs = 3000;
// Televisions take several seconds to accept a mode after the output is enabled.
constexpr int kOutputWaitMs = 10000;
// An HDMI sink only appears once its display is live.
constexpr int kSinkWaitMs = 8000;

bool defaultOwnerAlive(qint64 pid) {
  // An exited child can retain its name in /proc until its parent reaps it.
  // It must not prevent recovery of the desktop it left behind.
  if (!ProcFs::processRunning(pid)) {
    return false;
  }
  QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
  if (!comm.open(QIODevice::ReadOnly)) {
    return false;
  }
  return comm.readAll().trimmed() == "omakade";
}

QString outputLabel(const GameModeOutput& output) {
  return output.description.isEmpty() ? output.name : output.description;
}
} // namespace

QJsonObject GameModeState::toJson() const {
  QJsonArray gamesJson, streamsJson;
  for (const auto& game : games) {
    gamesJson.append(QJsonObject{{"address", game.address},
                                 {"pid", game.process.pid},
                                 {"start", game.process.procStart},
                                 {"steam", game.process.steamAppId},
                                 {"wine", game.process.winePrefix},
                                 {"flatpak", game.process.flatpakAppId},
                                 {"fullscreen", game.fullscreen},
                                 {"fullscreen_client", game.fullscreenClient}});
  }
  for (const auto& stream : mutedStreams) {
    streamsJson.append(QJsonObject{{"index", static_cast<qint64>(stream.index)},
                                   {"pid", stream.process.pid},
                                   {"start", stream.process.procStart},
                                   {"steam", stream.process.steamAppId},
                                   {"token", stream.token},
                                   {"muted", stream.muted}});
  }
  return {{"version", 2},
          {"phase", phase == GameModePhase::DesktopRetained ? "parked" : "active"},
          {"owner_start", ownerStart},
          {"temporary_window", temporaryWindow},
          {"desktop_pending", desktopPending},
          {"retention_ending", retentionEnding},
          {"focused_workspace", focusedWorkspace},
          {"focused_window", focusedWindow},
          {"last_game_window", lastGameWindow},
          {"games", gamesJson},
          {"streams", streamsJson},
          {"owner_pid", ownerPid},
          {"output", output},
          {"enabled_output", enabledOutput},
          {"output_workspace", outputWorkspace},
          {"focused_output", focusedOutput},
          {"window_workspace", windowWorkspace},
          {"window_fullscreen", windowFullscreen},
          {"window_fullscreen_client", windowFullscreenClient},
          {"window_placed", windowPlaced},
          {"library_presented", libraryPresented},
          {"placeholder", placeholder},
          {"previous_sink", previousSink},
          {"session_sink", sessionSink},
          {"silenced_notifications", silencedNotifications}};
}

bool GameModeState::fromJson(const QJsonObject& object, GameModeState* state) {
  if (state == nullptr ||
      (object.value("version").toInt() != 1 && object.value("version").toInt() != 2)) {
    return false;
  }
  if (object.value("version").toInt() == 2 &&
      object.value("phase").toString() != QStringLiteral("active") &&
      object.value("phase").toString() != QStringLiteral("parked"))
    return false;
  *state = {};
  state->phase = object.value("phase").toString() == "parked" ? GameModePhase::DesktopRetained
                                                              : GameModePhase::Active;
  state->ownerStart = object.value("owner_start").toVariant().toLongLong();
  if (!object.contains("owner_start"))
    state->ownerStart = -1;
  state->temporaryWindow = object.value("temporary_window").toBool();
  state->desktopPending = object.value("desktop_pending").toBool();
  state->retentionEnding = object.value("retention_ending").toBool();
  state->focusedWorkspace = object.value("focused_workspace").toString();
  state->focusedWindow = object.value("focused_window").toString();
  state->lastGameWindow = object.value("last_game_window").toString();
  for (const auto& value : object.value("games").toArray()) {
    const auto game = value.toObject();
    GameModeProcess process{game.value("pid").toVariant().toLongLong(),
                            game.value("start").toVariant().toLongLong(),
                            game.value("steam").toString(), game.value("wine").toString(),
                            game.value("flatpak").toString()};
    if (!process.valid() || game.value("address").toString().isEmpty())
      return false;
    const int fullscreen = game.value("fullscreen").toInt();
    const int fullscreenClient = game.value("fullscreen_client").toInt();
    if (fullscreen < 0 || fullscreen > 3 || fullscreenClient < 0 || fullscreenClient > 3)
      return false;
    state->games.append({game.value("address").toString(), process, fullscreen, fullscreenClient});
  }
  for (const auto& value : object.value("streams").toArray()) {
    const auto stream = value.toObject();
    GameModeStream record;
    bool indexOk = false;
    const auto streamIndex = stream.value("index").toVariant().toString().toULongLong(&indexOk);
    if (!indexOk || streamIndex > std::numeric_limits<quint32>::max())
      return false;
    record.index = static_cast<quint32>(streamIndex);
    record.process = {stream.value("pid").toVariant().toLongLong(),
                      stream.value("start").toVariant().toLongLong(),
                      stream.value("steam").toString()};
    record.token = stream.value("token").toString();
    record.muted = stream.value("muted").toBool();
    if (!record.process.valid() || record.token.isEmpty())
      return false;
    state->mutedStreams.append(record);
  }
  state->ownerPid = object.value("owner_pid").toVariant().toLongLong();
  state->output = object.value("output").toString();
  state->enabledOutput = object.value("enabled_output").toBool();
  state->outputWorkspace = object.value("output_workspace").toString();
  state->focusedOutput = object.value("focused_output").toString();
  state->windowWorkspace = object.value("window_workspace").toString();
  state->windowFullscreen = object.value("window_fullscreen").toInt(-1);
  state->windowFullscreenClient = object.value("window_fullscreen_client").toInt(-1);
  if (state->windowFullscreen < -1 || state->windowFullscreen > 3 ||
      state->windowFullscreenClient < -1 || state->windowFullscreenClient > 3 ||
      (state->windowFullscreen < 0) != (state->windowFullscreenClient < 0))
    return false;
  state->windowPlaced = object.value("window_placed").toBool();
  state->libraryPresented = object.value("library_presented").toBool();
  state->placeholder = object.value("placeholder").toBool();
  state->previousSink = object.value("previous_sink").toString();
  state->sessionSink = object.value("session_sink").toString();
  state->silencedNotifications = object.value("silenced_notifications").toBool();
  return true;
}

QString GameModeController::workspace() { return QStringLiteral("name:omakade"); }

GameModeController::GameModeController(GameModeCompositor* compositor, GameModeAudio* audio,
                                       GameModeNotifications* notifications,
                                       const QString& statePath, Sleep sleep, OwnerAlive ownerAlive)
    : m_compositor(compositor), m_audio(audio), m_notifications(notifications),
      m_statePath(statePath), m_sleep(std::move(sleep)),
      m_ownerAlive(ownerAlive ? std::move(ownerAlive) : OwnerAlive(defaultOwnerAlive)) {}

bool GameModeController::managed() const {
  return m_compositor != nullptr && m_compositor->available();
}

int GameModeController::findOutput(const QVector<GameModeOutput>& outputs, const QString& name,
                                   const QString& description) {
  int byDescription = -1;
  int descriptionMatches = 0;
  int byName = -1;
  for (int index = 0; index < outputs.size(); ++index) {
    const GameModeOutput& output = outputs.at(index);
    if (!description.isEmpty() && output.description == description) {
      ++descriptionMatches;
      // Two identical displays share a description; the connector breaks the tie.
      if (byDescription < 0 || output.name == name) {
        byDescription = index;
      }
    }
    if (!name.isEmpty() && output.name == name && byName < 0) {
      byName = index;
    }
  }
  if (descriptionMatches > 0) {
    return byDescription;
  }
  // A connector that now carries a different display is not the chosen display.
  if (byName >= 0 && !description.isEmpty() && !outputs.at(byName).description.isEmpty()) {
    return -1;
  }
  return byName;
}

bool GameModeController::save(const GameModeState& state) const {
  if (m_statePath.isEmpty() || !QDir().mkpath(QFileInfo(m_statePath).absolutePath())) {
    return false;
  }
  QSaveFile file(m_statePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  const auto bytes = QJsonDocument(state.toJson()).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size())
    return false;
  return file.commit();
}

bool GameModeController::load(GameModeState* state) const {
  QFile file(m_statePath);
  if (m_statePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  file.close();
  if (!document.isObject() || !GameModeState::fromJson(document.object(), state)) {
    // Preserve an unreadable record: dropping it could strand muted game audio.
    return false;
  }
  return true;
}

void GameModeController::forget() const {
  if (!m_statePath.isEmpty()) {
    QFile::remove(m_statePath);
  }
}

bool GameModeController::waitFor(const std::function<bool()>& ready, int timeoutMs,
                                 int stepMs) const {
  QElapsedTimer elapsed;
  elapsed.start();
  int slept = 0;
  for (;;) {
    if (ready()) {
      return true;
    }
    // Compositor/audio queries take time too, especially when a helper stalls.
    // Injected sleeps can advance a test's clock without actually blocking.
    const qint64 remaining = timeoutMs - qMax<qint64>(slept, elapsed.elapsed());
    if (remaining <= 0) {
      return false;
    }
    const int delay = static_cast<int>(qMin<qint64>(stepMs, remaining));
    if (m_sleep) {
      m_sleep(delay);
    } else {
      QThread::msleep(delay);
    }
    slept += delay;
  }
}

void GameModeController::showPlaceholder(bool visible) const {
  if (m_placeholder) {
    m_placeholder(visible);
  }
}

GameModeController::Result GameModeController::enter(const GameModeSettings& settings,
                                                     qint64 windowPid) {
  Result result;
  if (m_parked)
    return resume(settings, windowPid);
  if (m_active) {
    result.error = QStringLiteral("Game Mode is already on.");
    return result;
  }
  // What a crashed session left changed would otherwise be recorded as the state to
  // return to.
  const Result recovered = recover();
  if (!recovered.ok) {
    // Another running Omakade owns the session.
    result.error = recovered.error;
    return result;
  }
  // Completed recovery notes remain useful; unfinished recovery blocks new effects.
  result.notes = recovered.notes;
  forget();

  GameModeState state;
  state.ownerPid = QCoreApplication::applicationPid();
  for (const auto& process : ProcFs::listProcesses()) {
    if (process.pid == state.ownerPid)
      state.ownerStart = process.procStart;
  }
  state.temporaryWindow = m_temporaryWindow;
  const bool displayChosen =
      !settings.outputName.isEmpty() || !settings.outputDescription.isEmpty();
  const bool compositor = managed();
  if (displayChosen && !compositor) {
    result.error = QStringLiteral(
        "Choosing a Game Mode display needs Hyprland with a Lua configuration. Choose "
        "Current display instead.");
    return result;
  }
  const auto fail = [&](const QString& message) {
    if (!restore(state, windowPid, false, &result.notes)) {
      m_state = state;
      m_active = true;
      (void)save(m_state);
    } else {
      forget();
    }
    result.error = message;
    return result;
  };

  GameModeWindow window;
  if (compositor) {
    QString snapshotError;
    // Capture before showing a cold root: mapping it may take focus.
    const bool captured = captureDesktop(&state, &snapshotError);
    if (state.temporaryWindow && !captured) {
      result.error = snapshotError;
      return result;
    }
    if (state.temporaryWindow)
      visibility(true);
    QString error;
    const QVector<GameModeOutput> outputs = m_compositor->outputs(&error);
    if (outputs.isEmpty()) {
      result.error = QStringLiteral("Hyprland did not report any displays.");
      return result;
    }
    (void)waitFor(
        [&] {
          window = m_compositor->windowForPid(windowPid);
          return window.valid();
        },
        kWindowWaitMs);
    if (displayChosen && !window.valid()) {
      return fail(QStringLiteral("Could not find Omakade's window on the desktop."));
    }
    int index = -1;
    if (displayChosen) {
      index = findOutput(outputs, settings.outputName, settings.outputDescription);
      if (index < 0) {
        result.error = QStringLiteral("The Game Mode display is not connected: %1")
                           .arg(settings.outputDescription.isEmpty() ? settings.outputName
                                                                     : settings.outputDescription);
        return result;
      }
    } else {
      int focused = -1;
      for (int candidate = 0; candidate < outputs.size(); ++candidate) {
        if (window.valid() && outputs.at(candidate).name == window.output) {
          index = candidate;
        }
        if (outputs.at(candidate).focused) {
          focused = candidate;
        }
      }
      if (index < 0) {
        index = focused;
      }
      if (index < 0) {
        result.error = QStringLiteral("Could not tell which display Omakade is on.");
        return result;
      }
    }
    const GameModeOutput target = outputs.at(index);
    state.output = target.name;
    state.windowWorkspace = window.workspace;
    state.windowFullscreen = window.fullscreenMode;
    state.windowFullscreenClient = window.fullscreenClient;
    if (state.focusedOutput.isEmpty()) {
      for (const auto& output : outputs)
        if (output.focused)
          state.focusedOutput = output.name;
    }
    if (target.enabled) {
      state.outputWorkspace = target.workspace;
    } else {
      state.enabledOutput = true;
      if (!save(state)) {
        result.error =
            QStringLiteral("Could not record Game Mode's state, so nothing was changed.");
        return result;
      }
      if (!m_compositor->setOutputEnabled(target.name, true, &error)) {
        return fail(QStringLiteral("Could not turn on %1.").arg(outputLabel(target)));
      }
      const bool live = waitFor(
          [&] {
            for (const GameModeOutput& output : m_compositor->outputs()) {
              if (output.name == target.name) {
                if (output.enabled && !output.workspace.isEmpty()) {
                  state.outputWorkspace = output.workspace;
                  return true;
                }
                return false;
              }
            }
            return false;
          },
          kOutputWaitMs);
      if (!live) {
        return fail(QStringLiteral("%1 did not turn on. Check that it is powered and set to "
                                   "this computer's input.")
                        .arg(outputLabel(target)));
      }
    }
  }

  if (!settings.sinkName.isEmpty()) {
    if (m_audio == nullptr || !m_audio->available()) {
      return fail(QStringLiteral("The sound output cannot be changed because pactl is not "
                                 "available."));
    }
    const bool present = waitFor(
        [&] {
          for (const GameModeSink& sink : m_audio->sinks()) {
            if (sink.name == settings.sinkName) {
              return true;
            }
          }
          return false;
        },
        state.enabledOutput ? kSinkWaitMs : 0);
    if (!present) {
      return fail(QStringLiteral("The Game Mode sound output is not available."));
    }
    const QString previous = m_audio->defaultSink();
    if (previous != settings.sinkName) {
      state.previousSink = previous;
      state.sessionSink = settings.sinkName;
      if (!save(state)) {
        state.previousSink.clear();
        state.sessionSink.clear();
        return fail(QStringLiteral("Could not record Game Mode's state."));
      }
      if (!m_audio->setDefaultSink(settings.sinkName)) {
        return fail(QStringLiteral("Could not switch to the Game Mode sound output."));
      }
    }
  }

  if (compositor && window.valid()) {
    QString error;
    // A tiled window that is simply moved away and back lands wherever the layout puts a
    // new window. A placeholder keeps its exact place instead. A floating window keeps its
    // own position, so it needs none. The compositor clears fullscreen before swapping.
    GameModeWindow placeholder;
    if (window.workspace != workspace() && !state.temporaryWindow && m_placeholder &&
        !window.floating && m_compositor->holdPlaceholder()) {
      showPlaceholder(true);
      (void)waitFor(
          [&] {
            placeholder = m_compositor->placeholderForPid(windowPid);
            return placeholder.valid();
          },
          kPlaceholderWaitMs, kPlaceholderStepMs);
      if (!placeholder.valid()) {
        showPlaceholder(false);
      }
    }
    state.windowPlaced = true;
    state.desktopPending = true;
    state.placeholder = placeholder.valid();
    if (!save(state)) {
      return fail(QStringLiteral("Could not record Game Mode's window position."));
    }
    if (!m_compositor->prepareWindow(window.address, state.outputWorkspace, state.output, placeholder.address,
                                   &error)) {
      return fail(QStringLiteral("Could not move Omakade to the Game Mode display."));
    }
  }

  // Notifications never decide whether Game Mode starts.
  if (settings.silenceNotifications && m_notifications != nullptr && m_notifications->available()) {
    bool silenced = false;
    if (m_notifications->silenced(&silenced) && !silenced) {
      state.silencedNotifications = true;
      if (!save(state)) {
        state.silencedNotifications = false;
        result.notes.append(QStringLiteral(
            "Notifications were left on because recovery state could not be saved."));
      } else if (!m_notifications->setSilenced(true)) {
        state.silencedNotifications = false;
        (void)save(state);
      }
    }
  }

  if (!save(state)) {
    if (state.windowPlaced || state.enabledOutput || !state.sessionSink.isEmpty() ||
        state.silencedNotifications)
      return fail(QStringLiteral("Could not record Game Mode's state."));
    result.notes.append(
        QStringLiteral("Recovery storage is unavailable; no desktop settings were changed."));
  }
  if (compositor && window.valid()) {
    prepareFrame(state.output);
    QString error;
    if (!m_compositor->placeWindow(window.address, workspace(), state.output, {}, &error))
      return fail(QStringLiteral("Could not show Omakade on the Game Mode workspace."));
  }
  m_state = state;
  m_sessionSettings = settings;
  m_active = true;
  result.ok = true;
  result.output = state.output;
  return result;
}

void GameModeController::visibility(bool visible) const {
  if (m_windowVisibility)
    m_windowVisibility(visible);
}

void GameModeController::prepareFrame(const QString& output) const {
  if (!m_framePreparation) return;
  QSize size;
  for (const auto& candidate : m_compositor->outputs())
    if (candidate.name == output) {
      const double scale = candidate.scale > 0 ? candidate.scale : 1;
      size = QSize(qRound(candidate.width / scale), qRound(candidate.height / scale));
      if (candidate.transform % 2) size.transpose();
    }
  // The opaque scene is mapped on the visible output. A missing frame callback falls
  // through to the atomic move/focus, rather than rolling back or waiting seconds.
  (void)m_framePreparation(size);
}

bool GameModeController::captureDesktop(GameModeState* state, QString* error) {
  GameModeDesktopFocus focus;
  if (!managed()) {
    if (error)
      *error =
          QStringLiteral("The desktop compositor is unavailable; the session is still retained.");
    return false;
  }
  if (!m_compositor->desktopFocus(&focus, error))
    return false;
  state->focusedOutput = focus.output;
  state->focusedWorkspace = focus.workspace;
  state->focusedWindow = focus.address;
  return true;
}

bool GameModeController::muteGames(QString* error) {
  if (!m_audio || !m_audio->available()) {
    if (error)
      *error = QStringLiteral("Quiet Return to Desktop needs available game audio controls.");
    return false;
  }
  QVector<GameModeStream> streams;
  if (!m_audio->streams(&streams, error))
    return false;
  for (const auto& stream : streams) {
    bool attributed = false;
    for (const auto& game : m_state.games) {
      if (!m_compositor->processAlive(game.process))
        continue;
      if (stream.process == game.process ||
          (stream.process.valid() && !game.process.steamAppId.isEmpty() &&
           game.process.steamAppId != QStringLiteral("0") &&
           stream.process.steamAppId == game.process.steamAppId)) {
        attributed = true;
        break;
      }
    }
    if (!attributed) {
      bool ambiguous = !stream.muted && !stream.process.valid();
      for (const auto& game : m_state.games) {
        if (stream.process.pid == game.process.pid ||
            (!game.process.winePrefix.isEmpty() &&
             game.process.winePrefix == stream.process.winePrefix) ||
            (!game.process.flatpakAppId.isEmpty() &&
             game.process.flatpakAppId == stream.process.flatpakAppId))
          ambiguous = true;
      }
      if (ambiguous) {
        if (error)
          *error = QStringLiteral("Potential game audio could not be attributed safely; shared "
                                  "Wine or Flatpak scope cannot authorize muting.");
        return false;
      }
      continue;
    }
    if (!stream.process.valid() || stream.token.isEmpty()) {
      if (error)
        *error = QStringLiteral(
            "This game's audio has no stable stream identity, so it cannot be hidden safely.");
      return false;
    }
    bool recorded = false;
    for (const auto& previous : m_state.mutedStreams)
      if (previous.sameStream(stream))
        recorded = true;
    if (!recorded) {
      m_state.mutedStreams.append(stream); // includes its original mute state
      if (!save(m_state)) {
        m_state.mutedStreams.removeLast();
        if (error)
          *error = QStringLiteral("Could not record the game's audio recovery state.");
        return false;
      }
    }
    if (!stream.muted && !m_audio->setStreamMuted(stream, true, error))
      return false;
  }
  return true;
}

bool GameModeController::unmuteGames(QStringList* notes) {
  if (m_state.mutedStreams.isEmpty())
    return true;
  QVector<GameModeStream> streams;
  QString error;
  if (!m_audio || !m_audio->available() || !m_audio->streams(&streams, &error)) {
    if (notes)
      notes->append(QStringLiteral("Game audio recovery is pending: %1").arg(error));
    return false;
  }
  bool complete = true;
  for (int index = m_state.mutedStreams.size() - 1; index >= 0; --index) {
    const auto record = m_state.mutedStreams.at(index);
    bool found = false;
    for (const auto& stream : streams) {
      if (!stream.sameStream(record))
        continue;
      found = true;
      if (!record.muted && stream.muted && !m_audio->setStreamMuted(record, false, &error)) {
        complete = false;
        if (notes)
          notes->append(QStringLiteral("The game's audio could not be restored: %1").arg(error));
      } else {
        m_state.mutedStreams.removeAt(index);
        if (!save(m_state)) {
          m_state.mutedStreams.insert(index, record);
          complete = false;
        }
      }
      break;
    }
    if (!found) {
      // Gone or reused stream: do not write to the new process or stream.
      m_state.mutedStreams.removeAt(index);
      if (!save(m_state)) {
        m_state.mutedStreams.insert(index, record);
        complete = false;
      }
    }
  }
  return complete;
}

bool GameModeController::focusRetainedGame() {
  if (!managed() || m_state.games.isEmpty())
    return false;
  QVector<GameModeGameWindow> windows;
  QString error;
  if (!m_compositor->gameWindows(workspace(), m_state.ownerPid, &windows, &error))
    return false;
  // Recovery can carry an exited or reused owner's pid. Only this process's exact
  // identity permits clearing the library root's fullscreen state.
  const qint64 ownerPid = m_state.ownerPid == QCoreApplication::applicationPid() &&
                                  ProcFs::processAlive(m_state.ownerPid, m_state.ownerStart)
                              ? m_state.ownerPid
                              : 0;
  GameModeGameWindow fallback;
  for (const auto& window : windows) {
    for (const auto& retained : m_state.games) {
      if (window.address == retained.address && window.process == retained.process &&
          m_compositor->processAlive(window.process)) {
        if (window.address == m_state.lastGameWindow)
          return m_compositor->focusGameWindow(retained, ownerPid, &error);
        if (fallback.address.isEmpty())
          fallback = retained;
      }
    }
  }
  if (fallback.address.isEmpty())
    return false;
  m_state.lastGameWindow = fallback.address;
  return m_compositor->focusGameWindow(fallback, ownerPid, &error);
}

bool GameModeController::exposeGames(QStringList* notes, bool focus) {
  if (!managed()) {
    if (notes)
      notes->append(QStringLiteral("The desktop is unavailable; game recovery remains recorded."));
    return false;
  }
  for (int index = m_state.games.size() - 1; index >= 0; --index)
    if (!m_compositor->processAlive(m_state.games.at(index).process))
      m_state.games.removeAt(index);
  // A disappeared workspace is normal once the last game process has ended.
  if (m_state.games.isEmpty())
    return true;
  QString error;
  const auto outputs = m_compositor->outputs(&error);
  QString destination;
  for (const auto& output : outputs)
    if (output.enabled && (destination.isEmpty() || output.focused))
      destination = output.name;
  if (destination.isEmpty() || !m_compositor->moveWorkspace(workspace(), destination, &error)) {
    if (notes)
      notes->append(QStringLiteral("The game workspace could not be exposed: %1").arg(error));
    return false;
  }
  if (focus) {
    if (!focusRetainedGame() && !m_compositor->focusWorkspace(workspace(), &error)) {
      if (notes)
        notes->append(QStringLiteral("The retained game could not be focused: %1").arg(error));
      return false;
    }
  }
  return true;
}

bool GameModeController::finishRetention(qint64 windowPid, QStringList* notes) {
  m_state.retentionEnding = true;
  if (!save(m_state)) {
    if (notes)
      notes->append(QStringLiteral("Retention cleanup could not be recorded."));
    return false;
  }
  for (int index = m_state.games.size() - 1; index >= 0; --index)
    if (m_compositor && !m_compositor->processAlive(m_state.games.at(index).process))
      m_state.games.removeAt(index);
  // A partial park can still own a window, focus, sink, notifications or TV power.
  // Restore those first so the final exposure is on a surviving, accessible display.
  const bool libraryPresented = m_state.libraryPresented;
  const bool desktopRestored = restore(m_state, windowPid, false, notes, true);
  const bool exposed = m_state.games.isEmpty() || exposeGames(notes, !libraryPresented);
  const bool audioRestored = exposed && unmuteGames(notes);
  if (!desktopRestored || !exposed || !audioRestored) {
    (void)save(m_state);
    return false;
  }
  forget();
  m_state = {};
  m_active = false;
  m_parked = false;
  return true;
}

GameModeController::Result GameModeController::park(qint64 windowPid) {
  Result result;
  result.output = m_state.output;
  if (m_parked) {
    if (m_state.retentionEnding)
      return refreshParked();
    result.ok = true;
    return result;
  }
  if (!m_active) {
    result.error = QStringLiteral("Game Mode is not active.");
    return result;
  }
  QString error;
  QVector<GameModeGameWindow> games;
  if (!managed() || !m_compositor->gameWindows(workspace(), windowPid, &games, &error)) {
    result.error =
        error.isEmpty()
            ? QStringLiteral(
                  "Quiet Return to Desktop needs an available compositor and safe game discovery.")
            : error;
    return result;
  }
  for (const auto& game : games) {
    if (!game.process.valid() || !m_compositor->processAlive(game.process)) {
      result.error = QStringLiteral("The running game's identity could not be verified.");
      return result;
    }
  }
  const GameModeState original = m_state;
  m_state.games = games;
  GameModeDesktopFocus focus;
  if (!games.isEmpty() && !m_compositor->desktopFocus(&focus, &error)) {
    m_state = original;
    result.error = error;
    return result;
  }
  for (const auto& game : games)
    if (game.address == focus.address)
      m_state.lastGameWindow = game.address;
  if (games.isEmpty())
    m_state.lastGameWindow.clear();
  else if (m_state.lastGameWindow.isEmpty())
    m_state.lastGameWindow = games.first().address;
  // Persist park intent before any audio write; crash recovery must unmute and expose.
  m_state.phase = GameModePhase::DesktopRetained;
  if (!save(m_state)) {
    m_state = original;
    result.error = QStringLiteral("Could not record Return to Desktop recovery state.");
    return result;
  }
  if (!games.isEmpty() && !muteGames(&error)) {
    const bool undone = unmuteGames(&result.notes);
    if (undone) {
      m_state = original;
      if (!save(m_state))
        result.notes.append(QStringLiteral("Recovery state could not be updated."));
    }
    result.error =
        QStringLiteral("The game stayed visible because it could not be silenced: %1").arg(error);
    return result;
  }
  // Leaving Couch Mode can change fullscreen on the game workspace. Capture
  // the game first; UI work remains queued and is processed during unmap waits.
  if (m_beforeParkRestore)
    m_beforeParkRestore();
  if (!restore(m_state, windowPid, false, &result.notes, true)) {
    // Remain owned, with all pending effects recorded. Resume rolls back partial park.
    m_parked = true;
    m_active = false;
    const auto rollback = resume(m_sessionSettings, windowPid);
    // An unsuccessful return is not time spent away on a new desktop.
    if (m_active || m_parked) {
      m_state.focusedOutput = original.focusedOutput;
      m_state.focusedWorkspace = original.focusedWorkspace;
      m_state.focusedWindow = original.focusedWindow;
      m_state.outputWorkspace = original.outputWorkspace;
      if (!save(m_state))
        result.notes.append(QStringLiteral("The rollback desktop snapshot could not be recorded."));
    }
    result.notes.append(rollback.notes);
    if (!rollback.ok)
      result.notes.append(rollback.error);
    result.error = QStringLiteral("Return to Desktop failed; the retained session needs recovery.");
    return result;
  }
  m_parked = true;
  m_active = false;
  result.ok = true;
  return result;
}

GameModeController::Result GameModeController::showLibrary(qint64 windowPid) {
  Result result;
  result.output = m_state.output;
  if (!m_parked || m_state.retentionEnding) {
    result.error = QStringLiteral("No parked Game Mode library is available.");
    return result;
  }
  if (m_state.libraryPresented) {
    const auto window = m_compositor->windowForPid(windowPid);
    if (window.valid() && window.workspace == QStringLiteral("name:omakade-library")) {
      result.ok = m_compositor->focusWindow(window.address);
      return result;
    }
    if (!restore(m_state, windowPid, false, &result.notes, true)) {
      result.error = QStringLiteral("The guide library's desktop state needs recovery.");
      return result;
    }
  }
  QString error;
  // Own the presentation before mapping or changing the restored desktop window.
  // Resume must undo it before taking a fresh desktop snapshot.
  if (!captureDesktop(&m_state, &error)) {
    result.error = error;
    return result;
  }
  const auto desktopWindow = m_compositor->windowForPid(windowPid);
  if (!m_state.temporaryWindow && desktopWindow.valid()) {
    m_state.windowWorkspace = desktopWindow.workspace;
    m_state.windowFullscreen = desktopWindow.fullscreenMode;
    m_state.windowFullscreenClient = desktopWindow.fullscreenClient;
  }
  m_state.libraryPresented = true;
  m_state.windowPlaced = true;
  m_state.desktopPending = true;
  if (!save(m_state)) {
    m_state.libraryPresented = false;
    m_state.windowPlaced = false;
    m_state.desktopPending = false;
    result.error = QStringLiteral("Could not record the guide library presentation.");
    return result;
  }
  if (m_state.temporaryWindow) visibility(true);
  GameModeWindow window;
  const bool mapped = waitFor([&] {
    window = m_compositor->windowForPid(windowPid);
    return window.valid();
  }, kWindowWaitMs);
  GameModeWindow placeholder;
  if (mapped && !m_state.temporaryWindow && m_placeholder && !window.floating &&
      m_compositor->holdPlaceholder()) {
    showPlaceholder(true);
    (void)waitFor([&] {
      placeholder = m_compositor->placeholderForPid(windowPid);
      return placeholder.valid();
    }, kPlaceholderWaitMs, kPlaceholderStepMs);
    if (!placeholder.valid()) showPlaceholder(false);
  }
  m_state.placeholder = placeholder.valid();
  // The retained game's workspace stays hidden, with its fullscreen unchanged.
  if (!save(m_state) || !mapped ||
      !m_compositor->placeWindow(window.address, QStringLiteral("name:omakade-library"),
                                m_state.focusedOutput, placeholder.address, &error)) {
    (void)restore(m_state, windowPid, false, &result.notes, true);
    result.error = QStringLiteral("The guide library could not be placed: %1").arg(error);
    return result;
  }
  result.ok = true;
  return result;
}

GameModeController::Result GameModeController::resume(const GameModeSettings& settings,
                                                      qint64 windowPid) {
  Result result;
  result.output = m_state.output;
  if (!m_parked) {
    result.error = QStringLiteral("No retained Game Mode session is waiting to resume.");
    return result;
  }
  const auto refreshed = refreshParked();
  if (!refreshed.ok || !m_parked)
    return refreshed;
  if (m_state.libraryPresented && !restore(m_state, windowPid, false, &result.notes, true)) {
    result.error = QStringLiteral("The guide library's desktop state could not be restored.");
    return result;
  }
  const GameModeState parkedState = m_state;
  QString error;
  GameModeState next = m_state;
  // Capture the desktop before a hidden cold root is mapped or any focus changes.
  if (!captureDesktop(&next, &error)) {
    result.error = error;
    return result;
  }
  if (!next.temporaryWindow && !next.windowPlaced && !next.desktopPending) {
    const auto currentWindow = m_compositor->windowForPid(windowPid);
    if (currentWindow.valid() && currentWindow.workspace != workspace()) {
      next.windowWorkspace = currentWindow.workspace;
      next.windowFullscreen = currentWindow.fullscreenMode;
      next.windowFullscreenClient = currentWindow.fullscreenClient;
    }
  }
  const auto outputs = m_compositor->outputs(&error);
  QString chosen = m_state.output;
  if (!settings.outputName.isEmpty() || !settings.outputDescription.isEmpty()) {
    const int index = findOutput(outputs, settings.outputName, settings.outputDescription);
    if (index < 0) {
      result.error = QStringLiteral("The Game Mode display is no longer connected.");
      return result;
    }
    chosen = outputs.at(index).name;
  }
  int targetIndex = -1;
  for (int index = 0; index < outputs.size(); ++index)
    if (outputs.at(index).name == chosen)
      targetIndex = index;
  if (targetIndex < 0) {
    result.error = QStringLiteral("The Game Mode display is no longer connected.");
    return result;
  }
  next.output = chosen;
  next.outputWorkspace = outputs.at(targetIndex).workspace;
  next.enabledOutput = m_state.enabledOutput || !outputs.at(targetIndex).enabled;
  next.phase = GameModePhase::DesktopRetained; // pending resume remains recoverable
  m_state = next;
  const auto fail = [&](const QString& message) {
    // Restore only this resume's effects; games and original mute records stay retained.
    const bool undone = restore(m_state, windowPid, false, &result.notes, true);
    if (undone) {
      m_state = parkedState;
      (void)save(m_state);
    }
    // If unmute failed partially, re-mute every surviving stream before hiding.
    QString muteError;
    if (!m_state.games.isEmpty() && !muteGames(&muteError)) {
      result.notes.append(muteError);
      (void)finishRetention(windowPid, &result.notes);
    }
    result.error = message;
    return result;
  };
  if (!save(m_state)) {
    m_state = parkedState;
    result.error = QStringLiteral("Could not record resume recovery state.");
    return result;
  }
  if (m_state.enabledOutput) {
    if (!m_compositor->setOutputEnabled(chosen, true, &error))
      return fail(QStringLiteral("Could not turn the Game Mode display back on."));
    if (!waitFor(
            [&] {
              for (const auto& output : m_compositor->outputs())
                if (output.name == chosen && output.enabled && !output.workspace.isEmpty()) {
                  m_state.outputWorkspace = output.workspace;
                  return true;
                }
              return false;
            },
            kOutputWaitMs))
      return fail(QStringLiteral("The Game Mode display did not turn back on."));
  }
  if (!settings.sinkName.isEmpty()) {
    bool present = false;
    if (m_audio && m_audio->available())
      present = waitFor(
          [&] {
            for (const auto& sink : m_audio->sinks())
              if (sink.name == settings.sinkName)
                return true;
            return false;
          },
          m_state.enabledOutput ? kSinkWaitMs : 0);
    if (!present)
      return fail(QStringLiteral("The Game Mode sound output is unavailable."));
    const auto previous = m_audio->defaultSink();
    if (previous != settings.sinkName) {
      m_state.previousSink = previous;
      m_state.sessionSink = settings.sinkName;
      if (!save(m_state) || !m_audio->setDefaultSink(settings.sinkName))
        return fail(QStringLiteral("Could not select the Game Mode sound output."));
    }
  }
  m_state.desktopPending = true;
  m_state.windowPlaced = true;
  if (!save(m_state))
    return fail(QStringLiteral("Could not record desktop restoration state."));
  if (m_state.temporaryWindow)
    visibility(true);
  GameModeWindow window;
  if (!waitFor(
          [&] {
            window = m_compositor->windowForPid(windowPid);
            return window.valid();
          },
          kWindowWaitMs))
    return fail(QStringLiteral("Omakade's session window could not be mapped."));
  // The warm window may have moved while parked; this cycle owns its current home.
  if (window.workspace != workspace()) {
    GameModeWindow placeholder;
    if (!m_state.temporaryWindow && m_placeholder && !window.floating && m_compositor->holdPlaceholder()) {
      showPlaceholder(true);
      (void)waitFor(
          [&] {
            placeholder = m_compositor->placeholderForPid(windowPid);
            return placeholder.valid();
          },
          kPlaceholderWaitMs, kPlaceholderStepMs);
      if (!placeholder.valid())
        showPlaceholder(false);
    }
    m_state.placeholder = placeholder.valid();
    m_state.windowPlaced = true;
    if (!save(m_state) || !m_compositor->prepareWindow(window.address, m_state.outputWorkspace, chosen,
                                                       placeholder.address, &error))
      return fail(QStringLiteral("Omakade's session window could not be restored."));
  }
  else if (!m_compositor->prepareWindow(window.address, m_state.outputWorkspace, chosen, {}, &error))
    return fail(QStringLiteral("Omakade's session window could not be prepared."));
  prepareFrame(chosen);
  // Placement creates an empty library workspace if parking removed it, or
  // relocates the retained game workspace in the same compositor transaction.
  if (!m_compositor->placeWindow(window.address, workspace(), chosen, {}, &error))
    return fail(QStringLiteral("The retained game workspace could not be restored: %1").arg(error));
  if (settings.silenceNotifications && m_notifications && m_notifications->available()) {
    bool silenced = false;
    if (m_notifications->silenced(&silenced) && !silenced) {
      m_state.silencedNotifications = true;
      if (!save(m_state) || !m_notifications->setSilenced(true))
        return fail(QStringLiteral("Could not restore Game Mode notification settings."));
    }
  }
  // Unmute only once the game is exposed. On failure the rollback retains the journal.
  if (!unmuteGames(&result.notes))
    return fail(QStringLiteral("The retained game's audio could not be restored."));
  m_state.phase = GameModePhase::Active;
  if (!save(m_state))
    return fail(QStringLiteral("Could not record the resumed session."));
  m_parked = false;
  m_active = true;
  m_sessionSettings = settings;
  result.ok = true;
  result.resumedGame = !m_state.games.isEmpty();
  result.output = chosen;
  // Parent restores couch/fullscreen UI before focusRetainedGame(), avoiding UI focus races.
  return result;
}

GameModeController::Result GameModeController::refreshParked() {
  Result result;
  result.ok = true;
  result.output = m_state.output;
  if (!m_parked)
    return result;
  if (m_state.retentionEnding) {
    result.ok = finishRetention(m_state.ownerPid, &result.notes);
    if (!result.ok)
      result.error = QStringLiteral("Retained game cleanup remains pending.");
    return result;
  }
  QString error;
  QVector<GameModeGameWindow> windows;
  QVector<GameModeGameWindow> survivors;
  for (const auto& game : m_state.games)
    if (m_compositor && m_compositor->processAlive(game.process))
      survivors.append(game);
  if (!managed() ||
      !m_compositor->gameWindows(workspace(), m_state.ownerPid, &windows, &error)) {
    result.ok = false;
    result.error =
        error.isEmpty() ? QStringLiteral("The retained game cannot be inspected safely.") : error;
    // Unknown is not empty. Keep an empty library's authority and journal so a
    // delayed launch can be discovered on retry; never silently discard it.
    if (!survivors.isEmpty() && !finishRetention(m_state.ownerPid, &result.notes))
      result.notes.append(QStringLiteral("Retained game cleanup remains recorded."));
    return result;
  }
  // The same verified workspace discovery used at park authorizes late arrivals,
  // even when no game had mapped yet. Audio still requires a direct process/start
  // identity or exact Steam witness; Wine/Flatpak scope never authorizes adoption.
  for (const auto& window : windows) {
    if (window.address.isEmpty() || !window.process.valid() ||
        window.process.pid == m_state.ownerPid || !m_compositor->processAlive(window.process)) {
      result.ok = false;
      result.error = QStringLiteral("The arriving game's identity could not be verified.");
      return result;
    }
    bool known = false;
    for (const auto& game : survivors) {
      if (window.address == game.address && window.process == game.process)
        known = true;
    }
    // Preserve the captured presentation for known windows across every poll.
    if (!known)
      survivors.append(window);
  }
  m_state.games = survivors;
  if (survivors.isEmpty()) {
    // The library owns the session, even after the last game exits. Restore any
    // partial park effects and exact surviving audio records without moving focus
    // for a completed park. No audio service is needed for a verified empty,
    // fully restored library session.
    m_state.lastGameWindow.clear();
    for (int index = m_state.mutedStreams.size() - 1; index >= 0; --index)
      if (m_compositor && !m_compositor->processAlive(m_state.mutedStreams.at(index).process))
        m_state.mutedStreams.removeAt(index); // Never touch a dead or reused stream owner.
    const bool audioRestored = unmuteGames(&result.notes);
    const bool desktopRestored = m_state.libraryPresented ||
        restore(m_state, m_state.ownerPid, false, &result.notes, true);
    result.ok = audioRestored && desktopRestored && save(m_state);
    if (!result.ok)
      result.error =
          QStringLiteral("The library is retained, but desktop or audio recovery remains pending.");
    return result;
  }
  // Release exited/reused streams without changing any desktop focus.
  QVector<GameModeStream> streams;
  if (!m_audio || !m_audio->available() || !m_audio->streams(&streams, &error)) {
    result.ok = false;
  } else {
    for (int index = m_state.mutedStreams.size() - 1; index >= 0; --index) {
      bool found = false;
      for (const auto& stream : streams)
        if (stream.sameStream(m_state.mutedStreams.at(index)))
          found = true;
      if (!found)
        m_state.mutedStreams.removeAt(index);
    }
    result.ok = save(m_state) && muteGames(&error);
  }
  if (!result.ok) {
    result.error = QStringLiteral("The retained game could not stay quiet: %1").arg(error);
    // Fatal audio failure exposes the game, then restores audio; it is never silently hidden.
    if (!finishRetention(m_state.ownerPid, &result.notes))
      result.notes.append(QStringLiteral("Retained game cleanup remains recorded."));
  }
  return result;
}

GameModeController::Result GameModeController::exit(qint64 windowPid) {
  if (!m_active && !m_parked)
    return recover();
  Result result;
  result.output = m_state.output;
  if (m_parked) {
    // Shutdown/screen removal explicitly relinquishes retention and exposes the game.
    result.ok = finishRetention(windowPid, &result.notes);
  } else {
    const bool audioRestored = unmuteGames(&result.notes);
    const bool desktopRestored = restore(m_state, windowPid, false, &result.notes);
    result.ok = audioRestored && desktopRestored;
  }
  if (!result.ok) {
    result.error =
        QStringLiteral("Game Mode could not be fully restored; recovery remains recorded.");
    (void)save(m_state);
    return result;
  }
  forget();
  m_state = {};
  m_active = false;
  m_parked = false;
  return result;
}

GameModeController::Result GameModeController::recover() {
  Result result;
  result.ok = true;
  GameModeState state;
  if (!load(&state)) {
    if (QFile::exists(m_statePath)) {
      result.ok = false;
      result.error = QStringLiteral("Game Mode's recovery record could not be read safely.");
    }
    return result;
  }
  const qint64 self = QCoreApplication::applicationPid();
  const bool sameOwner =
      state.ownerStart < 0 || ProcFs::processAlive(state.ownerPid, state.ownerStart);
  const bool currentOwner = state.ownerPid == self && sameOwner;
  if (currentOwner && (m_active || m_parked))
    return result;
  if (state.ownerPid != self && sameOwner && m_ownerAlive(state.ownerPid)) {
    result.ok = false;
    result.error = QStringLiteral("Game Mode belongs to another running Omakade.");
    return result;
  }
  result.output = state.output;
  m_state = state;
  const bool retained = state.phase == GameModePhase::DesktopRetained;
  // A parked crash must not replay already-consumed desktop changes.
  result.ok = restore(m_state, currentOwner ? self : 0, !currentOwner, &result.notes, retained);
  bool audioSafe = true;
  if (retained && !m_state.games.isEmpty()) {
    audioSafe = exposeGames(&result.notes, true);
    result.ok = audioSafe && result.ok;
  }
  const bool audioRestored = audioSafe && unmuteGames(&result.notes);
  result.ok = audioRestored && result.ok;
  if (!result.ok) {
    (void)save(m_state);
    result.error = QStringLiteral("An interrupted Game Mode session could not be fully undone.");
  } else {
    forget();
    m_state = {};
  }
  return result;
}

bool GameModeController::restore(GameModeState& state, qint64 windowPid, bool ownerGone,
                                 QStringList* notes, bool retained) {
  bool complete = true;
  const auto note = [&](const QString& text) {
    if (notes)
      notes->append(text);
  };
  const auto record = [&] {
    if (!save(state)) {
      complete = false;
      note(QStringLiteral("Restored effects could not be recorded; recovery remains pending."));
    }
  };
  if (state.silencedNotifications) {
    if (!m_notifications || !m_notifications->available() || !m_notifications->setSilenced(false)) {
      complete = false;
      note(QStringLiteral("Notifications are still silenced."));
    } else {
      state.silencedNotifications = false;
      record();
    }
  }
  const bool compositor = managed();
  const bool putFocusBack = state.desktopPending || state.windowPlaced;
  if (putFocusBack && !ownerGone)
    state.desktopPending = true;
  if (putFocusBack && !compositor) {
    complete = false;
    note(QStringLiteral("The desktop is unavailable, so windows could not be put back."));
  }
  if (compositor && state.windowPlaced) {
    bool returned = true;
    if (!ownerGone && !state.temporaryWindow && windowPid > 0 && !state.windowWorkspace.isEmpty()) {
      const auto window = m_compositor->windowForPid(windowPid);
      if (window.valid() && (window.workspace == workspace() ||
          (state.libraryPresented && window.workspace == QStringLiteral("name:omakade-library")))) {
        const auto placeholder =
            state.placeholder ? m_compositor->placeholderForPid(windowPid) : GameModeWindow{};
        const bool traded =
            placeholder.valid() &&
            m_compositor->returnWindow(window.address, state.windowWorkspace,
                                       placeholder.address) &&
            m_compositor->windowForPid(windowPid).workspace == state.windowWorkspace;
        if (!traded && !m_compositor->returnWindow(window.address, state.windowWorkspace, {})) {
          returned = false;
          complete = false;
          note(QStringLiteral("Omakade's window could not be moved back."));
        }
      }
    }
    if (returned && !ownerGone && state.temporaryWindow) {
      visibility(false);
      if (!waitFor([&] { return !m_compositor->windowForPid(windowPid).valid(); }, kWindowWaitMs,
                   kPlaceholderStepMs)) {
        returned = false;
        complete = false;
        note(QStringLiteral(
            "Omakade's temporary window did not hide; desktop recovery is pending."));
      }
    }
    if (returned && state.placeholder) {
      showPlaceholder(false);
      if (!ownerGone &&
          !waitFor([&] { return !m_compositor->placeholderForPid(windowPid).valid(); },
                   kPlaceholderWaitMs, kPlaceholderStepMs)) {
        returned = false;
        complete = false;
        note(QStringLiteral("The layout placeholder did not hide; desktop recovery is pending."));
      }
    }
    if (returned && !ownerGone && !state.temporaryWindow && windowPid > 0 &&
        state.windowFullscreen >= 0) {
      const auto restoredWindow = m_compositor->windowForPid(windowPid);
      if (!restoredWindow.valid() ||
          !m_compositor->setWindowMode(restoredWindow.address, state.windowFullscreen,
                                       state.windowFullscreenClient)) {
        returned = false;
        complete = false;
        note(QStringLiteral("Omakade's original window mode could not be restored."));
      }
    }
    if (returned) {
      state.windowPlaced = false;
      state.libraryPresented = false;
      state.placeholder = false;
      record();
    }
  }
  if (state.windowPlaced && !ownerGone)
    return false;
  // Legacy exit/recovery still brings leftover windows home. Normal park never does.
  if (compositor && !retained) {
    QString destination = state.windowWorkspace;
    if (destination.isEmpty() && state.outputWorkspace != workspace())
      destination = state.outputWorkspace;
    if (!destination.isEmpty())
      for (const auto& address : m_compositor->otherWindowAddressesOn(workspace(), state.ownerPid))
        if (!m_compositor->returnWindow(address, destination, {})) {
          complete = false;
          note(QStringLiteral("A window left on the Game Mode display could not be moved."));
        }
  }
  if (compositor && putFocusBack && !ownerGone && !state.windowPlaced) {
    if (!state.output.isEmpty() && !state.outputWorkspace.isEmpty() &&
        state.outputWorkspace != workspace()) {
      if (!m_compositor->focusOutput(state.output) ||
          !m_compositor->focusWorkspace(state.outputWorkspace)) {
        complete = false;
        note(QStringLiteral("The previous display workspace could not be restored."));
      }
    }
    if (!state.focusedOutput.isEmpty() && !m_compositor->focusOutput(state.focusedOutput))
      complete = false;
    if (!state.focusedWorkspace.isEmpty() && !m_compositor->focusWorkspace(state.focusedWorkspace))
      complete = false;
    if (!state.focusedWindow.isEmpty() && !m_compositor->focusWindow(state.focusedWindow)) {
      // The precise window may have closed. The captured workspace remains the fallback.
      note(QStringLiteral("The previously focused window is no longer available."));
    }
  }
  if (compositor && putFocusBack && !state.windowPlaced) {
    if (complete) {
      state.desktopPending = false;
      record();
    }
  }
  if (!state.sessionSink.isEmpty()) {
    if (!m_audio || !m_audio->available()) {
      complete = false;
      note(QStringLiteral("The sound output could not be put back."));
    } else {
      const auto sinks = m_audio->sinks();
      const auto exists = [&](const QString& name) {
        for (const auto& sink : sinks)
          if (sink.name == name)
            return true;
        return false;
      };
      const QString current = m_audio->defaultSink();
      bool restored = true;
      if (current == state.sessionSink || !exists(state.sessionSink)) {
        if (state.previousSink.isEmpty() || !exists(state.previousSink))
          note(QStringLiteral("The previous sound output is gone, so the current one was kept."));
        else if (current != state.previousSink && !m_audio->setDefaultSink(state.previousSink))
          restored = false;
      }
      if (restored) {
        state.sessionSink.clear();
        state.previousSink.clear();
        record();
      } else {
        complete = false;
        note(QStringLiteral("The sound output could not be put back."));
      }
    }
  }
  if (state.enabledOutput && compositor) {
    QString outputError;
    const auto outputs = m_compositor->outputs(&outputError);
    bool connected = false;
    for (const auto& output : outputs)
      if (output.name == state.output)
        connected = true;
    if (!connected && outputError.isEmpty() && !outputs.isEmpty()) {
      state.enabledOutput = false;
      record();
    }
  }
  if (state.enabledOutput) {
    bool safe = compositor;
    if (safe && retained && !state.games.isEmpty()) {
      QString error;
      const auto outputs = m_compositor->outputs(&error);
      QString destination;
      for (const auto& output : outputs) {
        if (!output.enabled || output.name == state.output)
          continue;
        if (destination.isEmpty() || output.name == state.focusedOutput)
          destination = output.name;
      }
      safe =
          !destination.isEmpty() && m_compositor->moveWorkspace(workspace(), destination, &error);
      if (!safe)
        note(QStringLiteral("The game workspace could not be relocated safely: %1").arg(error));
    }
    if (!safe || !m_compositor->setOutputEnabled(state.output, false)) {
      complete = false;
      note(QStringLiteral("%1 was kept on to avoid stranding a game.").arg(state.output));
    } else {
      state.enabledOutput = false;
      record();
    }
  }
  if (complete) {
    state.outputWorkspace.clear();
    if (QFile::exists(m_statePath))
      record();
  }
  return complete;
}

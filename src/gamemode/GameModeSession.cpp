#include "gamemode/GameModeSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QtConcurrentRun>

GameModeSession::GameModeSession(GameModeCompositor* compositor, GameModeAudio* audio,
                                 GameModeNotifications* notifications, const QString& settingsPath,
                                 const QString& statePath, QObject* parent)
    : QObject(parent), m_compositor(compositor), m_audio(audio), m_notifications(notifications),
      m_settingsPath(settingsPath), m_controller(compositor, audio, notifications, statePath),
      m_settings(loadSettings(settingsPath)) {
  connect(&m_refreshWatcher, &QFutureWatcher<Devices>::finished, this,
          &GameModeSession::finishRefresh);
  connect(&m_changeWatcher, &QFutureWatcher<GameModeController::Result>::finished, this,
          &GameModeSession::finishChange);
  m_controller.setPlaceholder([this](bool visible) { emit placeholderRequested(visible); });
  m_controller.setWindowVisibility([this](bool visible) { emit windowVisibilityRequested(visible); });
  m_controller.setOpenOutput([this](const QString& output) { emit openOutputRequested(output); });
  m_controller.setBeforeParkRestore([this] {
    m_parkUiLeft = true;
    emit leaving(true);
  });
  m_parkTimer.setInterval(1000);
  connect(&m_parkTimer, &QTimer::timeout, this, &GameModeSession::refreshParked);
  connect(&m_workspaceWatcher, &QFutureWatcher<int>::finished, this,
          [this] { emit workspaceChecked(m_workspaceWatcher.result()); });
  if (auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    connect(application, &QGuiApplication::screenRemoved, this, &GameModeSession::screenRemoved);
  }
}

GameModeSession::~GameModeSession() {
  m_refreshWatcher.waitForFinished();
  m_changeWatcher.waitForFinished();
  m_focusFuture.waitForFinished();
  m_workspaceWatcher.waitForFinished();
}

GameModeSettings GameModeSession::loadSettings(const QString& path) {
  GameModeSettings settings;
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    return settings;
  }
  const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
  settings.outputName = object.value("output_name").toString();
  settings.outputDescription = object.value("output_description").toString();
  settings.sinkName = object.value("sink").toString();
  settings.silenceNotifications = object.value("silence_notifications").toBool(true);
  return settings;
}

bool GameModeSession::saveSettings(const QString& path, const GameModeSettings& settings) {
  if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(QJsonObject{{"output_name", settings.outputName},
                                       {"output_description", settings.outputDescription},
                                       {"sink", settings.sinkName},
                                       {"silence_notifications", settings.silenceNotifications}})
                 .toJson(QJsonDocument::Indented));
  return file.commit();
}

QString GameModeSession::outputLabel(const GameModeOutput& output) {
  if (output.description.isEmpty()) {
    return output.name;
  }
  return QStringLiteral("%1 (%2)").arg(output.description, output.name);
}

int GameModeSession::currentDisplayIndex() const {
  if (m_settings.outputName.isEmpty() && m_settings.outputDescription.isEmpty()) {
    return 0;
  }
  const int index = GameModeController::findOutput(m_outputs, m_settings.outputName,
                                                   m_settings.outputDescription);
  return index < 0 ? -1 : index + 1;
}

int GameModeSession::currentSoundIndex() const {
  if (m_settings.sinkName.isEmpty()) {
    return 0;
  }
  for (int index = 0; index < m_sinks.size(); ++index) {
    if (m_sinks.at(index).name == m_settings.sinkName) {
      return index + 1;
    }
  }
  return -1;
}

QString GameModeSession::displayLabel() const {
  const int index = currentDisplayIndex();
  if (index == 0) {
    return QStringLiteral("Current display");
  }
  if (index < 0) {
    return QStringLiteral("%1 · not connected")
        .arg(m_settings.outputDescription.isEmpty() ? m_settings.outputName
                                                    : m_settings.outputDescription);
  }
  const GameModeOutput& output = m_outputs.at(index - 1);
  return output.enabled ? outputLabel(output)
                        : QStringLiteral("%1 · off until Game Mode").arg(outputLabel(output));
}

QString GameModeSession::soundLabel() const {
  const int index = currentSoundIndex();
  if (index == 0) {
    return QStringLiteral("Current sound output");
  }
  if (index < 0) {
    return QStringLiteral("%1 · not available").arg(m_settings.sinkName);
  }
  const GameModeSink& sink = m_sinks.at(index - 1);
  return sink.description.isEmpty() ? sink.name : sink.description;
}

int GameModeSession::displayChoices() const {
  return m_displayManaged ? static_cast<int>(m_outputs.size()) + 1 : 1;
}

int GameModeSession::soundChoices() const {
  return m_soundManaged ? static_cast<int>(m_sinks.size()) + 1 : 1;
}

QVariantList GameModeSession::displayOptions() const {
  QVariantList options{QVariantMap{{"label", QStringLiteral("Current display")},
                                  {"available", true}}};
  for (const auto& output : m_outputs) {
    options.append(QVariantMap{{"label", output.enabled ? outputLabel(output)
        : QStringLiteral("%1 · off until Game Mode").arg(outputLabel(output))},
                               {"available", true}});
  }
  if (currentDisplayIndex() < 0) {
    options.append(QVariantMap{{"label", displayLabel()}, {"available", false}});
  }
  return options;
}

QVariantList GameModeSession::soundOptions() const {
  QVariantList options{QVariantMap{{"label", QStringLiteral("Keep current sound output")},
                                  {"available", true}}};
  for (const auto& sink : m_sinks) {
    options.append(QVariantMap{{"label", sink.description.isEmpty() ? sink.name : sink.description},
                               {"available", true}});
  }
  if (currentSoundIndex() < 0) {
    options.append(QVariantMap{{"label", soundLabel()}, {"available", false}});
  }
  return options;
}

int GameModeSession::displayIndex() const {
  const int index = currentDisplayIndex();
  return index < 0 ? static_cast<int>(m_outputs.size()) + 1 : index;
}

int GameModeSession::soundIndex() const {
  const int index = currentSoundIndex();
  return index < 0 ? static_cast<int>(m_sinks.size()) + 1 : index;
}

QString GameModeSession::sessionDisplayLabel() const {
  for (const auto& output : m_outputs) {
    if (output.name == m_output) {
      return outputLabel(output);
    }
  }
  return displayLabel();
}

QString GameModeSession::sessionSoundLabel() const {
  for (const auto& sink : m_sinks) {
    if (sink.name == m_defaultSink) {
      return sink.description.isEmpty() ? sink.name : sink.description;
    }
  }
  return m_defaultSink.isEmpty() ? soundLabel() : m_defaultSink;
}

void GameModeSession::setSilenceNotifications(bool value) {
  if (hasSession() || m_busy || m_settings.silenceNotifications == value) {
    return;
  }
  m_settings.silenceNotifications = value;
  persist();
  emit devicesChanged();
}

void GameModeSession::persist() {
  if (!saveSettings(m_settingsPath, m_settings)) {
    emit notice(QStringLiteral("Game Mode settings could not be saved."));
  }
}

void GameModeSession::setBusy(bool busy) {
  m_busy = busy;
  emit stateChanged();
}

void GameModeSession::setStatus(const QString& text) {
  m_statusText = text;
  emit stateChanged();
}

void GameModeSession::refresh() {
  if (m_refreshWatcher.isRunning() || m_changeWatcher.isRunning()) {
    m_refreshPending = true;
    return;
  }
  m_refreshPending = false;
  const bool recover = !m_recoveryChecked;
  m_recoveryChecked = true;
  m_refreshWatcher.setFuture(QtConcurrent::run([this, recover] {
    Devices devices;
    if (recover) {
      devices.recovered = m_controller.recover();
      devices.ranRecovery = true;
    }
    devices.displayManaged = m_compositor != nullptr && m_compositor->available();
    if (devices.displayManaged) {
      devices.outputs = m_compositor->outputs();
    }
    devices.soundManaged = m_audio != nullptr && m_audio->available();
    if (devices.soundManaged) {
      devices.sinks = m_audio->sinks();
      devices.defaultSink = m_audio->defaultSink();
    }
    devices.notificationsManaged = m_notifications != nullptr && m_notifications->available();
    return devices;
  }));
}

void GameModeSession::finishRefresh() {
  const Devices devices = m_refreshWatcher.result();
  m_displayManaged = devices.displayManaged;
  m_soundManaged = devices.soundManaged;
  m_notificationsManaged = devices.notificationsManaged;
  m_outputs = devices.outputs;
  m_sinks = devices.sinks;
  m_defaultSink = devices.defaultSink;
  emit devicesChanged();
  if (devices.ranRecovery &&
      (!devices.recovered.output.isEmpty() || !devices.recovered.notes.isEmpty())) {
    QStringList message{QStringLiteral("An interrupted Game Mode session was undone.")};
    message.append(devices.recovered.notes);
    emit notice(message.join(QLatin1Char(' ')));
  }
  if (m_changePending) {
    startChange();
  } else if (m_refreshPending) {
    refresh();
  }
}

void GameModeSession::cycleDisplay() {
  selectDisplay((currentDisplayIndex() + 1) % displayChoices());
}

void GameModeSession::selectDisplay(int index) {
  if (m_busy || hasSession() || index < 0 || index >= displayChoices()) {
    return;
  }
  if (index == 0) {
    m_settings.outputName.clear();
    m_settings.outputDescription.clear();
  } else {
    m_settings.outputName = m_outputs.at(index - 1).name;
    m_settings.outputDescription = m_outputs.at(index - 1).description;
  }
  persist();
  emit devicesChanged();
}

void GameModeSession::cycleSound() {
  selectSound((currentSoundIndex() + 1) % soundChoices());
}

void GameModeSession::selectSound(int index) {
  if (m_busy || hasSession() || index < 0 || index >= soundChoices()) {
    return;
  }
  m_settings.sinkName = index == 0 ? QString{} : m_sinks.at(index - 1).name;
  persist();
  emit devicesChanged();
}

void GameModeSession::setTemporaryWindow(bool temporary) {
  m_controller.setTemporaryWindow(temporary);
}

void GameModeSession::enter() {
  if (m_busy) {
    if (m_change == Change::RefreshParked) m_resumeAfterRefresh = true;
    return;
  }
  if (m_active) return;
  m_change = m_parked ? Change::Resume : Change::Enter;
  m_statusText =
      m_parked ? QStringLiteral("Returning to Game Mode") : QStringLiteral("Starting Game Mode");
  setBusy(true);
  if (!m_parked) emit entering();
  emit preparing(m_parked);
  startChange();
}

void GameModeSession::exit() {
  if (m_busy) {
    // Explicit End must survive entry, park and resume's asynchronous handoff.
    // Do not retry a failed Exit recursively; its recovery remains visible.
    if (m_change != Change::Exit) m_exitAfterChange = true;
    return;
  }
  if (!hasSession()) return;
  m_change = Change::Exit;
  m_statusText = QStringLiteral("Leaving Game Mode");
  setBusy(true);
  if (m_active) emit leaving(false);
  startChange();
}

void GameModeSession::park() {
  if (m_busy || !m_active) return;
  m_change = Change::Park;
  m_parkUiLeft = false;
  m_statusText = QStringLiteral("Returning to desktop");
  setBusy(true);
  emit parking();
  startChange();
}

void GameModeSession::refreshParked() {
  if (m_busy || !m_parked) return;
  m_change = Change::RefreshParked;
  m_busy = true;
  startChange();
}

void GameModeSession::startChange() {
  m_changePending = m_refreshWatcher.isRunning();
  if (m_changePending) return;
  const qint64 pid = QCoreApplication::applicationPid();
  const GameModeSettings settings = m_settings;
  const Change change = m_change;
  // Snapshot on the GUI thread; the worker must not read its mutable futures or watcher.
  QFuture<void> focusFuture = m_focusFuture;
  QFuture<int> workspaceFuture = m_workspaceWatcher.future();
  m_changeWatcher.setFuture(QtConcurrent::run([this, settings, pid, change, focusFuture,
                                              workspaceFuture]() mutable {
    // No late focus request may undo a return to the desktop.
    focusFuture.waitForFinished();
    workspaceFuture.waitForFinished();
    switch (change) {
    case Change::Enter: return m_controller.enter(settings, pid);
    case Change::Exit: return m_controller.exit(pid);
    case Change::Park: return m_controller.park(pid);
    case Change::Resume: return m_controller.resume(settings, pid);
    case Change::RefreshParked: return m_controller.refreshParked();
    }
    return GameModeController::Result{};
  }));
}

void GameModeSession::toggle() {
  if (m_active)
    park();
  else enter();
}

void GameModeSession::focusGame() {
  if (m_busy || !m_active || m_focusFuture.isRunning())
    return;
  const bool hasGame = !m_controller.state().games.isEmpty();
  const qint64 pid = QCoreApplication::applicationPid();
  m_focusFuture = QtConcurrent::run([this, pid, hasGame] {
    // Placement clears compositor fullscreen even if Qt still holds its old
    // fullscreen state. Reassert both sides after the retained UI has settled.
    GameModeWindow window;
    for (int waited = 0; m_compositor && waited < 1500; waited += 40) {
      window = m_compositor->windowForPid(pid);
      if (window.valid()) break;
      QThread::msleep(40);
    }
    if (!window.valid() || !m_compositor->setWindowMode(window.address, 2, 2)) {
      emit failed(QStringLiteral("Game Mode's fullscreen window could not be restored. Its session is still available."));
      return;
    }
    if (!hasGame) {
      m_compositor->focusWindow(window.address);
      emit gameFocused();
      return;
    }
    if (!m_controller.focusRetainedGame())
      emit failed(QStringLiteral("The retained game could not be focused. Its session is still available."));
    else emit gameFocused();
  });
}

void GameModeSession::focusWindow() {
  if (m_busy || m_compositor == nullptr || m_focusFuture.isRunning()) {
    return;
  }
  const qint64 pid = QCoreApplication::applicationPid();
  m_focusFuture = QtConcurrent::run([compositor = m_compositor, pid] {
    if (!compositor->available()) {
      return;
    }
    const GameModeWindow window = compositor->windowForPid(pid);
    if (window.valid()) {
      compositor->focusWindow(window.address);
    }
  });
}

void GameModeSession::checkWorkspace() {
  if (m_workspaceWatcher.isRunning()) {
    return;
  }
  const qint64 pid = QCoreApplication::applicationPid();
  m_workspaceWatcher.setFuture(QtConcurrent::run([compositor = m_compositor, pid] {
    if (compositor == nullptr || !compositor->available()) {
      return 0;
    }
    return compositor->otherWindowsOn(GameModeController::workspace(), pid);
  }));
}

void GameModeSession::finishChange() {
  const GameModeController::Result result = m_changeWatcher.result();
  const QString notes = result.notes.join(QLatin1Char(' '));
  const QString failure = notes.isEmpty() ? result.error : result.error + QLatin1Char(' ') + notes;
  const bool parkedRefresh = m_change == Change::RefreshParked;
  const bool wasParked = m_parked;
  const bool wasActive = m_active;
  const QString previousOutput = m_output;
  const QString previousStatus = m_statusText;
  m_active = m_controller.active();
  m_parked = m_controller.parked();
  m_busy = false;
  m_output = hasSession() ? result.output : QString{};
  const bool sessionChanged = wasActive != m_active || wasParked != m_parked ||
                              previousOutput != m_output;
  // Routine scans do not clear a useful status or rebuild the parked controls.
  if (!parkedRefresh || !result.ok || sessionChanged)
    m_statusText = result.ok ? QString{} : result.error;
  const bool notify = !parkedRefresh || sessionChanged || previousStatus != m_statusText ||
                      (!result.ok && failure != m_lastParkError);
  if (notify) emit stateChanged();
  if (m_parked) m_parkTimer.start();
  else m_parkTimer.stop();

  if (!m_active && (m_change == Change::Enter || m_change == Change::Resume))
    emit preparationCancelled();
  if (m_change == Change::Enter && m_active) emit entered();
  else if (m_change == Change::Park) {
    if (m_parked) emit parkedOnDesktop();
    else if (m_active && m_parkUiLeft) emit resumed(); // Only restore UI that actually left.
  } else if (m_change == Change::Exit && m_active && m_controller.state().windowPlaced) {
    // Audio cleanup can fail after the window has already returned to the desktop.
    // Keep End retryable without replaying Couch Mode over consumed window restoration.
    emit resumed();
  } else if (m_active && (m_change == Change::Resume || wasParked)) {
    emit resumed();
  }
  if (!hasSession() && (wasActive || wasParked)) emit exited();
  if (!result.ok && (m_change != Change::RefreshParked || failure != m_lastParkError)) {
    qWarning().noquote() << "Game Mode:" << failure;
    emit failed(failure);
  }
  if (m_change == Change::RefreshParked) m_lastParkError = result.ok ? QString{} : failure;
  else m_lastParkError.clear();
  if (result.ok && !notes.isEmpty()) emit notice(notes);
  if (notify) emit devicesChanged();

  const bool resume = m_resumeAfterRefresh;
  const bool end = m_exitAfterChange;
  m_resumeAfterRefresh = false;
  m_exitAfterChange = false;
  if (end && hasSession()) exit();
  else if (resume && m_parked) enter();
  else if (m_change != Change::RefreshParked) refresh();
}

void GameModeSession::screenRemoved(QScreen* screen) {
  if (!m_active || m_busy || screen == nullptr || m_output.isEmpty() ||
      screen->name() != m_output) {
    return;
  }
  emit notice(QStringLiteral("Game Mode ended because its display was disconnected."));
  exit();
}

void GameModeSession::shutdown() {
  m_parkTimer.stop();
  m_refreshWatcher.waitForFinished();
  m_changeWatcher.waitForFinished();
  m_focusFuture.waitForFinished();
  m_workspaceWatcher.waitForFinished();
  if (m_controller.active() || m_controller.parked()) {
    (void)m_controller.exit(QCoreApplication::applicationPid());
  }
  m_active = false;
  m_parked = false;
}

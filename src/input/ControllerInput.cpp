#include "input/ControllerInput.h"

#include <QDebug>
#include <Qt>
#include <QtConcurrent>

#include <SDL3/SDL.h>

namespace {
constexpr int kInitialRepeatDelayMs = 260;
constexpr int kRepeatIntervalMs = 80;
} // namespace

ControllerInput::ControllerInput(QObject* parent) : QObject(parent) {
  m_pollTimer.setInterval(8);
  connect(&m_pollTimer, &QTimer::timeout, this, &ControllerInput::pollEvents);
  m_repeatTimer.setTimerType(Qt::PreciseTimer);
  m_repeatTimer.setInterval(kInitialRepeatDelayMs);
  connect(&m_repeatTimer, &QTimer::timeout, this, [this] {
    if (m_repeatKey != 0) {
      emitDirection(m_repeatKey);
      m_repeatTimer.setInterval(kRepeatIntervalMs);
    }
  });
  connect(&m_initWatcher, &QFutureWatcher<InitResult>::finished, this, [this] {
    const InitResult result = m_initWatcher.result();
    m_sdlReady = result.ready;
    if (!m_sdlReady) {
      qWarning().noquote() << "Controller input unavailable:" << result.error;
      return;
    }
    openAvailableControllers();
    m_pollTimer.start();
  });
}

void ControllerInput::start() {
  if (m_sdlReady || m_initWatcher.isRunning()) {
    return;
  }
  m_initWatcher.setFuture(QtConcurrent::run([] {
    // SDL would otherwise catch SIGTERM and SIGINT and turn them into SDL quit events that
    // nothing here reads, so pkill, logout, and service stops could never close Omakade.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_GAMEPAD)) {
      return InitResult{.ready = true, .error = {}};
    }
    return InitResult{.ready = false, .error = QString::fromUtf8(SDL_GetError())};
  }));
}

ControllerInput::~ControllerInput() {
  if (m_initWatcher.isRunning()) {
    m_initWatcher.waitForFinished();
    m_sdlReady = m_initWatcher.result().ready;
  }
  for (SDL_Gamepad* controller : std::as_const(m_controllers)) {
    SDL_CloseGamepad(controller);
  }
  if (m_sdlReady) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }
}

bool ControllerInput::connected() const { return !m_controllers.isEmpty(); }

QString ControllerInput::name() const {
  if (m_controllers.isEmpty()) {
    return {};
  }
  const char* controllerName = SDL_GetGamepadName(m_controllers.cbegin().value());
  return controllerName == nullptr ? QStringLiteral("Game controller")
                                   : QString::fromUtf8(controllerName);
}

int ControllerInput::controllerCount() const { return static_cast<int>(m_controllers.size()); }

QString ControllerInput::primaryGlyph() const {
  return buttonLabel(SDL_GAMEPAD_BUTTON_SOUTH, QStringLiteral("SOUTH"));
}

QString ControllerInput::backGlyph() const {
  return buttonLabel(SDL_GAMEPAD_BUTTON_EAST, QStringLiteral("EAST"));
}

QString ControllerInput::favoriteGlyph() const {
  return buttonLabel(SDL_GAMEPAD_BUTTON_WEST, QStringLiteral("WEST"));
}

QString ControllerInput::toolbarGlyph() const {
  return buttonLabel(SDL_GAMEPAD_BUTTON_NORTH, QStringLiteral("NORTH"));
}

bool ControllerInput::focusNavigation() const { return m_focusNavigation; }

void ControllerInput::setFocusNavigation(bool enabled) {
  if (m_focusNavigation == enabled) {
    return;
  }
  m_focusNavigation = enabled;
  emit focusNavigationChanged();
}

void ControllerInput::pollEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_JOYSTICK_ADDED:
      openAvailableControllers();
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
      closeController(event.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      handleButtonPressed(event.gbutton.button);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      handleButtonReleased(event.gbutton.button);
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        m_axisX = event.gaxis.value;
        updateAxisKey();
      } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        m_axisY = event.gaxis.value;
        updateAxisKey();
      }
      break;
    default:
      break;
    }
  }
  const auto ids = m_controllers.keys();
  for (SDL_JoystickID id : ids) {
    if (!SDL_GamepadConnected(m_controllers.value(id))) {
      closeController(id);
    }
  }
  // With no pad connected only hot-plug events matter, so stop waking up 125 times a second.
  const int interval = m_controllers.isEmpty() ? 250 : 8;
  if (m_pollTimer.interval() != interval) {
    m_pollTimer.setInterval(interval);
  }
}

void ControllerInput::openAvailableControllers() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  bool changed = false;
  for (int index = 0; index < count; ++index) {
    if (!m_controllers.contains(ids[index])) {
      if (SDL_Gamepad* controller = SDL_OpenGamepad(ids[index])) {
        m_controllers.insert(ids[index], controller);
        changed = true;
      }
    }
  }
  SDL_free(ids);
  if (changed) {
    emit controllerChanged();
  }
}

void ControllerInput::closeController(SDL_JoystickID id) {
  if (SDL_Gamepad* controller = m_controllers.take(id)) {
    SDL_CloseGamepad(controller);
    m_axisX = 0;
    m_axisY = 0;
    m_axisKey = 0;
    m_dpadKeys.clear();
    m_repeatKey = 0;
    m_repeatTimer.stop();
    m_repeatTimer.setInterval(kInitialRepeatDelayMs);
    emit controllerChanged();
  }
}

void ControllerInput::handleButtonPressed(int button) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    emit keyRequested(Qt::Key_Return, Qt::NoModifier);
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    emit keyRequested(Qt::Key_Escape, Qt::NoModifier);
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    emit favoriteRequested();
    break;
  case SDL_GAMEPAD_BUTTON_NORTH:
    emit toolbarRequested();
    break;
  case SDL_GAMEPAD_BUTTON_START:
    emit keyRequested(Qt::Key_F11, Qt::NoModifier);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    setDpadPressed(Qt::Key_Up, true);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    setDpadPressed(Qt::Key_Down, true);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    setDpadPressed(Qt::Key_Left, true);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    setDpadPressed(Qt::Key_Right, true);
    break;
  default:
    break;
  }
}

void ControllerInput::handleButtonReleased(int button) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    setDpadPressed(Qt::Key_Up, false);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    setDpadPressed(Qt::Key_Down, false);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    setDpadPressed(Qt::Key_Left, false);
    break;
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    setDpadPressed(Qt::Key_Right, false);
    break;
  default:
    break;
  }
}

void ControllerInput::setDpadPressed(int key, bool pressed) {
  m_dpadKeys.removeAll(key);
  if (pressed) {
    // If two directions are held, the most recently pressed direction wins. Releasing it
    // resumes the direction that is still held instead of stopping navigation completely.
    m_dpadKeys.append(key);
  }
  updateRepeatKey();
}

void ControllerInput::emitDirection(int key) {
  if (m_focusNavigation) {
    emit focusDirectionRequested(key);
  } else {
    emit keyRequested(key, Qt::NoModifier);
  }
}

void ControllerInput::updateAxisKey() {
  constexpr int threshold = 18000;
  int key = 0;
  if (qAbs(m_axisX) > qAbs(m_axisY) && qAbs(m_axisX) > threshold) {
    key = m_axisX < 0 ? Qt::Key_Left : Qt::Key_Right;
  } else if (qAbs(m_axisY) > threshold) {
    key = m_axisY < 0 ? Qt::Key_Up : Qt::Key_Down;
  }
  if (key == m_axisKey) {
    return;
  }
  m_axisKey = key;
  updateRepeatKey();
}

void ControllerInput::updateRepeatKey() {
  const int key = m_dpadKeys.isEmpty() ? m_axisKey : m_dpadKeys.constLast();
  if (key == m_repeatKey) {
    return;
  }

  m_repeatKey = key;
  m_repeatTimer.stop();
  m_repeatTimer.setInterval(kInitialRepeatDelayMs);
  if (m_repeatKey != 0) {
    emitDirection(m_repeatKey);
    m_repeatTimer.start();
  }
}

QString ControllerInput::buttonLabel(SDL_GamepadButton button, const QString& fallback) const {
  if (m_controllers.isEmpty()) {
    return fallback;
  }
  switch (SDL_GetGamepadButtonLabel(m_controllers.cbegin().value(), button)) {
  case SDL_GAMEPAD_BUTTON_LABEL_A:
    return QStringLiteral("A");
  case SDL_GAMEPAD_BUTTON_LABEL_B:
    return QStringLiteral("B");
  case SDL_GAMEPAD_BUTTON_LABEL_X:
    return QStringLiteral("X");
  case SDL_GAMEPAD_BUTTON_LABEL_Y:
    return QStringLiteral("Y");
  case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
    return QStringLiteral("CROSS");
  case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
    return QStringLiteral("CIRCLE");
  case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
    return QStringLiteral("SQUARE");
  case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
    return QStringLiteral("TRIANGLE");
  case SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN:
  default:
    return fallback;
  }
}

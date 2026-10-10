#include "input/ControllerInput.h"

#include <QCoreApplication>
#include <QEvent>

#include <QDebug>
#include <Qt>
#include <QtConcurrent>

#include <SDL3/SDL.h>

namespace {
constexpr int kInitialRepeatDelayMs = 260;
constexpr int kRepeatIntervalMs = 80;
} // namespace

ControllerInput::ControllerInput(QObject* parent) : QObject(parent) {
  // Watch every event so the on screen keyboard can tell a controller from a mouse.
  if (auto* application = QCoreApplication::instance()) {
    application->installEventFilter(this);
  }
  // Follow the signals rather than the places that raise them. These are emitted from outside
  // this class as well, to stand in for the controller, and those count just the same.
  connect(this, &ControllerInput::acceptRequested, this, [this] { setDriving(true); });
  connect(this, &ControllerInput::backRequested, this, [this] { setDriving(true); });
  connect(this, &ControllerInput::focusDirectionRequested, this, [this] { setDriving(true); });
  connect(this, &ControllerInput::favoriteRequested, this, [this] { setDriving(true); });
  connect(this, &ControllerInput::toolbarRequested, this, [this] { setDriving(true); });
  connect(this, &ControllerInput::startRequested, this, [this] { setDriving(true); });
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
  return nintendoFaceButtons() ? QStringLiteral("A") : buttonLabel(SDL_GAMEPAD_BUTTON_SOUTH, QStringLiteral("A"));
}

QString ControllerInput::backGlyph() const {
  return nintendoFaceButtons() ? QStringLiteral("B") : buttonLabel(SDL_GAMEPAD_BUTTON_EAST, QStringLiteral("B"));
}

QString ControllerInput::favoriteGlyph() const {
  return nintendoFaceButtons() ? QStringLiteral("X") : buttonLabel(SDL_GAMEPAD_BUTTON_WEST, QStringLiteral("X"));
}

QString ControllerInput::toolbarGlyph() const {
  return nintendoFaceButtons() ? QStringLiteral("Y") : buttonLabel(SDL_GAMEPAD_BUTTON_NORTH, QStringLiteral("Y"));
}

bool ControllerInput::focusNavigation() const { return m_focusNavigation; }

void ControllerInput::setFocusNavigation(bool enabled) {
  if (m_focusNavigation == enabled) {
    return;
  }
  m_focusNavigation = enabled;
  emit focusNavigationChanged();
}

void ControllerInput::setInputEnabled(bool enabled) {
  if (m_inputEnabled == enabled) {
    return;
  }
  m_inputEnabled = enabled;
  emit inputEnabledChanged();
  m_repeatTimer.stop();
  m_repeatTimer.setInterval(kInitialRepeatDelayMs);
  m_axisX = 0;
  m_axisY = 0;
  m_axisKey = 0;
  m_repeatKey = 0;
  m_dpadKeys.clear();
  if (m_sdlReady) {
    // Do not replay input queued while another application owned focus.
    SDL_FlushEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    SDL_FlushEvent(SDL_EVENT_GAMEPAD_BUTTON_UP);
    SDL_FlushEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
  }
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
      if (m_inputEnabled && !mirrorsAnotherController(event.gbutton.which)) {
        if (m_activeController != event.gbutton.which) {
          m_activeController = event.gbutton.which;
          emit controllerChanged();
        }
        handleButtonPressed(event.gbutton.button);
      }
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      if (m_inputEnabled && !mirrorsAnotherController(event.gbutton.which)) {
        handleButtonReleased(event.gbutton.button);
      }
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (!m_inputEnabled || mirrorsAnotherController(event.gaxis.which)) {
        break;
      }
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

// Steam Input takes over a pad by adding a virtual one that repeats every press, so with
// both open each press would arrive twice. The virtual pad only counts while it is the only
// pad, as with a controller Steam reads directly.
bool ControllerInput::mirrorsAnotherController(SDL_JoystickID id) const {
  const auto steamMirror = [](SDL_Gamepad* pad) {
    return SDL_GetGamepadVendor(pad) == 0x28de && SDL_GetGamepadProduct(pad) == 0x11ff;
  };
  SDL_Gamepad* pad = m_controllers.value(id);
  if (pad == nullptr || !steamMirror(pad)) return false;
  for (SDL_Gamepad* other : m_controllers) {
    if (other != pad && !steamMirror(other)) return true;
  }
  return false;
}

void ControllerInput::handleButtonPressed(int button) {
  // SDL names these by position; Nintendo labels A/B/X/Y are east/south/north/west.
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    if (nintendoFaceButtons()) emit backRequested();
    else emit acceptRequested();
    break;
  case SDL_GAMEPAD_BUTTON_EAST:
    if (nintendoFaceButtons()) emit acceptRequested();
    else emit backRequested();
    break;
  case SDL_GAMEPAD_BUTTON_WEST:
    if (nintendoFaceButtons()) emit toolbarRequested();
    else emit favoriteRequested();
    break;
  case SDL_GAMEPAD_BUTTON_NORTH:
    if (nintendoFaceButtons()) emit favoriteRequested();
    else emit toolbarRequested();
    break;
  case SDL_GAMEPAD_BUTTON_START:
    emit startRequested();
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
  if (!m_inputEnabled) return;
  emit focusDirectionRequested(key);
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

bool ControllerInput::nintendoFaceButtons() const {
  if (m_controllers.isEmpty()) return false;
  auto* pad = m_controllers.value(m_activeController, m_controllers.cbegin().value());
  return SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_B
      && SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST) == SDL_GAMEPAD_BUTTON_LABEL_A;
}

QString ControllerInput::buttonLabel(SDL_GamepadButton button, const QString& fallback) const {
  if (m_controllers.isEmpty()) {
    return fallback;
  }
  switch (SDL_GetGamepadButtonLabel(m_controllers.value(m_activeController, m_controllers.cbegin().value()), button)) {
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

void ControllerInput::setWindowFocused(bool focused) {
  setInputEnabled(focused);
}

void ControllerInput::setDriving(bool driving) {
  if (m_driving == driving) {
    return;
  }
  m_driving = driving;
  emit drivingChanged();
}

bool ControllerInput::eventFilter(QObject* watched, QEvent* event) {
  // Commands never enter the window system, so spontaneous input is always genuine.
  if (event->spontaneous()) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
      setDriving(false);
      break;
    default:
      break;
    }
  }
  return QObject::eventFilter(watched, event);
}

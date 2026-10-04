#include "guide/GuideInput.h"
#include "guide/GuidePayload.h"
#include "guidebutton/GuideListener.h"

#include <QFile>
#include <QSocketNotifier>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
QString button(int code) {
  switch (code) {
  case BTN_SOUTH: return "a";
  case BTN_EAST: return "b";
  case BTN_WEST: return "x";
  case BTN_NORTH: return "y";
  case BTN_TL: return "lb";
  case BTN_TR: return "rb";
  case BTN_MODE: return "guide";
  case BTN_START: return "start";
  case BTN_DPAD_UP: return "up";
  case BTN_DPAD_DOWN: return "down";
  case BTN_DPAD_LEFT: return "left";
  case BTN_DPAD_RIGHT: return "right";
  default: return {};
  }
}
bool direction(const QString& action) {
  return action == "up" || action == "down" || action == "left" || action == "right";
}
} // namespace

void GuideInputMap::setAxis(int code, int minimum, int maximum, int flat) {
  m_axes.insert(code, {minimum, maximum, flat, 0});
}

QString GuideInputMap::event(int type, int code, int value) {
  if (type == EV_KEY) {
    if (value == 0) { m_keys.remove(code); return {}; }
    if (value != 1 || m_keys.contains(code)) return {};
    m_keys.insert(code);
    return button(code);
  }
  if (type != EV_ABS || (code != ABS_X && code != ABS_Y && code != ABS_HAT0X && code != ABS_HAT0Y))
    return {};
  auto& axis = m_axes[code];
  int next = 0;
  if (code == ABS_HAT0X || code == ABS_HAT0Y) {
    next = (value > 0) - (value < 0);
  } else {
    const double center = (double(axis.maximum) + axis.minimum) / 2;
    const double half = (double(axis.maximum) - axis.minimum) / 2;
    if (half <= 0) return {};
    const double normalized = (value - center) / half;
    const double threshold = std::max(axis.direction ? 0.30 : 0.55, double(axis.flat) / half);
    next = normalized > threshold ? 1 : normalized < -threshold ? -1 : 0;
  }
  if (next == axis.direction) return {};
  axis.direction = next;
  if (!next) return {};
  return (code == ABS_X || code == ABS_HAT0X) ? (next < 0 ? "left" : "right")
                                            : (next < 0 ? "up" : "down");
}

QStringList GuideInputMap::heldDirections() const {
  QStringList held;
  for (auto it = m_axes.cbegin(); it != m_axes.cend(); ++it) {
    if (!it->direction) continue;
    held.append((it.key() == ABS_X || it.key() == ABS_HAT0X)
                    ? (it->direction < 0 ? "left" : "right")
                    : (it->direction < 0 ? "up" : "down"));
  }
  for (const int key : m_keys) {
    const auto action = button(key);
    if (direction(action)) held.append(action);
  }
  held.removeDuplicates();
  return held;
}

void GuideInputMap::reset() {
  m_keys.clear();
  for (auto& axis : m_axes) axis.direction = 0;
}

struct GuideInput::Device {
  int fd = -1;
  QString family, node;
  bool grabbed = false;
  int effect = -1;
  QTimer effectTimer;
  std::function<void(int, int)> erase;
  std::function<void(int)> ungrab;
  GuideInputMap mapping;
  std::unique_ptr<QSocketNotifier> notifier;
  ~Device() {
    notifier.reset();
    if (fd >= 0) {
      if (effect >= 0) erase(fd, effect);
      if (grabbed) ungrab(fd);
      ::close(fd);
    }
  }
};

GuideInput::GuideInput(QObject* parent) : QObject(parent) {
  m_repeat.setInterval(80);
  connect(&m_repeat, &QTimer::timeout, this, [this] {
    if (++m_repeatTicks < 4) return;
    for (const auto& device : m_devices) {
      for (const auto& action : device->mapping.heldDirections()) emit this->action(action, device->family);
    }
    for (const auto& action : m_injected.heldDirections()) emit this->action(action, "xbox");
  });
}
GuideInput::~GuideInput() { release(); }

bool GuideInput::grab(const QString& preferredNode, QString* family, QString* error) {
  release();
  if (error) error->clear();
  const auto pads = m_access.scan ? m_access.scan() : GuideListener::scan("/dev/input", "/sys/class/input");
  bool preferredFound = preferredNode.isEmpty();
  QStringList warnings;
  for (const auto& pad : pads) {
    if (pad.node == preferredNode) {
      preferredFound = true;
      if (family) *family = GuidePayload::padFamily(pad.name);
    }
    auto device = std::make_unique<Device>();
    device->node = "/dev/input/" + pad.node;
    if (m_access.open) device->fd = m_access.open(pad.node);
    else {
      device->fd = ::open(QFile::encodeName(device->node).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (device->fd < 0) device->fd = ::open(QFile::encodeName(device->node).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (device->fd < 0) {
      warnings.append(pad.name + " could not be opened; may still reach the game");
      continue;
    }
    device->grabbed = m_access.grab ? m_access.grab(device->fd) : ::ioctl(device->fd, EVIOCGRAB, 1) == 0;
    if (!device->grabbed) warnings.append(pad.name + " may still reach the game");
    device->ungrab = m_access.ungrab ? m_access.ungrab : [](int fd) { ::ioctl(fd, EVIOCGRAB, 0); };
    device->erase = m_access.erase ? m_access.erase : [](int fd, int effect) { ::ioctl(fd, EVIOCRMFF, effect); };
    device->family = GuidePayload::padFamily(pad.name);
    if (pad.node == preferredNode || (preferredNode.isEmpty() && m_devices.empty())) {
      if (family) *family = device->family;
      preferredFound = true;
    }
    for (const int code : {ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y}) {
      input_absinfo info{};
      if (::ioctl(device->fd, EVIOCGABS(code), &info) == 0) {
        device->mapping.setAxis(code, info.minimum, info.maximum, info.flat);
        device->mapping.event(EV_ABS, code, info.value);
      }
    }
    std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
    if (::ioctl(device->fd, EVIOCGKEY(keys.size()), keys.data()) >= 0) {
      for (int code = 0; code <= KEY_MAX; ++code)
        if (keys[code / 8] & (1 << (code % 8))) device->mapping.event(EV_KEY, code, 1);
    }
    auto* reader = device.get();
    device->notifier = std::make_unique<QSocketNotifier>(device->fd, QSocketNotifier::Read);
    connect(device->notifier.get(), &QSocketNotifier::activated, this, [this, reader] { read(*reader); });
    device->effectTimer.setSingleShot(true);
    connect(&device->effectTimer, &QTimer::timeout, device->notifier.get(), [reader] {
      if (reader->effect >= 0) reader->erase(reader->fd, reader->effect);
      reader->effect = -1;
    });
    m_devices.push_back(std::move(device));
  }
  if (!preferredFound) warnings.append("The controller that opened the guide disconnected.");
  if (error) *error = warnings.join("; ");
  m_repeat.start();
  return true;
}

void GuideInput::release() {
  m_repeat.stop();
  m_devices.clear();
  m_injected.reset();
  m_repeatTicks = 0;
}

void GuideInput::read(Device& device) {
  input_event events[32];
  const auto size = ::read(device.fd, events, sizeof(events));
  if (size < 0 && (errno == EAGAIN || errno == EINTR)) return;
  if (size <= 0 || size % sizeof(input_event) != 0) { emit lost(); return; }
  // Dispatch after reading so closing in response cannot destroy the reader mid-loop.
  const auto family = device.family;
  QStringList actions;
  for (size_t i = 0; i < size / sizeof(input_event); ++i) {
    const auto& event = events[i];
    if (event.type == EV_SYN && event.code == SYN_DROPPED) { emit lost(); return; }
    const QString action = device.mapping.event(event.type, event.code, event.value);
    if (!action.isEmpty()) { actions.append(action); m_repeatTicks = 0; }
  }
  for (const auto& action : actions) emit this->action(action, family);
}

void GuideInput::inject(int type, int code, int value) {
  const QString action = m_injected.event(type, code, value);
  if (!action.isEmpty()) { m_repeatTicks = 0; emit this->action(action, "xbox"); }
}

size_t GuideInput::grabbedCount() const {
  return std::count_if(m_devices.begin(), m_devices.end(), [](const auto& device) { return device->grabbed; });
}

bool GuideInput::identify(const QString& node, QString* error) {
  if (error) error->clear();
  for (const auto& device : m_devices) {
    if (device->node != node) continue;
    const bool supported = m_access.supportsRumble ? m_access.supportsRumble(device->fd) : [&] {
      std::array<unsigned char, (FF_MAX + 8) / 8> effects{};
      return ::ioctl(device->fd, EVIOCGBIT(EV_FF, effects.size()), effects.data()) >= 0 &&
             (effects[FF_RUMBLE / 8] & (1 << (FF_RUMBLE % 8)));
    }();
    if (!supported) { if (error) *error = "This controller has no rumble support"; return false; }
    device->effectTimer.stop();
    if (device->effect >= 0) device->erase(device->fd, device->effect);
    device->effect = -1;
    ff_effect effect{};
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.replay.length = 500;
    effect.u.rumble.strong_magnitude = effect.u.rumble.weak_magnitude = 0x7000;
    const bool uploaded = m_access.upload ? m_access.upload(device->fd, &effect) : ::ioctl(device->fd, EVIOCSFF, &effect) == 0;
    if (!uploaded) { if (error) *error = "The controller could not upload a rumble effect"; return false; }
    device->effect = effect.id;
    input_event play{};
    play.type = EV_FF; play.code = effect.id; play.value = 1;
    const bool played = m_access.play ? m_access.play(device->fd, effect.id) : ::write(device->fd, &play, sizeof(play)) == sizeof(play);
    if (!played) {
      device->erase(device->fd, device->effect); device->effect = -1;
      if (error) *error = "The controller could not play the rumble effect";
      return false;
    }
    device->effectTimer.start(550);
    return true;
  }
  if (error) *error = "This controller is no longer connected to the guide";
  return false;
}

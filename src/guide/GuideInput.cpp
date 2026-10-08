#include "guide/GuideInput.h"
#include "guide/GuidePayload.h"
#include "guidebutton/GuideListener.h"

#include <QDir>
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
  m_rescan.setSingleShot(true);
  m_rescan.setInterval(120);
  connect(&m_rescan, &QTimer::timeout, this, &GuideInput::rescan);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_rescan, qOverload<>(&QTimer::start));
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

QList<GuideListener::Controller> GuideInput::scan() const {
  return m_access.scan ? m_access.scan() : GuideListener::scan("/dev/input", "/sys/class/input");
}

bool GuideInput::grab(const QString& preferredNode, QString* family, QString* error) {
  release();
  if (error) error->clear();
  bool preferredFound = preferredNode.isEmpty();
  QStringList warnings;
  for (const auto& pad : scan()) {
    if (pad.node == preferredNode) {
      preferredFound = true;
      if (family) *family = GuidePayload::padFamily(pad.name);
    }
    if (!attach(pad, &warnings)) continue;
    if (preferredNode.isEmpty() && m_devices.size() == 1 && family) *family = m_devices.front()->family;
  }
  if (!preferredFound) warnings.append("The controller that opened the guide disconnected.");
  if (error) *error = warnings.join("; ");
  if (!m_access.scan && QDir("/dev/input").exists()) m_watcher.addPath("/dev/input");
  m_repeat.start();
  return true;
}

bool GuideInput::attach(const GuideListener::Controller& pad, QStringList* warnings) {
  auto device = std::make_unique<Device>();
  device->node = "/dev/input/" + pad.node;
  if (m_access.open) device->fd = m_access.open(pad.node);
  else {
    device->fd = ::open(QFile::encodeName(device->node).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (device->fd < 0) device->fd = ::open(QFile::encodeName(device->node).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  }
  if (device->fd < 0) {
    qWarning("Guide: %s (%s) could not be opened; it may reach the game", qPrintable(pad.node), qPrintable(pad.name));
    if (warnings) warnings->append(pad.name + " could not be opened; may still reach the game");
    return false;
  }
  device->grabbed = m_access.grab ? m_access.grab(device->fd) : ::ioctl(device->fd, EVIOCGRAB, 1) == 0;
  if (device->grabbed) qInfo("Guide: holding %s (%s)", qPrintable(pad.node), qPrintable(pad.name));
  else {
    qWarning("Guide: %s (%s) is held by another program; it may reach the game", qPrintable(pad.node), qPrintable(pad.name));
    if (warnings) warnings->append(pad.name + " may still reach the game");
  }
  device->ungrab = m_access.ungrab ? m_access.ungrab : [](int fd) { ::ioctl(fd, EVIOCGRAB, 0); };
  device->erase = m_access.erase ? m_access.erase : [](int fd, int effect) { ::ioctl(fd, EVIOCRMFF, effect); };
  device->family = GuidePayload::padFamily(pad.name);
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
  return true;
}

void GuideInput::rescan() {
  if (!m_repeat.isActive()) return;
  const auto pads = scan();
  for (const auto& pad : pads) {
    const auto node = "/dev/input/" + pad.node;
    const bool held = std::any_of(m_devices.begin(), m_devices.end(), [&](const auto& device) { return device->node == node; });
    if (!held) attach(pad, nullptr);
  }
}

void GuideInput::drop(const QString& node) {
  const auto gone = std::remove_if(m_devices.begin(), m_devices.end(), [&](const auto& device) { return device->node == node; });
  if (gone != m_devices.end()) qInfo("Guide: %s went away", qPrintable(node));
  m_devices.erase(gone, m_devices.end());
}

void GuideInput::release() {
  m_rescan.stop();
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_repeat.stop();
  m_devices.clear();
  m_injected.reset();
  m_repeatTicks = 0;
}

void GuideInput::read(Device& device) {
  input_event events[32];
  const auto size = ::read(device.fd, events, sizeof(events));
  if (size < 0 && (errno == EAGAIN || errno == EINTR)) return;
  if (size <= 0 || size % sizeof(input_event) != 0) {
    // Unplugged, or Steam replaced its virtual pad: let that one go and keep the guide open.
    device.notifier->setEnabled(false);
    QTimer::singleShot(0, this, [this, node = device.node] { drop(node); });
    m_rescan.start();
    return;
  }
  // Dispatch after reading so closing in response cannot destroy the reader mid-loop.
  const auto family = device.family;
  QStringList actions;
  for (size_t i = 0; i < size / sizeof(input_event); ++i) {
    const auto& event = events[i];
    // The kernel queue overflowed: forget held buttons rather than act on half a report.
    if (event.type == EV_SYN && event.code == SYN_DROPPED) { device.mapping.reset(); return; }
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

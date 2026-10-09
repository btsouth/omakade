#include "guide/GuideInput.h"
#include "guide/GuidePayload.h"
#include "guidebutton/GuideListener.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <QRegularExpression>
#include <time.h>
#include <utility>
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

namespace {
qint64 clockMs(clockid_t clock = CLOCK_MONOTONIC) {
  timespec time{}; ::clock_gettime(clock, &time);
  return qint64(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}
QString controllerName(QString name) {
  name.remove(QRegularExpression(" [0-9]+$"));
  return name.trimmed().toLower();
}
}

void GuideInputMap::setAxis(int code, int minimum, int maximum, int flat) {
  m_axes.insert(code, {minimum, maximum, flat, 0});
}

QString GuideInputMap::event(int type, int code, int value) {
  if (type == EV_KEY) {
    if (value == 0) m_keys.remove(code);
    else if (value == 1 && !m_keys.contains(code)) {
      m_keys.insert(code);
      const auto action = button(code);
      if (!action.isEmpty() && !direction(action)) m_pending.append(action);
    }
  } else if (type == EV_ABS && (code == ABS_X || code == ABS_Y || code == ABS_HAT0X || code == ABS_HAT0Y)) {
    auto& axis = m_axes[code];
    const double half = (double(axis.maximum) - axis.minimum) / 2;
    axis.value = code == ABS_HAT0X || code == ABS_HAT0Y ? (value > 0) - (value < 0)
        : half > 0 ? (value - (double(axis.maximum) + axis.minimum) / 2) / half : 0;
  }
  return {}; // Only a complete SYN_REPORT may produce an intent.
}

QString GuideInputMap::cardinal(bool held) const {
  double x = int(m_keys.contains(BTN_DPAD_RIGHT)) - int(m_keys.contains(BTN_DPAD_LEFT));
  double y = int(m_keys.contains(BTN_DPAD_DOWN)) - int(m_keys.contains(BTN_DPAD_UP));
  if (x == 0 && y == 0) { x = m_axes.value(ABS_HAT0X).value; y = m_axes.value(ABS_HAT0Y).value; }
  if (x == 0 && y == 0) {
    x = m_axes.value(ABS_X).value; y = m_axes.value(ABS_Y).value;
    double deadzone = held ? 0.25 : 0.45;
    for (const int code : {ABS_X, ABS_Y}) {
      const auto axis = m_axes.value(code);
      const double half = (double(axis.maximum) - axis.minimum) / 2;
      if (half > 0) deadzone = std::max(deadzone, axis.flat / half);
    }
    if (std::hypot(x, y) < deadzone) return {};
  }
  if (x == 0 && y == 0) return {};
  // Stable tie break: a diagonal gesture is one cardinal intent, never two.
  return std::abs(x) > std::abs(y) ? (x < 0 ? "left" : "right") : (y < 0 ? "up" : "down");
}

QStringList GuideInputMap::report(qint64 now, bool stale) {
  auto actions = std::exchange(m_pending, {});
  const auto next = cardinal(!m_direction.isEmpty() || m_needsNeutral);
  if (next.isEmpty()) { m_direction.clear(); m_needsNeutral = false; }
  else if (stale) { m_direction.clear(); m_needsNeutral = true; }
  else if (!m_needsNeutral && m_direction.isEmpty()) {
    m_direction = next; m_started = now; m_nextRepeat = now + 350;
    actions.append(next);
  } else if (next != m_direction) { m_direction.clear(); m_needsNeutral = true; }
  return actions;
}
QStringList GuideInputMap::repeat(qint64 now) {
  if (m_direction.isEmpty() || now < m_nextRepeat) return {};
  // A delayed timer emits one move, never a catch-up burst.
  m_nextRepeat = now + (now - m_started >= 1000 ? 80 : 100);
  return {m_direction};
}
QStringList GuideInputMap::heldDirections() const { return m_direction.isEmpty() ? QStringList{} : QStringList{m_direction}; }
void GuideInputMap::suppressUntilNeutral() { m_pending.clear(); m_direction.clear(); m_needsNeutral = !cardinal(true).isEmpty(); }
void GuideInputMap::reset() {
  m_keys.clear(); m_pending.clear(); m_direction.clear(); m_needsNeutral = false;
  for (auto& axis : m_axes) axis.value = 0;
}

struct GuideInput::Device {
  int fd = -1;
  QString family, node, name, id, group;
  bool virtualDevice = false, dropping = false, monotonic = false;
  QStringList pending;
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
  m_dispatch.setSingleShot(true);
  connect(&m_dispatch, &QTimer::timeout, this, &GuideInput::dispatch);
  m_repeat.setInterval(10);
  m_repeat.setTimerType(Qt::PreciseTimer);
  connect(&m_repeat, &QTimer::timeout, this, [this] {
    const auto now = clockMs();
    for (const auto& device : m_devices)
      if (m_groups.value(device->group).primary == device->node)
        device->pending.append(device->mapping.repeat(now));
    for (const auto& action : m_injected.repeat(now)) emit this->action(action, "xbox");
    if (!m_dispatch.isActive()) m_dispatch.start(0);
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
  regroup();
  m_repeat.start();
  return true;
}

bool GuideInput::attach(const GuideListener::Controller& pad, QStringList* warnings) {
  auto device = std::make_unique<Device>();
  device->node = "/dev/input/" + pad.node;
  device->name = pad.name; device->id = pad.id; device->virtualDevice = pad.virtualDevice;
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
  const int clock = CLOCK_MONOTONIC;
  device->monotonic = ::ioctl(device->fd, EVIOCSCLOCKID, &clock) == 0;
  sample(*device);
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
  regroup();
}

void GuideInput::drop(const QString& node) {
  const auto gone = std::remove_if(m_devices.begin(), m_devices.end(), [&](const auto& device) { return device->node == node; });
  if (gone != m_devices.end()) qInfo("Guide: %s went away", qPrintable(node));
  m_devices.erase(gone, m_devices.end());
  regroup();
}

void GuideInput::release() {
  m_rescan.stop();
  if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
  m_repeat.stop();
  m_dispatch.stop();
  m_groups.clear();
  m_devices.clear();
  m_injected.reset();

}

void GuideInput::sample(Device& device) {
  device.mapping.reset();
  for (const int code : {ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y}) {
    input_absinfo info{};
    if (::ioctl(device.fd, EVIOCGABS(code), &info) == 0) {
      device.mapping.setAxis(code, info.minimum, info.maximum, info.flat);
      device.mapping.event(EV_ABS, code, info.value);
    }
  }
  std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
  if (::ioctl(device.fd, EVIOCGKEY(keys.size()), keys.data()) >= 0)
    for (int code = 0; code <= KEY_MAX; ++code)
      if (keys[code / 8] & (1 << (code % 8))) device.mapping.event(EV_KEY, code, 1);
  device.mapping.suppressUntilNeutral();
}

void GuideInput::regroup() {
  QHash<QString, QStringList> physical;
  for (const auto& device : m_devices)
    if (!device->virtualDevice) physical[controllerName(device->name)].append(device->id);
  for (auto& ids : physical) { ids.removeDuplicates(); ids.sort(); }
  QSet<QString> live;
  for (const auto& device : m_devices) {
    QString group = device->id;
    if (device->virtualDevice) {
      const auto ids = physical.value(controllerName(device->name));
      const auto match = QRegularExpression(" ([0-9]+)$").match(device->name);
      const int index = match.hasMatch() ? match.captured(1).toInt() : 0;
      if (index < ids.size()) group = ids[index];
    }
    device->group = group; live.insert(group);
  }
  for (auto it = m_groups.begin(); it != m_groups.end();)
    it = live.contains(it.key()) ? std::next(it) : m_groups.erase(it);
  for (const auto& group : live) {
    auto& state = m_groups[group];
    Device* primary = nullptr;
    for (const auto& device : m_devices) {
      if (device->group != group) continue;
      if (!primary || (primary->virtualDevice && !device->virtualDevice)) primary = device.get();
    }
    if (primary && state.primary != primary->node) {
      state.primary = primary->node;
      primary->mapping.suppressUntilNeutral();
    }
    for (const int key : {BTN_SOUTH, BTN_EAST, BTN_MODE}) {
      for (const auto& device : m_devices)
        if (device->group == group && device->mapping.held(key)) state.latched.insert(key);
    }
  }
}

void GuideInput::read(Device& device) {
  input_event events[64];
  for (;;) {
    const auto size = ::read(device.fd, events, sizeof(events));
    if (size < 0 && errno == EINTR) continue;
    if (size < 0 && errno == EAGAIN) break;
    if (size <= 0 || size % sizeof(input_event) != 0) {
      device.notifier->setEnabled(false);
      QTimer::singleShot(0, this, [this, node = device.node] { drop(node); });
      m_rescan.start(); break;
    }
    for (size_t i = 0; i < size / sizeof(input_event); ++i) {
      const auto& event = events[i];
      if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        device.dropping = true; device.pending.clear(); continue;
      }
      if (device.dropping) {
        if (event.type == EV_SYN && event.code == SYN_REPORT) {
          sample(device); device.dropping = false;
          // Never turn a recovery snapshot into a fresh button press.
          m_groups[device.group].latched = {BTN_SOUTH, BTN_EAST, BTN_MODE};
          m_groups[device.group].homeArmed = false;
        }
        continue;
      }
      device.mapping.event(event.type, event.code, event.value);
      if (event.type != EV_SYN || event.code != SYN_REPORT) continue;
      const auto at = qint64(event.input_event_sec) * 1000 + event.input_event_usec / 1000;
      const bool stale = at > 0 && clockMs(device.monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME) - at > 100;
      auto actions = device.mapping.report(clockMs(), stale);
      if (stale) {
        device.pending.clear(); // Discard older intents already read in this queue drain too.
        qInfo("Guide input: dropped stale report node=%s", qPrintable(device.node));
      }
      device.pending.append(actions);
    }
  }
  if (!m_dispatch.isActive()) m_dispatch.start(0);
}

void GuideInput::dispatch() {
  struct Intent { QString action, family; };
  QList<Intent> intents;
  for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
    auto& group = it.value();
    for (const auto& device : m_devices) {
      if (device->group != it.key()) continue;
      const auto pending = std::exchange(device->pending, {});
      for (const auto& action : pending) {
        if (direction(action)) {
          if (device->node == group.primary) intents.append({action, device->family});
          continue;
        }
        int key = action == "a" ? BTN_SOUTH : action == "b" ? BTN_EAST : action == "guide" ? BTN_MODE : 0;
        if (key && (group.latched.contains(key) || (key == BTN_MODE && !group.homeArmed))) continue;
        if (key) group.latched.insert(key);
        intents.append({action, device->family});
      }
    }
    for (const int key : {BTN_SOUTH, BTN_EAST, BTN_MODE}) {
      bool held = false;
      for (const auto& device : m_devices)
        if (device->group == it.key() && device->mapping.held(key)) held = true;
      if (!held) { group.latched.remove(key); if (key == BTN_MODE) group.homeArmed = true; }
    }
  }
  // Closing may release every device. Dispatch only after the device walk.
  for (const auto& intent : intents) emit action(intent.action, intent.family);
}

void GuideInput::inject(int type, int code, int value) {
  m_injected.event(type, code, value);
  if (type == EV_SYN && code == SYN_REPORT)
    for (const auto& action : m_injected.report(clockMs())) emit this->action(action, "xbox");
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

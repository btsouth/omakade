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

void GuideInputMap::setController(const QString& name, const QString& driver, quint16 vendor, quint16 product, bool compactHidButtons) {
  const auto family = GuidePayload::padFamily(name);
  const auto backend = driver.startsWith("hid-") ? driver.mid(4) : driver;
  const bool microsoftXbox = vendor == 0x045e && (family == "xbox" ||
      product == 0x02e0 || product == 0x02fd || product == 0x0b05 ||
      product == 0x0b13 || product == 0x0b20 || product == 0x0b22);
  // hid-microsoft leaves Xbox button usages to hid-input, as does hid-generic:
  // usage 4 (X) becomes BTN_GAMEPAD + 3 = BTN_X; usage 5 (Y) becomes BTN_Y.
  // xpad and xpadneo explicitly use the same legacy label codes.
  // This is device identity dependent; generic HID pads remain positional.
  // xpad, xpadneo and hid-steam retain BTN_X/BTN_Y's old label meanings.
  // hid-playstation and hid-nintendo use BTN_WEST/BTN_NORTH by position.
  // Steam's Xbox mirror has no hardware driver, so use its advertised family.
  // Older Xbox One S firmware uses compact usages 3/4 for X/Y. The
  // resulting BTN_C capability distinguishes it from modern usages 4/5.
  m_compactHidCodes = microsoftXbox && compactHidButtons &&
      (backend == "microsoft" || backend == "generic");
  m_labelCodes = !m_compactHidCodes && (backend == "xpad" || backend == "xpadneo" || backend == "steam" ||
      (microsoftXbox && (backend == "microsoft" || backend == "generic")) ||
      (driver.isEmpty() && (family == "xbox" || family == "deck")));
}

int GuideInputMap::buttonPosition(int code) const {
  if (m_compactHidCodes) {
    switch (code) {
    case BTN_C: return BTN_WEST;
    case BTN_WEST: return BTN_TL;
    case BTN_Z: return BTN_TR;
    case BTN_TL: return BTN_SELECT;
    case BTN_TR: return BTN_START;
    default: return code;
    }
  }
  return m_labelCodes && (code == BTN_NORTH || code == BTN_WEST)
      ? (code == BTN_NORTH ? BTN_WEST : BTN_NORTH) : code;
}

bool GuideInputMap::heldPosition(int code) const {
  if (m_compactHidCodes) {
    switch (code) {
    case BTN_WEST: return held(BTN_C);
    case BTN_TL: return held(BTN_WEST);
    case BTN_TR: return held(BTN_Z);
    case BTN_SELECT: return held(BTN_TL);
    case BTN_START: return held(BTN_TR);
    default: return held(code);
    }
  }
  if (m_labelCodes && (code == BTN_NORTH || code == BTN_WEST))
    code = code == BTN_NORTH ? BTN_WEST : BTN_NORTH;
  return held(code);
}

QString GuideInputMap::event(int type, int code, int value) {
  if (type == EV_KEY) {
    if (value == 0) m_keys.remove(code);
    else if (value == 1 && !m_keys.contains(code)) {
      m_keys.insert(code);
      const int position = buttonPosition(code);
      const auto action = button(position);
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
  if (stale) actions.clear();
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
  bool steamMirror = false, ignoreNavigation = false;
  qint64 attachedAt = 0;
  QStringList pending;
  bool grabbed = false;
  std::function<void(int)> ungrab;
  GuideInputMap mapping;
  std::unique_ptr<QSocketNotifier> notifier;
  ~Device() {
    notifier.reset();
    if (fd >= 0) {
      if (grabbed) ungrab(fd);
      ::close(fd);
    }
  }
};

GuideInput::GuideInput(QObject* parent) : QObject(parent) {
  m_injected.setController("Xbox test pad");
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
      if (!device->ignoreNavigation && m_groups.value(device->group).primary == device->node)
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
  device->steamMirror = pad.virtualDevice &&
      (pad.vendor == 0x28de || (pad.vendor == 0x045e && pad.product == 0x028e)) &&
      QRegularExpression("^Microsoft X-Box 360 pad(?: [0-9]+)?$",
                         QRegularExpression::CaseInsensitiveOption).match(pad.name).hasMatch();
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
  device->family = GuidePayload::padFamily(pad.name);
  device->mapping.setController(pad.name, pad.driver, pad.vendor, pad.product, pad.compactHidButtons);
  const int clock = CLOCK_MONOTONIC;
  device->monotonic = ::ioctl(device->fd, EVIOCSCLOCKID, &clock) == 0;
  device->attachedAt = clockMs(device->monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME);
  sample(*device);
  auto* reader = device.get();
  device->notifier = std::make_unique<QSocketNotifier>(device->fd, QSocketNotifier::Read);
  connect(device->notifier.get(), &QSocketNotifier::activated, this, [this, reader] { read(*reader); });
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
  // Steam presents PlayStation/Nintendo hardware as Xbox uinput devices, with
  // no stable per-physical pairing identifier. Once a non-Xbox physical pad is
  // grabbed, use the physical sources for navigation and only grab/drain Steam
  // mirrors. This also avoids Steam's Nintendo layout remapping the same press.
  // Virtual-only input remains usable when no such physical source is handled.
  const bool physicalNavigation = std::any_of(m_devices.begin(), m_devices.end(), [](const auto& device) {
    return !device->virtualDevice && device->grabbed && device->family != "xbox";
  });
  for (const auto& device : m_devices) {
    const bool ignored = physicalNavigation && device->steamMirror;
    if (ignored != device->ignoreNavigation) {
      device->pending.clear();
      device->mapping.suppressUntilNeutral();
    }
    device->ignoreNavigation = ignored;
  }
  QHash<QString, QStringList> physical;
  for (const auto& device : m_devices)
    if (!device->virtualDevice) physical[controllerName(device->name)].append(device->id);
  for (auto& ids : physical) { ids.removeDuplicates(); ids.sort(); }
  QSet<QString> live;
  for (const auto& device : m_devices) {
    if (device->ignoreNavigation) continue;
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
      if (device->ignoreNavigation || device->group != group) continue;
      if (!primary || (primary->virtualDevice && !device->virtualDevice)) primary = device.get();
    }
    if (primary && state.primary != primary->node) {
      state.primary = primary->node;
      primary->mapping.suppressUntilNeutral();
    }
    for (const int key : {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_MODE}) {
      for (const auto& device : m_devices)
        if (!device->ignoreNavigation && device->group == group && device->mapping.heldPosition(key)) state.latched.insert(key);
    }
  }
}

void GuideInput::read(Device& device) {
  input_event events[64];
  int staleReports = 0;
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
          m_groups[device.group].latched = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_MODE};
          m_groups[device.group].homeArmed = false;
        }
        continue;
      }
      device.mapping.event(event.type, event.code, event.value);
      if (event.type != EV_SYN || event.code != SYN_REPORT) continue;
      const auto at = qint64(event.input_event_sec) * 1000 + event.input_event_usec / 1000;
      const bool stale = at > 0 && clockMs(device.monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME) - at > 100;
      auto actions = device.mapping.report(clockMs(), stale);
      // The Home that opened the guide may still be queued on a mirror. Kernel time,
      // not callback order, prevents that pre-grab press from closing the new guide.
      if (at > 0 && at < device.attachedAt) actions.removeAll("guide");
      if (stale) {
        device.pending.clear(); // Discard older intents already read in this queue drain too.
        ++staleReports;
      }
      device.pending.append(actions);
    }
  }
  if (staleReports) qInfo("Guide input: dropped stale reports node=%s count=%d", qPrintable(device.node), staleReports);
  if (!m_dispatch.isActive()) m_dispatch.start(0);
}

void GuideInput::dispatch() {
  for (const auto& device : m_devices)
    if (device->ignoreNavigation) device->pending.clear();
  struct Intent { QString action, family; };
  QList<Intent> intents;
  for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
    auto& group = it.value();
    for (const auto& device : m_devices) {
      if (device->ignoreNavigation || device->group != it.key()) continue;
      const auto pending = std::exchange(device->pending, {});
      for (const auto& action : pending) {
        if (direction(action)) {
          if (device->node == group.primary) intents.append({action, device->family});
          continue;
        }
        int key = action == "a" ? BTN_SOUTH : action == "b" ? BTN_EAST : action == "x" ? BTN_WEST :
            action == "y" ? BTN_NORTH : action == "guide" ? BTN_MODE : 0;
        if (key && (group.latched.contains(key) || (key == BTN_MODE && !group.homeArmed))) continue;
        if (key) group.latched.insert(key);
        intents.append({action, device->family});
      }
    }
    for (const int key : {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_MODE}) {
      bool held = false;
      for (const auto& device : m_devices)
        if (!device->ignoreNavigation && device->group == it.key() && device->mapping.heldPosition(key)) held = true;
      if (!held) { group.latched.remove(key); if (key == BTN_MODE) group.homeArmed = true; }
      else { group.latched.insert(key); if (key == BTN_MODE) group.homeArmed = false; }
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

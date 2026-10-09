#include "guidebutton/GuideListener.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <ctime>
#include <utility>

namespace {
constexpr int kRetryIntervalMs = 250;
constexpr int kRetryAttempts = 20;
// Analog triggers, which hold no events on the reader except while Guide is down.
constexpr std::array<int, 4> kTriggerAxes{ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE};

// The clock the kernel stamps events with once a reader asks for CLOCK_MONOTONIC.
qint64 monotonicMs() {
  timespec now{};
  ::clock_gettime(CLOCK_MONOTONIC, &now);
  return qint64(now.tv_sec) * 1000 + now.tv_nsec / 1'000'000;
}

QString readLine(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readLine()).trimmed();
}

// Asks the kernel to deliver this reader key and d-pad hat events only, plus the triggers while
// Guide is held so a Guide and trigger chord can be seen. The mask is per open file, so other
// readers of the device are unaffected. Empty reports are not delivered either, which leaves
// stick and trigger motion during play without any wakeup here. Kernels before 4.4 refuse the
// request and deliver everything, which the reader handles the same way.
void restrictEvents(int fd, bool triggers) {
  constexpr size_t kLongBits = sizeof(unsigned long) * 8;
  std::array<unsigned long, (EV_CNT + kLongBits - 1) / kLongBits> types{};
  for (const int type : {EV_KEY, EV_ABS}) {
    types[type / kLongBits] |= 1UL << (type % kLongBits);
  }
  std::array<unsigned long, (ABS_CNT + kLongBits - 1) / kLongBits> axes{};
  for (int axis = ABS_HAT0X; axis <= ABS_HAT3Y; ++axis) {
    axes[axis / kLongBits] |= 1UL << (axis % kLongBits);
  }
  if (triggers) {
    for (const int axis : kTriggerAxes) {
      axes[axis / kLongBits] |= 1UL << (axis % kLongBits);
    }
  }
  input_mask typeMask{.type = 0,
                      .codes_size = sizeof(types),
                      .codes_ptr = reinterpret_cast<quintptr>(types.data())};
  input_mask axisMask{.type = EV_ABS,
                      .codes_size = sizeof(axes),
                      .codes_ptr = reinterpret_cast<quintptr>(axes.data())};
  ::ioctl(fd, EVIOCSMASK, &axisMask);
  ::ioctl(fd, EVIOCSMASK, &typeMask);
}
} // namespace

GuideListener::GuideListener(QString devDir, QString sysDir, QObject* parent)
    : QObject(parent), m_devDir(std::move(devDir)), m_sysDir(std::move(sysDir)) {
  m_retry.setInterval(kRetryIntervalMs);
  connect(&m_retry, &QTimer::timeout, this, &GuideListener::rescan);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &GuideListener::rescan);
}

GuideListener::~GuideListener() {
  const QStringList nodes = m_devices.keys();
  for (const QString& node : nodes) {
    close(node);
  }
}

void GuideListener::start() {
  m_watcher.addPath(m_devDir);
  rescan();
}

QStringList GuideListener::openNodes() const { return m_devices.keys(); }

QList<GuideListener::Controller> GuideListener::scan(const QString& devDir,
                                                     const QString& sysDir) {
  QList<Controller> controllers;
  const QStringList nodes = QDir(devDir).entryList({QStringLiteral("event*")}, QDir::System);
  for (const QString& node : nodes) {
    const QString device = sysDir + QLatin1Char('/') + node + QStringLiteral("/device");
    const auto keys = readLine(device + QStringLiteral("/capabilities/key"));
    if (!GuidePress::isController(keys,
                                  readLine(device + QStringLiteral("/capabilities/abs")))) {
      continue;
    }
    const QString id = QFileInfo(device).canonicalFilePath();
    QString driver;
    QDir parent(id);
    // The evdev input node's driver belongs to an ancestor USB/HID device.
    while (!parent.isRoot()) {
      const auto target = QFileInfo(parent.filePath("driver")).canonicalFilePath();
      if (!target.isEmpty()) { driver = QFileInfo(target).fileName(); break; }
      if (!parent.cdUp()) break;
    }
    controllers.append(Controller{
        .node = node,
        .id = id,
        .name = readLine(device + QStringLiteral("/name")),
        .virtualDevice = id.contains(QStringLiteral("/devices/virtual/input/")),
        .driver = driver,
        .vendor = readLine(device + QStringLiteral("/id/vendor")).toUShort(nullptr, 16),
        .product = readLine(device + QStringLiteral("/id/product")).toUShort(nullptr, 16),
        .compactHidButtons = GuidePress::hasBit(keys, BTN_C) && !GuidePress::hasBit(keys, BTN_SELECT),
    });
  }
  return controllers;
}

void GuideListener::rescan() {
  const QList<Controller> controllers = scan(m_devDir, m_sysDir);
  QSet<QString> present;
  QSet<QString> ids;
  for (const Controller& controller : controllers) {
    present.insert(controller.node);
    ids.insert(controller.id);
  }
  bool changed = false;
  const QStringList openNodes = m_devices.keys();
  for (const QString& node : openNodes) {
    // A node that is gone, or that now belongs to a different device, is closed here even when
    // its read side has not reported the removal yet.
    if (!present.contains(node) || !current(node, m_devices.value(node))) {
      close(node);
      changed = true;
    }
  }
  // Retry and warning state belongs to a device, not to its node name, which a new controller
  // can reuse.
  for (auto waiting = m_waiting.begin(); waiting != m_waiting.end();) {
    waiting = ids.contains(waiting.key()) ? std::next(waiting) : m_waiting.erase(waiting);
  }
  for (auto reported = m_reported.begin(); reported != m_reported.end();) {
    reported = ids.contains(*reported) ? std::next(reported) : m_reported.erase(reported);
  }
  const bool timedRetry = sender() == &m_retry;
  for (const Controller& controller : controllers) {
    if (m_devices.contains(controller.node) ||
        (timedRetry && !m_waiting.contains(controller.id))) {
      continue;
    }
    const int error = open(controller);
    if (error == 0) {
      m_waiting.remove(controller.id);
      m_reported.remove(controller.id);
      changed = true;
      continue;
    }
    // Any device change tries again; the timer only covers the moments after one appears.
    const int left = m_waiting.value(controller.id, m_reported.contains(controller.id)
                                                        ? 1
                                                        : kRetryAttempts) -
                     1;
    if (left > 0) {
      m_waiting.insert(controller.id, left);
      continue;
    }
    m_waiting.remove(controller.id);
    if (!m_reported.contains(controller.id)) {
      m_reported.insert(controller.id);
      qWarning().noquote() << QStringLiteral("Cannot read %1 (%2): %3")
                                  .arg(controller.node, controller.name,
                                       QString::fromLocal8Bit(strerror(error)));
    }
  }
  if (m_waiting.isEmpty()) {
    m_retry.stop();
  } else if (!m_retry.isActive()) {
    m_retry.start();
  }
  if (changed) {
    emit devicesChanged();
  }
}

bool GuideListener::current(const QString& node, const Device& device) const {
  struct stat path {};
  return ::stat(QFile::encodeName(m_devDir + QLatin1Char('/') + node).constData(), &path) == 0 &&
         path.st_rdev == device.rdev && path.st_ino == device.inode;
}

int GuideListener::open(const Controller& controller) {
  const QByteArray path = QFile::encodeName(m_devDir + QLatin1Char('/') + controller.node);
  const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return errno;
  }
  struct stat opened {};
  if (::fstat(fd, &opened) != 0) {
    const int error = errno;
    ::close(fd);
    return error;
  }
  restrictEvents(fd, false);
  // Event times on the monotonic clock measure holds as the kernel saw them, however late this
  // process gets to read them.
  const int clock = CLOCK_MONOTONIC;
  Device device;
  device.fd = fd;
  device.rdev = opened.st_rdev;
  device.inode = opened.st_ino;
  device.name = controller.name;
  device.eventTimes = ::ioctl(fd, EVIOCSCLOCKID, &clock) == 0;
  device.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
  connect(device.notifier, &QSocketNotifier::activated, this,
          [this, node = controller.node] { read(node); });
  // Buttons already held count toward a chord with a Guide press that follows.
  QList<int> keysDown;
  std::array<unsigned long, (KEY_CNT + sizeof(unsigned long) * 8 - 1) /
                                (sizeof(unsigned long) * 8)>
      keys{};
  if (::ioctl(fd, EVIOCGKEY(sizeof(keys)), keys.data()) >= 0) {
    constexpr size_t kLongBits = sizeof(unsigned long) * 8;
    for (int key = 0; key < KEY_CNT; ++key) {
      if ((keys[key / kLongBits] >> (key % kLongBits)) & 1UL) {
        keysDown.append(key);
      }
    }
  }
  m_press.opened(controller.node, monotonicMs(), keysDown);
  for (const int axis : kTriggerAxes) {
    input_absinfo info{};
    if (::ioctl(fd, EVIOCGABS(axis), &info) == 0 && info.maximum > info.minimum) {
      // A trigger rests at its minimum; three quarters of its travel is a deliberate pull,
      // and stays clear of a right stick that some controllers report on these axes.
      m_press.setTrigger(controller.node, axis,
                         info.minimum + (info.maximum - info.minimum) * 3 / 4);
      device.triggers.append(axis);
    }
  }
  m_devices.insert(controller.node, device);
  qInfo().noquote() << QStringLiteral("Watching %1 (%2%3)")
                           .arg(controller.node, controller.name,
                                controller.virtualDevice ? QStringLiteral(", virtual")
                                                         : QString{});
  return 0;
}

void GuideListener::close(const QString& node) {
  Device device = m_devices.take(node);
  if (device.fd < 0) {
    return;
  }
  device.notifier->setEnabled(false);
  device.notifier->deleteLater();
  ::close(device.fd);
  m_press.closed(node);
  qInfo().noquote() << QStringLiteral("Stopped watching %1 (%2)").arg(node, device.name);
}

void GuideListener::watchTriggers(const QString& node, Device& device, qint64 now) {
  const bool holding = m_press.holding(node);
  if (device.triggers.isEmpty() || holding == device.watchingTriggers) {
    return;
  }
  device.watchingTriggers = holding;
  restrictEvents(device.fd, holding);
  if (!holding) {
    return;
  }
  // A trigger pulled before the mask opened sent its events while they were filtered, so its
  // current position is read directly.
  for (const int axis : std::as_const(device.triggers)) {
    input_absinfo info{};
    if (::ioctl(device.fd, EVIOCGABS(axis), &info) == 0) {
      // Only a Guide release completes a press, so a trigger position never does.
      static_cast<void>(m_press.event(node, EV_ABS, axis, info.value, now));
    }
  }
}

void GuideListener::read(const QString& node) {
  auto device = m_devices.find(node);
  if (device == m_devices.end()) {
    return;
  }
  // Reported once the device's queue is drained, so nothing a receiver does can change the
  // device table while this loop walks it.
  bool fired = false;
  const QString name = device->name;
  std::array<input_event, 64> events{};
  for (;;) {
    const ssize_t bytes = ::read(device->fd, events.data(), sizeof(events));
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (bytes <= 0) {
      // ENODEV when the controller disconnects. A read of nothing is end of file, which a
      // device never reports, so it is treated the same way.
      close(node);
      emit devicesChanged();
      break;
    }
    const qint64 readAt = monotonicMs();
    const auto count = static_cast<size_t>(bytes) / sizeof(input_event);
    for (size_t index = 0; index < count; ++index) {
      const input_event& event = events[index];
      if (device->dropping) {
        device->dropping = !(event.type == EV_SYN && event.code == SYN_REPORT);
        continue;
      }
      if (event.type == EV_SYN && event.code == SYN_DROPPED) {
        device->dropping = true;
        m_press.dropped(node);
        continue;
      }
      const qint64 at = device->eventTimes ? qint64(event.input_event_sec) * 1000 +
                                                 qint64(event.input_event_usec) / 1000
                                           : readAt;
      const bool wasHolding = m_press.holding(node);
      fired = m_press.event(node, event.type, event.code, event.value, at) || fired;
      if (!wasHolding && m_press.holding(node)) emit preparing(node, name);
    }
    watchTriggers(node, *device, readAt);
  }
  if (fired) {
    emit pressed(node, name);
  }
}

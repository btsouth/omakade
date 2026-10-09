#pragma once

#include "guidebutton/GuidePress.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <sys/types.h>

class QSocketNotifier;

// Watches every controller's Guide or Home button through evdev and reports presses. It only
// reads: no device is grabbed, written to, remapped or created, so games, Steam Input and
// other readers see exactly what they did before. Only devices sysfs describes as controllers
// are opened, and each reader asks the kernel for button and d-pad events alone, so stick and
// trigger motion never wakes this process.
class GuideListener final : public QObject {
  Q_OBJECT

public:
  struct Controller {
    QString node;
    // The input device behind the node, which a later controller reusing the node name does
    // not share.
    QString id;
    QString name;
    // Made by software, such as Steam Input's pad, rather than a driver for hardware.
    bool virtualDevice = false;
    QString driver;
    quint16 vendor = 0;
    quint16 product = 0;
  };

  explicit GuideListener(QString devDir = QStringLiteral("/dev/input"),
                         QString sysDir = QStringLiteral("/sys/class/input"),
                         QObject* parent = nullptr);
  ~GuideListener() override;

  void start();
  // The controllers sysfs lists now, opened or not.
  [[nodiscard]] static QList<Controller> scan(const QString& devDir, const QString& sysDir);
  [[nodiscard]] QStringList openNodes() const;

signals:
  void preparing(const QString& node, const QString& name);
  void pressed(const QString& node, const QString& name);
  void devicesChanged();

private:
  struct Device {
    int fd = -1;
    dev_t rdev = 0;
    ino_t inode = 0;
    QString name;
    QSocketNotifier* notifier = nullptr;
    bool dropping = false;
    // The kernel stamps events on the monotonic clock; otherwise read time stands in.
    bool eventTimes = false;
    QList<int> triggers;
    bool watchingTriggers = false;
  };

  void rescan();
  // Zero once the controller is open, otherwise the errno that refused it.
  int open(const Controller& controller);
  void close(const QString& node);
  void read(const QString& node);
  void watchTriggers(const QString& node, Device& device, qint64 now);
  [[nodiscard]] bool current(const QString& node, const Device& device) const;

  QString m_devDir;
  QString m_sysDir;
  QHash<QString, Device> m_devices;
  // Controllers that could not be opened yet, by device, with the attempts left. udev grants
  // access a moment after a node appears, so a refusal right after a hotplug is retried briefly.
  QHash<QString, int> m_waiting;
  QSet<QString> m_reported;
  QFileSystemWatcher m_watcher;
  QTimer m_retry;
  GuidePress m_press;
};

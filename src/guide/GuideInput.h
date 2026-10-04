#pragma once

#include <QHash>
#include "guidebutton/GuideListener.h"
#include <functional>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <vector>

// The same translator is used by evdev and the isolated acceptance harness.
class GuideInputMap {
public:
  void setAxis(int code, int minimum, int maximum, int flat);
  QString event(int type, int code, int value);
  QStringList heldDirections() const;
  void reset();
private:
  struct Axis { int minimum = -32768, maximum = 32767, flat = 4096, direction = 0; };
  QHash<int, Axis> m_axes;
  QSet<int> m_keys;
};

struct ff_effect;

class GuideInput final : public QObject {
  Q_OBJECT
public:
  explicit GuideInput(QObject* parent = nullptr);
  ~GuideInput() override;
  bool grab(const QString& preferredNode, QString* family, QString* error);
  void release();
  void inject(int type, int code, int value);
  bool identify(const QString& node, QString* error);
  struct Access {
    std::function<QList<GuideListener::Controller>()> scan;
    std::function<int(const QString&)> open;
    std::function<bool(int)> grab;
    std::function<bool(int)> supportsRumble;
    std::function<bool(int, ff_effect*)> upload;
    std::function<bool(int, int)> play;
    std::function<void(int, int)> erase;
    std::function<void(int)> ungrab;
  };
  void setAccess(Access access) { m_access = std::move(access); }
  size_t deviceCount() const { return m_devices.size(); }
  size_t grabbedCount() const;
signals:
  void action(const QString& action, const QString& family);
  void lost();
private:
  struct Device;
  Access m_access;
  void read(Device& device);
  std::vector<std::unique_ptr<Device>> m_devices;
  GuideInputMap m_injected;
  QTimer m_repeat;
  int m_repeatTicks = 0;
};

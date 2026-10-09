#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

// Decides when a controller's Guide or Home button counts as a press that toggles Game Mode.
// It sees only the events a listener reads from evdev and never touches a device, so this is
// the whole policy and can be tested without hardware.
//
// A press counts when the button is released within kMaxHoldMs, no other button, d-pad
// direction or trigger on the same controller was held at any point while it was down, and the
// press began after the controller had been open for kArmDelayMs. One physical press can arrive
// from several devices at once, such as a controller and the virtual pad Steam Input makes from
// it, so a release within kSameReleaseMs of the previous one is taken as the same press and
// ignored. A fresh release from the same source does not wait for this mirror window.
class GuidePress {
public:
  static constexpr qint64 kMaxHoldMs = 1000;
  static constexpr qint64 kArmDelayMs = 1000;
  static constexpr qint64 kSameReleaseMs = 1000;

  // Linux input event types and codes, kept here so the policy has no kernel header dependency.
  static constexpr int kEvSyn = 0x00;
  static constexpr int kEvKey = 0x01;
  static constexpr int kEvAbs = 0x03;
  static constexpr int kSynDropped = 3;
  static constexpr int kBtnSouth = 0x130;
  static constexpr int kBtnMode = 0x13c;
  static constexpr int kAbsHat0X = 0x10;
  static constexpr int kAbsHat3Y = 0x17;

  // `keysDown` are the buttons already held when the device was opened.
  void opened(const QString& device, qint64 nowMs, const QList<int>& keysDown = {});
  void closed(const QString& device);
  // An analog trigger axis and the value past which it counts as pulled.
  void setTrigger(const QString& device, int code, int pulledAt);
  // The events after a dropped report are incomplete until the next one, so whatever the device
  // was doing is forgotten rather than guessed at.
  void dropped(const QString& device);
  // True when this event completes a press that should toggle Game Mode.
  [[nodiscard]] bool event(const QString& device, int type, int code, int value, qint64 nowMs);
  // The Guide button is down on this device and its press is still a candidate.
  [[nodiscard]] bool holding(const QString& device) const;

  // A controller as sysfs describes it: the Guide or Home button and the south face button, a
  // stick or d-pad axis, and no letter keys. Keyboards, including virtual ones that declare every
  // key code, mice and other input devices never match.
  [[nodiscard]] static bool isController(const QString& keyCapabilities,
                                         const QString& absCapabilities);
  // Reads one bit from a sysfs capability bitmap: hex words of the kernel's long, most
  // significant first.
  [[nodiscard]] static bool hasBit(const QString& bitmap, int bit,
                                   int wordBits = int(sizeof(long)) * 8);

private:
  struct State {
    qint64 armedAt = 0;
    qint64 heldSince = -1;
    bool chord = false;
    QSet<int> keysDown;
    QHash<int, int> triggers;
  };
  QHash<QString, State> m_devices;
  qint64 m_lastRelease = -1;
  QString m_lastReleaseDevice;
};

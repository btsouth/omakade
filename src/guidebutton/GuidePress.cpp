#include "guidebutton/GuidePress.h"

#include <QStringList>

void GuidePress::opened(const QString& device, qint64 nowMs, const QList<int>& keysDown) {
  State state;
  state.armedAt = nowMs + kArmDelayMs;
  for (const int key : keysDown) {
    if (key != kBtnMode) {
      state.keysDown.insert(key);
    }
  }
  m_devices.insert(device, state);
}

void GuidePress::closed(const QString& device) { m_devices.remove(device); }

void GuidePress::setTrigger(const QString& device, int code, int pulledAt) {
  auto state = m_devices.find(device);
  if (state != m_devices.end()) {
    state->triggers.insert(code, pulledAt);
  }
}

void GuidePress::dropped(const QString& device) {
  auto state = m_devices.find(device);
  if (state != m_devices.end()) {
    state->heldSince = -1;
    state->chord = false;
    state->keysDown.clear();
  }
}

bool GuidePress::holding(const QString& device) const {
  const auto state = m_devices.constFind(device);
  return state != m_devices.cend() && state->heldSince >= 0 && !state->chord;
}

bool GuidePress::event(const QString& device, int type, int code, int value, qint64 nowMs) {
  auto state = m_devices.find(device);
  if (state == m_devices.end()) {
    return false;
  }
  if (type == kEvKey && code == kBtnMode) {
    if (value == 1) {
      // A button already down when the controller connected, or the press that switched it on,
      // was not seen starting here and does not count. Another button already held makes this
      // the second half of a chord.
      state->heldSince = nowMs >= state->armedAt ? nowMs : -1;
      state->chord = !state->keysDown.isEmpty();
      return false;
    }
    if (value != 0 || state->heldSince < 0) {
      return false;
    }
    const bool shortPress = nowMs - state->heldSince <= kMaxHoldMs;
    const bool alone = !state->chord;
    state->heldSince = -1;
    state->chord = false;
    const bool samePress = m_lastRelease >= 0 && device != m_lastReleaseDevice && nowMs - m_lastRelease < kSameReleaseMs;
    if (!samePress) { m_lastRelease = nowMs; m_lastReleaseDevice = device; }
    return shortPress && alone && !samePress;
  }
  bool pressed = false;
  if (type == kEvKey) {
    if (value == 1) {
      state->keysDown.insert(code);
      pressed = true;
    } else if (value == 0) {
      state->keysDown.remove(code);
    }
  } else if (type == kEvAbs) {
    const auto trigger = state->triggers.constFind(code);
    pressed = (code >= kAbsHat0X && code <= kAbsHat3Y && value != 0) ||
              (trigger != state->triggers.cend() && value >= trigger.value());
  }
  // Holding Guide with another button, the d-pad or a trigger is a chord for Steam or an
  // emulator hotkey, not a press.
  if (pressed && state->heldSince >= 0) {
    state->chord = true;
  }
  return false;
}

bool GuidePress::hasBit(const QString& bitmap, int bit, int wordBits) {
  const QStringList words = bitmap.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const int index = words.size() - 1 - bit / wordBits;
  if (index < 0) {
    return false;
  }
  bool ok = false;
  const quint64 word = words.at(index).toULongLong(&ok, 16);
  return ok && ((word >> (bit % wordBits)) & 1U) != 0;
}

bool GuidePress::isController(const QString& keyCapabilities, const QString& absCapabilities) {
  constexpr int kKeyA = 30;
  constexpr int kAbsX = 0x00;
  constexpr int kAbsY = 0x01;
  constexpr int kAbsRx = 0x03;
  constexpr int kAbsRy = 0x04;
  bool axis = false;
  for (const int code : {kAbsX, kAbsY, kAbsRx, kAbsRy, kAbsHat0X}) {
    axis = axis || hasBit(absCapabilities, code);
  }
  return axis && hasBit(keyCapabilities, kBtnMode) && hasBit(keyCapabilities, kBtnSouth) &&
         !hasBit(keyCapabilities, kKeyA);
}

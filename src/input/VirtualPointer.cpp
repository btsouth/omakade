#include "input/VirtualPointer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
constexpr int kDeadZone = 7000;
// Pixels a second with the stick at its edge: across a 1080p screen in about a second.
constexpr double kTopSpeed = 1500.0;
} // namespace

VirtualPointer::~VirtualPointer() { close(); }

bool VirtualPointer::available() { return ::access("/dev/uinput", W_OK) == 0; }

double VirtualPointer::step(int axis, int milliseconds) {
  const int magnitude = std::abs(axis);
  if (magnitude <= kDeadZone || milliseconds <= 0) return 0.0;
  const double travel = std::min(1.0, (magnitude - kDeadZone) / double(32767 - kDeadZone));
  // Squared, so small moves stay fine enough to land on a button.
  const double distance = kTopSpeed * travel * travel * milliseconds / 1000.0;
  return axis < 0 ? -distance : distance;
}

bool VirtualPointer::open() {
  if (m_fd >= 0) return true;
  const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return false;
  bool ok = ::ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ::ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) == 0 &&
            ::ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT) == 0 && ::ioctl(fd, UI_SET_EVBIT, EV_REL) == 0 &&
            ::ioctl(fd, UI_SET_RELBIT, REL_X) == 0 && ::ioctl(fd, UI_SET_RELBIT, REL_Y) == 0 &&
            ::ioctl(fd, UI_SET_RELBIT, REL_WHEEL) == 0;
  if (ok) {
    uinput_setup setup;
    std::memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_VIRTUAL;
    std::strncpy(setup.name, "Omakade controller pointer", UINPUT_MAX_NAME_SIZE - 1);
    ok = ::ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ::ioctl(fd, UI_DEV_CREATE) == 0;
  }
  if (!ok) {
    ::close(fd);
    return false;
  }
  m_fd = fd;
  return true;
}

void VirtualPointer::close() {
  if (m_fd < 0) return;
  // A button still held would stay held in whatever window is under the pointer.
  setPressed(false);
  ::ioctl(m_fd, UI_DEV_DESTROY);
  ::close(m_fd);
  m_fd = -1;
}

void VirtualPointer::emitEvent(int type, int code, int value) {
  if (m_fd < 0) return;
  input_event event;
  std::memset(&event, 0, sizeof(event));
  event.type = static_cast<__u16>(type);
  event.code = static_cast<__u16>(code);
  event.value = value;
  // A full device queue drops the event; the next one carries on.
  [[maybe_unused]] const ssize_t written = ::write(m_fd, &event, sizeof(event));
}

void VirtualPointer::move(int dx, int dy) {
  if (dx == 0 && dy == 0) return;
  if (dx != 0) emitEvent(EV_REL, REL_X, dx);
  if (dy != 0) emitEvent(EV_REL, REL_Y, dy);
  emitEvent(EV_SYN, SYN_REPORT, 0);
}

void VirtualPointer::scroll(int notches) {
  if (notches == 0) return;
  emitEvent(EV_REL, REL_WHEEL, notches);
  emitEvent(EV_SYN, SYN_REPORT, 0);
}

void VirtualPointer::setPressed(bool pressed) {
  if (pressed == m_pressed || m_fd < 0) return;
  m_pressed = pressed;
  emitEvent(EV_KEY, BTN_LEFT, pressed ? 1 : 0);
  emitEvent(EV_SYN, SYN_REPORT, 0);
}

#pragma once

// A mouse made from the controller, through uinput, for answering a launcher's window
// that only takes a pointer, such as Steam's EULA. The compositor treats it like any
// other mouse.
class VirtualPointer final {
public:
  VirtualPointer() = default;
  ~VirtualPointer();
  VirtualPointer(const VirtualPointer&) = delete;
  VirtualPointer& operator=(const VirtualPointer&) = delete;

  // Whether this user may add input devices. Steam's own udev rules allow it for the
  // person at the seat.
  [[nodiscard]] static bool available();
  // How far the pointer goes in one step of `milliseconds` for a stick held at `axis`
  // (-32768 to 32767): nothing inside the dead zone, slow near it, quick at the edge.
  [[nodiscard]] static double step(int axis, int milliseconds);

  // False when the device cannot be made.
  bool open();
  void close();
  [[nodiscard]] bool isOpen() const { return m_fd >= 0; }
  void move(int dx, int dy);
  // Positive scrolls up, as a wheel turned away does.
  void scroll(int notches);
  void setPressed(bool pressed);

private:
  void emitEvent(int type, int code, int value);
  int m_fd = -1;
  bool m_pressed = false;
};

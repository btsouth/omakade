#pragma once

#include <Qt>

class QWindow;

// Delivers a key press and release through Qt's platform input path, the one a real keyboard
// uses. A key a window shortcut takes is consumed there; sent with QCoreApplication::sendEvent
// instead, it also reached the focused control, so one press could act twice. Every key Omakade
// sends on the controller's behalf, and every key a self-test presses, goes through here.
void deliverKey(QWindow* window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

#pragma once

#include <QString>
#include <Qt>

class QWindow;

// Real keyboard input for the app's keyboard self-tests. Controller commands never use this.
void deliverKeyboardTestKey(QWindow* window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
// Types `text` one character at a time, as a physical keyboard would.
void deliverKeyboardTestText(QWindow* window, const QString& text);

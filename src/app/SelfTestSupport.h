#pragma once

#include <QString>

class ControllerInput;
class QQuickItem;
class QQuickWindow;

// Helpers shared by the self-tests still in OmakadeApp.cpp and the ones in src/selftest. They
// move to src/selftest with the last of the in-app self-tests.

// Adapter for table-driven pad self-tests. Keyboard checks use KeyboardTestInput directly.
void requestControllerCommand(ControllerInput& controller, int key);
// Checks every text field in `container`: its Clear button, and the controller keyboard's keys,
// shortcuts, Cancel and Start. Returns what failed, or empty.
QString verifyEditorTextFields(QQuickWindow* window, QQuickItem* container,
                               ControllerInput& controller);
// The first item named `name` in `item`'s visual tree, including `item` itself.
QQuickItem* findVisualItem(QQuickItem* item, const QString& name);
// The Details action grid's columns: Stop Game holds every cell to its scaled minimum.
int expectedDetailActionColumns(qreal width, qreal scale, bool running);
// Names the first visible action that reaches past the details column, or returns empty.
QString clippedDetailAction(QQuickItem* content, QQuickItem* actions);

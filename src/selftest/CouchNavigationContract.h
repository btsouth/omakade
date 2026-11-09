#pragma once

class QQuickWindow;
class ControllerInput;

// Runs against the live QML scene and the production SDL input path, with mock games.
bool runCouchNavigationContract(QQuickWindow* window, ControllerInput& controller);
bool runStartupNavigationContract(QQuickWindow* window, ControllerInput& controller);
// Presses every direction from every control on each couch screen and checks where focus
// lands, that every control can be reached, and that Back returns one screen.
bool runCouchNavigationSweep(QQuickWindow* window, ControllerInput& controller);

import QtQuick
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland

// A full-screen overlay layer for shells that predate Omarchy's OverlayWindow,
// such as the 4.0 stable releases. It offers the same shown, targetScreen and
// focusedScreen() the guide drives, maps a fresh surface on each open and has
// no contentReady, so the guide waits for a real size before presenting.
PanelWindow {
  id: window

  property bool shown: false
  property var targetScreen: null

  function focusedScreen() {
    var monitor = Hyprland.focusedMonitor
    var name = monitor ? String(monitor.name || "") : ""
    for (var i = 0; i < Quickshell.screens.length; i++) {
      if (Quickshell.screens[i].name === name) return Quickshell.screens[i]
    }
    return Quickshell.screens.length > 0 ? Quickshell.screens[0] : null
  }

  onShownChanged: targetScreen = shown ? focusedScreen() : null

  visible: shown && !!targetScreen
  screen: targetScreen
  anchors { top: true; left: true; bottom: true; right: true }
  color: "transparent"
  exclusionMode: ExclusionMode.Ignore
  WlrLayershell.layer: WlrLayer.Overlay
  WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
  WlrLayershell.namespace: "omakade-guide"
}

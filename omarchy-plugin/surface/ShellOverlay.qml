import Quickshell.Wayland
import qs.Ui

// Omarchy's own overlay surface. Shells without OverlayWindow fail to load
// this file, and Guide.qml falls back to LayerOverlay.qml.
OverlayWindow {
  WlrLayershell.namespace: "omakade-guide"
}

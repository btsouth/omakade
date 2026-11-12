import QtQuick
import QtQuick.Layouts

// Vertical menu actions share a full-width hit target, with the label centered like
// other buttons. Source/filter chips inside a Flow keep their content-sized GlassButton style.
GlassButton {
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    compact: true
    implicitWidth: 120 * displayScale
    maximumLabelWidth: Math.max(0, width - leftPadding - rightPadding)
}

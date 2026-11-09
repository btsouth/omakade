import QtQuick

// The keyboard and controller focus ring shared by buttons, fields and cards. It is thick, drawn
// in Theme.focusRing (3:1 against every surface), and only shown for real focus, so it never
// reads as hover, a selected tab or the retained current card. By default it lies along the
// inside of the control's edge, where a clipping list or scroll view cannot cut it off; set
// offset to push it outside, as library cards do.
Rectangle {
    id: ring

    property Item target: parent
    property bool shown: false
    property real thickness: 3
    property real offset: 0
    property real targetRadius: target && target.radius !== undefined ? target.radius : 0

    objectName: "focusRing"
    anchors.fill: target
    anchors.margins: -offset
    radius: targetRadius > 0 ? targetRadius + offset : 0
    color: "transparent"
    border.width: thickness
    border.color: Theme.focusRing
    visible: shown
}

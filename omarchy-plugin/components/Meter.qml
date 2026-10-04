import QtQuick

// A label, a value and a thin bar, like the shell clock's year and life bars.
Item {
  id: root
  required property var g
  property string label: ""
  property string value: ""
  property real progress: 0
  property color fill: g.accent

  implicitHeight: labels.height + g.s(6) + bar.height

  Label {
    id: labels
    g: root.g
    role: "caps"
    text: root.label
    anchors.left: parent.left
  }
  Label {
    g: root.g
    role: "small"
    text: root.value
    color: root.g.foreground
    anchors.right: parent.right
    anchors.verticalCenter: labels.verticalCenter
  }
  Rectangle {
    id: bar
    anchors.top: labels.bottom
    anchors.topMargin: root.g.s(6)
    width: parent.width
    height: Math.max(3, root.g.s(4))
    radius: height / 2
    color: root.g.track
    Rectangle {
      width: Math.max(height, parent.width * Math.max(0, Math.min(1, root.progress)))
      height: parent.height
      radius: parent.radius
      color: root.fill
      Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }
  }
}

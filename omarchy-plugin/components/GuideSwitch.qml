import QtQuick
import qs.Commons

Rectangle {
  id: root
  required property var g
  property bool checked: false
  width: Math.max(g.s(54), height * 2.2); height: g.s(24)
  radius: g.innerRadius
  color: checked ? g.accent : g.well
  border.width: checked ? 0 : 1
  border.color: g.line

  Rectangle {
    width: Math.floor(root.height * 0.62); height: width
    readonly property real inset: (root.height - height) / 2
    x: root.checked ? root.width - width - inset : inset
    anchors.verticalCenter: parent.verticalCenter
    radius: root.g.innerRadius
    color: root.checked ? root.g.accentInk : root.g.dim
    Behavior on x { NumberAnimation { duration: Style.reduceMotion ? 0 : Style.duration(120); easing.type: Easing.OutCubic } }
  }
}

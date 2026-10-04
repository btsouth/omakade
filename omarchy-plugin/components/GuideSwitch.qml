import QtQuick
import qs.Commons

Rectangle {
  id: root
  required property var g
  property bool checked: false
  width: g.s(44); height: g.s(24)
  radius: g.innerRadius
  color: checked ? g.accent : g.well
  border.width: checked ? 0 : 1
  border.color: g.line

  Rectangle {
    width: root.height - root.g.s(6); height: width
    x: root.checked ? root.width - width - root.g.s(3) : root.g.s(3)
    anchors.verticalCenter: parent.verticalCenter
    radius: root.g.innerRadius
    color: root.checked ? root.g.accentInk : root.g.dim
    Behavior on x { NumberAnimation { duration: Style.reduceMotion ? 0 : Style.duration(120); easing.type: Easing.OutCubic } }
  }
}

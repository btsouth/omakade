import QtQuick
import qs.Commons

// Equal choices on one surface. Left and right change the value directly.
Item {
  id: root
  required property var g
  property string label: ""
  property string detail: ""
  property var options: []
  property string value: ""
  property bool cursor: false
  signal chosen(string value)

  function indexOf(v) {
    for (var i = 0; i < options.length; i++) if (String(options[i].value) === v) return i
    return -1
  }
  function step(d) {
    var i = Math.max(0, Math.min(options.length - 1, indexOf(value) + d))
    if (String(options[i].value) !== value) chosen(String(options[i].value))
    return true
  }
  function activate() { chosen(String(options[(indexOf(value) + 1) % options.length].value)) }

  implicitHeight: head.height + g.s(56)

  Label { id: head; g: root.g; role: "caps"; text: root.label; y: root.g.s(4) }
  Label { g: root.g; role: "small"; text: root.detail; anchors.right: parent.right; anchors.verticalCenter: head.verticalCenter }
  Rectangle {
    id: group
    width: parent.width; height: root.g.s(40)
    anchors.top: head.bottom; anchors.topMargin: root.g.s(8)
    radius: root.g.innerRadius
    color: root.g.well
    border.width: 1; border.color: root.g.line

    Rectangle {
      x: root.g.s(3) + Math.max(0, root.indexOf(root.value)) * (group.width - root.g.s(6)) / Math.max(1, root.options.length)
      y: root.g.s(3)
      width: (group.width - root.g.s(6)) / Math.max(1, root.options.length)
      height: group.height - root.g.s(6)
      radius: root.g.innerRadius
      color: root.g.accent
      Behavior on x { NumberAnimation { duration: Style.reduceMotion ? 0 : Style.duration(120); easing.type: Easing.OutCubic } }
    }
    Row {
      anchors.fill: parent; anchors.margins: root.g.s(3)
      Repeater {
        model: root.options
        delegate: Item {
          required property var modelData
          width: (group.width - root.g.s(6)) / Math.max(1, root.options.length)
          height: group.height - root.g.s(6)
          Label {
            g: root.g; role: "small"; anchors.fill: parent
            horizontalAlignment: Text.AlignHCenter
            text: modelData.label
            color: String(modelData.value) === root.value ? root.g.accentInk : root.g.dim
          }
          MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.chosen(String(modelData.value)) }
        }
      }
    }
  }
}

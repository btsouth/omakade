import QtQuick
import qs.Commons

// The six pages as icons down the panel's edge, with the bumpers that move
// between them at either end.
Item {
  id: root
  required property var g
  property var tabs: []
  property int current: 0
  property string family: "xbox"
  signal picked(int index)

  implicitWidth: g.s(64)

  PadGlyph {
    id: lb
    g: root.g; family: root.family; button: "lb"; size: root.g.f(18)
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top; anchors.topMargin: root.g.s(18)
  }

  Column {
    id: column
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: lb.bottom; anchors.topMargin: root.g.s(18)
    spacing: root.g.s(6)

    Repeater {
      model: root.tabs
      delegate: Item {
        required property var modelData
        required property int index
        readonly property bool active: index === root.current
        width: root.g.s(46); height: width

        Rectangle {
          anchors.fill: parent
          radius: root.g.radius
          color: parent.active ? Style.selectedFillFor(root.g.foreground, root.g.accent) : "transparent"
          Behavior on color { ColorAnimation { duration: Style.duration(140) } }
        }
        Rectangle {
          width: Math.max(2, root.g.s(3)); height: parent.active ? parent.height * 0.5 : 0
          radius: width / 2
          anchors.verticalCenter: parent.verticalCenter
          x: -root.g.s(9)
          color: root.g.accent
          Behavior on height { NumberAnimation { duration: Style.duration(160); easing.type: Easing.OutCubic } }
        }
        Glyph {
          g: root.g; anchors.centerIn: parent; name: modelData.icon; size: root.g.f(20)
          color: parent.active ? root.g.foreground : root.g.dim
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.picked(index) }
      }
    }
  }

  PadGlyph {
    g: root.g; family: root.family; button: "rb"; size: root.g.f(18)
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: column.bottom; anchors.topMargin: root.g.s(18)
  }

  Rectangle {
    anchors.right: parent.right
    anchors.top: parent.top; anchors.bottom: parent.bottom
    width: Math.max(1, root.g.s(1))
    color: root.g.line
  }
}

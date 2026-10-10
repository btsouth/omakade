import QtQuick
import qs.Commons
import qs.Commons as Commons

// A section's name in quiet capitals on the left, its figure on the right.
Item {
  id: head
  property var g
  property string text: ""
  property string value: ""
  width: parent ? parent.width : 0
  implicitHeight: Math.max(name.implicitHeight, figure.implicitHeight)

  Caps {
    id: name
    g: head.g
    text: head.text
    anchors.verticalCenter: parent.verticalCenter
  }

  Text {
    id: figure
    textFormat: Text.PlainText
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    text: head.value
    color: head.g.dim
    font.family: head.g.fontFamily
    font.pixelSize: head.g.sized(Style.font.body)
    font.weight: Font.Medium
  }
}

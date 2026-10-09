import QtQuick
import qs.Commons
import qs.Commons as Commons

// A section's name on the left and its figure on the right, as the audio
// panel heads its output slider. Both sit on the edges of the row below:
// the name over its icon, the figure over the end of its control.
Item {
  id: head
  property var g
  property string text: ""
  property string value: ""
  readonly property int inset: g.sized(Style.space(8))
  width: parent ? parent.width : 0
  implicitHeight: Math.max(name.implicitHeight, figure.implicitHeight)

  Caps {
    id: name
    g: head.g
    x: head.inset
    text: head.text
    anchors.verticalCenter: parent.verticalCenter
  }

  Text {
    id: figure
    textFormat: Text.PlainText
    anchors.right: parent.right
    anchors.rightMargin: head.inset
    anchors.verticalCenter: parent.verticalCenter
    text: head.value
    color: head.g.text
    font.family: head.g.fontFamily
    font.pixelSize: head.g.sized(Style.font.body)
  }
}

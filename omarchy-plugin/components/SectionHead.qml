import QtQuick
import qs.Commons
import qs.Commons as Commons

// A section's name on the left and its figure on the right, as the audio
// panel heads its output and input sliders.
Item {
  id: head
  property var g
  property string text: ""
  property string value: ""
  property int valueInset: 0
  width: parent ? parent.width : 0
  implicitHeight: Math.max(name.implicitHeight, figure.implicitHeight)

  Caps {
    id: name
    g: head.g
    text: head.text
    anchors.left: parent.left
    anchors.verticalCenter: parent.verticalCenter
  }

  Text {
    id: figure
    textFormat: Text.PlainText
    anchors.right: parent.right
    anchors.rightMargin: head.valueInset
    anchors.verticalCenter: parent.verticalCenter
    text: head.value
    color: head.g.text
    font.family: head.g.fontFamily
    font.pixelSize: head.g.sized(Style.font.body)
  }
}

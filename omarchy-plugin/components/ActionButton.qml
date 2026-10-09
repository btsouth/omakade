import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A button with an icon and its name side by side, centred, in a bordered
// chip. Quit takes the urgent colour when the cursor is on it.
Focusable {
  id: button

  property string icon: ""
  property real iconScale: 1
  property color iconColor: button.ink
  property string label: ""
  property string value: ""
  property bool leftAlign: false

  bordered: true

  Row {
    id: content
    anchors.verticalCenter: parent.verticalCenter
    x: button.leftAlign ? button.g.sized(Style.space(8)) : Math.round((button.width - width) / 2)
    spacing: button.g.sized(Style.space(button.leftAlign ? 10 : 6))

    InkGlyph {
      width: button.g.sized(Style.space(button.leftAlign ? 28 : 26))
      height: button.height
      text: button.icon
      color: button.iconColor
      size: Math.round(button.g.sized(Style.font.iconLarge) * button.iconScale)
      fontFamily: button.g.fontFamily
    }

    Text {
      textFormat: Text.PlainText
      anchors.verticalCenter: parent.verticalCenter
      text: button.label
      color: button.ink
      font.family: button.g.fontFamily
      font.pixelSize: button.g.sized(Style.font.heading)
      font.weight: Font.Medium
    }
  }

  Text {
    textFormat: Text.PlainText
    visible: button.value !== ""
    anchors.right: parent.right
    anchors.rightMargin: button.g.sized(Style.space(14))
    anchors.verticalCenter: parent.verticalCenter
    text: button.value
    color: button.ink
    font.family: button.g.fontFamily
    font.pixelSize: button.g.sized(Style.font.body)
  }
}

import QtQuick
import qs.Commons
import qs.Commons as Commons

// What the buttons do: the button's name in the text colour, the action quiet.
Row {
  id: line
  property var g
  spacing: g.sized(Style.space(16))

  Repeater {
    model: line.g.hint
    Row {
      required property var modelData
      Text {
        textFormat: Text.PlainText
        text: modelData[0] + " "
        color: line.g.text
        font.family: line.g.fontFamily
        font.pixelSize: line.g.sized(Style.font.body)
      }
      Text {
        textFormat: Text.PlainText
        text: modelData[1]
        color: line.g.quiet
        font.family: line.g.fontFamily
        font.pixelSize: line.g.sized(Style.font.body)
      }
    }
  }
}

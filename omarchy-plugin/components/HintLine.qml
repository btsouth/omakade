import QtQuick
import qs.Commons
import qs.Commons as Commons

// What the buttons do: each button's glyph, then its action, quiet.
Row {
  id: line
  property var g
  spacing: g.sized(Style.space(21))

  Repeater {
    model: line.g.hint
    Row {
      required property var modelData
      spacing: line.g.sized(Style.space(7))
      ButtonGlyph {
        g: line.g
        text: modelData[0]
      }
      Text {
        textFormat: Text.PlainText
        anchors.verticalCenter: parent.verticalCenter
        text: modelData[1]
        color: line.g.quiet
        font.family: line.g.fontFamily
        font.pixelSize: line.g.sized(Style.font.body)
      }
    }
  }
}

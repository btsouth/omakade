import QtQuick
import qs.Commons
import qs.Commons as Commons

// A pad button's printed name in a ring, as the hint line and the buttons
// show it; a longer keyboard name takes a pill.
Rectangle {
  id: glyph
  property var g
  property string text: ""
  height: g.sized(Style.space(21))
  width: Math.max(height, label.implicitWidth + g.sized(Style.space(10)))
  radius: height / 2
  color: "transparent"
  border.width: Math.max(1, Math.round(1.2 * g.zoom))
  border.color: Util.alpha(g.text, 0.45)

  Text {
    id: label
    anchors.centerIn: parent
    textFormat: Text.PlainText
    text: glyph.text
    color: glyph.g.dim
    font.family: glyph.g.fontFamily
    font.pixelSize: glyph.g.sized(Style.font.caption)
    font.bold: true
  }
}

import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A button with its icon and name side by side, centred, in a bordered chip:
// Resume and Quit, and the quit question's two choices.
Focusable {
  id: button

  property string icon: ""
  property real iconScale: 1
  property color iconColor: button.ink
  property string label: ""

  bordered: true
  iconItem: glyph
  truncated: labelText.truncated

  Row {
    id: content
    x: Math.round((button.width - width) / 2)
    height: button.height
    spacing: button.g.sized(Style.space(6))

    InkGlyph {
      id: glyph
      width: button.g.sized(Style.space(26))
      height: button.height
      text: button.icon
      color: button.iconColor
      size: Math.round(button.g.sized(Style.font.iconLarge) * button.iconScale)
      fontFamily: button.g.fontFamily
    }

    Text {
      id: labelText
      textFormat: Text.PlainText
      anchors.verticalCenter: parent.verticalCenter
      width: Math.min(implicitWidth, button.width - glyph.width - content.spacing - button.g.sized(Style.space(16)))
      text: button.label
      color: button.ink
      font.family: button.g.fontFamily
      font.pixelSize: button.g.sized(Style.font.heading)
      font.weight: Font.Medium
      elide: Text.ElideRight
    }
  }
}

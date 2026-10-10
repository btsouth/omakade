import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A capture tile: the icon over its name, both from the left edge, with a pad
// button's glyph (`hint`) in the top corner.
Focusable {
  id: tile

  property string icon: ""
  property string label: ""
  property string hint: ""
  readonly property int padX: g.sized(Style.space(18))

  iconItem: glyph
  truncated: labelText.truncated

  Column {
    x: tile.padX
    anchors.verticalCenter: parent.verticalCenter
    spacing: tile.g.sized(Style.space(8))

    InkGlyph {
      id: glyph
      width: Math.round(tile.g.sized(Style.font.display) * 1.15)
      height: width
      text: tile.icon
      color: tile.ink
      size: Math.round(tile.g.sized(Style.font.display) * 1.1)
      fontFamily: tile.g.fontFamily
    }

    Text {
      id: labelText
      textFormat: Text.PlainText
      width: Math.min(implicitWidth, tile.width - tile.padX * 2)
      text: tile.label
      color: tile.ink
      font.family: tile.g.fontFamily
      font.pixelSize: tile.g.sized(Style.font.title)
      elide: Text.ElideRight
    }
  }

  ButtonGlyph {
    g: tile.g
    visible: tile.hint !== ""
    text: tile.hint
    anchors.right: parent.right
    anchors.rightMargin: tile.g.sized(Style.space(12))
    anchors.top: parent.top
    anchors.topMargin: tile.g.sized(Style.space(11))
  }
}

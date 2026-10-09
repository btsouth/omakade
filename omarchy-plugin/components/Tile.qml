import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// An action tile: the icon over its name, in a bordered chip like a
// ButtonGroup option, filled when the cursor is on it.
Focusable {
  id: tile

  property string icon: ""
  property real iconScale: 1
  property color iconColor: tile.ink
  property string label: ""

  bordered: true

  InkGlyph {
    id: glyph
    x: 0
    width: tile.width
    y: Math.round(tile.height * 0.36 - height / 2)
    height: tile.g.sized(Style.space(30))
    text: tile.icon
    color: tile.iconColor
    size: Math.round(tile.g.sized(Style.font.display) * tile.iconScale)
    fontFamily: tile.g.fontFamily
  }

  Text {
    textFormat: Text.PlainText
    anchors.horizontalCenter: parent.horizontalCenter
    y: Math.round(tile.height * 0.74 - height / 2)
    width: Math.min(implicitWidth, tile.width - tile.g.sized(Style.space(8)))
    horizontalAlignment: Text.AlignHCenter
    text: tile.label
    color: tile.ink
    font.family: tile.g.fontFamily
    font.pixelSize: tile.g.sized(Style.font.body)
    elide: Text.ElideRight
  }
}

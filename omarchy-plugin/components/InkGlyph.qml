import QtQuick
import qs.Commons
import qs.Ui

// An icon centred on its drawn ink in both directions. OpticalGlyph corrects
// the horizontal painted bounds; this shifts the line box so the ink's vertical
// centre lands on the item's centre too, on a whole pixel.
Item {
  id: root

  property string text: ""
  property color color: Color.menu.text
  property real size: Style.font.iconLarge
  property string fontFamily: Style.font.menuFamily

  TextMetrics {
    id: ink
    font.family: root.fontFamily
    font.pixelSize: Math.max(1, Math.round(root.size))
    text: root.text
  }

  OpticalGlyph {
    id: glyph
    width: root.width
    height: root.height
    y: Math.round(root.height / 2 - (glyph.baselineY + ink.tightBoundingRect.y + ink.tightBoundingRect.height / 2))
    text: root.text
    color: root.color
    fontFamily: root.fontFamily
    fontSize: root.size
  }
}

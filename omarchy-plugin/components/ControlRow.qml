import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A row in the card's icon column with a control after the icon: a progress
// rail, the volume slider or a name, and a value on the right edge. The audio
// panel's slider row, at the menu's row metrics.
Focusable {
  id: row

  // "meter", "slider" or "text"
  property string kind: "text"
  property string icon: ""
  property real iconScale: 1
  property color iconColor: row.ink
  property string label: ""
  property string value: ""
  property real amount: 0
  property bool dim: false
  property int inset: g.sized(Style.space(8))

  height: g.sized(Style.space(38))

  InkGlyph {
    id: glyph
    x: row.inset
    width: row.g.sized(Style.space(28))
    height: row.height
    text: row.icon
    color: row.iconColor
    size: Math.round(row.g.sized(Style.font.iconLarge) * row.iconScale)
    fontFamily: row.g.fontFamily
  }

  readonly property int controlX: glyph.x + glyph.width + row.g.sized(Style.space(10))
  readonly property int controlRight: row.value === "" ? row.width - row.inset : valueText.x - row.g.sized(Style.space(14))

  Text {
    id: valueText
    textFormat: Text.PlainText
    anchors.right: parent.right
    anchors.rightMargin: row.inset + row.g.sized(Style.space(4))
    anchors.verticalCenter: parent.verticalCenter
    width: Math.max(implicitWidth, widest.advanceWidth)
    horizontalAlignment: Text.AlignRight
    text: row.value
    color: row.ink
    font.family: row.g.fontFamily
    font.pixelSize: row.g.sized(Style.font.body)
  }

  TextMetrics {
    id: widest
    font: valueText.font
    text: row.kind === "text" || row.value === "" ? "" : "100%"
  }

  Text {
    visible: row.kind === "text"
    textFormat: Text.PlainText
    x: row.controlX
    width: row.controlRight - x
    anchors.verticalCenter: parent.verticalCenter
    text: row.label
    color: row.ink
    font.family: row.g.fontFamily
    font.pixelSize: row.g.sized(Style.font.heading)
    font.weight: Font.Medium
    elide: Text.ElideRight
  }

  Meter {
    visible: row.kind === "meter"
    g: row.g
    x: row.controlX
    width: row.controlRight - x
    anchors.verticalCenter: parent.verticalCenter
    progress: row.amount
    ink: row.ink
  }

  QtObject {
    id: sliderColors
    property color foreground: row.ink
    property color background: Commons.Color.menu.background
  }

  PanelSlider {
    visible: row.kind === "slider"
    bar: sliderColors
    x: row.controlX
    width: row.controlRight - x
    height: row.height
    anchors.verticalCenter: parent.verticalCenter
    trackHeight: row.g.sized(Math.max(4, Math.round(Style.spacing.controlHeight * 0.11)))
    knobSize: row.g.sized(Math.max(14, Math.round(Style.spacing.controlHeight * 0.38)))
    minimum: 0
    maximum: 1
    step: 0.05
    value: row.amount
    opacity: row.dim ? 0.5 : 1
    onMoved: function(v) { row.g.cursor = "volume"; row.g.act("volume", v) }
  }
}

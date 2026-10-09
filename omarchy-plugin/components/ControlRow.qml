import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A panel section's control row: the icon in the card's icon column, then a
// progress rail, the volume slider or a name, as Omarchy's audio panel draws
// its slider and device rows. A name may take two lines; the row grows.
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
  readonly property int inset: g.sized(Style.space(8))
  readonly property int iconBox: g.sized(Style.space(28))
  readonly property int controlX: row.inset + row.iconBox + g.sized(Style.space(10))
  // Controls end on the section's right edge, where its figure ends.
  readonly property int controlRight: valueText.visible ? valueText.x - g.sized(Style.space(14)) : row.width - row.inset
  readonly property int padY: g.sized(Style.space(9))

  iconItem: glyph
  truncated: nameText.truncated
  height: Math.max(g.sized(Style.space(38)), row.kind === "text" ? nameText.height + row.padY * 2 : 0)

  InkGlyph {
    id: glyph
    x: row.inset
    width: row.iconBox
    height: row.height
    text: row.icon
    color: row.iconColor
    size: Math.round(row.g.sized(Style.font.iconLarge) * row.iconScale)
    fontFamily: row.g.fontFamily
  }

  Text {
    id: valueText
    textFormat: Text.PlainText
    visible: row.value !== ""
    anchors.right: parent.right
    anchors.rightMargin: row.inset
    anchors.verticalCenter: parent.verticalCenter
    text: row.value
    color: row.ink
    font.family: row.g.fontFamily
    font.pixelSize: row.g.sized(Style.font.body)
  }

  Text {
    id: nameText
    visible: row.kind === "text"
    textFormat: Text.PlainText
    x: row.controlX
    width: row.controlRight - x
    anchors.verticalCenter: parent.verticalCenter
    text: row.label
    color: row.ink
    font.family: row.g.fontFamily
    font.pixelSize: row.g.sized(Style.font.body)
    wrapMode: Text.Wrap
    maximumLineCount: 2
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

  // PanelSlider takes its colours from a bar; the card is that bar here.
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
    onMoved: function(v) { row.g.cursor = row.key; row.g.act("volume", v) }
  }
}

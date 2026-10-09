import QtQuick
import qs.Commons
import qs.Ui

// One row of the card, drawn like an Omarchy menu row: icon column, label in the
// menu's heading weight, the cursor as the menu's selected fill. A row can carry
// a value at its right edge, and the volume row a slider between the two.
BorderSurface {
  id: row

  property string icon: ""
  // Optical size: some glyphs draw less ink than the rest of the set.
  property real iconScale: 1
  property color iconColor: row.ink
  property string label: ""
  property string value: ""
  property bool current: false
  property bool urgent: false
  property bool slider: false
  property real sliderValue: 0
  property bool sliderMuted: false
  property string fontFamily: Style.font.menuFamily
  // The card's legible versions of the menu's selected and urgent colours, and
  // the focus hairline for themes whose selected fill is too faint on its own.
  property color selectedInk: Color.menu.selectedText
  property color urgentInk: Color.urgent
  property bool edge: false
  property color edgeColor: Color.menu.selectedText

  // Couch scale: one multiplier on the card's Omarchy tokens.
  property real zoom: 1
  function sized(v) { return v > 0 ? Math.max(1, Math.round(v * row.zoom)) : 0 }

  signal hovered(Item source, var mouse)
  signal activated()
  signal sliderMoved(real value)

  readonly property var selectedBorderSpec: Border.surfaceSpec("menu", "selected-border", Color.menu.selectedBorder, 0)
  readonly property color ink: row.current ? (row.urgent ? row.urgentInk : row.selectedInk) : Color.menu.text
  // The menu's icon column. Its ink box's left edge is the row's inset, which
  // the value on the right and the card's header and hint line share.
  readonly property int iconBox: row.sized(Style.space(36))
  readonly property bool truncated: labelText.truncated
  readonly property Item iconItem: glyph
  readonly property int inset: row.sized(Style.space(8)) + Math.round((iconBox - row.sized(Style.font.iconLarge)) / 2)

  height: Math.max(row.sized(Style.space(50)), row.sized(Style.font.body) + row.sized(Style.spacing.rowPaddingX) * 2)
  radius: row.sized(Style.cornerRadius)
  color: row.current ? Color.menu.selectedBackground : "transparent"
  borderSpec: !row.current ? Border.none()
    : row.edge ? Border.flat(row.edgeColor, Math.max(1, Style.focusBorderWidth)) : row.selectedBorderSpec

  // Cap height, so labels sit on the row's centre by their capitals rather than
  // by the font's line box, which carries more descent than ascent.
  TextMetrics {
    id: caps
    font.family: row.fontFamily
    font.pixelSize: row.sized(Style.font.heading)
    font.weight: Font.Medium
    text: "H"
  }

  InkGlyph {
    id: glyph
    x: row.borderLeft + row.sized(Style.space(8))
    width: row.iconBox
    height: row.height
    text: row.icon
    color: row.iconColor
    size: Math.round(row.sized(Style.font.iconLarge) * row.iconScale)
    fontFamily: row.fontFamily
  }

  Text {
    id: labelText
    textFormat: Text.PlainText
    x: glyph.x + glyph.width + row.sized(Style.space(6))
    width: row.slider ? implicitWidth : Math.max(0, (valueText.visible ? valueText.x - row.sized(Style.space(12)) : row.width - row.borderRight - row.inset) - x)
    y: Math.round(row.height / 2 - (labelText.baselineOffset + caps.tightBoundingRect.y + caps.tightBoundingRect.height / 2))
    text: row.label
    color: row.ink
    font.family: row.fontFamily
    font.pixelSize: row.sized(Style.font.heading)
    font.weight: Font.Medium
    elide: Text.ElideRight
  }

  Text {
    id: valueText
    textFormat: Text.PlainText
    visible: row.value.length > 0
    anchors.right: parent.right
    anchors.rightMargin: row.borderRight + row.inset
    anchors.baseline: labelText.baseline
    // Wide enough for "100%", so the slider's end does not move with the value.
    width: row.slider ? Math.max(implicitWidth, widest.advanceWidth) : implicitWidth
    horizontalAlignment: Text.AlignRight
    text: row.value
    color: row.ink
    font.family: row.fontFamily
    font.pixelSize: row.sized(Style.font.body)
  }

  TextMetrics {
    id: widest
    font: valueText.font
    text: "100%"
  }

  // PanelSlider takes its colours from a bar; the card is that bar here.
  QtObject {
    id: sliderColors
    property color foreground: row.ink
    property color background: Color.menu.background
  }

  PanelSlider {
    visible: row.slider
    trackHeight: row.sized(Math.max(4, Math.round(Style.spacing.controlHeight * 0.11)))
    knobSize: row.sized(Math.max(14, Math.round(Style.spacing.controlHeight * 0.38)))
    bar: sliderColors
    anchors.left: labelText.right
    anchors.leftMargin: row.sized(Style.space(14))
    anchors.right: valueText.left
    anchors.rightMargin: row.sized(Style.space(12))
    anchors.verticalCenter: parent.verticalCenter
    minimum: 0
    maximum: 1
    step: 0.05
    value: row.sliderValue
    opacity: row.sliderMuted ? 0.5 : 1
    onMoved: function(v) { row.sliderMoved(v) }
  }

  MouseArea {
    id: pointer
    anchors.fill: parent
    z: -1
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onEntered: row.hovered(pointer, {x: pointer.mouseX, y: pointer.mouseY})
    onPositionChanged: function(mouse) { row.hovered(pointer, mouse) }
    onClicked: row.activated()
  }
}

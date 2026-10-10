import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A sound row: the icon on the section's left edge, then the volume slider,
// or the output's name with which of how many and a chevron. The cursor's
// ring reaches a little past the row so the icon keeps its edge.
Focusable {
  id: row

  // "slider" or "text"
  property string kind: "text"
  property string icon: ""
  property real iconSize: g.sized(Style.font.iconLarge)
  property string label: ""
  property string value: ""
  property real amount: 0
  property bool dim: false
  readonly property int iconBox: g.sized(Style.space(22))
  readonly property int controlX: row.iconBox + g.sized(Style.space(12))

  tone: "row"
  outsetX: g.sized(Style.space(10))
  outsetY: g.sized(Style.space(3))
  iconItem: glyph
  truncated: nameText.truncated
  height: row.kind === "slider" ? g.sized(Style.space(30)) : Math.max(g.sized(Style.space(26)), nameText.height + g.sized(Style.space(6)))

  InkGlyph {
    id: glyph
    width: row.iconBox
    height: row.height
    text: row.icon
    color: row.kind === "slider" ? row.g.text : row.g.dim
    size: row.iconSize
    fontFamily: row.g.fontFamily
  }

  Text {
    id: nameText
    visible: row.kind === "text"
    textFormat: Text.PlainText
    x: row.controlX
    width: tail.x - x - row.g.sized(Style.space(12))
    anchors.verticalCenter: parent.verticalCenter
    text: row.label
    color: row.g.dim
    font.family: row.g.fontFamily
    font.pixelSize: row.g.sized(Style.font.subtitle)
    wrapMode: Text.Wrap
    maximumLineCount: 2
    elide: Text.ElideRight
  }

  Row {
    id: tail
    visible: row.kind === "text"
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    spacing: row.g.sized(Style.space(8))
    Text {
      textFormat: Text.PlainText
      anchors.verticalCenter: parent.verticalCenter
      text: row.value
      color: row.g.quiet
      font.family: row.g.fontFamily
      font.pixelSize: row.g.sized(Style.font.subtitle)
    }
    InkGlyph {
      width: row.g.sized(Style.space(14))
      height: row.height
      text: row.g.icons.chevron
      color: row.g.quiet
      size: row.g.sized(Style.font.title)
      fontFamily: row.g.fontFamily
    }
  }

  // PanelSlider rings its knob in its bar's background; the card is that bar.
  QtObject {
    id: sliderColors
    property color foreground: row.g.text
    property color background: Commons.Color.menu.background
  }

  PanelSlider {
    visible: row.kind === "slider"
    bar: sliderColors
    x: row.controlX
    width: row.width - x
    height: row.height
    anchors.verticalCenter: parent.verticalCenter
    trackColor: Util.alpha(row.g.text, 0.14)
    fillColor: row.g.accentInk
    knobColor: row.g.text
    trackHeight: Math.max(4, row.g.sized(Style.space(5)))
    knobSize: row.g.sized(Style.space(16))
    minimum: 0
    maximum: 1
    step: 0.05
    value: row.amount
    opacity: row.dim ? 0.5 : 1
    onMoved: function(v) { row.g.cursor = row.key; row.g.act("volume", v) }
  }
}

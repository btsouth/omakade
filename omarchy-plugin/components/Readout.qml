import QtQuick
import qs.Commons
import qs.Commons as Commons

// One reading as Omarchy's weather panel shows it: the name in quiet
// capitals, the number under it, its unit or second figure quiet beside it.
Column {
  id: readout
  property var g
  property string label: ""
  property string value: ""
  property string unit: ""
  property bool centered: false
  spacing: g.sized(Style.space(4))

  Caps {
    g: readout.g
    text: readout.label
    anchors.horizontalCenter: readout.centered ? parent.horizontalCenter : undefined
  }

  Row {
    anchors.horizontalCenter: readout.centered ? parent.horizontalCenter : undefined
    Text {
      id: number
      textFormat: Text.PlainText
      text: readout.value
      color: readout.g.text
      font.family: readout.g.fontFamily
      font.pixelSize: readout.g.sized(Style.font.heading)
      font.weight: Font.Medium
    }
    Text {
      textFormat: Text.PlainText
      visible: readout.unit !== ""
      anchors.baseline: number.baseline
      text: readout.unit
      color: readout.g.quiet
      font.family: readout.g.fontFamily
      font.pixelSize: readout.g.sized(Style.font.body)
    }
  }
}

import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// One performance reading in a box a step below the card: its name in quiet
// capitals, the number under it, a second figure (temperature, unit) smaller
// beside it.
BorderSurface {
  id: readout
  property var g
  property string label: ""
  property string value: ""
  property string unit: ""
  readonly property int padX: g.sized(Style.space(13))
  readonly property int padY: g.sized(Style.space(11))
  // What the box needs across, for the grid to choose its columns.
  readonly property int naturalWidth: readout.padX * 2 + Math.max(caps.implicitWidth, number.implicitWidth
    + (second.visible ? figures.spacing + second.implicitWidth : 0))

  radius: Math.min(g.sized(Style.cornerRadius), g.sized(Style.space(8)))
  color: g.well
  implicitHeight: column.implicitHeight + readout.padY * 2

  Column {
    id: column
    x: readout.padX
    y: readout.padY
    width: readout.width - readout.padX * 2
    spacing: readout.g.sized(Style.space(4))

    Caps {
      id: caps
      g: readout.g
      text: readout.label
    }

    Row {
      id: figures
      spacing: readout.g.sized(Style.space(8))
      Text {
        id: number
        textFormat: Text.PlainText
        text: readout.value
        color: readout.g.text
        font.family: readout.g.fontFamily
        font.pixelSize: Math.round(readout.g.sized(Style.font.display) * 0.88)
        font.weight: Font.DemiBold
      }
      Text {
        id: second
        textFormat: Text.PlainText
        visible: readout.unit !== ""
        anchors.baseline: number.baseline
        text: readout.unit
        color: readout.g.dim
        font.family: readout.g.fontFamily
        font.pixelSize: readout.g.sized(Style.font.subtitle)
      }
    }
  }
}

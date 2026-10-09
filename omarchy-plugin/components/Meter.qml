import QtQuick
import qs.Commons
import qs.Commons as Commons

// A progress rail, as the clock panel draws the year, at the volume slider's
// track size so the two read as one family.
Rectangle {
  id: meter
  property var g
  property real progress: 0
  property color ink: g.text
  height: g.sized(Math.max(4, Math.round(Style.spacing.controlHeight * 0.11)))
  radius: height / 2
  color: Style.selectedFillFor(meter.ink, Commons.Color.accent)

  Rectangle {
    width: Math.round(parent.width * Math.max(0, Math.min(1, meter.progress)))
    height: parent.height
    radius: parent.radius
    color: meter.ink
  }
}

import QtQuick
import qs.Commons
import qs.Commons as Commons

// A progress rail, as the clock panel draws the year: a faint track and the
// selected colour for what is done.
Rectangle {
  id: meter
  property var g
  property real progress: 0
  property color ink: g.text
  height: g.sized(Style.space(6))
  radius: Style.cornerRadius > 0 ? height / 2 : 0
  color: Qt.rgba(meter.ink.r, meter.ink.g, meter.ink.b, 0.16)

  Rectangle {
    width: Math.round(parent.width * Math.max(0, Math.min(1, meter.progress)))
    height: parent.height
    radius: parent.radius
    color: meter.ink
  }
}

import QtQuick
import qs.Commons

// One line of readings: frame rate and frame time when MangoHud reports them,
// CPU and GPU load and temperature where the drivers expose them. Numbers in
// the text colour, labels and units quiet. Readings that are missing are left
// out; with none at all the line is not shown. The groups run like text, a
// fixed gap apart; when they cannot fit on one line they wrap in pairs.
Item {
  id: root

  property var fps
  property var frametime
  property var cpu
  property var cpuTemp
  property var gpu
  property var gpuTemp
  property color text: Color.menu.text
  property color quiet: Color.menu.text
  property string fontFamily: Style.font.menuFamily

  // Couch scale: one multiplier on the card's Omarchy tokens.
  property real zoom: 1
  function sized(v) { return v > 0 ? Math.max(1, Math.round(v * root.zoom)) : 0 }

  function known(v) { return v !== undefined && v !== null && isFinite(Number(v)) }

  function load(label, value, temperature) {
    if (!known(value) && !known(temperature)) return null
    var parts = [[label, true]]
    if (known(value)) parts.push([" " + Math.round(value), false], ["%", true])
    if (known(temperature)) parts.push([" " + Math.round(temperature), false], ["°", true])
    return parts
  }

  readonly property var groups: [
    known(fps) ? [[String(Math.round(fps)), false], [" fps", true]] : null,
    known(frametime) ? [[Number(frametime).toFixed(1), false], [" ms", true]] : null,
    load("CPU", cpu, cpuTemp),
    load("GPU", gpu, gpuTemp)
  ].filter(function(g) { return g !== null })

  visible: groups.length > 0
  height: visible ? lineHeight * lines : 0

  property int lines: 1
  readonly property int lineHeight: Math.ceil(metrics.height)
  readonly property int gap: root.sized(Style.space(16))

  FontMetrics {
    id: metrics
    font.family: root.fontFamily
    font.pixelSize: root.sized(Style.font.body)
  }

  function arrange() {
    var items = []
    for (var i = 0; i < repeater.count; i++) if (repeater.itemAt(i)) items.push(repeater.itemAt(i))
    if (items.length === 0) return
    var total = items.reduce(function(sum, item) { return sum + item.implicitWidth }, 0)
    if (total + root.gap * (items.length - 1) <= root.width) {
      root.lines = 1
      root.spread(items, 0)
    } else {
      // Two lines of pairs: frame rate and time, then CPU and GPU.
      root.lines = Math.ceil(items.length / 2)
      for (var line = 0; line < root.lines; line++) root.spread(items.slice(line * 2, line * 2 + 2), line)
    }
  }

  function spread(items, line) {
    var x = 0
    for (var i = 0; i < items.length; i++) {
      items[i].x = Math.round(x)
      items[i].y = line * root.lineHeight
      x += items[i].implicitWidth + root.gap
    }
  }

  onWidthChanged: Qt.callLater(arrange)
  onGroupsChanged: Qt.callLater(arrange)

  Repeater {
    id: repeater
    model: root.groups

    Row {
      required property var modelData
      onImplicitWidthChanged: Qt.callLater(root.arrange)

      Repeater {
        model: modelData
        Text {
          required property var modelData
          textFormat: Text.PlainText
          text: modelData[0]
          color: modelData[1] ? root.quiet : root.text
          font.family: root.fontFamily
          font.pixelSize: root.sized(Style.font.body)
        }
      }
    }
  }
}

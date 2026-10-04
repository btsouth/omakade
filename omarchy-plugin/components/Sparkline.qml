import QtQuick

// Frame times as a filled line. Values are milliseconds, newest last.
Canvas {
  id: root
  required property var g
  property var values: []
  property real ceiling: 50
  property color stroke: g.accent

  onValuesChanged: requestPaint()
  onStrokeChanged: requestPaint()
  onWidthChanged: requestPaint()
  onHeightChanged: requestPaint()
  Connections { target: root.g; function onGraphFillAlphaChanged() { root.requestPaint() } }

  onPaint: {
    var ctx = getContext("2d")
    ctx.reset()
    var n = values.length
    if (n < 2) return
    var top = Math.max(ceiling, Math.max.apply(null, values))
    function px(i) { return i / (n - 1) * width }
    function py(v) { return height - Math.min(1, v / top) * (height - 2) - 1 }
    ctx.beginPath()
    ctx.moveTo(0, height)
    for (var i = 0; i < n; i++) ctx.lineTo(px(i), py(values[i]))
    ctx.lineTo(width, height)
    ctx.closePath()
    var grad = ctx.createLinearGradient(0, 0, 0, height)
    grad.addColorStop(0, Qt.rgba(stroke.r, stroke.g, stroke.b, g.graphFillAlpha))
    grad.addColorStop(1, Qt.rgba(stroke.r, stroke.g, stroke.b, 0))
    ctx.fillStyle = grad
    ctx.fill()
    ctx.beginPath()
    for (var j = 0; j < n; j++) {
      if (j === 0) ctx.moveTo(px(j), py(values[j]))
      else ctx.lineTo(px(j), py(values[j]))
    }
    ctx.lineWidth = Math.max(1.5, g.s(1.5))
    ctx.lineJoin = "round"
    ctx.strokeStyle = stroke
    ctx.stroke()
  }
}

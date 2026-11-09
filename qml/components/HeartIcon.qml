import QtQuick

// The favorite heart, drawn instead of taken from a font: the theme monospace fonts draw
// both U+2665 and U+2661 as outlines, so a favorite looked the same as one that is not.
// A Canvas rather than QtQuick.Shapes keeps another QML plugin out of startup.
Canvas {
    id: root

    property bool filled: false
    property color color: "black"
    property real size: 14

    implicitWidth: size
    implicitHeight: size

    onFilledChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const context = getContext("2d")
        context.reset()
        // The outline is drawn in a 24 unit box, centred and scaled to fit.
        const unit = Math.min(width, height) / 24
        context.translate((width - 24 * unit) / 2, (height - 24 * unit) / 2)
        context.scale(unit, unit)
        context.beginPath()
        context.moveTo(12, 20.5)
        context.bezierCurveTo(10.8, 19.4, 3, 13.4, 3, 8.6)
        context.bezierCurveTo(3, 5.9, 5.1, 4, 7.6, 4)
        context.bezierCurveTo(9.4, 4, 10.9, 5, 12, 6.6)
        context.bezierCurveTo(13.1, 5, 14.6, 4, 16.4, 4)
        context.bezierCurveTo(18.9, 4, 21, 5.9, 21, 8.6)
        context.bezierCurveTo(21, 13.4, 13.2, 19.4, 12, 20.5)
        context.closePath()
        context.lineJoin = "round"
        context.lineWidth = 2
        context.strokeStyle = root.color
        context.stroke()
        if (root.filled) {
            context.fillStyle = root.color
            context.fill()
        }
    }
}

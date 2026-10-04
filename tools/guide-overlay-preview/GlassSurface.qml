pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

// Only the outside rim is more translucent. All content sits over the audited tint.
Item {
    id: glass
    property var colors
    property url backdrop
    property real alpha: 1
    property real radius: 0
    property real scaleFactor: 1
    property real originX: x
    property real originY: y
    property real frameWidth: 1920
    property real frameHeight: 1080
    property real rim: 0
    property var zones: [{x: rim, y: rim, width: width - 2 * rim, height: height - 2 * rim}]
    default property alias content: contentItem.data
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    RectangularShadow {
        anchors.fill: parent
        radius: glass.radius
        blur: 80 * glass.scaleFactor
        spread: 4 * glass.scaleFactor
        offset.y: 20 * glass.scaleFactor
        color: Qt.rgba(0, 0, 0, 0.48)
    }
    Rectangle { id: mask; anchors.fill: parent; radius: glass.radius; visible: false; layer.enabled: true }
    Item {
        anchors.fill: parent
        clip: true
        layer.enabled: glass.radius > 0
        layer.effect: MultiEffect { maskEnabled: true; maskSource: mask; maskThresholdMin: 0.5; maskSpreadAtMin: 0.5 }
        Image {
            id: under
            source: glass.backdrop
            x: -glass.originX; y: -glass.originY
            width: glass.frameWidth; height: glass.frameHeight
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            source: under
            x: under.x; y: under.y; width: under.width; height: under.height
            blurEnabled: true; blur: 1; blurMax: 96; saturation: 0.65
        }
        // Fine theme-coloured noise below both protective layers, so the audit covers it.
        Canvas {
            id: grain
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                let seed = 42;
                for (let i = 0; i < width * height / 10; ++i) {
                    seed = (seed * 1664525 + 1013904223) >>> 0;
                    const px = seed % Math.ceil(width);
                    seed = (seed * 1664525 + 1013904223) >>> 0;
                    ctx.fillStyle = glass.tint(glass.colors.fg, 0.16);
                    ctx.fillRect(px, seed % Math.ceil(height), 1, 1);
                }
            }
            Connections { target: glass.colors; function onFgChanged() { grain.requestPaint() } }
        }
        Rectangle { anchors.fill: parent; color: glass.tint(glass.colors.darkerBg, 0.6) }
        Rectangle { anchors.fill: parent; color: glass.tint(glass.colors.bg, Math.min(glass.alpha, 0.32)) }
        Repeater {
            model: glass.zones
            delegate: Rectangle {
                required property var modelData
                x: modelData.x; y: modelData.y; width: modelData.width; height: modelData.height
                radius: Math.max(0, glass.radius * 0.6)
                color: glass.tint(glass.colors.bg, (glass.alpha - Math.min(glass.alpha, 0.32)) / 0.68)
            }
        }
        Item { id: contentItem; anchors.fill: parent }
    }
    Rectangle {
        anchors.fill: parent; radius: glass.radius; color: "transparent"
        border.width: 1; border.color: glass.tint(glass.colors.brightFg, 0.20)
    }
    Rectangle {
        x: glass.radius; y: 1; width: parent.width - 2 * x; height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: glass.tint(glass.colors.brightFg, 0) }
            GradientStop { position: 0.3; color: glass.tint(glass.colors.brightFg, 0.52) }
            GradientStop { position: 1; color: glass.tint(glass.colors.accent, 0.12) }
        }
    }
}

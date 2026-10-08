import QtQuick
import QtQuick.Shapes

// Line icons on a 24 unit grid, stroked in the theme's colors so they recolor with it.
Item {
    id: icon
    property string name: ""
    property color color
    property real stroke: 1.8
    implicitWidth: 24
    implicitHeight: 24

    readonly property var paths: ({
        "stats": "M4 20V12 M10 20V4 M16 20V9 M22 20H2",
        "mic": "M9 5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0z M5 10v2a7 7 0 0 0 14 0v-2 M12 19v3 M8 22h8",
        "pause": "M8 5v14 M16 5v14",
        "next": "M5 5l10 7-10 7z M19 5v14",
        "back": "M14 5l-7 7 7 7 M7 12h14",
        "sliders": "M3 6h18 M3 12h18 M3 18h18 M8 3v6 M16 9v6 M9 15v6",
        "play": "M8 5.5 L18.5 12 L8 18.5 Z",
        "desktop": "M5 4h14a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z M9 20h6 M12 16v4",
        "replay": "M4 12a8 8 0 1 0 2.4-5.7 M4 4.5v4h4 M12 8.2v4l2.6 2.4",
        "camera": "M4.5 8h2.8l1.6-2.2h6.2L16.7 8h2.8a1.5 1.5 0 0 1 1.5 1.5v8a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5v-8A1.5 1.5 0 0 1 4.5 8z M12 10.4a3.1 3.1 0 1 0 0.01 0z",
        "layers": "M12 3.5l8.5 4.5L12 12.5 3.5 8z M3.5 12l8.5 4.5 8.5-4.5 M3.5 16l8.5 4.5 8.5-4.5",
        "save": "M12 14.5V4 M8 7.8l4-3.8 4 3.8 M4 14v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4",
        "library": "M4 4h6.5v6.5H4z M13.5 4H20v6.5h-6.5z M4 13.5h6.5V20H4z M13.5 13.5H20V20h-6.5z",
        "power": "M12 3.5v8 M6.6 6.6a7.6 7.6 0 1 0 10.8 0",
        "gamepad": "M7.2 7.5h9.6a4.6 4.6 0 0 1 4.5 5.6l-.9 3.8a2.2 2.2 0 0 1-3.7 1.1l-2.4-2.5H9.7l-2.4 2.5a2.2 2.2 0 0 1-3.7-1.1l-.9-3.8a4.6 4.6 0 0 1 4.5-5.6z M7.6 10.5v3.4 M5.9 12.2h3.4 M15.6 11.4h.02 M17.6 13.4h.02",
        "volume": "M4 9.3h3.4L12 5.4v13.2l-4.6-3.9H4z M15.6 9a4.2 4.2 0 0 1 0 6 M18.2 6.4a7.8 7.8 0 0 1 0 11.2",
        "trophy": "M8 4h8v5.5a4 4 0 0 1-8 0z M8 6H5.2v1.3A3.2 3.2 0 0 0 8 10.4 M16 6h2.8v1.3A3.2 3.2 0 0 1 16 10.4 M12 13.5v3.5 M9 20h6 M10.2 17h3.6",
        "home": "M4 11.2L12 4.5l8 6.7 M6.5 9.5V19.5h11V9.5 M10 19.5v-5h4v5",
        "check": "M5 12.5l4.5 4.5L19 7.5"
    })

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.stroke
            fillColor: icon.name === "play" ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(icon.width / 24, icon.height / 24)
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}

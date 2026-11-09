import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    property real uiScale: 1
    spacing: 10 * uiScale
    font.family: Theme.fontFamily
    font.pixelSize: 12 * uiScale

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.foreground : Theme.mutedText
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }

    indicator: Rectangle {
        implicitWidth: 22 * control.uiScale
        implicitHeight: 22 * control.uiScale
        x: control.mirrored ? control.width - width - control.leftPadding : control.leftPadding
        y: (control.height - height) / 2
        radius: Math.max(4, Theme.cornerRadius)
        color: control.checked
               ? Theme.accent
               : Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                         Theme.foreground.b, 0.045)
        border.width: 1
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                              Theme.foreground.b, 0.16)

        FocusRing {
            shown: control.activeFocus
            thickness: 3 * control.uiScale
        }

        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            color: Theme.background
            font.family: Theme.fontFamily
            font.pixelSize: 14 * control.uiScale
            font.weight: Font.Bold
        }
    }
}

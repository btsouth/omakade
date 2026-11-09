import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property real uiScale: 1
    implicitWidth: 136 * uiScale
    implicitHeight: 40 * uiScale
    font.family: Theme.fontFamily
    font.pixelSize: 12 * uiScale
    leftPadding: 8 * uiScale
    rightPadding: 34 * uiScale

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: Theme.foreground
        selectionColor: Theme.accent
        selectedTextColor: Theme.background
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onAccepted: control.value = control.valueFromText(text, control.locale)
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 0
        implicitWidth: 30 * control.uiScale
        height: control.height / 2
        color: control.up.pressed
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
               : Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                         Theme.foreground.b, 0.045)
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                              Theme.foreground.b, 0.12)
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12 * control.uiScale
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: control.height / 2
        implicitWidth: 30 * control.uiScale
        height: control.height / 2
        color: control.down.pressed
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
               : Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                         Theme.foreground.b, 0.045)
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                              Theme.foreground.b, 0.12)
        Text {
            anchors.centerIn: parent
            text: "−"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12 * control.uiScale
        }
    }

    background: Rectangle {
        radius: Math.max(5, Theme.cornerRadius)
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
        border.width: 1
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                              Theme.foreground.b, 0.12)

        FocusRing {
            shown: control.activeFocus
            thickness: 3 * control.uiScale
        }
    }
}

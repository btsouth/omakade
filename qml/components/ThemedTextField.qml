import QtQuick
import QtQuick.Controls

TextField {
    id: control

    color: Theme.foreground
    placeholderTextColor: Theme.mutedText
    font.family: Theme.fontFamily
    selectionColor: Theme.accent
    selectedTextColor: Theme.background
    leftPadding: 12
    rightPadding: 12

    background: Rectangle {
        radius: Math.max(5, Theme.cornerRadius)
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
        border.width: 1
        border.color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                              Theme.foreground.b, 0.12)

        FocusRing { shown: control.activeFocus }
    }
}

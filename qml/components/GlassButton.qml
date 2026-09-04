import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string iconText: ""
    property bool primary: false
    property bool selected: false
    property bool compact: false
    readonly property var hostWindow: root.Window.window
    property real displayScale: hostWindow && hostWindow.couchMode
                                ? Math.max(1, Math.min(2.4,
                                                      hostWindow.height / 900))
                                : 1

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    implicitHeight: (compact ? 34 : 42) * displayScale
    implicitWidth: Math.max((compact ? 76 : 104) * displayScale,
                            contentRow.implicitWidth + (compact ? 22 : 30) * displayScale)
    leftPadding: (compact ? 11 : 15) * displayScale
    rightPadding: leftPadding
    spacing: 8 * displayScale
    focusPolicy: Qt.StrongFocus
    KeyNavigation.priority: KeyNavigation.BeforeItem

    // Qt only presses a Button on Return or Enter when the platform theme says so. Controller
    // and keyboard confirm must work on every desktop, so handle both keys here.
    Keys.onReturnPressed: function(event) {
        if (enabled) {
            clicked()
        }
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (enabled) {
            clicked()
        }
        event.accepted = true
    }

    background: Rectangle {
        radius: Math.max(4, Theme.cornerRadius)
        color: root.down
               ? root.alpha(root.primary ? Theme.accent : Theme.foreground, 0.24)
               : root.hovered || root.activeFocus
                 ? root.alpha(root.primary ? Theme.accent : Theme.foreground, 0.14)
                 : root.primary
                   ? root.alpha(Theme.accent, 0.18)
                   : root.selected
                     ? root.alpha(Theme.foreground, 0.12)
                     : root.alpha(Theme.foreground, 0.045)
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus
                      ? Theme.accent
                      : root.primary
                        ? root.alpha(Theme.accent, 0.58)
                        : root.alpha(Theme.foreground, root.hovered ? 0.32 : 0.16)

        Behavior on color {
            enabled: !Preferences.reducedMotion
            ColorAnimation { duration: 120 }
        }
        Behavior on border.color {
            enabled: !Preferences.reducedMotion
            ColorAnimation { duration: 120 }
        }
    }

    contentItem: Row {
        id: contentRow
        spacing: root.spacing
        anchors.centerIn: parent

        Text {
            visible: root.iconText.length > 0
            text: root.iconText
            color: root.enabled ? (root.primary ? Theme.brightForeground : Theme.foreground)
                                : root.alpha(Theme.foreground, 0.35)
            font.family: Theme.fontFamily
            font.pixelSize: (root.compact ? 12 : 14) * root.displayScale
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.text
            color: root.enabled ? (root.primary ? Theme.brightForeground : Theme.foreground)
                                : root.alpha(Theme.foreground, 0.35)
            font.family: Theme.fontFamily
            font.pixelSize: (root.compact ? 11 : 12) * root.displayScale
            font.weight: root.primary || root.selected ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}

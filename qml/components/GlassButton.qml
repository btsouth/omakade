import QtQuick
import QtQuick.Controls

Button {
    id: root

    property real maximumLabelWidth: Infinity
    property string iconText: ""
    property bool primary: false
    property bool selected: false
    // Set on tabs, choices and on/off toggles so screen readers hear selected as checked.
    // The call site's handler owns the state, so a click never flips it here.
    property bool selectable: false
    property bool compact: false
    // Fired instead of clicked() for Shift or Ctrl clicks and Shift+Enter: the
    // "add to selection" gesture on filter chips that combine.
    signal secondaryClicked()
    readonly property var hostWindow: root.Window.window
    property real displayScale: hostWindow && hostWindow.couchMode
                                ? Math.max(1, Math.min(2.4,
                                                      hostWindow.height / 900))
                                : 1

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    implicitHeight: (compact ? 36 : UiMetrics.controlHeight) * displayScale
    implicitWidth: Math.max((compact ? 76 : 104) * displayScale,
                            contentRow.implicitWidth + (compact ? 22 : 30) * displayScale)
    leftPadding: (compact ? 11 : 15) * displayScale
    rightPadding: leftPadding
    spacing: 8 * displayScale
    focusPolicy: Qt.StrongFocus
    KeyNavigation.priority: KeyNavigation.BeforeItem
    checkable: selectable
    checked: selected
    onToggled: checked = Qt.binding(() => root.selected)

    // Qt only presses a Button on Return or Enter when the platform theme says so. Controller
    // and keyboard confirm must work on every desktop, so handle both keys here.

    function acceptInput(event) {
        if (enabled) {
            if (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) secondaryClicked()
            else clicked()
        }
        event.accepted = true
    }
    function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
    Keys.onReturnPressed: event => acceptInput(event)
    Keys.onEnterPressed: function(event) {
        if (enabled) {
            if (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) secondaryClicked()
            else clicked()
        }
        event.accepted = true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        // Only modifier clicks are taken here; plain presses fall through to the button.
        onPressed: function(mouse) {
            mouse.accepted = root.enabled && (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
        }
        onClicked: function(mouse) {
            if (root.enabled && (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) {
                root.forceActiveFocus(Qt.MouseFocusReason)
                root.secondaryClicked()
            }
        }
    }

    ToolTip.visible: hovered && buttonLabel.truncated
    ToolTip.text: text
    ToolTip.delay: 500

    background: Rectangle {
        // Selected keeps its accent fill and bar while hovered, pressed or focused.
        readonly property bool marked: root.selected && !root.primary
        readonly property color tint: root.primary || marked ? Theme.accent : Theme.foreground
        radius: Math.max(4, Theme.cornerRadius)
        color: root.down
               ? root.alpha(tint, marked ? 0.42 : 0.24)
               : root.hovered || root.activeFocus
                 ? root.alpha(tint, marked ? 0.34 : 0.14)
                 : root.alpha(tint, marked ? 0.26 : root.primary ? 0.18 : 0.045)
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus
                      ? Theme.accent
                      : marked
                        ? root.alpha(Theme.accent, 0.8)
                        : root.primary
                          ? root.alpha(Theme.accent, 0.58)
                          : root.alpha(Theme.foreground, root.hovered ? 0.5 : 0.35)

        Rectangle {
            objectName: "selectedIndicator"
            visible: parent.marked
            x: 4 * root.displayScale
            y: Math.max(5, parent.height * 0.22)
            width: 3 * root.displayScale
            height: parent.height - 2 * y
            radius: width / 2
            color: Theme.accent
        }

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
            textFormat: Text.PlainText
            visible: root.iconText.length > 0
            text: root.iconText
            color: root.enabled ? (root.primary ? Theme.brightForeground : Theme.foreground)
                                : root.alpha(Theme.foreground, 0.35)
            font.family: Theme.fontFamily
            font.pixelSize: (root.compact ? 12 : 14) * root.displayScale
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: buttonLabel
            textFormat: Text.PlainText
            width: Math.min(implicitWidth, root.maximumLabelWidth)
            elide: Text.ElideRight
            text: root.text
            color: root.enabled ? (root.primary || root.selected ? Theme.brightForeground : Theme.foreground)
                                : root.alpha(Theme.foreground, 0.35)
            font.family: Theme.fontFamily
            font.pixelSize: (root.compact ? UiMetrics.label : UiMetrics.body) * root.displayScale
            font.weight: root.primary || root.selected ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}

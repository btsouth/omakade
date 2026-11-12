import QtQuick
import QtQuick.Controls

Button {
    id: root

    property real maximumLabelWidth: Infinity
    property string iconText: ""
    // Drawn icon in place of iconText, such as HeartIcon. It can size and color itself from
    // iconSize and iconColor so it matches a text icon.
    property Component iconDelegate: null
    readonly property real iconSize: (compact ? UiMetrics.label : UiMetrics.body) * displayScale
    readonly property color iconColor: root.labelColor
    // The screen's main action, such as Play, drawn as a solid accent fill.
    property bool primary: false
    // Confirms something that cannot be undone, such as a delete, drawn as a solid red fill.
    property bool destructive: false
    // Labels sit in the middle of the button. Qt.AlignLeft keeps a label column where one is needed.
    property int labelAlignment: Qt.AlignHCenter
    readonly property bool filled: root.primary || root.destructive
    readonly property color fillColor: root.destructive ? Theme.destructive : Theme.accent
    readonly property color labelColor: {
        if (!root.enabled) return root.alpha(Theme.foreground, 0.35)
        if (!root.filled) return root.selected ? Theme.brightForeground : Theme.foreground
        // readableOn reads theme colors the binding cannot see, so name them here.
        Theme.brightForeground
        Theme.darkerBackground
        return Theme.readableOn(root.fillColor)
    }
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

    implicitHeight: (compact ? UiMetrics.compactControlHeight : UiMetrics.controlHeight) * displayScale
    implicitWidth: Math.max((compact ? 76 : 104) * displayScale,
                            contentHolder.implicitWidth + (compact ? 22 : 30) * displayScale)
    leftPadding: (compact ? 11 : 15) * displayScale
    rightPadding: leftPadding
    spacing: 8 * displayScale
    focusPolicy: Qt.StrongFocus
    KeyNavigation.priority: KeyNavigation.BeforeItem
    checkable: selectable
    // Buttons that only use selected as a highlight must not report checked.
    checked: selectable && selected
    onToggled: checked = Qt.binding(() => root.selectable && root.selected)

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
        // Selected keeps its accent fill and bar while hovered, pressed or focused. Hover only
        // changes the fill; focus adds the thick ring, so the two never look alike.
        // Primary and destructive buttons are solid, so they outrank the selected tint.
        readonly property bool marked: root.selected && !root.filled
        readonly property color tint: marked ? Theme.accent : Theme.foreground
        radius: Math.max(4, Theme.cornerRadius)
        color: root.filled
               ? (!root.enabled ? root.alpha(root.fillColor, 0.3)
                  : root.down ? Qt.tint(root.fillColor, root.alpha(root.labelColor, 0.24))
                  : root.hovered ? Qt.tint(root.fillColor, root.alpha(root.labelColor, 0.12))
                  : root.fillColor)
               : root.down
                 ? root.alpha(tint, marked ? 0.42 : 0.24)
                 : root.hovered
                   ? root.alpha(tint, marked ? 0.34 : 0.14)
                   : root.alpha(tint, marked ? 0.26 : 0.045)
        border.width: 1
        border.color: root.filled
                      ? root.alpha(root.fillColor, root.enabled ? 1 : 0.5)
                      : marked
                        ? root.alpha(Theme.accent, 0.8)
                        : root.alpha(Theme.foreground, root.hovered ? 0.5 : 0.35)

        FocusRing {
            shown: root.activeFocus
            thickness: 3 * root.displayScale
            // The theme ring is often the accent, which would vanish on a solid accent fill.
            border.color: root.filled && root.enabled ? root.labelColor : Theme.focusRing
        }

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

    // Button sizes contentItem to the padded area, so the row sits in a holder that places it
    // by labelAlignment instead of starting every label at the left edge.
    contentItem: Item {
        id: contentHolder
        // Sized from the label's own width, so a narrow button can elide it without a loop.
        readonly property real iconWidth: iconLoader.visible ? iconLoader.width + root.spacing
                                          : iconLabel.visible ? iconLabel.implicitWidth + root.spacing
                                          : 0
        implicitWidth: iconWidth + Math.min(buttonLabel.implicitWidth, root.maximumLabelWidth)
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            objectName: "buttonContent"
            spacing: root.spacing
            anchors.verticalCenter: parent.verticalCenter
            x: root.labelAlignment === Qt.AlignHCenter ? Math.max(0, (parent.width - width) / 2)
               : root.labelAlignment === Qt.AlignRight ? Math.max(0, parent.width - width) : 0

            Loader {
                id: iconLoader
                active: root.iconDelegate !== null
                visible: active
                sourceComponent: root.iconDelegate
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: iconLabel
                textFormat: Text.PlainText
                visible: root.iconDelegate === null && root.iconText.length > 0
                text: root.iconText
                color: root.iconColor
                font.family: Theme.fontFamily
                font.pixelSize: root.iconSize
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: buttonLabel
                objectName: "buttonLabel"
                textFormat: Text.PlainText
                // The half pixel absorbs rounding error: a button sized to its label gets a
                // hair less back at fractional scales, which elided labels in Game Mode.
                width: Math.max(0, Math.min(implicitWidth, root.maximumLabelWidth,
                                            contentHolder.width - contentHolder.iconWidth + 0.5))
                elide: Text.ElideRight
                text: root.text
                color: root.labelColor
                font.family: Theme.fontFamily
                font.pixelSize: (root.compact ? UiMetrics.label : UiMetrics.body) * root.displayScale
                font.weight: root.filled || root.selected ? Font.DemiBold : Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}

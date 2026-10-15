import QtQuick
import QtQuick.Layouts

// Covers the library while a launch hold keeps the launcher's windows out of sight.
// Each held window can be opened here when it needs an answer, such as a sign-in.
Rectangle {
    id: screen
    objectName: "launchingScreen"

    property real uiScale: 1

    color: Theme.background
    visible: LaunchHold.active

    readonly property var buttons: {
        const list = []
        for (let index = 0; index < windowRepeater.count; ++index) list.push(windowRepeater.itemAt(index))
        list.push(showAllButton)
        return list.filter(button => button !== null)
    }

    function focusFirst() {
        if (buttons.length > 0) buttons[0].forceActiveFocus(Qt.TabFocusReason)
    }

    // Up and Down move through the list; Left and Right stay put.
    function navigate(key) {
        const index = buttons.findIndex(button => button.activeFocus)
        if (index < 0) { focusFirst(); return true }
        const step = key === Qt.Key_Up ? -1 : key === Qt.Key_Down ? 1 : 0
        const next = Math.max(0, Math.min(buttons.length - 1, index + step))
        buttons[next].forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    onVisibleChanged: if (visible) Qt.callLater(focusFirst)
    // A window that opened or closed must not strand focus on a button that went away.
    onButtonsChanged: if (visible && !buttons.some(button => button.activeFocus)) Qt.callLater(focusFirst)

    Keys.onUpPressed: event => event.accepted = navigate(Qt.Key_Up)
    Keys.onDownPressed: event => event.accepted = navigate(Qt.Key_Down)

    // Takes clicks and wheel events so the library underneath stays still.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96 * screen.uiScale, 820 * screen.uiScale)
        spacing: 16 * screen.uiScale

        Text {
            Layout.fillWidth: true
            text: "STARTING " + (LaunchHold.title || "GAME").toUpperCase()
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 28 * screen.uiScale
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: windowRepeater.count > 0
                  ? "These windows are waiting. Open one if it needs you, such as a sign-in."
                  : "Waiting for the game to open."
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 14 * screen.uiScale
            wrapMode: Text.Wrap
        }
        Repeater {
            id: windowRepeater
            model: LaunchHold.windows
            GlassButton {
                required property var modelData
                Layout.fillWidth: true
                objectName: "launchingWindow_" + modelData.address
                text: (modelData.title || modelData.windowClass || "Window")
                      + (modelData.title && modelData.windowClass ? "  ·  " + modelData.windowClass : "")
                maximumLabelWidth: width - 40 * screen.uiScale
                onClicked: LaunchHold.present(modelData.address)
            }
        }
        GlassButton {
            id: showAllButton
            objectName: "launchingShowAllButton"
            Layout.alignment: Qt.AlignLeft
            text: "SHOW ALL WINDOWS"
            onClicked: LaunchHold.cancel()
        }
    }
}

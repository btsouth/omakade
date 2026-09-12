import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: menu
    required property var host
    required property Item anchorItem
    property bool showCloseButton: true
    property bool fixedHeader: false
    property real preferredWidth: 320
    property string doneObjectName: "actionMenuDoneButton"
    property Item headerDownTarget: null
    readonly property Item doneControl: headerDone
    property string title: "ACTIONS"
    property Item initialFocus: null
    default property alias actions: actionColumn.data

    parent: Overlay.overlay
    width: Math.min(preferredWidth * (host.couchMode ? Math.max(1, Math.min(2.4, host.height / 900)) : 1), host.width - 48)
    height: Math.min(implicitHeight, host.height - 48)
    padding: 16
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Close and restore the invoker before another editor captures its return focus.
    function invoke(action) {
        close()
        Qt.callLater(action)
    }

    function handleMenuKey(event) {
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            host.focusWithin(menu.contentItem,
                event.key !== Qt.Key_Backtab && !(event.modifiers & Qt.ShiftModifier))
            event.accepted = true
        } else host.handleArrowKey(menu.contentItem, event)
    }
    function positionWithinWindow() {
        if (fixedHeader) {
            x = Math.max(24, (host.width - width) / 2)
            y = Math.max(24, (host.height - height) / 2)
        } else {
            const position = anchorItem.mapToItem(Overlay.overlay, 0, anchorItem.height)
            x = Math.max(24, Math.min(host.width - width - 24, position.x))
            y = Math.max(24, Math.min(host.height - height - 24, position.y + 8))
        }
    }
    onWidthChanged: if (visible) positionWithinWindow()
    onHeightChanged: if (visible) positionWithinWindow()
    Connections {
        target: menu.host
        function onWidthChanged() { if (menu.visible) menu.positionWithinWindow() }
        function onHeightChanged() { if (menu.visible) menu.positionWithinWindow() }
    }
    onAboutToShow: {
        if (host.activeActionMenu && host.activeActionMenu !== menu)
            host.activeActionMenu.close()
        host.activeActionMenu = menu
        positionWithinWindow()
    }
    onOpened: {
        // A reopened popup may restore its old child focus before this signal.
        // Start at the first enabled action, not the item after that old child.
        if (initialFocus && initialFocus.visible && initialFocus.enabled) {
            host.focusWithin(contentItem, true, initialFocus)
            return
        }
        for (const action of actionColumn.children) {
            if (action.visible && action.enabled && action.activeFocusOnTab) {
                host.focusWithin(contentItem, true, action)
                return
            }
        }
        host.focusWithin(contentItem, true, closeButton)
    }
    onClosed: {
        if (host.activeActionMenu === menu)
            host.activeActionMenu = null
        if (anchorItem.visible && anchorItem.enabled)
            anchorItem.forceActiveFocus(Qt.TabFocusReason)
    }

    background: Rectangle {
        color: Theme.background
        radius: Math.max(8, Theme.cornerRadius)
        border.color: Theme.mutedText
    }
    contentItem: ColumnLayout {
        property var navigationScrollView: actionScroll
        spacing: menu.fixedHeader ? 12 : 0
        // Modal popups block the window shortcuts. Handle navigation inside
        // the popup so physical keyboard input follows the controller path.
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => menu.handleMenuKey(event)
        RowLayout {
            Layout.fillWidth: true
            visible: menu.fixedHeader
            Text {
                Layout.fillWidth: true
                text: menu.title
                wrapMode: Text.Wrap
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            GlassButton {
                id: headerDone
                objectName: menu.doneObjectName
                compact: true
                text: "DONE"
                property Item controllerDownTarget: menu.headerDownTarget
                onClicked: menu.close()
            }
        }
        ScrollView {
        id: actionScroll
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => menu.handleMenuKey(event)
        objectName: "actionMenuScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        rightPadding: 12
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        implicitHeight: menuColumn.implicitHeight
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: menuColumn
            width: actionScroll.availableWidth
            spacing: 8
            Text {
                Layout.fillWidth: true
                visible: !menu.fixedHeader
                text: menu.title
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                id: actionColumn
                Layout.fillWidth: true
                spacing: 6
            }
            MenuAction {
                id: closeButton
                objectName: "actionMenuCloseButton"
                visible: menu.showCloseButton
                Layout.fillWidth: true
                text: "CLOSE"
                compact: true
                onClicked: menu.close()
            }
        }
    }
    }
}

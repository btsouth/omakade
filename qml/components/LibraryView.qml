import QtQuick
import QtQuick.Controls

Item {
    id: root

    Accessible.name: "Game library"
    Accessible.role: Accessible.List

    required property var libraryModel
    property alias currentIndex: grid.currentIndex
    readonly property int count: grid.count
    readonly property bool gridFocused: grid.activeFocus
    property bool scanning: false
    property string emptyTitle: "No games found"
    property string emptyMessage: "Try a different search or library view."

    signal gameActivated(int index)
    signal favoriteToggled(int index)
    signal refreshRequested()

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function focusGrid() {
        if (grid.count > 0) {
            grid.forceActiveFocus()
        }
    }

    GridView {
        id: grid
        objectName: "libraryGrid"
        anchors.fill: parent
        anchors.rightMargin: libraryScrollTrack.visible ? 20 : 0
        clip: true
        model: root.libraryModel
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 110
        cacheBuffer: height * 0.25
        reuseItems: true
        focus: true
        currentIndex: count > 0 ? Math.min(currentIndex, count - 1) : -1
        property real wheelTargetY: contentY

        NumberAnimation {
            id: wheelScrollAnimation
            target: grid
            property: "contentY"
            duration: 180
            easing.type: Easing.OutCubic
        }

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: true

            onWheel: function(event) {
                const travel = event.pixelDelta.y !== 0
                               ? event.pixelDelta.y * 1.35
                               : event.angleDelta.y / 120 * grid.cellHeight * 1.5
                if (travel === 0) {
                    return
                }
                const maximumY = Math.max(0, grid.contentHeight - grid.height)
                const startingY = wheelScrollAnimation.running
                                  ? grid.wheelTargetY : grid.contentY
                grid.wheelTargetY = Math.max(0, Math.min(maximumY,
                                                        startingY - travel))
                wheelScrollAnimation.stop()
                if (Preferences.reducedMotion) {
                    grid.contentY = grid.wheelTargetY
                } else {
                    wheelScrollAnimation.from = grid.contentY
                    wheelScrollAnimation.to = grid.wheelTargetY
                    wheelScrollAnimation.start()
                }
                event.accepted = true
            }
        }

        Keys.onReturnPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onEnterPressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onSpacePressed: function(event) {
            if (currentIndex >= 0) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onUpPressed: function(event) {
            if (currentIndex >= columns) {
                currentIndex -= columns
                positionViewAtIndex(currentIndex, GridView.Contain)
            }
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            if (currentIndex >= 0 && currentIndex + columns < count) {
                currentIndex += columns
                positionViewAtIndex(currentIndex, GridView.Contain)
                event.accepted = true
            }
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F && currentIndex >= 0) {
                root.favoriteToggled(currentIndex)
                event.accepted = true
            }
        }

        readonly property int columns: Math.max(2, Math.min(8, Math.floor(width / 210)))
        cellWidth: width / columns
        cellHeight: Math.round(cellWidth * 1.5) + 64

        delegate: Item {
            id: delegateRoot
            required property int index
            required property string title
            required property string subtitle
            required property int hours
            required property int progress
            required property bool favorite
            required property string completionStatus
            required property color accentStart
            required property color accentEnd
            required property string coverMark
            required property string coverPath
            required property bool installed

            width: grid.cellWidth
            height: grid.cellHeight

            GameCard {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 7
                anchors.bottomMargin: 7
                title: delegateRoot.title
                subtitle: delegateRoot.subtitle
                hours: delegateRoot.hours
                progress: delegateRoot.progress
                favorite: delegateRoot.favorite
                completionStatus: delegateRoot.completionStatus
                accentStart: delegateRoot.accentStart
                accentEnd: delegateRoot.accentEnd
                coverMark: delegateRoot.coverMark
                coverPath: delegateRoot.coverPath
                installed: delegateRoot.installed
                current: grid.currentIndex === delegateRoot.index
                focus: current

                onActiveFocusChanged: {
                    if (activeFocus) {
                        grid.currentIndex = delegateRoot.index
                    }
                }
                onActivated: root.gameActivated(delegateRoot.index)
                onFavoriteToggled: root.favoriteToggled(delegateRoot.index)
            }
        }

        onCountChanged: {
            if (count > 0 && currentIndex < 0) {
                currentIndex = 0
            }
        }
    }

    Rectangle {
        id: libraryScrollTrack
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 16
        radius: width / 2
        visible: grid.contentHeight > grid.height + 1
        color: root.alpha(Theme.foreground, scrollMouse.containsMouse ? 0.11 : 0.06)

        function scrollTo(pointerY) {
            const trackRange = height - scrollThumb.height
            const contentRange = grid.contentHeight - grid.height
            if (trackRange <= 0 || contentRange <= 0) {
                return
            }
            const thumbY = Math.max(0, Math.min(trackRange,
                                                pointerY - scrollMouse.dragOffset))
            grid.contentY = thumbY / trackRange * contentRange
        }

        Rectangle {
            id: scrollThumb
            width: 10
            height: Math.max(52, Math.min(parent.height,
                                         parent.height * grid.height / grid.contentHeight))
            x: (parent.width - width) / 2
            y: {
                const trackRange = parent.height - height
                const contentRange = grid.contentHeight - grid.height
                return contentRange > 0 ? trackRange * grid.contentY / contentRange : 0
            }
            radius: width / 2
            color: root.alpha(Theme.foreground,
                              scrollMouse.pressed ? 0.78
                              : scrollMouse.containsMouse ? 0.58 : 0.38)
        }

        MouseArea {
            id: scrollMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            property real dragOffset: scrollThumb.height / 2

            onPressed: function(mouse) {
                wheelScrollAnimation.stop()
                dragOffset = mouse.y >= scrollThumb.y
                             && mouse.y <= scrollThumb.y + scrollThumb.height
                             ? mouse.y - scrollThumb.y : scrollThumb.height / 2
                libraryScrollTrack.scrollTo(mouse.y)
            }
            onPositionChanged: function(mouse) {
                if (pressed) {
                    libraryScrollTrack.scrollTo(mouse.y)
                }
            }
        }
    }

    Column {
        visible: grid.count === 0
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "◇"
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: 42
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.scanning ? "Scanning Steam" : root.emptyTitle
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.scanning ? "Looking for installed games and local artwork." : root.emptyMessage
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
        GlassButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.scanning
            text: "RESCAN"
            compact: true
            onClicked: root.refreshRequested()
        }
    }
}

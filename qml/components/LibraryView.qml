import QtQuick

Item {
    id: root

    Accessible.name: "Game library"
    Accessible.role: Accessible.List

    required property var libraryModel
    property alias currentIndex: grid.currentIndex
    property alias navigationTarget: grid
    readonly property int count: grid.count
    readonly property int columns: grid.columns
    readonly property bool gridFocused: grid.activeFocus
    // Where focus should land when something above hands it downwards. With no games left the
    // grid has nothing to select, so the empty state takes it instead.
    readonly property Item focusTarget: grid.count > 0 ? grid
                                      : root.filtersActive ? emptyClearButton : emptyRescanButton
    property bool scanning: false
    property bool filtersActive: false
    property string emptyTitle: "No games found"
    property string emptyMessage: "Try a different search or library view."

    signal gameActivated(int index)
    signal favoriteToggled(int index)
    signal refreshRequested()
    signal clearFiltersRequested()
    signal coverRequested(string source, string appId)
    signal focusAboveRequested()

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function focusGrid() {
        if (grid.count > 0) {
            grid.forceActiveFocus()
        } else if (!root.scanning && root.filtersActive) {
            // Keyboard focus needs somewhere to land when the last visible game leaves the grid.
            Qt.callLater(emptyClearButton.forceActiveFocus)
        } else if (!root.scanning) {
            Qt.callLater(emptyRescanButton.forceActiveFocus)
        }
    }

    // Keep the highlighted card across model resets caused by background rescans.
    property var pendingCurrent: null

    Connections {
        target: root.libraryModel
        function onModelAboutToBeReset() {
            if (grid.currentIndex >= 0 && grid.currentIndex < grid.count) {
                const game = root.libraryModel.get(grid.currentIndex)
                root.pendingCurrent = { source: game.source, runner: game.runner || "",
                                        appId: game.appId }
            } else {
                root.pendingCurrent = null
            }
        }
        function onModelReset() {
            const pending = root.pendingCurrent
            root.pendingCurrent = null
            if (!pending) {
                return
            }
            const index = root.libraryModel.indexOf(pending.source, pending.runner, pending.appId)
            if (index >= 0) {
                grid.currentIndex = index
                Qt.callLater(function() { grid.positionViewAtIndex(index, GridView.Contain) })
            }
        }
    }

    GridView {
        id: grid
        objectName: "libraryGrid"
        anchors.fill: parent
        // Keep the grid width stable. Making it depend on scrollbar visibility can change the
        // column count, which changes content height and makes visibility oscillate.
        anchors.rightMargin: 20
        clip: true
        model: root.libraryModel
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 110
        cacheBuffer: height * 0.25
        reuseItems: true
        focus: true
        property real wheelTargetY: contentY
        // Filtering can move the first row without moving retained delegates.
        // Scroll coordinates must use the view's current origin, not zero.
        readonly property real minimumScrollY: originY
        readonly property real maximumScrollY: originY + Math.max(0, contentHeight - height)

        function stopWheelScroll() {
            wheelScrollAnimation.stop()
            wheelTargetY = contentY
        }
        onOriginYChanged: stopWheelScroll()
        onContentHeightChanged: stopWheelScroll()
        onHeightChanged: stopWheelScroll()
        onCurrentIndexChanged: stopWheelScroll()
        onMovementStarted: stopWheelScroll()

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
                grid.forceLayout()
                const startingY = wheelScrollAnimation.running
                                  ? grid.wheelTargetY : grid.contentY
                grid.wheelTargetY = Math.max(grid.minimumScrollY, Math.min(grid.maximumScrollY,
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
            if (currentIndex >= 0 && currentIndex < count) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onEnterPressed: function(event) {
            if (currentIndex >= 0 && currentIndex < count) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onSpacePressed: function(event) {
            if (currentIndex >= 0 && currentIndex < count) {
                root.gameActivated(currentIndex)
                event.accepted = true
            }
        }
        Keys.onUpPressed: function(event) {
            if (currentIndex >= columns) {
                currentIndex -= columns
                positionViewAtIndex(currentIndex, GridView.Contain)
            } else {
                // Top row: hand focus to the filters and toolbar above the grid.
                root.focusAboveRequested()
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

        readonly property int columns: Math.max(1, Math.min(Math.floor(8 * 100 / Preferences.coverSize), Math.floor(width / (210 * Preferences.coverSize / 100))))
        onColumnsChanged: Qt.callLater(function() { if (grid.currentIndex >= 0) grid.positionViewAtIndex(grid.currentIndex, GridView.Contain) })
        cellWidth: width / columns
        cellHeight: Math.round(cellWidth * 1.5) + 64

        delegate: Item {
            id: delegateRoot
            required property int index
            required property string title
            required property string subtitle
            required property int hours
            required property int rating
            required property int progress
            required property bool favorite
            required property string completionStatus
            required property color accentStart
            required property color accentEnd
            required property string coverMark
            required property string coverPath
            required property string source
            required property string appId

            width: grid.cellWidth
            height: grid.cellHeight

            function requestVisibleCover() {
                if (!visible || coverPath.length > 0) {
                    return
                }
                const requestedSource = source
                const requestedAppId = appId
                Qt.callLater(function() {
                    if (visible && coverPath.length === 0 && source === requestedSource
                            && appId === requestedAppId) {
                        root.coverRequested(source, appId)
                    }
                })
            }

            Component.onCompleted: requestVisibleCover()
            onAppIdChanged: requestVisibleCover()
            onVisibleChanged: requestVisibleCover()

            GameCard {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 7
                anchors.bottomMargin: 7
                title: delegateRoot.title
                // Inside a console the heading already names the system, so the launcher's
                // long core name is repetition. Drop it and let the rating and playtime have
                // the room instead.
                subtitle: Library.consoleFilter.length > 0 && delegateRoot.source.length > 0
                          ? delegateRoot.source : delegateRoot.subtitle
                hours: delegateRoot.hours
                rating: delegateRoot.rating
                progress: delegateRoot.progress
                favorite: delegateRoot.favorite
                completionStatus: delegateRoot.completionStatus
                accentStart: delegateRoot.accentStart
                accentEnd: delegateRoot.accentEnd
                coverMark: delegateRoot.coverMark
                coverPath: delegateRoot.coverPath
                current: grid.currentIndex === delegateRoot.index
                focus: current
                // A recycled delegate keeps its place in the scene but stands for no row, and
                // carries index -1. Leaving it in the focus chain let a keyboard or controller
                // move land on a card the person cannot see and open a game that is not there.
                activeFocusOnTab: delegateRoot.index >= 0

                onActiveFocusChanged: {
                    if (activeFocus && delegateRoot.index >= 0) {
                        grid.currentIndex = delegateRoot.index
                    }
                }
                onActivated: {
                    if (delegateRoot.index >= 0) {
                        root.gameActivated(delegateRoot.index)
                    }
                }
                onFavoriteToggled: {
                    if (delegateRoot.index >= 0) {
                        root.favoriteToggled(delegateRoot.index)
                    }
                }
            }
        }

        onCountChanged: {
            stopWheelScroll()
            if (count === 0) {
                currentIndex = -1
            } else if (currentIndex < 0) {
                currentIndex = 0
            } else if (currentIndex >= count) {
                currentIndex = count - 1
            }
        }
    }

    Rectangle {
        id: libraryScrollTrack
        objectName: "libraryScrollTrack"
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
            grid.contentY = grid.originY + thumbY / trackRange * contentRange
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
                return contentRange > 0
                       ? Math.max(0, Math.min(trackRange, trackRange * (grid.contentY - grid.originY) / contentRange))
                       : 0
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
            text: root.scanning ? "Scanning libraries" : root.emptyTitle
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
            id: emptyClearButton
            objectName: "emptyClearButton"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.scanning && root.filtersActive
            text: "CLEAR FILTERS"
            compact: true
            onClicked: root.clearFiltersRequested()
        }
        GlassButton {
            id: emptyRescanButton
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !root.scanning && !root.filtersActive
            text: "RESCAN"
            compact: true
            onClicked: root.refreshRequested()
        }
    }
}

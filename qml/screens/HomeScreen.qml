import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

FocusScope {
    id: root
    property bool couchMode: false
    readonly property real scaleFactor: couchMode ? 1.25 : 1
    // The Now Playing panel is where a couch player acts on a running game, so it
    // gets a real couch scale rather than the desktop scale the rest of Home uses:
    // its text has to be readable from a sofa and its stop control has to be a
    // controller-sized target.
    readonly property real panelScale: couchMode
        ? Math.max(1.25, Math.min(2.4, Math.min(width / 1920, height / 1080) * 1.3))
        : 1
    // The stop control of each running game, in panel order, so arrow-key
    // navigation can move into the panel and back out of it.
    readonly property var nowPlayingStops: {
        const stops = []
        if (typeof SessionRecorderStatus === "undefined" || !SessionRecorderStatus) return stops
        for (let index = 0; index < SessionRecorderStatus.nowPlaying.length; ++index) {
            const game = SessionRecorderStatus.nowPlaying[index]
            if (game.stoppable !== false) stops.push(index)
        }
        return stops
    }
    // Which running game's stop control holds focus, by session identity. The panel
    // model is replaced whenever a running session's elapsed time advances, which
    // destroys and recreates every delegate, so focus has to be restored by identity
    // rather than left on an item that no longer exists: without this the controller
    // loses its place about once a second while a game runs.
    property string focusedNowPlayingKey: ""
    function nowPlayingKeyAt(index) {
        if (typeof SessionRecorderStatus === "undefined" || !SessionRecorderStatus) return ""
        const game = SessionRecorderStatus.nowPlaying[index]
        return game ? String(game.pid) + ":" + String(game.path) : ""
    }
    // Moves focus to the stop control the player asked for. `step` is the direction they
    // pressed in, and `current` is the control they are standing on. Both matter: a
    // control that is mid-stop is disabled and cannot take focus, and falling back onto
    // the current one would consume the key with no visible movement, which reads as a
    // broken d-pad.
    function focusNowPlayingStop(index, step = 0, current = null) {
        if (!nowPlayingStops.length) return false
        const position = Math.max(0, Math.min(index, nowPlayingStops.length - 1))
        // The requested control first, then onward in the direction of travel, and only
        // then back the other way to find anything focusable at all.
        const candidates = step >= 0 ? [position, position + 1, position - 1]
                                     : [position, position - 1, position + 1]
        for (const candidate of candidates) {
            if (candidate < 0 || candidate >= nowPlayingStops.length) continue
            const row = nowPlayingList.itemAt(nowPlayingStops[candidate])
            const stop = row ? row.stopControl : null
            if (!stop || stop === current || !stop.visible || !stop.enabled) continue
            root.nowPlayingLeftPanel = false
            root.focusedNowPlayingKey = root.nowPlayingKeyAt(nowPlayingStops[candidate])
            stop.forceActiveFocus(Qt.TabFocusReason)
            reveal(stop)
            return true
        }
        return false
    }
    // Puts focus back on the same running game after the model was replaced. Called
    // once the delegates exist again, and a no-op unless focus was in the panel and
    // something has taken it away.
    function restoreNowPlayingFocus() {
        if (!root.focusedNowPlayingKey || !root.couchMode) return
        // Focus moved on purpose (the player went back to the toolbar or into the
        // content), so this is not a refresh having stolen it.
        if (root.nowPlayingLeftPanel) return
        if (root.nowPlayingPosition(root.Window.window.activeFocusItem) >= 0) return
        // A refresh can pass through an empty list before the same rows return, so an
        // empty panel proves nothing about the session having ended. Wait for rows.
        if (!nowPlayingStops.length) return
        for (let position = 0; position < nowPlayingStops.length; ++position) {
            const index = nowPlayingStops[position]
            if (root.nowPlayingKeyAt(index) !== root.focusedNowPlayingKey) continue
            const row = nowPlayingList.itemAt(index)
            if (row && row.stopControl && row.stopControl.visible) {
                // A control that is mid-stop is disabled and cannot take focus, so the
                // place is held rather than given up: a later refresh focuses it the
                // moment it becomes the enabled FORCE STOP, which is what puts the
                // force-stop within one press of the pad.
                if (row.stopControl.enabled)
                    row.stopControl.forceActiveFocus(Qt.TabFocusReason)
                return
            }
            return
        }
        // The rows came back without that game, so the session ended. That is a
        // deliberate outcome of stopping it, so focus goes back to the toolbar.
        root.focusedNowPlayingKey = ""
        focusHome()
    }
    // Set while a deliberate move takes focus off the panel, so the next model
    // refresh does not pull it back.
    property bool nowPlayingLeftPanel: false
    function leaveNowPlayingPanel() {
        root.nowPlayingLeftPanel = true
        root.focusedNowPlayingKey = ""
    }
    property string focusedIdentity: ""
    property string focusedAction: ""
    property bool launchBusy: false
    property string notice: ""
    property var menuGame: ({})
    property bool allPlaces: false
    signal libraryRequested()
    signal gameRequested(var game, string action)
    signal playRequested(var game)
    signal browseRequested(string kind, string value)
    readonly property var featured: Home.recent.length ? Home.recent[0] : ({})
    readonly property var nextGame: Home.queue.length ? Home.queue[0] : Home.suggestions.length ? Home.suggestions[0] : ({})
    function focusHome() { libraryButton.forceActiveFocus() }
    function focusKey(game) { return game.queueKey ? "queue:" + game.queueKey : game.identity || "" }
    function focusIdentity(identity) { restoreIdentity(identity, "") }
    function restoreIdentity(identity, action) {
        if (identity && focusKey(featured) === identity) {
            const target = action === "details" || !featuredPlay.enabled ? featuredOpen : featuredPlay
            target.forceActiveFocus(); reveal(target); return
        }
        for (const repeater of [recentTiles, queueTiles, suggestionTiles]) {
            for (let i = 0; i < repeater.count; ++i) {
                const tile = repeater.itemAt(i)
                if (tile && focusKey(tile.game) === identity) {
                    if (action === "actions") tile.focusActions()
                    else tile.focusTile()
                    return
                }
            }
        }
        focusHome()
    }
    function focusQueueActions(identity) {
        for (let i = 0; i < queueTiles.count; ++i) {
            const tile = queueTiles.itemAt(i)
            if (tile && focusKey(tile.game) === identity) { tile.focusActions(); return }
        }
    }
    function queueAction(game, operation) {
        let identity = focusKey(game)
        if (operation === "remove") {
            const queued = Home.queue
            const index = queued.findIndex(item => item.queueKey === game.queueKey)
            const neighbor = queued[index + 1] || queued[index - 1]
            identity = neighbor ? focusKey(neighbor) : ""
        }
        let okay = false
        if (operation === "add") okay = Home.enqueue(game.source, game.runner || "", game.appId)
        else if (operation === "remove") okay = Home.remove(game.queueKey)
        else okay = Home.move(game.queueKey, operation === "up" ? -1 : 1)
        notice = okay ? (operation === "add" ? "Added to Up next" : "") : (Home.error || "Could not update Up next.")
        Qt.callLater(function() { root.focusIdentity(identity) })
    }
    Connections {
        target: Home
        function onChanged() {
            if (root.visible && root.activeFocus && root.focusedIdentity !== "") {
                const identity = root.focusedIdentity
                const action = root.focusedAction
                Qt.callLater(function() {
                    const current = root.Window.window.activeFocusItem
                    if (root.visible && root.activeFocus && (!current || current.homeIdentity !== identity)) root.restoreIdentity(identity, action)
                })
            }
        }
    }
    function reveal(item) {
        if (!item || !root.Window.window.isWithin(item, content)) return
        scroll.stopWheelScroll("focus-reveal")
        const y = item.mapToItem(content, 0, 0).y
        if (y < scroll.contentY + 16) scroll.contentY = Math.max(0, y - 16)
        else if (y + item.height > scroll.contentY + scroll.height - 16)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height), y + item.height - scroll.height + 16)
    }
    // The position of a focused stop control within the panel, or -1 when focus is
    // somewhere else. Arrow handling uses it to move between running games.
    function nowPlayingPosition(current) {
        for (let position = 0; position < nowPlayingStops.length; ++position) {
            const row = nowPlayingList.itemAt(nowPlayingStops[position])
            if (row && row.stopControl && row.stopControl === current) return position
        }
        return -1
    }
    // Moves focus into the Home content below the panel, in the order the page
    // reads. Shared so Down out of the panel behaves like Down out of the toolbar.
    function focusContent() {
        if (Home.recent.length) focusIdentity(focusKey(featured))
        else if (Home.queue.length) focusIdentity(focusKey(Home.queue[0]))
        else if (Home.suggestions.length) focusIdentity(focusKey(Home.suggestions[0]))
        else browseAll.forceActiveFocus()
        return true
    }
    function navigate(current, key) {
        if (current && key === Qt.Key_Up && root.Window.window.isWithin(current, featureRow)) {
            focusHome()
            return true
        }
        // The Now Playing panel sits above everything else on Home, so in Couch Mode it
        // is the first stop below the toolbar and the way back up out of the content.
        // Desktop keeps the arrow order it had, where Down goes straight to the games,
        // and its stop control stays reachable by Tab.
        const position = root.nowPlayingPosition(current)
        if (position >= 0 && root.couchMode &&
            (key === Qt.Key_Up || key === Qt.Key_Down)) {
            const step = key === Qt.Key_Down ? 1 : -1
            const next = position + step
            // Only a control in the direction of travel counts as movement, and never the
            // one already focused: falling back onto it would swallow the key.
            if (next >= 0 && next < root.nowPlayingStops.length &&
                root.focusNowPlayingStop(next, step, current)) {
                return true
            }
            root.leaveNowPlayingPanel()
            // Up out of the first running game returns to the toolbar, which is the
            // only thing above the panel. Down out of the last one moves into the
            // content, so the pad never lands on something that does nothing.
            if (key === Qt.Key_Up) {
                focusHome()
                return true
            }
            return root.focusContent()
        }
        if (current === libraryButton && key === Qt.Key_Down) {
            if (root.couchMode && root.focusNowPlayingStop(0)) return true
            return root.focusContent()
        }
        return false
    }
    function openGame(game, action = "tile") {
        if (game.available) gameRequested(game, action)
        else notice = "Reconnect the drive or enable this game's source in Settings."
    }
    function gameCaption(game) {
        const parts = [game.system ? game.subtitle || game.source : game.source]
        if (game.playtimeSeconds > 0) parts.push(game.playtimeText + " played")
        return parts.filter(value => !!value).join(" · ")
    }

    function elapsedText(seconds) {
        const total = Math.max(0, Math.floor(seconds || 0))
        if (total < 60) return "<1m"
        const minutes = Math.floor(total / 60)
        if (minutes < 60) return minutes + "m"
        const hours = Math.floor(minutes / 60)
        return (minutes % 60) ? hours + "h " + (minutes % 60) + "m" : hours + "h"
    }
    function stopRunningGame(game) {
        if (!SessionRecorderStatus) return
        if (game.forceReady) SessionRecorderStatus.forceStopSession(game.pid, game.procStart)
        else SessionRecorderStatus.stopSession(game.pid, game.procStart)
    }

    component SectionTitle: RowLayout {
        property string title
        property string caption: ""
        property string actionText: ""
        signal actionRequested()
        Layout.fillWidth: true
        Layout.topMargin: 12
        spacing: 12
        Text { text: title; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 19 * root.scaleFactor; font.bold: true }
        Text { Layout.fillWidth: true; text: caption; color: Theme.mutedText; font.family: Theme.fontFamily; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight }
        GlassButton { visible: actionText !== ""; text: actionText; compact: true; onClicked: actionRequested() }
    }
    // Shelf dimensions depend on the available width and item count, never on
    // the implicit width of children that are themselves sized by the shelf.
    component GameShelf: Item {
        id: shelf
        property var games: []
        property bool queued: false
        property bool suggested: false
        readonly property int columns: Math.max(2, Math.min(6, Math.floor(width / (175 * root.scaleFactor))))
        readonly property real gap: 16
        readonly property real tileWidth: Math.max(1, (width - (columns - 1) * gap) / columns)
        readonly property real buttonScale: root.couchMode ? Math.max(1, Math.min(2.4, root.Window.window.height / 900)) : 1
        readonly property real tileHeight: tileWidth * 1.5 + 99 * root.scaleFactor + 34 * buttonScale + 14
        readonly property int count: tiles.count
        Layout.fillWidth: true
        implicitHeight: games.length ? Math.ceil(games.length / columns) * (tileHeight + gap) - gap : 0
        function itemAt(index) { return tiles.itemAt(index) }
        Repeater {
            id: tiles
            model: shelf.games.length
            GameTile {
                required property int index
                x: (index % shelf.columns) * (shelf.tileWidth + shelf.gap)
                y: Math.floor(index / shelf.columns) * (shelf.tileHeight + shelf.gap)
                width: shelf.tileWidth
                height: shelf.tileHeight
                game: shelf.games[index] || ({})
                queued: shelf.queued
                suggested: shelf.suggested
            }
        }
    }
    component GameTile: ColumnLayout {
        id: tile
        required property var game
        property bool queued: false
        property bool suggested: false
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 7
        function focusTile() { openButton.forceActiveFocus(); root.reveal(openButton) }
        function focusActions() { tileAction.forceActiveFocus(); root.reveal(tileAction) }
        Button {
            id: openButton
            objectName: "homeTile-" + root.focusKey(tile.game)
            property string homeIdentity: root.focusKey(tile.game)
            Layout.fillWidth: true
            implicitHeight: width * 1.5 + 69 * root.scaleFactor
            focusPolicy: Qt.StrongFocus
            Accessible.name: tile.game.title + (tile.game.available ? "" : ", unavailable")
            onActiveFocusChanged: if (activeFocus) { root.focusedIdentity = root.focusKey(tile.game); root.focusedAction = "tile" }
            onClicked: root.openGame(tile.game)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            padding: 0
            background: Rectangle { color: Theme.background; radius: 7; border.width: openButton.activeFocus ? 3 : 1; border.color: openButton.activeFocus ? Theme.accent : Qt.alpha(Theme.foreground, 0.15) }
            contentItem: Column {
                spacing: 8
                Item {
                    width: parent.width; height: width * 1.5
                    Rectangle { anchors.fill: parent; anchors.margins: 3; color: tile.game.accentStart || Theme.background
                        Text { anchors.centerIn: parent; text: (tile.game.title || "?").substring(0, 1); color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 48; visible: !art.ready }
                    }
                    CoverArtwork { id: art; anchors.fill: parent; anchors.margins: 3; source: tile.game.coverPath || "" }
                    Rectangle { visible: !tile.game.available; anchors.bottom: parent.bottom; width: parent.width; height: 30; color: Theme.darkerBackground
                        Text { anchors.centerIn: parent; text: "UNAVAILABLE"; color: Theme.mutedText; font.family: Theme.fontFamily }
                    }
                }
                Text { x: 10; width: parent.width - 20; text: tile.game.title || "Unavailable game"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 13 * root.scaleFactor; font.bold: true; maximumLineCount: 2; wrapMode: Text.Wrap; elide: Text.ElideRight; height: 39 * root.scaleFactor }
            }
        }
        Text { Layout.fillWidth: true; text: tile.suggested ? tile.game.suggestionReason : root.gameCaption(tile.game); color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * root.scaleFactor; elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.Wrap; Layout.preferredHeight: 30 * root.scaleFactor }
        GlassButton {
            id: tileAction
            property string homeIdentity: root.focusKey(tile.game)
            Layout.fillWidth: true; Layout.minimumWidth: 0
            compact: true
            maximumLabelWidth: Math.max(30, width - 24)
            text: tile.queued ? "QUEUE ACTIONS" : "+ UP NEXT"
            Accessible.name: text + " for " + tile.game.title
            onActiveFocusChanged: if (activeFocus) { root.focusedIdentity = root.focusKey(tile.game); root.focusedAction = "actions" }
            onClicked: {
                if (tile.queued) { root.menuGame = tile.game; queueMenu.anchorItem = tileAction; queueMenu.open() }
                else root.queueAction(tile.game, "add")
            }
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.darkerBackground }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.couchMode ? 32 : 24
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            Text { text: "HOME"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 25 * root.scaleFactor }
            Item { Layout.fillWidth: true }
            Flow {
                Layout.preferredWidth: Math.min(410, root.width - 160)
                Layout.preferredHeight: implicitHeight
                spacing: 6
                GlassButton { id: libraryButton; objectName: "homeLibraryButton"; text: "LIBRARY"; compact: true; onActiveFocusChanged: if (activeFocus) root.focusedIdentity = ""; onClicked: root.libraryRequested() }
                GlassButton { text: "SEARCH"; compact: true; onClicked: root.Window.window.openLibrarySearch() }
                GlassButton { text: "SETTINGS"; compact: true; onClicked: root.Window.window.diagnosticsOpen = true }
                GlassButton { text: root.couchMode ? "DESKTOP" : "COUCH"; compact: true; onClicked: root.Window.window.setCouchMode(!root.couchMode) }
            }
        }
        Flickable {
            id: scroll
            objectName: "homeList"
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight + 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            property real wheelTargetY: contentY
            property real wheelDirection: 0
            property bool wheelActive: false
            property real wheelPosition: 0
            onWheelPositionChanged: if (wheelActive) contentY = wheelPosition
            readonly property real maximumScrollY: originY + Math.max(0, contentHeight - height)
            function traceScroll(reason) {
                if (ScrollTraceEnabled) console.info("scroll-trace", Date.now(), reason,
                    "y", contentY, "target", wheelTargetY, "running", wheelAnimation.running)
            }
            onContentYChanged: traceScroll("position")
            function stopWheelScroll(reason) {
                traceScroll("stop:" + (reason || "explicit"))
                wheelActive = false
                wheelPosition = contentY
                wheelTargetY = contentY
                wheelDirection = 0
            }
            onMovementStarted: stopWheelScroll("movement-started")
            onContentHeightChanged: stopWheelScroll("content-height")
            onHeightChanged: stopWheelScroll("viewport-height")
            onVisibleChanged: stopWheelScroll("visibility")
            Behavior on wheelPosition {
                enabled: scroll.wheelActive
                SmoothedAnimation {
                    id: wheelAnimation
                    velocity: 1000 * root.scaleFactor
                    maximumEasingTime: 80
                    reversingMode: SmoothedAnimation.Immediate
                }
            }
            WheelHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                blocking: true
                onWheel: function(event) {
                    scroll.traceScroll("wheel:" + event.angleDelta.y + ":" + event.pixelDelta.y)
                    const pixels = event.pixelDelta.y !== 0
                    const travel = pixels ? event.pixelDelta.y : event.angleDelta.y / 120 * 100 * root.scaleFactor
                    if (travel === 0) return
                    const direction = Math.sign(travel)
                    const start = wheelAnimation.running && direction === scroll.wheelDirection
                                ? scroll.wheelTargetY : scroll.contentY
                    const destination = Math.max(scroll.originY, Math.min(scroll.maximumScrollY, start - travel))
                    scroll.wheelDirection = direction
                    // Retarget the running animation without restarting its easing
                    // curve. Preserve velocity across consecutive mouse notches.
                    if (pixels || Preferences.reducedMotion) {
                        scroll.stopWheelScroll()
                        scroll.wheelTargetY = destination
                        scroll.contentY = destination
                    } else {
                        if (!scroll.wheelActive) scroll.wheelPosition = scroll.contentY
                        scroll.wheelActive = true
                        scroll.wheelTargetY = destination
                        scroll.wheelPosition = destination
                    }
                    event.accepted = true
                }
            }
            ScrollBar.vertical: ScrollBar {
                onPressedChanged: if (pressed) scroll.stopWheelScroll()
            }
            ColumnLayout {
                id: content
                width: Math.min(scroll.width - 12, 1480 * root.scaleFactor)
                x: (scroll.width - width) / 2
                spacing: 18
                Text { text: Home.gameCount + " games ready to explore"; color: Theme.mutedText; font.family: Theme.fontFamily }
                Text { Layout.fillWidth: true; visible: Home.error !== "" || root.notice !== ""; text: Home.error || root.notice; color: Theme.brightForeground; font.family: Theme.fontFamily; wrapMode: Text.Wrap }
                Rectangle {
                    objectName: "homeNowPlayingSection"
                    Layout.fillWidth: true
                    visible: !!SessionRecorderStatus && SessionRecorderStatus.nowPlaying.length > 0
                    Layout.preferredHeight: nowPlayingColumn.implicitHeight + (root.couchMode ? 40 : 28)
                    radius: root.couchMode ? 16 : 10
                    color: Qt.alpha(Theme.accent, 0.10)
                    border.color: Qt.alpha(Theme.accent, root.couchMode ? 0.5 : 0.35)
                    border.width: root.couchMode ? 2 : 1
                    ColumnLayout {
                        id: nowPlayingColumn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: root.couchMode ? 20 : 14
                        spacing: root.couchMode ? 14 : 10
                        Text {
                            text: "NOW PLAYING"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: (root.couchMode ? 16 : 12) * root.panelScale
                            font.bold: root.couchMode
                            font.letterSpacing: root.couchMode ? 1.5 : 0
                        }
                        Repeater {
                            id: nowPlayingList
                            objectName: "homeNowPlayingList"
                            model: SessionRecorderStatus ? SessionRecorderStatus.nowPlaying : []
                            // The model is replaced whenever a running session's
                            // elapsed time advances, which recreates these delegates.
                            // Focus is put back on the same game once they exist again,
                            // so a controller does not lose its place mid-session.
                            onModelChanged: Qt.callLater(root.restoreNowPlayingFocus)
                            RowLayout {
                                id: nowPlayingRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: root.couchMode ? 18 : 10
                                // The row exposes its stop control so navigation can
                                // reach it without searching the whole scene.
                                readonly property Item stopControl: nowPlayingStop
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: root.couchMode ? 4 : 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name || "Running game"
                                        color: Theme.brightForeground
                                        font.family: Theme.fontFamily
                                        font.bold: true
                                        font.pixelSize: (root.couchMode ? 24 : 14) * root.panelScale
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.source ? modelData.source + " · " : "")
                                              + root.elapsedText(modelData.elapsedSeconds)
                                              + (modelData.stopping ? " · closing" : "")
                                              + (modelData.stoppable === false ? " · from window title" : "")
                                        color: Theme.mutedText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: (root.couchMode ? 17 : 11) * root.panelScale
                                        elide: Text.ElideRight
                                    }
                                }
                                GlassButton {
                                    id: nowPlayingStop
                                    objectName: "nowPlayingStop_" + modelData.pid
                                    compact: !root.couchMode
                                    // A couch player confirms with A, so the control the
                                    // controller lands on has to be the stop itself.
                                    displayScale: root.couchMode ? Math.max(1.25, root.panelScale) : 1
                                    // A session recorded from a window title has no verified
                                    // process identity, so it is listed without a stop control
                                    // rather than offering to signal a process we cannot prove.
                                    visible: modelData.stoppable !== false
                                    text: modelData.forceReady ? "FORCE STOP" : modelData.stopping ? "STOPPING…" : "STOP"
                                    enabled: !modelData.stopping || modelData.forceReady
                                    Accessible.name: text + " " + (modelData.name || "")
                                    Accessible.description: modelData.stopping && !modelData.forceReady
                                                            ? "Waiting for the game to close"
                                                            : ""
                                    onClicked: root.stopRunningGame(modelData)
                                }
                            }
                        }
                        Text {
                            objectName: "nowPlayingHint"
                            Layout.fillWidth: true
                            visible: root.couchMode && nowPlayingStops.length > 0
                            // The button is drawn as the pad's own glyph everywhere else
                            // in Couch Mode, so naming a letter here would tell a
                            // PlayStation player to press a button they do not have.
                            text: nowPlayingStops.length > 1
                                  ? "Choose a game with up and down, then press "
                                    + Controller.primaryGlyph + " to stop it."
                                  : "Press " + Controller.primaryGlyph + " to stop this game."
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: (root.couchMode ? 15 : 11) * root.panelScale
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Rectangle {
                    objectName: "homeFeaturedSection"
                    Layout.fillWidth: true
                    Layout.preferredHeight: featureRow.implicitHeight + 32
                    visible: Home.recent.length > 0
                    radius: 10
                    gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Qt.alpha(root.featured.accentStart || Theme.accent, 0.24) } GradientStop { position: 1; color: Theme.background } }
                    border.color: Qt.alpha(Theme.foreground, 0.14)
                    RowLayout {
                        id: featureRow
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
                        spacing: content.width < 650 ? 16 : 28
                        Rectangle {
                            Layout.preferredWidth: content.width < 650 ? 105 : 154 * root.scaleFactor
                            Layout.preferredHeight: width * 1.5
                            color: root.featured.accentStart || Theme.background
                            Text { anchors.centerIn: parent; text: (root.featured.title || "?").substring(0, 1); font.family: Theme.fontFamily; font.pixelSize: 48; color: Theme.brightForeground; visible: !featuredCover.ready }
                            CoverArtwork { id: featuredCover; anchors.fill: parent; source: root.featured.coverPath || "" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 12
                            Text { text: "JUMP BACK IN"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 12 * root.scaleFactor }
                            Text { Layout.fillWidth: true; text: root.featured.title || ""; color: Theme.brightForeground; font.family: Theme.fontFamily; font.bold: true; font.pixelSize: (content.width < 650 ? 22 : 32) * root.scaleFactor; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: root.gameCaption(root.featured); color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap }
                            Text { visible: root.featured.lastPlayed > 0; text: root.featured.lastPlayed > 0 ? "Last played " + Qt.formatDateTime(new Date(root.featured.lastPlayed * 1000), "MMM d, yyyy") : ""; color: Theme.mutedText; font.family: Theme.fontFamily }
                            Flow {
                                Layout.fillWidth: true; Layout.preferredHeight: implicitHeight; spacing: 8
                                GlassButton {
                                    id: featuredPlay
                                    property string homeIdentity: root.focusKey(root.featured)
                                    objectName: "homeFeaturedPlay"
                                    text: root.launchBusy ? "OPENING..." : "PLAY"
                                    primary: true
                                    enabled: !!root.featured.available
                                    Accessible.description: root.launchBusy ? "Launch request in progress" : ""
                                    onActiveFocusChanged: if (activeFocus) {
                                        root.focusedIdentity = root.focusKey(root.featured)
                                        root.focusedAction = "play"
                                    }
                                    onClicked: if (!root.launchBusy) root.playRequested(root.featured)
                                }
                                GlassButton {
                                    id: featuredOpen
                                    property string homeIdentity: root.focusKey(root.featured)
                                    objectName: "homeFeaturedOpen"
                                    text: "DETAILS"
                                    onActiveFocusChanged: if (activeFocus) {
                                        root.focusedIdentity = root.focusKey(root.featured)
                                        root.focusedAction = "details"
                                    }
                                    onClicked: root.openGame(root.featured, "details")
                                }
                                GlassButton { text: "+ UP NEXT"; onClicked: root.queueAction(root.featured, "add") }
                            }
                        }
                        Rectangle { visible: content.width > 1000 && !!root.nextGame.identity; Layout.preferredWidth: 1; Layout.preferredHeight: 160; color: Qt.alpha(Theme.foreground, 0.15) }
                        ColumnLayout {
                            visible: content.width > 1000 && !!root.nextGame.identity
                            Layout.preferredWidth: 280 * root.scaleFactor
                            Layout.maximumWidth: 280 * root.scaleFactor
                            spacing: 12
                            Text { text: Home.queue.length ? "NEXT IN YOUR QUEUE" : "ON YOUR RADAR"; color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 12 * root.scaleFactor }
                            Text { Layout.fillWidth: true; text: root.nextGame.title || ""; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 22 * root.scaleFactor; font.bold: true; maximumLineCount: 2; wrapMode: Text.Wrap; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: Home.queue.length ? "Picked by you. Ready when you are." : root.nextGame.suggestionReason || ""; color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap }
                            GlassButton { text: "EXPLORE GAME"; onClicked: root.openGame(root.nextGame) }
                        }
                    }
                }
                SectionTitle { title: "Continue playing"; caption: "Recently played"; actionText: "VIEW ALL"; onActionRequested: root.browseRequested("recent", ""); visible: Home.recent.length > 1 }
                GameShelf {
                    id: recentTiles
                    objectName: "homeRecentShelf"
                    games: Home.recent.slice(1, 7)
                    visible: games.length > 0
                }
                SectionTitle { title: "Up next"; caption: Home.queue.length ? Home.queue.length + " in your queue" : "Your own shortlist" }
                Text { Layout.fillWidth: true; visible: !Home.queue.length; text: "Something catch your eye? Add it to Up next and keep your next session ready."; color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap }
                GameShelf {
                    id: queueTiles
                    objectName: "homeQueueShelf"
                    games: Home.queue
                    queued: true
                    visible: games.length > 0
                }
                SectionTitle { title: "Find your next game"; caption: "From your library"; visible: Home.suggestions.length > 0 }
                GameShelf {
                    id: suggestionTiles
                    objectName: "homeSuggestionShelf"
                    games: Home.suggestions
                    suggested: true
                    visible: games.length > 0
                }
                SectionTitle { title: "Quick access"; caption: "" }
                Flow {
                    Layout.fillWidth: true; Layout.preferredHeight: implicitHeight; spacing: 8
                    GlassButton { id: browseAll; objectName: "homeBrowseAll"; text: "ALL GAMES"; onClicked: root.browseRequested("all", "") }
                    GlassButton { text: "FAVORITES"; onClicked: root.browseRequested("favorites", "") }
                    GlassButton { text: "BACKLOG"; onClicked: root.browseRequested("backlog", "") }
                    Repeater {
                        model: root.allPlaces ? Home.shortcuts : Home.shortcuts.slice(0, 5)
                        GlassButton {
                            required property var modelData
                            text: modelData.title + " · " + modelData.count
                            maximumLabelWidth: Math.min(220, content.width - 40)
                            onClicked: root.browseRequested(modelData.kind, modelData.value)
                        }
                    }
                    Repeater {
                        model: root.allPlaces ? Library.savedFilters : Library.savedFilters.slice(0, 3)
                        GlassButton {
                            required property var modelData
                            text: modelData.name
                            Accessible.name: "Saved view: " + modelData.name
                            maximumLabelWidth: Math.min(220, content.width - 40)
                            onClicked: root.browseRequested("saved", modelData.id)
                        }
                    }
                    GlassButton { visible: Home.shortcuts.length > 5 || Library.savedFilters.length > 3; text: root.allPlaces ? "FEWER PLACES" : "ALL PLACES"; onClicked: root.allPlaces = !root.allPlaces }
                }
                Text { Layout.fillWidth: true; visible: Home.gameCount === 0; text: "Your Home starts with your games. Add a source or ROM folder in Settings, then play something to make this space yours."; color: Theme.mutedText; font.family: Theme.fontFamily; wrapMode: Text.Wrap }
            }
        }
    }
    ActionMenu {
        id: queueMenu
        objectName: "homeQueueMenu"
        host: root.Window.window
        anchorItem: libraryButton
        title: root.menuGame.title || "UP NEXT"
        MenuAction { Layout.fillWidth: true; text: "MOVE EARLIER"; enabled: Home.queue.findIndex(game => game.queueKey === root.menuGame.queueKey) > 0; onClicked: queueMenu.invoke(function() { root.queueAction(root.menuGame, "up") }) }
        MenuAction { Layout.fillWidth: true; text: "MOVE LATER"; enabled: Home.queue.findIndex(game => game.queueKey === root.menuGame.queueKey) < Home.queue.length - 1; onClicked: queueMenu.invoke(function() { root.queueAction(root.menuGame, "down") }) }
        MenuAction { Layout.fillWidth: true; text: "REMOVE"; onClicked: queueMenu.invoke(function() { root.queueAction(root.menuGame, "remove") }) }
    }
}

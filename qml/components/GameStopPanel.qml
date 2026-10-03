import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Confirms and applies a stop, for one game or for every game that has
// something attributable. Everything it shows comes from GameStop, so the list
// on screen is the list that will be signalled.
ActionMenu {
    id: stopPanel
    property string namePrefix: ""
    property string heading: "STOP GAME"
    // "one" for a single game, "all" for the global action.
    property string mode: "one"
    property var game: ({})
    property var targets: []
    property var notes: []
    property var games: []
    property bool pending: false
    property bool waitingForExitScan: false
    readonly property bool scanning: mode === "all" && typeof GameStop !== "undefined" && !!GameStop && GameStop.scanning
    property bool leaveGameModeAfterStop: false
    readonly property real textScale: leaveGameModeAfterStop && host.couchMode
                                     ? Math.max(1, Math.min(2, host.height / 900)) : 1
    headerTextScale: textScale
    property string resultMessage: ""
    property var resultLines: []

    objectName: namePrefix + "gameStopPanel"
    title: heading
    showCloseButton: false
    fixedHeader: true
    preferredWidth: 460
    // The safe action, not the one that signals: the menu focuses this on open.
    initialFocus: cancelStop

    function refresh() {
        const available = typeof GameStop !== "undefined" && GameStop
        if (mode === "all") {
            targets = []
            notes = []
            games = available ? GameStop.runningGames : []
            if (available) GameStop.refreshLiveGames()
            return
        }
        games = []
        targets = available ? GameStop.preview(game) : []
        notes = available ? GameStop.notesFor(game) : []
    }

    function begin(gameRow) {
        mode = "one"
        heading = "STOP GAME"
        game = gameRow || ({})
        resultMessage = ""
        resultLines = []
        pending = false
        refresh()
        open()
        Qt.callLater(cancelStop.forceActiveFocus)
    }

    function beginAll() {
        // A shortcut can hide a confirmed stop while its worker is still
        // running. Reopening its dialog must keep that completion attached.
        if (pending && leaveGameModeAfterStop && GameMode.hasSession) {
            open()
            return
        }
        mode = "all"
        heading = leaveGameModeAfterStop ? "STOP GAMES AND LEAVE" : "STOP ALL GAMES"
        game = ({})
        resultMessage = ""
        resultLines = []
        pending = false
        refresh()
        open()
        Qt.callLater(cancelStop.forceActiveFocus)
    }

    function hasSomethingToStop() {
        if (mode === "all")
            return games.length > 0
        return targets.length > 0
    }

    Connections {
        target: typeof GameStop !== "undefined" ? GameStop : null
        function onLiveGamesChanged() {
            if (GameStop.scanning) return
            if (stopPanel.mode === "all") stopPanel.games = GameStop.runningGames
            if (!stopPanel.waitingForExitScan) return
            stopPanel.waitingForExitScan = false
            stopPanel.pending = false
            if (GameStop.runningGames.length === 0) {
                stopPanel.close()
                GameMode.exit()
            } else {
                stopPanel.resultMessage = "Games are still running. Game Mode remains active."
                Qt.callLater(doneStop.forceActiveFocus)
            }
        }
        function onFinished(okay, message, lines) {
            if (!stopPanel.pending) return
            stopPanel.pending = false
            if (okay && stopPanel.leaveGameModeAfterStop && GameMode.hasSession) {
                stopPanel.pending = true
                stopPanel.waitingForExitScan = true
                GameStop.refreshLiveGames(true)
                return
            }
            stopPanel.resultMessage = message
            stopPanel.resultLines = lines
            if (stopPanel.opened)
                Qt.callLater(doneStop.forceActiveFocus)
        }
    }

    Connections {
        target: GameMode
        function onStateChanged() {
            if (!GameMode.hasSession) {
                stopPanel.waitingForExitScan = false
                stopPanel.pending = false
            }
        }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: 12 * stopPanel.textScale
        lineHeight: 1.2
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
                 && (stopPanel.hasSomethingToStop() || stopPanel.mode === "all"
                     || stopPanel.notes.length === 0)
        text: stopPanel.scanning ? "Checking running games…" : stopPanel.hasSomethingToStop()
              ? "These processes will be asked to close, and forced after a few seconds if they do not:"
              : (stopPanel.mode === "all"
                 ? "No game on this machine can be attributed to something running."
                 : "Nothing attributable to this game is running.")
    }

    Text {
        Layout.fillWidth: true
        visible: stopPanel.mode === "one" && stopPanel.targets.length > 0
                 && !stopPanel.pending && stopPanel.resultMessage.length === 0
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 13 * stopPanel.textScale
        font.weight: Font.DemiBold
        text: stopPanel.game.title || ""
    }

    Text {
        Layout.fillWidth: true
        visible: stopPanel.mode === "one" && stopPanel.game.source === "Steam"
                 && !stopPanel.pending && stopPanel.resultMessage.length === 0
        wrapMode: Text.Wrap
        text: "Steam stays open. Stop targets only processes attributed to this game."
        color: Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: UiMetrics.body * stopPanel.textScale
    }

    Repeater {
        model: stopPanel.mode === "all" ? [] : stopPanel.targets
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12 * stopPanel.textScale
            text: "•  " + modelData
        }
    }

    Repeater {
        model: stopPanel.mode === "all" ? stopPanel.games : []
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 13 * stopPanel.textScale
                font.weight: Font.DemiBold
                text: modelData.title || modelData.appId || ""
            }
            Repeater {
                model: modelData.lines || []
                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * stopPanel.textScale
                    text: "•  " + modelData
                }
            }
        }
    }

    Repeater {
        model: stopPanel.notes
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12 * stopPanel.textScale
            text: modelData
        }
    }

    Text {
        Layout.fillWidth: true
        visible: stopPanel.pending
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 12 * stopPanel.textScale
        font.weight: Font.DemiBold
        text: "Stopping…"
    }

    MenuAction {
        id: cancelStop
        objectName: stopPanel.namePrefix + "cancelStop"
        Layout.fillWidth: true
        text: "CANCEL"
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
        onClicked: {
            stopPanel.close()
            stopPanel.doneControl.forceActiveFocus()
        }
    }

    MenuAction {
        objectName: stopPanel.namePrefix + "confirmStop"
        enabled: !stopPanel.scanning
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length === 0
                 && stopPanel.hasSomethingToStop()
        text: stopPanel.leaveGameModeAfterStop ? "STOP GAMES AND LEAVE"
              : stopPanel.mode === "all" ? "STOP ALL GAMES" : "STOP THIS GAME"
        onClicked: {
            stopPanel.pending = true
            const started = stopPanel.mode === "all" ? GameStop.stopListedGames(stopPanel.games) : GameStop.stop(stopPanel.game)
            if (!started) {
                stopPanel.pending = false
                stopPanel.refresh()
                stopPanel.resultMessage = "No stop was started. The game may have closed, or another stop is still running."
                Qt.callLater(doneStop.forceActiveFocus)
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length > 0
        wrapMode: Text.Wrap
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: 12 * stopPanel.textScale
        font.weight: Font.DemiBold
        text: stopPanel.resultMessage
    }

    Repeater {
        model: stopPanel.resultMessage.length > 0 ? stopPanel.resultLines : []
        Text {
            required property var modelData
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12 * stopPanel.textScale
            text: modelData
        }
    }

    MenuAction {
        id: doneStop
        objectName: stopPanel.namePrefix + "dismissStop"
        Layout.fillWidth: true
        visible: !stopPanel.pending && stopPanel.resultMessage.length > 0
        text: "DONE"
        onClicked: {
            stopPanel.resultMessage = ""
            stopPanel.resultLines = []
            stopPanel.close()
            stopPanel.doneControl.forceActiveFocus()
        }
    }

    onClosed: {
        // Hiding the controls on park does not cancel a confirmed Stop + Leave.
        if (!(pending && leaveGameModeAfterStop && GameMode.hasSession))
            pending = false
        resultMessage = ""
        resultLines = []
    }
}

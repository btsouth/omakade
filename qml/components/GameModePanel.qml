import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ActionMenu {
    id: panel
    // The overlay shows a second instance over the game, so its controls carry a prefix to
    // keep them apart from the in-window ones in the object tree.
    property string namePrefix: ""
    // True when this instance lives on the Game Mode overlay surface: the first entry returns
    // to the game rather than the library, and the stop dialog opens without the gap that
    // would otherwise hide the overlay between the two menus.
    property bool overlayMode: false
    objectName: namePrefix + "gameModeControls"
    title: "GAME MODE"
    fixedHeader: true
    showCloseButton: false
    showHeaderCloseButton: false
    preferredWidth: 520
    headerTextScale: textScale
    initialFocus: backButton
    readonly property var liveGames: typeof GameStop !== "undefined" && GameStop ? GameStop.runningGames : []
    readonly property bool scanning: typeof GameStop !== "undefined" && GameStop && GameStop.scanning
    property int workspaceGames: 0
    readonly property bool untrackedGame: (Launcher.gameRunning || workspaceGames > 0)
                                         && liveGames.length === 0
    readonly property real textScale: host.couchMode ? Math.max(1, Math.min(2, host.height / 900)) : 1

    function refreshGames() {
        if (typeof GameStop !== "undefined" && GameStop) GameStop.refreshLiveGames()
    }
    function openControls() {
        workspaceGames = 0
        open()
        refreshGames()
        GameMode.refresh()
        GameMode.checkWorkspace()
    }
    // Closes this panel and the stop dialog it may have opened, so the overlay surface can
    // hide without leaving either of them thinking they are still open.
    function closeAll() {
        stopAndLeave.close()
        panel.close()
    }
    function ownsMenu(menu) {
        return menu === panel || menu === stopAndLeave
    }

    GameStopPanel {
        id: stopAndLeave
        namePrefix: panel.namePrefix + "gameMode"
        host: panel.host
        anchorItem: panel.anchorItem
        leaveGameModeAfterStop: true
    }

    Timer {
        interval: 2000
        running: panel.opened
        repeat: true
        onTriggered: panel.refreshGames()
    }
    Connections {
        target: panel
        function onClosed() { Qt.callLater(panel.host.focusCurrentSurface) }
    }
    Connections {
        target: GameMode
        function onWorkspaceChecked(otherWindows) { panel.workspaceGames = otherWindows }
    }

    Text {
        Layout.fillWidth: true
        text: GameMode.sessionDisplayLabel
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 16 * panel.textScale
        font.weight: Font.DemiBold
        color: Theme.foreground
    }
    Text {
        Layout.fillWidth: true
        text: "Sound: " + GameMode.sessionSoundLabel
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 12 * panel.textScale
        color: Theme.mutedText
    }
    Text {
        Layout.fillWidth: true
        text: GameMode.statusText || (panel.scanning ? "Checking running games…" : panel.liveGames.length > 0
              ? panel.liveGames.map(game => game.title || game.appId).join(", ")
                + (panel.liveGames.length === 1 ? " is running." : " are running.")
              : panel.untrackedGame
                ? "Game activity is detected, but no safe stop target is available."
                : "Your display, sound and notification changes will be restored when you leave.")
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 12 * panel.textScale
        color: Theme.foreground
    }
    MenuAction {
        id: backButton
        objectName: panel.namePrefix + "gameModeBackButton"
        text: panel.overlayMode ? "BACK TO GAME" : "BACK TO LIBRARY"
        enabled: !GameMode.busy
        onClicked: panel.close()
    }
    MenuAction {
        objectName: panel.namePrefix + "gameModeStopAndLeaveButton"
        text: "STOP GAMES AND LEAVE…"
        visible: panel.liveGames.length > 0
        enabled: !GameMode.busy && !panel.scanning
        // On the overlay the stop dialog must open in the same call: closing this panel first
        // would let the overlay hide before the confirmation appears.
        onClicked: panel.overlayMode ? stopAndLeave.beginAll() : panel.invoke(stopAndLeave.beginAll)
    }
    MenuAction {
        id: leaveButton
        objectName: panel.namePrefix + "gameModeLeaveButton"
        text: "RETURN TO DESKTOP"
        enabled: !GameMode.busy && GameMode.active
        onClicked: {
            GameMode.park()
            panel.close()
        }
    }
    MenuAction {
        objectName: panel.namePrefix + "gameModeEndButton"
        text: "END GAME MODE"
        visible: panel.liveGames.length === 0
        enabled: !GameMode.busy && !panel.scanning
        onClicked: {
            panel.close()
            GameMode.exit()
        }
    }
    Text {
        Layout.fillWidth: true
        text: "Return to Desktop keeps your library, selection and place in the session. "
              + "Super + Ctrl + G resumes it. Running games stay on their workspace with sound muted; "
              + "gameplay may continue unless the game pauses itself. Ending Game Mode restores the desktop "
              + "and releases the session; it only stops games through Stop Games and Leave."
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 11 * panel.textScale
        color: Theme.mutedText
    }
}

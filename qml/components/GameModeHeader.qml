import QtQuick
import QtQuick.Layouts

// The header Home and the library share in Game Mode: the same destinations in the same
// places as the desktop header, with the page on screen marked and Leave last.
RowLayout {
    id: header
    property string current: "library"
    property real uiScale: 1
    // Pages with their own arrow and Tab handling set these; others leave them unset.
    property var navigate: null
    property var tabTarget: null
    // Called with a button whose focus, visibility or availability changed.
    property var buttonChanged: null
    property string homeName: ""
    property string libraryName: ""
    property string statsName: ""
    property string settingsName: ""
    property string leaveName: ""
    property alias homeButton: homeTab
    property alias libraryButton: libraryTab
    property alias statsButton: statsTab
    property alias settingsButton: settingsTab
    property alias leaveButton: leaveTab
    signal homeRequested()
    signal libraryRequested()
    signal statsRequested()
    signal settingsRequested()
    signal leaveRequested()
    Layout.fillWidth: true
    spacing: 7 * header.uiScale

    component HeaderButton: GlassButton {
        id: button
        compact: true
        displayScale: Math.max(1, header.uiScale * 1.18)
        KeyNavigation.tab: header.tabTarget ? header.tabTarget(button, true) : null
        KeyNavigation.backtab: header.tabTarget ? header.tabTarget(button, false) : null
        Keys.onPressed: function(event) { if (header.navigate) header.navigate(button, event) }
        onActiveFocusChanged: if (header.buttonChanged) header.buttonChanged(button)
        onVisibleChanged: if (header.buttonChanged) header.buttonChanged(button)
        onEnabledChanged: if (header.buttonChanged) header.buttonChanged(button)
    }

    Row {
        spacing: 12 * header.uiScale
        Layout.alignment: Qt.AlignVCenter
        Layout.rightMargin: 12 * header.uiScale

        Image {
            width: 42 * header.uiScale
            height: width
            source: "qrc:/icons/resources/icons/io.github.tsouth89.Omakade.svg"
            sourceSize: Qt.size(96, 96)
            Accessible.ignored: true
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                text: "OMAKADE"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 18 * header.uiScale
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
            Text {
                // Fullscreen without a session is what a start that failed falls back to.
                text: GameMode.hasSession ? "GAME MODE" : "FULLSCREEN"
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 9 * header.uiScale
                font.weight: Font.DemiBold
                font.letterSpacing: 1.4
            }
        }
    }

    HeaderButton {
        id: homeTab
        objectName: header.homeName
        text: "HOME"
        selected: header.current === "home"
        onClicked: header.homeRequested()
    }
    HeaderButton {
        id: libraryTab
        objectName: header.libraryName
        text: "LIBRARY"
        selected: header.current === "library"
        onClicked: header.libraryRequested()
    }
    HeaderButton {
        id: statsTab
        objectName: header.statsName
        text: "STATS"
        selected: header.current === "stats"
        onClicked: header.statsRequested()
    }
    Item { Layout.fillWidth: true }
    HeaderButton {
        id: settingsTab
        objectName: header.settingsName
        text: "SETTINGS"
        onClicked: header.settingsRequested()
    }
    HeaderButton {
        id: leaveTab
        objectName: header.leaveName
        // With no game running this ends Game Mode; with one it opens the controls.
        text: GameMode.hasSession ? "LEAVE" : "DESKTOP"
        enabled: !GameMode.busy
        onClicked: header.leaveRequested()
    }
}

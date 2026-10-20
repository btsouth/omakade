import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property string current: "library"
    property string objectNamePrefix: ""
    property Item contentFocusTarget: null
    property alias homeButton: homeTab
    property alias libraryButton: libraryTab
    property alias statsButton: statsTab
    property alias settingsButton: settingsTab
    property alias couchModeButton: couchTab
    signal homeRequested()
    signal libraryRequested()
    signal statsRequested()
    signal settingsRequested()
    signal couchRequested()
    Layout.fillWidth: true
    spacing: 18

    Row {
        spacing: 11
        Layout.alignment: Qt.AlignVCenter
        Image {
            width: 34
            height: 34
            source: "qrc:/icons/resources/icons/io.github.tsouth89.Omakade.svg"
            sourceSize: Qt.size(68, 68)
            fillMode: Image.PreserveAspectFit
            Accessible.ignored: true
        }
        Column {
            visible: root.width >= 700
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                text: "OMAKADE"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.Bold
                font.letterSpacing: 1.5
            }
            Text {
                text: Theme.themeName.toUpperCase()
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 8
                font.letterSpacing: 0.7
            }
        }
    }

    GlassButton {
        id: homeTab
        objectName: root.objectNamePrefix === "" ? "openHomeButton" : root.objectNamePrefix + "OpenHomeButton"
        property Item controllerRightTarget: libraryTab
        property Item controllerDownTarget: root.contentFocusTarget
        text: "HOME"
        compact: true
        selected: root.current === "home"
        KeyNavigation.right: libraryTab
        onClicked: root.homeRequested()
    }
    GlassButton {
        id: libraryTab
        objectName: root.objectNamePrefix === "" ? "libraryDestinationButton" : root.objectNamePrefix + "LibraryDestinationButton"
        property Item controllerLeftTarget: homeTab
        property Item controllerRightTarget: statsTab
        property Item controllerDownTarget: root.contentFocusTarget
        text: "LIBRARY"
        compact: true
        selected: root.current === "library"
        KeyNavigation.left: homeTab
        KeyNavigation.right: statsTab
        onClicked: root.libraryRequested()
    }
    GlassButton {
        id: statsTab
        objectName: root.objectNamePrefix === "" ? "statsDestinationButton" : root.objectNamePrefix + "StatsDestinationButton"
        property Item controllerLeftTarget: libraryTab
        property Item controllerRightTarget: settingsTab
        property Item controllerDownTarget: root.contentFocusTarget
        text: "STATS"
        compact: true
        selected: root.current === "stats"
        KeyNavigation.left: libraryTab
        KeyNavigation.right: settingsTab
        onClicked: root.statsRequested()
    }
    Item { Layout.fillWidth: true }
    GlassButton {
        id: settingsTab
        objectName: root.objectNamePrefix === "" ? "settingsButton" : root.objectNamePrefix + "SettingsButton"
        property Item controllerLeftTarget: statsTab
        property Item controllerRightTarget: couchTab
        property Item controllerDownTarget: root.contentFocusTarget
        text: "SETTINGS"
        compact: true
        KeyNavigation.left: statsTab
        KeyNavigation.right: couchTab
        onClicked: root.settingsRequested()
    }
    GlassButton {
        id: couchTab
        objectName: root.objectNamePrefix === "" ? "couchModeButton" : root.objectNamePrefix + "CouchModeButton"
        property Item controllerLeftTarget: settingsTab
        property Item controllerDownTarget: root.contentFocusTarget
        // Fullscreen Game Mode on this display. Super + Ctrl + G uses the configured one.
        text: "GAME MODE"
        compact: true
        KeyNavigation.left: settingsTab
        onClicked: root.couchRequested()
    }
}

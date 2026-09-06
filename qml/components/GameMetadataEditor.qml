import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var game
    property bool couchMode: false
    property real uiScale: 1
    property bool editing: false
    signal textEntryRequested(var target, string title, bool password, string placeholder)
    Layout.fillWidth: true
    spacing: 10
    visible: Metadata !== null && !game.isPortal
    readonly property string gameKey: game.metadataKey || ""
    onGameKeyChanged: { editing = false; if (Metadata) Metadata.inspect(game) }
    Component.onCompleted: if (Metadata) Metadata.inspect(game)
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: "RATING & COVER ART"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale }
        GlassButton { compact: true; text: root.editing ? "DONE" : "IDENTIFY / ARTWORK"; onClicked: root.editing = !root.editing }
    }
    Text {
        Layout.fillWidth: true
        text: !Metadata ? "" : Metadata.current.rating >= 0
              ? "IGDB  " + Metadata.current.rating + " / 100 · " + Metadata.current.ratingCount + " ratings"
              : "No rating available"
        color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 10; visible: root.editing
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: Metadata ? (Metadata.current.title || root.game.title) + (Metadata.current.year ? " (" + Metadata.current.year + ")" : "") + " · " + (Metadata.current.matchStatus || "Not identified") : ""
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: titleSearch; Layout.fillWidth: true; text: root.game.title || ""
                placeholderTextColor: Theme.mutedText
                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                        border.width: titleSearch.activeFocus ? 2 : 1
                        border.color: titleSearch.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                    }
                    property bool controllerNavigation: root.couchMode
                Accessible.name: "Game title for identification"
                color: Theme.foreground; font.family: Theme.fontFamily
                Keys.onReturnPressed: event => { if (root.couchMode) { root.textEntryRequested(titleSearch, "GAME TITLE", false, "Search title"); event.accepted = true } else Metadata.search(text) }
            }
            GlassButton { compact: true; text: "SEARCH IGDB"; enabled: Metadata && !Metadata.busy && Insights && Insights.configured; onClicked: Metadata.search(titleSearch.text) }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            GlassButton { compact: true; text: "NOT THIS GAME"; enabled: Metadata && !Metadata.busy; onClicked: Metadata.rejectMatch() }
            GlassButton { compact: true; text: "CHOOSE PORTRAIT"; enabled: Metadata && Metadata.hasGridKey && !Metadata.busy; onClicked: Metadata.findCovers() }
        }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Metadata ? Metadata.status : ""; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
        Repeater {
            model: Metadata ? Metadata.candidates : []
            GlassButton {
                required property var modelData
                required property int index
                Layout.fillWidth: true; compact: true
                text: modelData.title + (modelData.year ? " · " + modelData.year : "")
                enabled: Metadata && !Metadata.busy
                onClicked: { Metadata.chooseMatch(index); Metadata.chooseGridGame(index) }
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 12
            Repeater {
                model: Metadata ? Metadata.covers : []
                Column {
                    required property var modelData
                    required property int index
                    spacing: 6; width: 120 * root.uiScale
                    Image { width: parent.width; height: width * 1.5; source: modelData.url; asynchronous: true; fillMode: Image.PreserveAspectFit; sourceSize.width: 180 }
                    GlassButton { width: parent.width; compact: true; text: "USE COVER"; enabled: Metadata && !Metadata.busy; onClicked: Metadata.chooseCover(index) }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "Ratings from IGDB. Portraits from SteamGridDB. Your custom cover always takes priority. Connections are managed in Settings."
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale
        }
    }
}

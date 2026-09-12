import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: Theme.background
    signal dismissed()
    signal openGame(string kind)
    property var service: typeof LibraryRepair !== "undefined" ? LibraryRepair : null
    readonly property real uiScale: root.Window.window && root.Window.window.couchMode ? 1.35 : 1
    property var game: service ? service.current : ({})
    function reveal(item) { root.Window.window.revealInScrollView(reviewScroll, item) }
    MouseArea { anchors.fill: parent }
    function focusEditor() { closeButton.forceActiveFocus() }
    ScrollView {
        id: reviewScroll
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 1100 * root.uiScale)
        height: parent.height - 64
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text { text: "REPAIR LIBRARY"; color: Theme.foreground; font.pixelSize: 26 * root.uiScale; Layout.fillWidth: true }
                GlassButton { id: closeButton; text: "CLOSE"; onClicked: root.dismissed() }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12 * root.uiScale; color: Theme.mutedText
                text: "Review identity, artwork, unavailable installations, and duplicate suggestions. Linking always requires your choice. Skip keeps a game unresolved."
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    model: ["All sources"].concat(root.service ? root.service.sources : [])
                    currentIndex: Math.max(0, model.indexOf(root.service && root.service.source ? root.service.source : "All sources"))
                    onActivated: root.service.source = currentIndex ? currentText : ""
                }
                ComboBox {
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    property var values: ["", "identification", "artwork", "unavailable", "duplicates"]
                    model: ["All reasons", "Identity", "Artwork", "Unavailable", "Duplicate suggestions"]
                    currentIndex: Math.max(0, values.indexOf(root.service ? root.service.reason : ""))
                    onActivated: root.service.reason = values[currentIndex]
                }
            }
            Text { font.pixelSize: 12 * root.uiScale; color: Theme.mutedText; text: (root.service ? root.service.entries.length : 0) + " games remaining in these filters" }
            Image { Layout.preferredWidth: 100; Layout.preferredHeight: 130; visible: source.toString().length > 0; source: root.game.coverPath || ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.foreground; font.pixelSize: 24 * root.uiScale; text: root.game.title || "No games need review" }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 12 * root.uiScale; color: Theme.mutedText
                text: root.game.title ? [root.game.source, root.game.system || "Unknown platform", root.game.igdbId ? "Catalog identity: " + (root.game.matchedTitle || "IGDB") + " (" + root.game.igdbId + ")" : "No confirmed catalog identity", root.game.installPath || root.game.launchTarget || "No local path", (root.game.reasons || []).join(", ") || "Resolved. Continue when ready."].join("\n") : ""
            }
            Flow {
                Layout.fillWidth: true; spacing: 10
                GlassButton { text: "CORRECT IDENTITY"; enabled: !!root.game.title && !Metadata.busy; onClicked: root.openGame("identity") }
                GlassButton { text: "CHOOSE ARTWORK"; enabled: !!root.game.title && !Metadata.busy; onClicked: root.openGame("artwork") }
                GlassButton { text: "LAUNCH SETUP / LINK"; enabled: !!root.game.title; onClicked: root.openGame("") }
                GlassButton { text: "UNDO IDENTITY"; enabled: !!root.game.undoIdentity && !Metadata.busy; onClicked: root.service.undo("identity") }
                GlassButton { text: "UNDO ARTWORK"; enabled: !!root.game.undoArtwork && !Metadata.busy; onClicked: root.service.undo("artwork") }
            }
            Flow {
                Layout.fillWidth: true; spacing: 10
                GlassButton { text: "PREVIOUS"; enabled: !!root.game.title; onClicked: root.service.move(-1) }
                GlassButton { text: "NEXT / SKIP"; enabled: !!root.game.title; onClicked: root.service.move(1) }
                GlassButton { text: "RETRY THIS GAME"; enabled: !!root.game.title && !Metadata.busy; onClicked: root.service.retry(false) }
                GlassButton { text: root.game.selected ? "REMOVE FROM RETRY" : "SELECT FOR RETRY"; enabled: !!root.game.title; onClicked: root.service.toggleSelected() }
                GlassButton { text: "RETRY SELECTED (" + (root.service ? root.service.selectedTitles.length : 0) + ")"; enabled: root.service && root.service.selectedTitles.length > 0 && !Metadata.busy; onClicked: root.service.retrySelected() }
                GlassButton { text: "STOP RETRY"; enabled: Metadata.busy; onClicked: Metadata.cancel() }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 * root.uiScale; color: Theme.mutedText; text: root.service && root.service.selectedTitles.length ? "Selected for retry: " + root.service.selectedTitles.join(", ") : "" }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 * root.uiScale; color: Theme.mutedText; text: root.service ? root.service.message : "" }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 * root.uiScale; color: Theme.mutedText; text: Metadata.status + (Metadata.busy ? " • " + Metadata.pending + " pending" : "") }
        }
    }
}

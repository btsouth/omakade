import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: editor
    property bool couchMode: false
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2.4, height / 600)) : 1
    readonly property bool stacked: width < 720 * uiScale
    property int statusIndex: 0
    readonly property var statuses: ["backlog", "playing", "completed", "abandoned", ""]
    readonly property int focusedRow: games.currentIndex
    signal dismissed()
    signal textEntryRequested(var target, string title)
    color: Theme.background
    function focusEditor() { selectAllButton.forceActiveFocus() }
    function apply(changes) {
        if (Library.applyBulkChanges(changes)) selectAllButton.forceActiveFocus()
    }
    function focusRow(index) {
        games.currentIndex = index
        games.positionViewAtIndex(index, ListView.Contain)
        games.forceLayout()
        const item = games.itemAtIndex(index)
        if (item) item.forceActiveFocus()
    }
    function navigate(current, key) {
        if (editor.stacked && current === favoriteButton && key === Qt.Key_Up && games.count > 0) {
            focusRow(games.count - 1)
            return true
        }
        if (!current || current.bulkRow === undefined || (key !== Qt.Key_Up && key !== Qt.Key_Down)) return false
        const next = current.bulkRow + (key === Qt.Key_Down ? 1 : -1)
        if (next < 0) return false
        if (next < games.count) focusRow(next)
        else if (editor.stacked && key === Qt.Key_Down && favoriteButton.enabled) {
            favoriteButton.forceActiveFocus()
            reveal(favoriteButton)
        }
        return true
    }
    function reveal(item) {
        if (!item) return
        if (item.bulkRow !== undefined) {
            games.currentIndex = item.bulkRow
            games.positionViewAtIndex(games.currentIndex, ListView.Contain)
        } else {
            let ancestor = item
            while (ancestor && ancestor !== actionsColumn) ancestor = ancestor.parent
            if (!ancestor) return
            const flick = actions.contentItem
            const point = item.mapToItem(flick, 0, 0)
            if (point.y < 8) flick.contentY = Math.max(0, flick.contentY + point.y - 8)
            else if (point.y + item.height > flick.height - 8)
                flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), flick.contentY + point.y + item.height - flick.height + 8)
        }
    }
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12 * editor.uiScale
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapMode: Text.Wrap
                text: "ORGANIZE · " + Library.selectionCount + " SELECTED"
                color: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: 20 * editor.uiScale
            }
            GlassButton {
                text: "DONE"
                displayScale: editor.uiScale
                onClicked: editor.dismissed()
            }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: editor.stacked ? 1 : 2
            columnSpacing: 20 * editor.uiScale
            rowSpacing: 20 * editor.uiScale
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                Layout.preferredHeight: 0
                RowLayout {
                    GlassButton {
                        id: selectAllButton
                        objectName: "bulkSelectAllButton"
                        text: "SELECT RESULTS"
                        compact: true
                        displayScale: editor.uiScale
                        onClicked: Library.selectAllFiltered()
                    }
                    GlassButton {
                        text: "CLEAR"
                        compact: true
                        displayScale: editor.uiScale
                        onClicked: Library.clearSelection()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "Selection uses the current filtered results."
                    wrapMode: Text.Wrap
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: UiMetrics.supporting * editor.uiScale
                }
                ListView {
                    id: games
                    objectName: "bulkGameList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6 * editor.uiScale
                    model: Library
                    ScrollBar.vertical: ScrollBar {}
                    delegate: GlassButton {
                        id: gameButton
                        required property int index
                        required property string title
                        property int bulkRow: index
                        width: games.width
                        displayScale: editor.uiScale
                        selected: { Library.selectionRevision; return Library.isSelected(index) }
                        text: (selected ? "✓  " : "□  ") + title
                        implicitWidth: 100
                        Accessible.name: title
                        Accessible.checkable: true
                        Accessible.checked: selected
                        contentItem: Text {
                            text: gameButton.text
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.body * editor.uiScale
                        }
                        onClicked: Library.toggleSelection(index)
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: games.count === 0
                        text: "No games match these filters"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * editor.uiScale
                    }
                }
            }
            ScrollView {
                id: actions
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                Layout.preferredHeight: 0
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: actionsColumn
                    width: actions.availableWidth
                    spacing: 12 * editor.uiScale
                    enabled: Library.selectionCount > 0
                    Text {
                        Layout.fillWidth: true
                        text: "Apply an action to every selected game."
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 12 * editor.uiScale
                    }
                    RowLayout {
                        GlassButton { id: favoriteButton; objectName: "bulkFavoriteButton"; text: "FAVORITE"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({favorite: true}) }
                        GlassButton { text: "UNFAVORITE"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({favorite: false}) }
                    }
                    RowLayout {
                        GlassButton { text: "HIDE"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({hidden: true}) }
                        GlassButton { text: "UNHIDE"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({hidden: false}) }
                    }
                    Text { text: "COMPLETION STATUS"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: UiMetrics.label * editor.uiScale }
                    RowLayout {
                        GlassButton {
                            text: editor.statuses[editor.statusIndex] ? editor.statuses[editor.statusIndex].toUpperCase() : "NO STATUS"
                            compact: true
                            displayScale: editor.uiScale
                            onClicked: editor.statusIndex = (editor.statusIndex + 1) % editor.statuses.length
                        }
                        GlassButton { text: "SET"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({status: editor.statuses[editor.statusIndex]}) }
                    }
                    Text { text: "TAGS · COMMA SEPARATED"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: UiMetrics.label * editor.uiScale }
                    ThemedTextField {
                        id: tags
                        property Item controllerRightTarget: tagsClear.visible ? tagsClear : null
                        rightPadding: tagsClear.reservedWidth
                        FieldClearButton { id: tagsClear; field: tags }
                        objectName: "bulkTagsField"
                        property bool controllerNavigation: editor.couchMode || (Controller !== null && Controller.driving)
                        Layout.fillWidth: true
                        color: Theme.foreground
                        placeholderText: "short, relaxing"
                        placeholderTextColor: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 13 * editor.uiScale

                        function acceptInput(event) { if (TextEntry.keyboardNeeded) editor.textEntryRequested(tags, "TAGS"); event.accepted = true }
                        function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                        Keys.onReturnPressed: event => acceptInput(event)
                        Keys.onEnterPressed: function(event) { if (TextEntry.keyboardNeeded) editor.textEntryRequested(tags, "TAGS"); event.accepted = true }
                    }
                    RowLayout {
                        GlassButton { text: "ADD TAGS"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({tagsAdd: tags.text}) }
                        GlassButton { text: "REMOVE TAGS"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({tagsRemove: tags.text}) }
                    }
                    Text { text: "COLLECTION"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: UiMetrics.label * editor.uiScale }
                    ThemedTextField {
                        id: collection
                        property Item controllerRightTarget: collectionClear.visible ? collectionClear : null
                        rightPadding: collectionClear.reservedWidth
                        FieldClearButton { id: collectionClear; field: collection }
                        objectName: "bulkCollectionField"
                        property bool controllerNavigation: editor.couchMode || (Controller !== null && Controller.driving)
                        Layout.fillWidth: true
                        maximumLength: 48
                        color: Theme.foreground
                        placeholderText: "Collection name"
                        placeholderTextColor: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 13 * editor.uiScale

                        function acceptInput(event) { if (TextEntry.keyboardNeeded) editor.textEntryRequested(collection, "COLLECTION"); event.accepted = true }
                        function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                        Keys.onReturnPressed: event => acceptInput(event)
                        Keys.onEnterPressed: function(event) { if (TextEntry.keyboardNeeded) editor.textEntryRequested(collection, "COLLECTION"); event.accepted = true }
                    }
                    RowLayout {
                        GlassButton { text: "ADD TO"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({collection: collection.text, collectionIncluded: true}) }
                        GlassButton { text: "REMOVE FROM"; compact: true; displayScale: editor.uiScale; onClicked: editor.apply({collection: collection.text, collectionIncluded: false}) }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Adding to a new collection creates it. Other tags and collections are kept. Limit: 20 tags per game, 32 characters per tag."
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * editor.uiScale
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: Library.bulkMessage
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12 * editor.uiScale
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var entry: Metadata ? Metadata.current : ({})
    required property var game
    property bool couchMode: false
    property real uiScale: 1
    property bool editing: false
    property bool panelMode: false
    property Item externalDone: null
    readonly property Item headerControl: externalDone || artworkButton
    readonly property Item firstBodyControl: identifyButton.visible ? identifyButton : changeMatchButton.visible ? changeMatchButton : choosePortraitButton
    property var coverChoices: Metadata ? Metadata.covers : []
    property int coverGeneration: 0
    function coverAt(index) {
        const generation = coverGeneration
        return coverRepeater.itemAt(index)
    }
    property bool matchControlsOpen: false
    property bool coverControlsOpen: false
    signal localArtworkRequested()
    signal connectionsRequested()
    property bool autoCoverPending: false
    function loadCoverChoices() {
        if (!autoCoverPending || !editing || !Metadata || Metadata.busy || !Metadata.hasGridKey) return
        autoCoverPending = false
        Metadata.findCovers()
    }
    signal textEntryRequested(var target, string title, bool password, string placeholder)
    // The details page navigates by an explicit controller chain, and a section left out of it
    // is unreachable however plainly it is on screen: arrow keys follow the chain in preference
    // to the geometry. This section was missing from it entirely, so pressing down off the
    // collections row jumped straight past it to the achievements. These name the way in and the
    // way out; the page wires them to whatever sits either side.
    property Item previousSection: null
    property Item nextSection: null
    readonly property Item firstControl: firstBodyControl
    readonly property Item lastControl: !root.editing ? artworkButton
                                      : coverSearchButton.visible && coverSearchButton.enabled
                                        ? coverSearchButton
                                        : root.headerControl
    Layout.fillWidth: true
    spacing: 10
    visible: Metadata !== null && !game.isPortal
    readonly property string gameKey: game.metadataKey || ""
    // Seed once per game. A text binding would overwrite native typing whenever
    // metadata emits changed, including when a search starts or finishes.
    property bool searchFieldsReady: false
    function resetSearchFields() {
        const initialTitle = (Metadata ? Metadata.current.title : "") || root.game.title || ""
        titleSearch.text = initialTitle
        coverSearch.text = initialTitle
    }
    onGameKeyChanged: {
        matchControlsOpen = false
        coverControlsOpen = false
        editing = false
        if (Metadata) Metadata.inspect(game)
        if (searchFieldsReady) resetSearchFields()
    }
    Component.onCompleted: {
        if (Metadata) Metadata.inspect(game)
        searchFieldsReady = true
        resetSearchFields()
    }
    // Identifying a game by hand takes precedence over the background pass, which would
    // otherwise hold the service busy and leave every control here disabled.
    onEditingChanged: {
        if (Metadata) Metadata.setEditing(editing)
        autoCoverPending = editing && root.entry.igdbId > 0
        Qt.callLater(loadCoverChoices)
    }
    Component.onDestruction: if (Metadata) Metadata.setEditing(false)
    Connections {
        target: Metadata
        function onChanged() { root.loadCoverChoices() }
        function onPortraitSelected(key) {
            if (key !== root.gameKey) return
            root.editing = false
            if (!root.panelMode) artworkButton.forceActiveFocus()
        }
    }
    RowLayout {
        visible: !root.panelMode
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: root.panelMode ? "CURRENT GAME" : "RATING & COVER ART"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale }
        GlassButton {
            id: artworkButton
            objectName: root.externalDone ? "metadataInlineDoneButton" : "metadataArtworkButton"
            visible: !root.externalDone
            compact: true
            text: root.editing ? "DONE" : "IDENTIFY / ARTWORK"
            property Item controllerUpTarget: root.previousSection
            // Expanded, down goes into the section rather than past it. Collapsed, there is
            // nothing inside to reach, so it goes on to whatever follows.
            property Item controllerDownTarget: root.editing ? (identifyButton.visible ? identifyButton : choosePortraitButton) : root.nextSection
            onClicked: root.editing = !root.editing
        }
    }
    Text {
        Layout.fillWidth: true
        visible: !root.panelMode
        text: !Metadata ? "" : root.entry.rating >= 0
              ? "IGDB  " + root.entry.rating + " / 100 · " + root.entry.ratingCount + " ratings"
              : "No rating available"
        color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 12 * root.uiScale
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 10; visible: root.editing
        RowLayout {
            Layout.fillWidth: true
            spacing: 14 * root.uiScale
            Image {
                Layout.preferredWidth: 48 * root.uiScale
                Layout.preferredHeight: 72 * root.uiScale
                source: root.game.coverPath || root.entry.portrait || ""
                visible: source.toString() !== ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 96
            }
            ColumnLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: root.entry.title || root.game.title || ""
                    color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 14 * root.uiScale
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: (root.entry.year ? root.entry.year + " · " : "") + (root.entry.matchStatus || "Not identified")
                    color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale
                }
            }
            GlassButton {
                id: changeMatchButton
                compact: true
                text: root.matchControlsOpen ? "HIDE SEARCH" : "CHANGE MATCH"
                visible: root.entry.igdbId > 0
                onClicked: root.matchControlsOpen = !root.matchControlsOpen
            }
        }
        RowLayout {
            visible: root.matchControlsOpen || !(root.entry.igdbId > 0)
            Layout.fillWidth: true
            TextField {
                id: titleSearch; objectName: "metadataTitleField"; Layout.fillWidth: true; text: ""
                placeholderTextColor: Theme.mutedText
                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                        border.width: titleSearch.activeFocus ? 2 : 1
                        border.color: titleSearch.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                    }
                    property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                Accessible.name: "Game title for identification"
                color: Theme.foreground; font.family: Theme.fontFamily
                Keys.onReturnPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(titleSearch, "GAME TITLE", false, "Search title"); event.accepted = true } else Metadata.search(text) }
                Keys.onEnterPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(titleSearch, "GAME TITLE", false, "Search title"); event.accepted = true } else Metadata.search(text) }
            
                rightPadding: titleSearchClear.visible ? titleSearchClear.reservedWidth : 12
                property Item controllerRightTarget: titleSearchClear.visible ? titleSearchClear : null
                FieldClearButton { id: titleSearchClear; field: titleSearch }
            }
            GlassButton {
                id: identifyButton
                objectName: "metadataIdentifyButton"
                compact: true
                text: Insights && Insights.configured ? "SEARCH IGDB" : "CONNECT IGDB"
                property Item controllerUpTarget: root.headerControl
                property Item controllerDownTarget: rejectButton.visible ? rejectButton : choosePortraitButton
                enabled: Metadata && !Metadata.busy
                onClicked: Insights && Insights.configured ? Metadata.search(titleSearch.text) : root.connectionsRequested()
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            GlassButton {
                id: rejectButton
                objectName: "metadataRejectButton"
                compact: true
                visible: root.matchControlsOpen
                text: "REMOVE MATCH"
                property Item controllerUpTarget: identifyButton.visible ? identifyButton : root.headerControl
                property Item controllerDownTarget: coverSearchButton.visible ? coverSearchButton : customImagesButton
                property Item controllerRightTarget: choosePortraitButton
                enabled: Metadata && !Metadata.busy
                onClicked: Metadata.rejectMatch()
            }
            GlassButton {
                id: choosePortraitButton
                objectName: "metadataChoosePortraitButton"
                compact: true
                text: Metadata && !Metadata.hasGridKey ? "CONNECT COVER SERVICE" : root.coverChoices.length ? "REFRESH COVERS" : "FIND COVERS"
                property Item controllerUpTarget: identifyButton.visible ? identifyButton : root.headerControl
                property Item controllerDownTarget: coverSearchButton.visible ? coverSearchButton : customImagesButton
                property Item controllerLeftTarget: rejectButton
                property Item controllerRightTarget: clearCoverButton
                enabled: Metadata && !Metadata.busy
                onClicked: Metadata.hasGridKey ? Metadata.findCovers() : root.connectionsRequested()
            }
            GlassButton {
                id: clearCoverButton
                objectName: "metadataClearCoverButton"
                compact: true
                visible: root.coverControlsOpen
                text: "RESET COVER MATCH"
                property Item controllerUpTarget: identifyButton.visible ? identifyButton : root.headerControl
                property Item controllerDownTarget: coverSearchButton.visible ? coverSearchButton : customImagesButton
                property Item controllerLeftTarget: choosePortraitButton
                enabled: Metadata && Metadata.hasGridKey && !Metadata.busy
                onClicked: Metadata.clearGridSelection()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                compact: true
                visible: Metadata && Metadata.hasGridKey
                text: root.coverControlsOpen ? "HIDE COVER SEARCH" : "SEARCH BY TITLE"
                onClicked: root.coverControlsOpen = !root.coverControlsOpen
            }
            GlassButton {
                id: customImagesButton
                compact: true
                text: "CUSTOM IMAGES…"
                visible: root.panelMode
                onClicked: root.localArtworkRequested()
            }
        }
        // The two catalogues do not always agree on a name: SteamGridDB files Dragon Quest V
        // under Hand of the Heavenly Bride while IGDB gives its Japanese title, and nothing
        // automatic bridges that. The name to search for can be typed here instead.
        RowLayout {
            Layout.fillWidth: true
            visible: root.coverControlsOpen && Metadata && Metadata.hasGridKey
            TextField {
                id: coverSearch; objectName: "metadataCoverField"; Layout.fillWidth: true; text: ""
                placeholderText: "Search SteamGridDB by name"
                placeholderTextColor: Theme.mutedText
                background: Rectangle {
                    radius: Math.max(5, Theme.cornerRadius)
                    color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                    border.width: coverSearch.activeFocus ? 2 : 1
                    border.color: coverSearch.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                }
                property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                Accessible.name: "Cover art search"
                color: Theme.foreground; font.family: Theme.fontFamily
                Keys.onReturnPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(coverSearch, "COVER SEARCH", false, "Search cover art"); event.accepted = true } else Metadata.searchCovers(text) }
                Keys.onEnterPressed: event => { if (TextEntry.keyboardNeeded) { root.textEntryRequested(coverSearch, "COVER SEARCH", false, "Search cover art"); event.accepted = true } else Metadata.searchCovers(text) }
            
                rightPadding: coverSearchClear.visible ? coverSearchClear.reservedWidth : 12
                property Item controllerRightTarget: coverSearchClear.visible ? coverSearchClear : null
                FieldClearButton { id: coverSearchClear; field: coverSearch }
            }
            GlassButton {
                id: coverSearchButton
                objectName: "metadataCoverSearchButton"
                compact: true
                text: "SEARCH COVERS"
                // The last control in the section, so this is where the controller leaves it.
                property Item controllerUpTarget: rejectButton
                property Item controllerDownTarget: root.nextSection
                enabled: Metadata && Metadata.hasGridKey && !Metadata.busy
                onClicked: Metadata.searchCovers(coverSearch.text)
            }
        }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Metadata ? Metadata.status : ""; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale }
        Repeater {
            model: Metadata ? Metadata.candidates : []
            GlassButton {
                required property var modelData
                required property int index
                Layout.fillWidth: true; compact: true
                text: modelData.title + (modelData.year ? " · " + modelData.year : "")
                      + (modelData.edition ? " · " + modelData.edition : "")
                      + (modelData.releaseRegions && modelData.releaseRegions.length
                         ? " · " + modelData.releaseRegions.join(", ") : "")
                      + (modelData.id ? " · ID " + modelData.id : "")
                enabled: Metadata && !Metadata.busy
                onClicked: { Metadata.chooseMatch(index); Metadata.chooseGridGame(index) }
            }
        }
        Flow {
            id: coverGrid
            Layout.fillWidth: true
            spacing: 10 * root.uiScale
            readonly property int columns: Math.max(2, Math.floor((width + spacing) / (128 * root.uiScale + spacing)))
            Repeater {
                id: coverRepeater
                onItemAdded: Qt.callLater(function() { root.coverGeneration++ })
                onItemRemoved: Qt.callLater(function() { root.coverGeneration++ })
                model: root.coverChoices
                GlassButton {
                    id: tile
                    required property var modelData
                    required property int index
                    objectName: "metadataCoverTile" + index
                    width: Math.floor((coverGrid.width - (coverGrid.columns - 1) * coverGrid.spacing) / coverGrid.columns)
                    height: width * 1.5 + 8
                    padding: 4
                    leftPadding: 4
                    rightPadding: 4
                    topPadding: 4
                    bottomPadding: 4
                    text: "Use cover " + (index + 1) + (modelData.author ? " by " + modelData.author : "")
                    Accessible.name: text + (currentCover ? ", current cover" : "")
                    readonly property bool currentCover: Number(root.entry.gridCoverId) === Number(modelData.id)
                        && [root.entry.portrait, root.entry.selectedCoverPath].filter(Boolean).some(function(path) {
                            return String(path).replace(/^file:\/\//, "") === String(root.game.coverPath || "").replace(/^file:\/\//, "")
                        })
                    property Item controllerLeftTarget: index % coverGrid.columns ? root.coverAt(index - 1) : null
                    property Item controllerRightTarget: index % coverGrid.columns < coverGrid.columns - 1 ? root.coverAt(index + 1) : null
                    property Item controllerUpTarget: index >= coverGrid.columns ? root.coverAt(index - coverGrid.columns) : customImagesButton
                    property Item controllerDownTarget: index + coverGrid.columns < coverRepeater.count ? root.coverAt(index + coverGrid.columns) : null
                    enabled: Metadata && !Metadata.busy
                    onClicked: Metadata.chooseCover(index)
                    contentItem: Item {
                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: tile.modelData.url
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 300
                        }
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: coverImage.status === Image.Error
                            text: "Preview unavailable"
                            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale
                        }
                        Rectangle {
                            visible: tile.currentCover
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            height: 26 * root.uiScale
                            color: Theme.background
                            Text {
                                anchors.centerIn: parent
                                text: "CURRENT"
                                color: Theme.accent; font.family: Theme.fontFamily; font.pixelSize: 11 * root.uiScale
                            }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "Covers from SteamGridDB · Select a cover to apply it"
            visible: root.coverChoices.length > 0
            color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * root.uiScale
        }
    }
}

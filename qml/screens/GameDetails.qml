import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../components"

Item {
    id: root
    objectName: "gameDetails"

    Accessible.name: (game.title || "Game") + " details"
    Accessible.role: Accessible.Pane

    required property var game
    required property var installations
    required property var selectedInstallation
    readonly property var detailsEntry: gameInfoSection.entry || ({})
    readonly property int releaseYear: gameInfoSection.entry && gameInfoSection.entry.year > 0
                                       ? gameInfoSection.entry.year : (game.year || 0)
    property bool showOrganizationControls: !DemoMode
    property bool collectionEditorOpen: false
    property bool aliasesExpanded: false
    property bool titleExpanded: false
    property bool romDetailsExpanded: false
    readonly property string displayTitle: root.game.source === "RetroArch"
        ? (root.game.title || "").replace(/\s*\([^)]*\b(?:translated|translation|patch|patched|rev|revision|hack|fastrom)\b[^)]*\)/gi, "").trim()
        : (root.game.title || "")
    readonly property string detailIdentity: game.metadataKey || game.appId || game.title || ""
    onDetailIdentityChanged: { aliasesExpanded = false; titleExpanded = false; romDetailsExpanded = false; gameInfoSection.expanded = false }
    property bool couchMode: false
    readonly property real uiScale: couchMode
                                    ? Math.max(1, Math.min(2.4,
                                                          Math.min(width / 1920,
                                                                   height / 1080) * 1.18))
                                    : 1

    // Closing the editor hides the focused field, so hand focus back to the button that
    // opened it and drop the draft instead of showing it again next time.
    function closeCollectionEditor() {
        collectionEditorOpen = false
        collectionField.clear()
        newCollectionButton.forceActiveFocus()
    }
    property bool navigationEnabled: true
    property bool launchBusy: false
    property string launchMessage: ""
    property bool launchFailed: false
    readonly property bool achievementSourceIsRetroArch: selectedInstallation.source === "RetroArch"
    readonly property var achievementAccount: achievementSourceIsRetroArch ? RetroAchievements : SteamAccount
    property bool randomSelection: false
    signal randomRequested()
    signal backRequested()
    signal favoriteRequested()
    signal pinRequested()
    signal playRequested()
    Connections { target: typeof Metadata !== "undefined" ? Metadata : null; function onEntryChanged() { root.reviewRevision++ } }
    property int reviewRevision: 0
    Connections { target: typeof LibraryRepair !== "undefined" ? LibraryRepair : null; function onChanged() { root.reviewRevision++ } }
    readonly property var reviewReasons: { const update=reviewRevision; return typeof LibraryRepair !== "undefined" && LibraryRepair ? LibraryRepair.reasonsFor(root.game.metadataKey || "") : [] }
    property int setupRevision: 0
    readonly property var launchPlan: { const revision = setupRevision; return typeof Launcher !== "undefined" && Launcher ? Launcher.inspect(selectedInstallation) : ({}) }
    Connections { target: typeof Launcher !== "undefined" ? Launcher : null; function onSetupChanged() { root.setupRevision++ } }
    readonly property bool saveSourceSupported: launchPlan.supported === true
    readonly property string saveGamePath: saveSourceSupported ? (launchPlan.gamePath || selectedInstallation.installPath || selectedInstallation.launchTarget || "") : ""
    readonly property int saveBackupCount: {
        if (typeof SaveBackups === "undefined" || !saveGamePath) return 0
        const revision = SaveBackups.revision
        return SaveBackups.count(saveGamePath)
    }
    readonly property var sessionHistoryPaths: {
        const paths = []
        const candidates = [root.selectedInstallation].concat(root.installations || [])
        for (const installation of candidates) {
            const path = installation ? (installation.installPath || "") : ""
            if (path && paths.indexOf(path) < 0) paths.push(path)
        }
        return paths
    }
    readonly property var recordedSessions: {
        if (typeof SessionRecorderStatus === "undefined" || !SessionRecorderStatus
                || !SessionRecorderStatus.storageAvailable || sessionHistoryPaths.length === 0)
            return []
        const revision = SessionRecorderStatus.revision
        return SessionRecorderStatus.historyForPaths(sessionHistoryPaths, 8)
    }
    function sessionDurationText(value) {
        const seconds = Math.max(0, Number(value) || 0)
        if (seconds < 60) return "Less than 1m"
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        if (hours > 0) return hours + "h" + (minutes > 0 ? " " + minutes + "m" : "")
        return minutes + "m"
    }
    function storageSizeText(value) {
        const bytes = Math.max(0, Number(value) || 0)
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0) + " KiB"
        return (bytes / (1024 * 1024)).toFixed(bytes < 10 * 1024 * 1024 ? 1 : 0) + " MiB"
    }
    function openIdentification() { identifyPanel.open() }
    function showLaunchSetup() {
        launchSetup.expanded = true
        Qt.callLater(function() { root.Window.window.restoreFocus(launchSetup.firstControl); root.revealFocusedItem(launchSetup.firstControl) })
    }
    function showSaveBackups() {
        const context = launchPlan.saveContext || ({})
        SaveBackups.selectLaunch(context.source || selectedInstallation.source, saveGamePath,
                                 context.core || "", context.flatpak || false, context.id || selectedInstallation.appId || "",
                                 context.runner || "", context.target || "")
        saveBackupsMenu.pendingVersion = ""
        saveBackupsMenu.pendingDelete = false
        saveBackupsMenu.open()
    }
    signal manageRequested()
    signal hiddenRequested()
    signal connectRequested()
    signal coverRequested()
    signal coverResetRequested()
    signal installationSelected(var installation)
    signal preferredInstallationRequested()
    signal manualEditRequested()
    signal linkRequested()
    signal unlinkRequested()
    signal completionStatusRequested(string status)
    signal tagsRequested(string tags)
    signal collectionToggled(string name, bool included)
    signal collectionCreateRequested(string name)
    signal textEntryRequested(var target, string title, bool password, string placeholder)

    function focusPrimary() {
        playButton.forceActiveFocus(Qt.TabFocusReason)
        const flickable = detailsScroll.navigationFlickable
        if (flickable) flickable.contentY = flickable.originY
    }

    function comparableTitle(value) {
        return (value || "").toLowerCase().replace(/\([^)]*\)|\[[^\]]*\]/g, "").replace(/[\s_:.!?'-]+/g, "")
    }
    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function revealFocusedItem(item) {
        const flickable = detailsScroll.navigationFlickable
        if (!item || !flickable) {
            return
        }
        let ancestor = item
        while (ancestor) {
            if (ancestor === backButton) {
                flickable.contentY = flickable.originY
                return
            }
            if (ancestor === coverSidebar) {
                return
            }
            ancestor = ancestor.parent
        }
        const position = item.mapToItem(flickable, 0, 0)
        const margin = 16
        if (position.y < margin) {
            flickable.contentY = Math.max(flickable.originY,
                                          flickable.contentY + position.y - margin)
        } else if (position.y + item.height > flickable.height - margin) {
            flickable.contentY = Math.min(
                        flickable.originY + Math.max(0, flickable.contentHeight - flickable.height),
                        flickable.contentY + position.y + item.height - flickable.height + margin)
        }
    }

    Keys.onPressed: function(event) {
        if (root.navigationEnabled && event.key === Qt.Key_F) {
            root.favoriteRequested()
            event.accepted = true
        }
    }

    Connections {
        target: root.Window.window
        enabled: root.navigationEnabled && target !== null
        function onActiveFocusItemChanged() {
            Qt.callLater(function() {
                const window = root.Window.window
                if (window) {
                    root.revealFocusedItem(window.activeFocusItem)
                }
            })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, root.couchMode ? 0.88 : 0.76)
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.couchMode ? parent.height * 0.68
                               : Math.min(parent.height * 0.58, 500)
        opacity: 0.42
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: root.game.accentStart || Theme.accent }
            GradientStop { position: 1.0; color: root.game.accentEnd || Theme.blue }
        }
    }

    Image {
        id: detailsHeroImage
        objectName: "detailsHero"
        anchors.top: parent.top
        anchors.right: parent.right
        height: root.couchMode ? Math.min(parent.height * 0.60, 600 * root.uiScale)
                               : Math.min(parent.height * 0.50, 500)
        width: Math.min(parent.width, height * 16 / 9)
        // Never enlarge a portrait cover into a backdrop or reuse legacy first-artwork picks.
        source: root.game.heroPath || (root.detailsEntry.heroKind === "screenshot"
                                       && !root.detailsEntry.identityAmbiguous && !root.detailsEntry.rejected
                                       ? root.detailsEntry.heroUrl || "" : "")
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignRight
        verticalAlignment: Image.AlignTop
        sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
        opacity: status === Image.Ready ? 0.40 : 0
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: detailsHeroImage.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.darkerBackground }
            GradientStop {
                position: Math.max(0, Math.min(0.8, 1 - detailsHeroImage.paintedWidth / root.width))
                color: Theme.darkerBackground
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
        visible: detailsHeroImage.status === Image.Ready
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.couchMode ? parent.height * 0.74
                               : Math.min(parent.height * 0.62, 540)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.darkerBackground }
        }
    }

    GlassButton {
        id: backButton
        objectName: "detailsBackButton"
        property Item controllerDownTarget: playButton
        property Item controllerRightTarget: detailSettingsButton
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: root.couchMode ? 42 * root.uiScale : 24
        text: "BACK"
        iconText: "←"
        compact: true
        onClicked: root.backRequested()
    }

    GlassButton {
        id: detailSettingsButton
        property Item controllerLeftTarget: backButton
        property Item controllerDownTarget: detailManageButton
        anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: root.couchMode ? 42 * root.uiScale : 24
        text: "SETTINGS"; compact: true
        onClicked: root.Window.window.diagnosticsOpen = true
    }

    Item {
        id: detailsArea
        anchors.fill: parent
        anchors.topMargin: root.couchMode ? 112 * root.uiScale : 80
        anchors.leftMargin: root.couchMode ? 64 * root.uiScale
                                           : Math.max(28, parent.width * 0.055)
        anchors.rightMargin: root.couchMode ? 64 * root.uiScale
                                            : Math.max(28, parent.width * 0.055)
        anchors.bottomMargin: root.couchMode ? 64 * root.uiScale : 22
        readonly property real columnSpacing: Math.max(28, width * 0.045)

        ColumnLayout {
            id: coverSidebar
            anchors.top: parent.top
            anchors.left: parent.left
            // Fixed 2:3 frame so every game shows the same cover size; keep it
            // compact so the description column stays the focus.
            width: Math.max(0, Math.min(root.couchMode ? 300 * root.uiScale : 240,
                                        root.width * (root.couchMode ? 0.2 : 0.22),
                                        (detailsArea.height - reservedControlHeight) / 1.5,
                                        detailsArea.width * 0.4))
            spacing: 8
            readonly property real reservedControlHeight: (coverEditButton.visible ? 42 * root.uiScale : 0)

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width * 1.5
                radius: Math.max(6, Theme.cornerRadius)
                clip: true
                border.color: root.alpha(Theme.foreground, 0.22)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: root.game.accentStart || Theme.accent }
                    GradientStop { position: 1.0; color: root.game.accentEnd || Theme.blue }
                }

                Image {
                    id: coverArtwork
                    anchors.fill: parent
                    source: root.game.coverPath || ""
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(width * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    sourceSize.height: Math.ceil(height * Math.max(1, Screen.devicePixelRatio) / 64) * 64
                    opacity: status === Image.Ready ? 1 : 0
                }

                Rectangle {
                    visible: coverArtwork.status !== Image.Ready
                    width: parent.width * 0.95
                    height: width
                    radius: width / 2
                    x: parent.width * 0.44
                    y: -height * 0.18
                    color: root.alpha(Theme.brightForeground, 0.10)
                }

                Text {
                    visible: coverArtwork.status !== Image.Ready
                    anchors.centerIn: parent
                    text: root.game.coverMark || "◇"
                    color: root.alpha(Theme.brightForeground, 0.9)
                    font.family: Theme.fontFamily
                    font.pixelSize: Math.max(74, parent.width * 0.34)
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * 0.34
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: root.alpha(Theme.darkerBackground, 0.84) }
                    }
                }
            }

            MenuAction {
                id: coverEditButton
                objectName: "coverEditButton"
                text: !(root.detailsEntry.igdbId > 0) ? "IDENTIFY GAME"
                      : !(root.game.coverPath || root.detailsEntry.portrait) ? "FIND COVER" : "GAME & ARTWORK"
                visible: !DemoMode
                onClicked: identifyPanel.open()
            }
            GlassButton {
                objectName: "pickAnotherButton"
                visible: root.randomSelection
                Layout.fillWidth: true
                displayScale: root.uiScale
                text: "PICK ANOTHER"
                onClicked: root.randomRequested()
            }

        }

        ScrollView {
            id: detailsScroll
            objectName: "detailsScroll"
            readonly property var navigationFlickable: contentItem
            readonly property real navigationContentY: contentItem ? contentItem.contentY : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: coverSidebar.right
            anchors.leftMargin: detailsArea.columnSpacing
            anchors.right: parent.right
            rightPadding: 18
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: detailsContent
                width: detailsScroll.availableWidth
                spacing: root.couchMode ? 20 * root.uiScale : 16

                Text {
                    Layout.fillWidth: true
                    id: gameTitle
                    objectName: "gameDetailsTitle"
                    maximumLineCount: root.titleExpanded ? 1000 : 3
                    elide: Text.ElideRight
                    HoverHandler { id: titleHover }
                    ToolTip.visible: titleHover.hovered && gameTitle.truncated
                    ToolTip.text: root.game.title || ""
                    text: root.displayTitle || "Unknown game"
                    textFormat: Text.PlainText
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: root.couchMode
                                    ? Math.max(28, Math.min(48, width * 0.065)) * root.uiScale
                                    : Math.max(26, Math.min(44, width * 0.065))
                    font.weight: Font.Bold
                    wrapMode: Text.Wrap
                }

                GlassButton {
                    objectName: "fullTitleButton"
                    visible: gameTitle.truncated || root.titleExpanded
                    compact: true
                    text: root.titleExpanded ? "SHORTEN TITLE" : "FULL TITLE"
                    onClicked: { root.titleExpanded = !root.titleExpanded; Qt.callLater(function() { root.revealFocusedItem(gameTitle) }) }
                }
                Flow {
                    id: identitySummary
                    objectName: "gameIdentitySummary"
                    Layout.fillWidth: true
                    spacing: 6 * root.uiScale
                    Text {
                        objectName: "gamePlatformRelease"
                        width: Math.min(implicitWidth, identitySummary.width)
                        text: {
                            const info = root.detailsEntry
                            const values = []
                            const platform = info.platformText || root.game.system
                            if (platform) values.push(platform)
                            if (info.releaseText) values.push((info.releaseLabel || "First catalog release") + ": " + info.releaseText)
                            else if (root.releaseYear > 0) values.push(String(root.releaseYear))
                            return values.join("  ·  ")
                        }
                        visible: text !== ""
                        textFormat: Text.PlainText
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        wrapMode: Text.Wrap
                    }
                    Text {
                        id: gameRating
                        objectName: "gameRating"
                        visible: root.detailsEntry.rating >= 0
                        width: Math.min(implicitWidth, identitySummary.width)
                        text: visible ? root.detailsEntry.rating + "/100 · IGDB" : ""
                        textFormat: Text.PlainText
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text + (root.detailsEntry.ratingCount > 0
                                                ? ", " + root.detailsEntry.ratingCount + " IGDB ratings" : "")
                        HoverHandler { id: ratingHover; enabled: gameRating.visible }
                        ToolTip {
                            objectName: "gameRatingTooltip"
                            visible: gameRating.visible && ratingHover.hovered && root.detailsEntry.ratingCount > 0
                            text: (root.detailsEntry.ratingCount || 0) + " IGDB ratings"
                            delay: 500
                            x: 0
                            y: gameRating.height + 4
                            width: Math.min(implicitWidth, detailsScroll.availableWidth)
                            margins: 8
                        }
                    }
                }
                Text {
                    objectName: "gameActivitySummary"
                    Layout.fillWidth: true
                    text: {
                        const values = []
                        const seconds = root.game.playtimeSeconds || (root.game.hours || 0) * 3600
                        values.push(seconds > 0 ? (root.game.playtimeText || root.game.hours + "h") + " played"
                                               : root.game.lastPlayed > 0 ? "Less than a minute recorded" : "Not played in Omakade")
                        if (root.game.lastPlayed > 0) values.push("Last played " + Qt.formatDate(new Date(root.game.lastPlayed * 1000), "MMM d, yyyy"))
                        if (root.game.completionStatus) values.push(root.game.completionStatus.charAt(0).toUpperCase() + root.game.completionStatus.slice(1))
                        const total = Achievements.total || root.game.achievementsTotal || 0
                        if (total > 0) values.push((Achievements.total > 0 ? Achievements.unlocked : root.game.achievementsUnlocked || 0) + "/" + total + " achievements")
                        return values.join("  ·  ")
                    }
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                    wrapMode: Text.Wrap
                }
                Text {
                    objectName: "playtimeProvenanceText"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: root.selectedInstallation.playtimeProvenance || ""
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 13 : 11) * root.uiScale
                    wrapMode: Text.Wrap
                }
                GlassButton {
                    id: playHistoryButton
                    objectName: "playHistoryButton"
                    Layout.alignment: Qt.AlignLeft
                    visible: root.recordedSessions.length > 0
                    compact: true
                    text: "PLAY HISTORY"
                    onClicked: playHistoryMenu.open()
                }
                Text {
                    objectName: "launchInstallationSummary"
                    Layout.fillWidth: true
                    text: "Launch with " + (root.selectedInstallation.source || "local installation")
                          + (root.selectedInstallation.runner ? " · " + root.selectedInstallation.runner : "")
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 14 : 11) * root.uiScale
                    wrapMode: Text.Wrap
                }

                Text {
                    objectName: "preferredUnavailableText"
                    Layout.fillWidth: true
                    visible: root.selectedInstallation.preferredUnavailable === true || (root.selectedInstallation.launchAvailable === false && root.selectedInstallation.installed !== false)
                    text: "This installation is unavailable. Choose another in Manage or reconnect its drive."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 16 : 11) * root.uiScale
                    wrapMode: Text.Wrap
                }

                Text {
                    objectName: "launchStatusText"
                    Layout.fillWidth: true
                    visible: root.launchMessage !== ""
                    text: (root.launchFailed ? "Launch failed: " : "") + root.launchMessage
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: (root.couchMode ? 16 : 12) * root.uiScale
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                GridLayout {
                    id: gameActions
                    objectName: "gameActions"
                    Layout.fillWidth: true
                    Layout.maximumWidth: columns * 220 * root.uiScale + (columns - 1) * columnSpacing
                    uniformCellWidths: true
                    Layout.alignment: Qt.AlignLeft
                    // One column below the width where two buttons and their text fit, for the
                    // same reason as the status grid: a GridLayout overflows rather than
                    // shrinking a child under its own label.
                    columns: detailsContent.width < 300 ? 1
                           : detailsContent.width < 620 ? 2 : 4
                    columnSpacing: 10
                    rowSpacing: 8

                    GlassButton {
                        id: playButton
                        Layout.fillWidth: true
                        objectName: "playButton"
                        property Item controllerUpTarget: backButton
                        property Item controllerRightTarget: favoriteButton
                        property Item controllerDownTarget:
                            gameActions.columns === 2 ? addToQueueButton : null
                        text: root.launchBusy ? "OPENING..." : root.selectedInstallation.installed === false && root.selectedInstallation.source === "Steam"
                              ? "INSTALL IN STEAM" : "PLAY"
                        iconText: root.selectedInstallation.installed === false && root.selectedInstallation.source === "Steam" ? "↓" : "▶"
                        primary: true
                        // Keep focus on this button while suppressing repeated launches.
                        Accessible.description: root.launchBusy ? "Launch request in progress" : ""
                        onClicked: if (!root.launchBusy) root.playRequested()
                        Component.onCompleted: forceActiveFocus()
                    }

                    GlassButton {
                        id: favoriteButton
                        Layout.fillWidth: true
                        objectName: "favoriteButton"
                        property Item controllerLeftTarget: playButton
                        property Item controllerRightTarget:
                            gameActions.columns === 4 ? addToQueueButton : null
                        property Item controllerDownTarget:
                            gameActions.columns === 2 ? detailManageButton : null
                        text: root.game.favorite ? "FAVORITE" : "ADD FAVORITE"
                        iconText: root.game.favorite ? "♥" : "♡"
                        onClicked: root.favoriteRequested()
                    }

                    GlassButton {
                        id: addToQueueButton
                        Layout.fillWidth: true
                        objectName: "addToQueueButton"
                        property Item controllerRightTarget: detailManageButton
                        property string addedIdentity: ""
                        property string currentIdentity: root.game.metadataKey || ""
                        onCurrentIdentityChanged: { addedIdentity = ""; saveFailed = false }
                        property bool saveFailed: false
                        text: saveFailed ? "RETRY ADD TO UP NEXT"
                              : addedIdentity !== "" && addedIdentity === root.game.metadataKey ? "ADDED TO UP NEXT" : "ADD TO UP NEXT"
                        onClicked: {
                            saveFailed = !Home.enqueue(root.game.source, root.game.runner || "", root.game.appId)
                            if (!saveFailed) addedIdentity = root.game.metadataKey || ""
                        }
                    }

                    GlassButton {
                        id: detailManageButton
                        Layout.fillWidth: true
                        objectName: "detailManageButton"
                        text: "MANAGE"
                        property Item controllerLeftTarget: addToQueueButton
                        property Item controllerUpTarget: gameActions.columns === 2 ? favoriteButton : null
                        onClicked: detailManage.open()
                    }
                }

                ColumnLayout {
                    id: gameInfoSection
                    objectName: "gameInfoSection"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 10
                    property var entry: Metadata !== null ? Metadata.current : null
                    property bool expanded: false

                    readonly property var facts: {
                        const info = gameInfoSection.entry
                        if (!info) {
                            return []
                        }
                        const values = []
                        if (info.genres && info.genres.length > 0) {
                            values.push(info.genres.join(" · "))
                        }
                        return values
                    }
                    readonly property string credits: {
                        const info = gameInfoSection.entry
                        if (!info) {
                            return ""
                        }
                        const parts = []
                        if (info.developers && info.developers.length > 0) {
                            parts.push("Developed by " + info.developers.join(", "))
                        }
                        if (info.publishers && info.publishers.length > 0) {
                            parts.push("Published by " + info.publishers.join(", "))
                        }
                        return parts.join(". ")
                    }
                    readonly property string background:
                        (gameInfoSection.entry ? gameInfoSection.entry.summary : "") || root.game.description || ""
                    visible: !game.isPortal
                             && (gameInfoSection.facts.length > 0 || gameInfoSection.credits !== ""
                                 || gameInfoSection.background !== ""
                                 || (!DemoMode && Metadata && Metadata.selectedStatus !== ""))

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "ABOUT THE GAME"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 13 * root.uiScale
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !DemoMode && Metadata && Metadata.selectedStatus !== ""
                        Text {
                            Layout.fillWidth: true
                            text: Metadata ? Metadata.selectedStatus : ""
                            wrapMode: Text.Wrap
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 13 * root.uiScale
                        }
                        GlassButton {
                            objectName: "detailsRetryButton"
                            text: "RETRY"
                            compact: true
                            enabled: Metadata && !Metadata.busy && (Metadata.selectedWritePending || (Insights && Insights.configured))
                            onClicked: Metadata.refreshSelected()
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 760 * root.uiScale
                        columns: 1
                        rowSpacing: 6
                        Text {
                            Layout.fillWidth: true
                            visible: gameInfoSection.background !== ""
                            Layout.row: gameInfoSection.expanded ? 1 : 0
                            id: gameDescription
                            objectName: "gameDescription"
                            text: gameInfoSection.background
                            Layout.maximumWidth: 760 * root.uiScale
                            textFormat: Text.PlainText
                            maximumLineCount: gameInfoSection.expanded ? 1000 : 3
                            elide: Text.ElideRight
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: (root.couchMode ? 17 : 13) * root.uiScale
                            lineHeight: 1.3
                            wrapMode: Text.Wrap
                        }
                        GlassButton {
                            Layout.row: gameInfoSection.expanded ? 0 : 1
                            id: descriptionToggle
                            objectName: "descriptionToggle"
                            visible: gameDescription.truncated || gameInfoSection.expanded
                            compact: true
                            text: gameInfoSection.expanded ? "READ LESS" : "READ MORE"
                            property Item controllerUpTarget: playButton
                            property Item controllerDownTarget: aliasesToggle.visible ? aliasesToggle : statusButtons.firstControl
                            onClicked: {
                                gameInfoSection.expanded = !gameInfoSection.expanded
                                Qt.callLater(function() { root.revealFocusedItem(descriptionToggle) })
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: gameInfoSection.facts.length > 0
                        text: gameInfoSection.facts.join("  ·  ")
                        textFormat: Text.PlainText
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        wrapMode: Text.Wrap
                    }
                    Text {
                        objectName: "gameCredits"
                        Layout.fillWidth: true
                        visible: gameInfoSection.credits !== ""
                        text: gameInfoSection.credits
                        textFormat: Text.PlainText
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        wrapMode: Text.Wrap
                    }
                    Text {
                        objectName: "regionalIdentityText"
                        Layout.fillWidth: true
                        readonly property var info: gameInfoSection.entry || ({})
                        text: {
                            const lines = []
                            if (info.romContext) lines.push(info.romContext)
                            if (info.title && root.comparableTitle(info.title) !== root.comparableTitle(info.localTitle || root.game.title)) {
                                lines.push("Catalog title: " + (info.title || ""))

                            }
                            return lines.join("\n")
                        }
                        visible: text !== ""
                        textFormat: Text.PlainText
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        wrapMode: Text.Wrap
                    }
                    GlassButton {
                        id: aliasesToggle
                        property Item controllerUpTarget: descriptionToggle.visible ? descriptionToggle : playButton
                        property Item controllerDownTarget: statusButtons.firstControl
                        objectName: "aliasesToggle"
                        readonly property var names: ((gameInfoSection.entry || {}).titleEvidence || []).filter((name, index, all) => all.indexOf(name) === index)
                        visible: names.length > 0
                        compact: true
                        text: (root.aliasesExpanded ? "HIDE OTHER NAMES" : "OTHER NAMES") + " (" + names.length + ")"
                        onClicked: root.aliasesExpanded = !root.aliasesExpanded
                    }
                    Text {
                        objectName: "aliasesText"
                        Layout.fillWidth: true
                        visible: aliasesToggle.visible && root.aliasesExpanded
                        text: aliasesToggle.names.join("\n")
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                    }
                    ProtonDbBadge {
                        id: detailsProtonBadge
                        Layout.fillWidth: true
                        gameSource: root.selectedInstallation.source || ""
                        appId: root.selectedInstallation.appId || ""
                        font.pixelSize: (root.couchMode ? 15 : 12) * root.uiScale
                        fetchEnabled: root.visible && !DemoMode
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: detailsProtonBadge.visible
                        text: detailsProtonBadge.details
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: (root.couchMode ? 13 : 11) * root.uiScale
                    }
                    Flow {
                        Layout.fillWidth: true
                        id: externalLinks
                        spacing: 8
                        visible: !DemoMode

                        GlassButton {
                            visible: root.selectedInstallation.source === "Steam"
                                     && /^[1-9][0-9]{0,9}$/.test(root.selectedInstallation.appId)
                                     && Number(root.selectedInstallation.appId) < 2147483648
                            compact: true
                            text: "PROTONDB"
                            onClicked: Qt.openUrlExternally(
                                "https://www.protondb.com/app/" + root.selectedInstallation.appId)
                        }

                        GlassButton {
                            objectName: "pcGamingWikiButton"
                            compact: true
                            text: "PCGAMINGWIKI"
                            onClicked: Qt.openUrlExternally(
                                "https://www.pcgamingwiki.com/w/index.php?search="
                                + encodeURIComponent(root.game.title || ""))
                        }
                    }

                    GlassButton {
                        objectName: "romDetailsToggle"
                        visible: root.displayTitle !== (root.game.title || "")
                        compact: true
                        text: root.romDetailsExpanded ? "HIDE ROM DETAILS" : "ROM DETAILS"
                        onClicked: root.romDetailsExpanded = !root.romDetailsExpanded
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 760 * root.uiScale
                        visible: root.romDetailsExpanded
                        text: (root.detailsEntry.romFilename || root.game.title || "")
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 12 * root.uiScale
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Game information from IGDB"
                        textFormat: Text.PlainText
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * root.uiScale
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    visible: root.showOrganizationControls
                    spacing: 9

                    Text {
                        text: "ORGANIZE"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.6
                    }

                    GridLayout {
                        id: statusLayout
                        objectName: "statusLayout"
                        Layout.fillWidth: true
                        // Two columns of buttons need about two hundred and thirty pixels, and
                        // a GridLayout does not shrink a child below the width of its own text:
                        // it overflows and the scroll view clips it. Drop to a single column
                        // before that happens rather than cutting the labels in half.
                        columns: detailsContent.width < 300 ? 1
                               : detailsContent.width < 560 ? 2 : 5
                        columnSpacing: 6
                        rowSpacing: 6
                        Text {
                            text: "STATUS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: statusLayout.columns === 1 ? -1 : 76
                            Layout.columnSpan: statusLayout.columns === 2 ? 2 : 1
                        }
                        Repeater {
                            id: statusButtons
                            property Item firstControl: null
                            onItemAdded: function(index, item) { if (index === 0) firstControl = item }
                            onItemRemoved: function(index, item) { if (firstControl === item) firstControl = null }
                            model: ["backlog", "playing", "completed", "abandoned"]
                            GlassButton {
                                required property string modelData
                                required property int index
                                objectName: "completionStatus-" + modelData
                                property Item controllerUpTarget: index === 0 ? (aliasesToggle.visible ? aliasesToggle : descriptionToggle.visible ? descriptionToggle : playButton) : null
                                compact: true
                                Layout.fillWidth: true
                                text: modelData.toUpperCase()
                                selected: (root.game.completionStatus || "") === modelData
                                onClicked: root.completionStatusRequested(
                                               selected ? "" : modelData)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "TAGS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: statusLayout.columns === 1 ? -1 : 76
                        }
                        TextField {
                            id: tagsField
                            objectName: "detailsTagsField"
                            property Item controllerRightTarget: tagsFieldClear.visible ? tagsFieldClear : null
                            rightPadding: tagsFieldClear.reservedWidth
                            FieldClearButton { id: tagsFieldClear; field: tagsField }
                            property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                            Layout.fillWidth: true
                            placeholderText: "Co-op, cozy, difficult"
                            Accessible.name: "Tags"
                            // Copy the saved tags in instead of binding so an achievement
                            // refresh or rescan mid-edit cannot overwrite what is being typed.
                            readonly property string savedText: root.game.tags ? root.game.tags.join(", ") : ""
                            onSavedTextChanged: if (!activeFocus) text = savedText
                            Component.onCompleted: text = savedText
                            color: Theme.foreground
                            placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                            font.family: Theme.fontFamily
                            selectByMouse: true
                            background: Rectangle {
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: tagsField.activeFocus
                                              ? Theme.accent
                                              : root.alpha(Theme.foreground, 0.15)
                            }
                            Keys.onReturnPressed: function(event) {
                                if (TextEntry.keyboardNeeded) {
                                    root.textEntryRequested(tagsField, "EDIT TAGS", false,
                                                            tagsField.placeholderText)
                                    event.accepted = true
                                } else {
                                    root.tagsRequested(text)
                                }
                            }
                            Keys.onEnterPressed: function(event) {
                                if (TextEntry.keyboardNeeded) {
                                    root.textEntryRequested(tagsField, "EDIT TAGS", false,
                                                            tagsField.placeholderText)
                                    event.accepted = true
                                } else {
                                    root.tagsRequested(text)
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            text: "SAVE"
                            onClicked: root.tagsRequested(tagsField.text)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "COLLECTIONS"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: statusLayout.columns === 1 ? -1 : 76
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            objectName: "collectionsScroll"
                            Layout.preferredHeight: collectionButtons.implicitHeight + 12 * root.uiScale
                            contentHeight: collectionButtons.implicitHeight
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                            Row {
                                id: collectionButtons
                                spacing: 6
                                Repeater {
                                    model: Library.collectionNames
                                    GlassButton {
                                        required property string modelData
                                        compact: true
                                        text: modelData.toUpperCase()
                                        selected: root.game.collections
                                                  ? root.game.collections.indexOf(modelData) >= 0
                                                  : false
                                        onClicked: root.collectionToggled(modelData, !selected)
                                    }
                                }
                                GlassButton {
                                    id: newCollectionButton
                                    objectName: "newCollectionButton"
                                    property Item controllerDownTarget:
                                        metadataEditor.visible && metadataEditor.firstControl.enabled
                                        ? metadataEditor.firstControl
                                        : insightRefreshButton.visible && insightRefreshButton.enabled
                                          ? insightRefreshButton
                                          : achievementSortButton.visible && achievementSortButton.enabled
                                            ? achievementSortButton
                                            : achievementRefreshButton.visible && achievementRefreshButton.enabled
                                              ? achievementRefreshButton : null
                                    compact: true
                                    text: "+ NEW COLLECTION"
                                    onClicked: {
                                        root.collectionEditorOpen = true
                                        Qt.callLater(function() {
                                            if (TextEntry.keyboardNeeded) {
                                                root.textEntryRequested(
                                                    collectionField, "NEW COLLECTION", false,
                                                    collectionField.placeholderText)
                                            } else {
                                                collectionField.forceActiveFocus()
                                            }
                                        })
                                    }
                                }
                            }
                        }
                    }

                    GridLayout {
                        id: collectionEditor
                        Layout.fillWidth: true
                        visible: root.collectionEditorOpen
                        columns: detailsContent.width < 600 ? 2 : 4
                        columnSpacing: 8
                        rowSpacing: 8
                        Text {
                            text: "NEW"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            Layout.preferredWidth: 76
                            Layout.columnSpan: collectionEditor.columns === 2 ? 2 : 1
                        }
                        TextField {
                            id: collectionField
                            property Item controllerRightTarget: collectionFieldClear.visible ? collectionFieldClear : null
                            rightPadding: collectionFieldClear.reservedWidth
                            FieldClearButton { id: collectionFieldClear; field: collectionField }
                            property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
                            Layout.fillWidth: true
                            Layout.maximumWidth: 360
                            Layout.columnSpan: collectionEditor.columns === 2 ? 2 : 1
                            placeholderText: "New collection"
                            Accessible.name: placeholderText
                            color: Theme.foreground
                            placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                            font.family: Theme.fontFamily
                            selectByMouse: true
                            background: Rectangle {
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: collectionField.activeFocus
                                              ? Theme.accent
                                              : root.alpha(Theme.foreground, 0.15)
                            }
                            Keys.onReturnPressed: {
                                if (TextEntry.keyboardNeeded) {
                                    root.textEntryRequested(collectionField, "NEW COLLECTION",
                                                            false, collectionField.placeholderText)
                                } else {
                                    root.collectionCreateRequested(text)
                                    clear()
                                }
                            }
                            Keys.onEnterPressed: {
                                if (TextEntry.keyboardNeeded) {
                                    root.textEntryRequested(collectionField, "NEW COLLECTION",
                                                            false, collectionField.placeholderText)
                                } else {
                                    root.collectionCreateRequested(text)
                                    clear()
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            Layout.fillWidth: collectionEditor.columns === 2
                            text: "CREATE + ADD"
                            onClicked: {
                                root.collectionCreateRequested(collectionField.text)
                                collectionField.clear()
                            }
                        }
                        GlassButton {
                            compact: true
                            Layout.fillWidth: collectionEditor.columns === 2
                            text: "CANCEL"
                            onClicked: root.closeCollectionEditor()
                        }
                    }
                }

                ColumnLayout {
                    id: insightsSection
                    objectName: "insightsSection"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 10
                    visible: root.selectedInstallation.source === "Steam" && Insights !== null
                    readonly property var metrics: {
                        if (!Insights) {
                            return []
                        }
                        const values = []
                        if (Insights.criticScore >= 0) {
                            values.push({ label: "IGDB CRITIC",
                                          value: Insights.criticScore + " / 100" })
                        }
                        if (Insights.rushedHours > 0) {
                            values.push({ label: "RUSHED",
                                          value: Insights.rushedHours + " H" })
                        }
                        if (Insights.normalHours > 0) {
                            values.push({ label: "MAIN + EXTRAS",
                                          value: Insights.normalHours + " H" })
                        }
                        if (Insights.completeHours > 0) {
                            values.push({ label: "COMPLETIONIST",
                                          value: Insights.completeHours + " H" })
                        }
                        return values
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "GAME INSIGHTS · IGDB"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                        }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            id: insightRefreshButton
                            objectName: "insightRefreshButton"
                            property Item controllerUpTarget:
                                metadataEditor.visible && metadataEditor.lastControl.enabled
                                ? metadataEditor.lastControl : newCollectionButton
                            property Item controllerDownTarget:
                                achievementSortButton.visible && achievementSortButton.enabled
                                ? achievementSortButton
                                : achievementRefreshButton.visible && achievementRefreshButton.enabled
                                  ? achievementRefreshButton : null
                            compact: true
                            text: Insights && Insights.configured
                                  ? (Insights.refreshing ? "REFRESHING" : "REFRESH")
                                  : "CONNECT IGDB"
                            enabled: Insights && !Insights.refreshing
                            onClicked: {
                                if (Insights.configured) {
                                    Insights.refreshSteam(root.selectedInstallation.appId)
                                } else {
                                    root.connectRequested()
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Insights ? Insights.statusText : ""
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        id: insightsGrid
                        Layout.fillWidth: true
                        visible: insightsSection.metrics.length > 0
                        readonly property real minimumMetricWidth: 130
                        columns: Math.max(1, Math.min(
                                              insightsSection.metrics.length,
                                              Math.floor((detailsContent.width + columnSpacing)
                                                         / (minimumMetricWidth + columnSpacing))))
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: insightsSection.metrics

                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: insightsGrid.minimumMetricWidth
                                Layout.maximumWidth: 340
                                Layout.preferredHeight: 72
                                radius: Math.max(5, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, 0.045)
                                border.color: root.alpha(Theme.foreground, 0.13)

                                Column {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14
                                    spacing: 6
                                    Text {
                                        text: modelData.label
                                        color: Theme.mutedText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 8
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: modelData.value
                                        color: Theme.brightForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: insightsSection.metrics.length > 0
                        text: "Critic aggregate and time estimates provided by IGDB"
                        color: root.alpha(Theme.foreground, 0.48)
                        font.family: Theme.fontFamily
                        font.pixelSize: 8
                    }
                }

                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: root.reviewReasons.length > 0; text: "Needs review: " + root.reviewReasons.join(", "); color: Theme.mutedText }
                LaunchSetupPanel {
                    id: launchSetup
                    Layout.fillWidth: true
                    installation: root.selectedInstallation
                    onTextEntryRequested: (target,title,password,placeholder) => root.textEntryRequested(target,title,password,placeholder)
                }

                ColumnLayout {
                    id: achievementListSection
                    objectName: "achievementListSection"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 9
                    visible: root.selectedInstallation.source === "Steam"
                             || root.selectedInstallation.source === "RetroArch"

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "ACHIEVEMENT PROGRESS"
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: Achievements.total > 0 ? Math.round(Achievements.unlocked * 100 / Achievements.total) + "%" : (root.game.progress || 0) + "%"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        radius: 3
                        color: root.alpha(Theme.foreground, 0.1)

                        Rectangle {
                            width: parent.width * (Achievements.total > 0
                                                   ? Achievements.unlocked / Achievements.total
                                                   : (root.game.progress || 0) / 100)
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    spacing: 10
                    visible: root.selectedInstallation.source === "Steam"
                             || root.selectedInstallation.source === "RetroArch"

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "ACHIEVEMENTS"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            id: achievementSortButton
                            objectName: "achievementSortButton"
                            property Item controllerUpTarget:
                                insightRefreshButton.visible && insightRefreshButton.enabled
                                ? insightRefreshButton
                                : metadataEditor.visible && metadataEditor.lastControl.enabled
                                  ? metadataEditor.lastControl : newCollectionButton
                            property Item controllerRightTarget:
                                achievementRefreshButton.visible && achievementRefreshButton.enabled
                                ? achievementRefreshButton : null
                            visible: Achievements.total > 1
                            compact: true
                            text: Achievements.sortMode === 0
                                  ? "SORT: STATUS" : "SORT: UNLOCK DATE"
                            onClicked: Achievements.sortMode = (Achievements.sortMode + 1) % 2
                        }
                        GlassButton {
                            id: achievementRefreshButton
                            objectName: "achievementRefreshButton"
                            property Item controllerUpTarget:
                                insightRefreshButton.visible && insightRefreshButton.enabled
                                ? insightRefreshButton
                                : metadataEditor.visible && metadataEditor.lastControl.enabled
                                  ? metadataEditor.lastControl : newCollectionButton
                            property Item controllerLeftTarget:
                                achievementSortButton.visible && achievementSortButton.enabled
                                ? achievementSortButton : null
                            visible: root.achievementAccount !== null
                            compact: true
                            text: root.achievementAccount && root.achievementAccount.hasApiKey
                                  ? (root.achievementAccount.busy ? "REFRESHING"
                                     : root.achievementSourceIsRetroArch ? "REFRESH RETROACHIEVEMENTS"
                                     : "REFRESH STEAM")
                                  : root.achievementSourceIsRetroArch ? "CONNECT RETROACHIEVEMENTS"
                                                                       : "CONNECT STEAM"
                            enabled: !root.achievementAccount || !root.achievementAccount.busy
                            onClicked: {
                                if (root.achievementAccount.hasApiKey) {
                                    root.achievementAccount.refreshAchievements(
                                                root.selectedInstallation.appId)
                                } else {
                                    root.connectRequested()
                                }
                            }
                        }
                        Text {
                            Layout.leftMargin: 10 * root.uiScale
                            text: Achievements.unlocked + " / " + Achievements.total
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Achievements.statusText
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.achievementAccount && root.achievementAccount.statusText.length > 0
                        text: root.achievementAccount ? root.achievementAccount.statusText : ""
                        color: root.achievementAccount && (root.achievementAccount.state === "invalid-key"
                                                || root.achievementAccount.state === "private"
                                                || root.achievementAccount.state === "unsupported"
                                                || root.achievementAccount.state === "rate-limited")
                               ? Theme.yellow : Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        id: achievementGrid
                        Layout.fillWidth: true
                        visible: Achievements.total > 0
                        columns: detailsContent.width < 620 ? 1 : 2
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: Achievements

                            Rectangle {
                                required property int index
                                required property string title
                                required property string description
                                required property string iconPath
                                required property bool unlocked
                                required property double unlockTime
                                required property real rarity
                                required property bool hidden
                                objectName: "achievementCard" + index
                                activeFocusOnTab: true
                                Accessible.name: title
                                Accessible.role: Accessible.ListItem
                                Accessible.focused: activeFocus
                                Layout.fillWidth: true
                                Layout.minimumWidth: 260
                                Layout.preferredHeight: 82
                                radius: Math.max(6, Theme.cornerRadius)
                                color: root.alpha(Theme.foreground, unlocked ? 0.075 : 0.035)
                                border.width: activeFocus ? 2 : 1
                                border.color: activeFocus
                                              ? Theme.accent
                                              : unlocked
                                                ? root.alpha(Theme.accent, 0.34)
                                                : root.alpha(Theme.foreground, 0.10)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 11
                                    spacing: 12

                                    Rectangle {
                                        Layout.preferredWidth: 54
                                        Layout.preferredHeight: 54
                                        radius: 5
                                        color: root.alpha(Theme.darkerBackground, 0.54)
                                        border.color: root.alpha(Theme.foreground, 0.12)
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            source: iconPath
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                            opacity: unlocked ? 1 : 0.42
                                        }
                                        Text {
                                            visible: iconPath.length === 0
                                            anchors.centerIn: parent
                                            text: unlocked ? "◆" : "◇"
                                            color: unlocked ? Theme.accent : Theme.mutedText
                                            font.pixelSize: 19
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: hidden && !unlocked ? "Hidden achievement" : title
                                            textFormat: Text.PlainText
                                            color: unlocked ? Theme.brightForeground : Theme.foreground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: hidden && !unlocked ? "Unlock to reveal details" : description
                                            textFormat: Text.PlainText
                                            color: Theme.mutedText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: (unlocked && unlockTime > 0
                                                   ? "UNLOCKED " + Qt.formatDateTime(new Date(unlockTime * 1000), "MMM d, yyyy").toUpperCase() + "  ·  "
                                                   : "")
                                                  + (rarity > 0 ? rarity.toFixed(1) + "% OF PLAYERS"
                                                     : root.achievementSourceIsRetroArch ? "RETROACHIEVEMENTS" : "STEAM")
                                            color: unlocked ? Theme.accent : root.alpha(Theme.foreground, 0.45)
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
        }
    }

    Row {
        id: detailsFooter
        objectName: "detailsFooter"
        parent: root
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 54 * root.uiScale
        anchors.bottomMargin: 20 * root.uiScale
        spacing: 20 * root.uiScale
        visible: root.couchMode
        z: 20

        Repeater {
            model: [
                { glyph: Controller.primaryGlyph, label: "SELECT" },
                { glyph: Controller.backGlyph, label: "BACK" },
                { glyph: Controller.favoriteGlyph, label: "FAVORITE" },
                { glyph: "START", label: "DESKTOP" }
            ]

            Row {
                required property var modelData
                spacing: 7 * root.uiScale

                Rectangle {
                    width: Math.max(31 * root.uiScale, glyphText.implicitWidth + 14 * root.uiScale)
                    height: 31 * root.uiScale
                    radius: height / 2
                    color: root.alpha(Theme.foreground, 0.12)
                    border.color: root.alpha(Theme.foreground, 0.22)

                    Text {
                        id: glyphText
                        anchors.centerIn: parent
                        text: modelData.glyph
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.uiScale
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }
            }
        }
    }
}
    ActionMenu {
        id: identifyPanel
        objectName: "identifyGamePanel"
        host: root.Window.window
        anchorItem: coverEditButton
        title: "GAME & ARTWORK"
        width: Math.min(760 * root.uiScale, root.width - 48)
        height: Math.min(implicitHeight, host.height - 48, 820 * root.uiScale)
        showCloseButton: false
        fixedHeader: true
        doneObjectName: "metadataArtworkButton"
        headerDownTarget: metadataEditor.firstBodyControl
        GameMetadataEditor {
            id: metadataEditor
            objectName: "metadataEditor"
            panelMode: true
            externalDone: identifyPanel.doneControl
            onLocalArtworkRequested: identifyPanel.invoke(root.coverRequested)
            onConnectionsRequested: identifyPanel.invoke(root.connectRequested)
            game: root.game
            couchMode: root.couchMode
            uiScale: root.uiScale
            previousSection: null
            nextSection: null
            onTextEntryRequested: (target, title, password, placeholder) => root.textEntryRequested(target, title, password, placeholder)
        }

    }
    Connections {
        target: identifyPanel
        function onOpened() {
            metadataEditor.editing = true
            Qt.callLater(function() { root.Window.window.focusWithin(identifyPanel.contentItem, true, metadataEditor.firstControl) })
        }
        function onClosed() { metadataEditor.editing = false }
    }
    Connections {
        target: metadataEditor
        function onEditingChanged() { if (!metadataEditor.editing && identifyPanel.opened) identifyPanel.close() }
    }

    ActionMenu {
        id: detailManage
        objectName: "detailManageMenu"
        host: root.Window.window
        anchorItem: detailManageButton
        title: "MANAGE GAME"
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.installations.length > 1
                    spacing: 7
                    Text {
                        text: "LAUNCH WITH"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 1
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            id: installationButtons
                            model: root.installations
                            MenuAction {
                                required property var modelData
                                required property int index

                                objectName: "installationChoice_" + index
                                compact: true
                                text: (modelData.source || "LOCAL").toUpperCase()
                                      + (modelData.runner ? " · " + modelData.runner.toUpperCase() : "")
                                      + (modelData.preferred ? " · DEFAULT" : "")
                                selected: root.selectedInstallation.source === modelData.source
                                          && (root.selectedInstallation.runner || "") === (modelData.runner || "")
                                          && root.selectedInstallation.appId === modelData.appId
                                onClicked: { root.installationSelected(modelData); detailManage.close() }
                            }
                        }
                    }
                }


        MenuAction {
            id: manageButton
            Layout.fillWidth: true
            compact: true
            objectName: "manageButton"
            visible: root.selectedInstallation.source === "Steam" || root.selectedInstallation.source === "Lutris" || root.selectedInstallation.source === "Heroic" || root.selectedInstallation.source === "GOG" || root.selectedInstallation.source === "Faugus" || root.selectedInstallation.source === "RetroArch" || root.selectedInstallation.source === "PCSX2" || root.selectedInstallation.source === "Ryujinx" || root.selectedInstallation.source === "shadPS4" || root.selectedInstallation.source === "Cemu" || root.selectedInstallation.source === "Dolphin" || root.selectedInstallation.source === "Battle.net"
            text: "MANAGE IN " + (root.selectedInstallation.source || "LAUNCHER").toUpperCase()
            onClicked: detailManage.invoke(root.manageRequested)
        }
        MenuAction {
            Layout.fillWidth: true
            compact: true
            objectName: "launchSetupMenuButton"
            text: "LAUNCH SETUP"
            onClicked: detailManage.invoke(root.showLaunchSetup)
        }
        MenuAction {
            Layout.fillWidth: true
            compact: true
            objectName: "saveBackupsButton"
            visible: root.saveSourceSupported
            text: "SAVE BACKUPS"
            onClicked: detailManage.invoke(root.showSaveBackups)
        }
        MenuAction {
            id: hideButton
            Layout.fillWidth: true
            compact: true
            objectName: "hideButton"
            text: root.game.hidden ? "UNHIDE" : "HIDE"
            onClicked: detailManage.invoke(root.hiddenRequested)
        }
        MenuAction {
            id: pinButton
            Layout.fillWidth: true
            compact: true
            objectName: "pinButton"
            // Games of a system that lives behind a console card can
            // still hold a spot in the main library.
            visible: !root.game.isPortal && !!root.game.system && Preferences.consolePortalsEnabled && Preferences.consoleLayout(root.game.system) === "card"
            text: root.game.pinned ? "SHOW ONLY INSIDE CONSOLE" : "SHOW BESIDE CONSOLE"
            onClicked: detailManage.invoke(root.pinRequested)
        }
        MenuAction {
            Layout.fillWidth: true
            compact: true
            objectName: "editManualGameButton"
            visible: root.selectedInstallation.source === "Manual"
            text: "EDIT MANUAL GAME"
            onClicked: detailManage.invoke(root.manualEditRequested)
        }
        MenuAction {
            Layout.fillWidth: true
            compact: true
            objectName: "preferredInstallationButton"
            visible: root.installations.length > 1
            text: root.selectedInstallation.preferred ? "DEFAULT INSTALLATION" : "MAKE DEFAULT"
            enabled: !root.selectedInstallation.preferred
            onClicked: detailManage.invoke(root.preferredInstallationRequested)
        }
        MenuAction {
            Layout.fillWidth: true
            compact: true
            visible: !DemoMode
            text: root.game.linked ? "UNLINK INSTALLATIONS" : "LINK INSTALLATION"
            onClicked: detailManage.invoke(function () {
                root.game.linked ? root.unlinkRequested() : root.linkRequested()
            })
        }
    }

    ActionMenu {
        id: playHistoryMenu
        objectName: "playHistoryMenu"
        showCloseButton: false
        host: root.Window.window
        anchorItem: playHistoryButton
        title: "PLAY HISTORY"
        fixedHeader: true
        preferredWidth: 460
        doneObjectName: "playHistoryDoneButton"
        initialFocus: doneControl
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            text: SessionRecorderStatus && SessionRecorderStatus.enabled
                  ? "Recent sessions recorded locally by Omakade."
                  : "Recording is off. Existing local history is retained."
        }
        Repeater {
            model: root.recordedSessions
            Rectangle {
                required property var modelData
                required property int index
                objectName: "playHistoryEntry_" + index
                Layout.fillWidth: true
                implicitHeight: 50 * root.uiScale
                radius: Math.max(4, Theme.cornerRadius)
                color: root.alpha(Theme.foreground, 0.045)
                border.color: root.alpha(Theme.foreground, 0.16)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9 * root.uiScale
                    spacing: 3 * root.uiScale
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: Qt.formatDateTime(new Date(modelData.startedAt * 1000),
                                                    "MMM d, yyyy  ·  h:mm AP")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * root.uiScale
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            text: (modelData.active ? "IN PROGRESS  ·  " : "")
                                  + root.sessionDurationText(modelData.seconds)
                            color: modelData.active ? Theme.accent : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * root.uiScale
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.source || "Omakade"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * root.uiScale
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    SaveBackupMenu {
        id: saveBackupsMenu
        host: root.Window.window
        anchorItem: detailManageButton
    }

}

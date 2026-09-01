import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "screens"

ApplicationWindow {
    id: root

    property bool detailOpen: false
    property var selectedGame: ({})
    property var selectedInstallation: ({})
    property var selectedInstallations: []
    property var linkResults: []
    property int selectedIndex: -1
    property bool smokeReady: false
    property bool diagnosticsOpen: false
    property bool linkDialogOpen: false
    property bool collectionDeleteOpen: false
    property string pendingCollectionDelete: ""

    function isWithin(item, container) {
        while (item) {
            if (item === container) {
                return true
            }
            item = item.parent
        }
        return false
    }

    function navigationContainer() {
        if (collectionDeleteOpen) {
            return collectionDeleteOverlay
        }
        if (linkDialogOpen) {
            return linkDialogOverlay
        }
        if (diagnosticsOpen) {
            return settingsOverlay
        }
        if (detailOpen && detailsLoader.item) {
            return detailsLoader.item
        }
        return null
    }

    function focusWithin(container, forward, preferred) {
        if (!container) {
            return
        }
        if (preferred && preferred.visible && preferred.enabled) {
            preferred.forceActiveFocus(forward ? Qt.TabFocusReason
                                               : Qt.BacktabFocusReason)
            revealNavigationItem(container, preferred)
            return
        }
        const current = root.activeFocusItem
        const origin = root.isWithin(current, container) ? current : container
        let candidate = origin.nextItemInFocusChain(forward)
        for (let attempts = 0; candidate && attempts < 300; ++attempts) {
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab) {
                candidate.forceActiveFocus(forward ? Qt.TabFocusReason
                                                   : Qt.BacktabFocusReason)
                revealNavigationItem(container, candidate)
                return
            }
            candidate = candidate.nextItemInFocusChain(forward)
        }
    }

    function focusSpatial(container, key) {
        if (!container) {
            return
        }
        const current = root.activeFocusItem
        if (!root.isWithin(current, container)) {
            root.focusWithin(container, true)
            return
        }
        const currentCenter = current.mapToItem(container, current.width / 2,
                                                current.height / 2)
        let best = null
        let bestScore = Number.MAX_VALUE
        let candidate = current.nextItemInFocusChain(true)
        for (let attempts = 0; candidate && candidate !== current
             && attempts < 300; ++attempts) {
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab) {
                const center = candidate.mapToItem(container, candidate.width / 2,
                                                   candidate.height / 2)
                const dx = center.x - currentCenter.x
                const dy = center.y - currentCenter.y
                let primary = 0
                let cross = 0
                if (key === Qt.Key_Up) {
                    primary = -dy
                    cross = Math.abs(dx)
                } else if (key === Qt.Key_Down) {
                    primary = dy
                    cross = Math.abs(dx)
                } else if (key === Qt.Key_Left) {
                    primary = -dx
                    cross = Math.abs(dy)
                } else if (key === Qt.Key_Right) {
                    primary = dx
                    cross = Math.abs(dy)
                }
                if (primary > 3) {
                    const score = primary + cross * 2.5
                    if (score < bestScore) {
                        best = candidate
                        bestScore = score
                    }
                }
            }
            candidate = candidate.nextItemInFocusChain(true)
        }
        if (best) {
            best.forceActiveFocus(Qt.TabFocusReason)
            root.revealNavigationItem(container, best)
        }
    }

    function revealInScrollView(scrollView, item) {
        const flickable = scrollView ? scrollView.contentItem : null
        if (!flickable || !item) {
            return
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

    function revealNavigationItem(container, item) {
        if (container === settingsOverlay) {
            root.revealInScrollView(settingsScroll, item)
        } else if (container === linkDialogOverlay && root.isWithin(item, candidateList)) {
            candidateList.positionViewAtIndex(candidateList.currentIndex, ListView.Contain)
        } else if (container === detailsLoader.item) {
            container.revealFocusedItem(item)
        }
    }

    function restoreFocus(item) {
        if (item && item.visible && item.enabled) {
            Qt.callLater(item.forceActiveFocus)
        } else if (detailOpen && detailsLoader.item) {
            Qt.callLater(function() { root.focusWithin(detailsLoader.item, true) })
        } else {
            Qt.callLater(libraryView.focusGrid)
        }
    }

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    function scanTime(seconds) {
        if (!seconds) {
            return "Not scanned yet"
        }
        return new Date(seconds * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    function openGame(index) {
        selectedIndex = index
        selectedGame = Library.get(index)
        selectedInstallations = Library.installations(index)
        selectedInstallation = selectedInstallations.length > 0
                               ? selectedInstallations[0] : selectedGame
        if (!DemoMode && selectedInstallation.source === "Steam") {
            Achievements.load(selectedInstallation.appId)
            if (SteamAccount) {
                SteamAccount.refreshAchievementsIfStale(selectedInstallation.appId)
            }
            if (Insights) {
                Insights.loadSteam(selectedInstallation.appId)
            }
        } else {
            Achievements.load("")
            if (Insights) {
                Insights.loadSteam("")
            }
        }
        detailOpen = true
    }

    function closeDetails() {
        detailOpen = false
        Qt.callLater(libraryView.focusGrid)
    }

    function showToast(message) {
        toast.message = message
        toastTimer.restart()
    }

    function nextFilter(current, values) {
        if (!values || values.length === 0) {
            return ""
        }
        const index = values.indexOf(current)
        return index < 0 ? values[0] : index + 1 < values.length ? values[index + 1] : ""
    }

    function filterLabel(prefix, value) {
        if (!value || value.length === 0) {
            return prefix
        }
        const shortened = value.length > 16 ? value.substring(0, 15) + "…" : value
        return prefix + ": " + shortened.toUpperCase()
    }

    function confirmCollectionDelete() {
        const name = pendingCollectionDelete
        const source = selectedGame.source || ""
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId || ""
        if (Library.deleteCollection(name)) {
            if (detailOpen) {
                refreshSelected(source, runner, appId)
            }
            showToast("Deleted " + name)
        }
        collectionDeleteOpen = false
        pendingCollectionDelete = ""
    }

    function refreshAfterOrganization() {
        const source = selectedGame.source
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId
        if (!refreshSelected(source, runner, appId)) {
            closeDetails()
        }
    }

    function playSelected() {
        if (DemoMode) {
            showToast("Demo games cannot be launched")
        } else if (Launcher.launch(selectedInstallation.source, selectedInstallation.appId,
                                   selectedInstallation.flatpak || false,
                                   selectedInstallation.runner || "",
                                   selectedInstallation.installPath || "",
                                   selectedInstallation.launchTarget || "")) {
            Library.recordLaunch(selectedIndex, selectedInstallation.source,
                                 selectedInstallation.runner || "", selectedInstallation.appId)
            showToast("Opening " + selectedGame.title + " in " + selectedInstallation.source)
            if (Preferences.closeAfterLaunch) {
                Qt.callLater(Qt.quit)
            }
        } else {
            showToast(Launcher.lastError)
        }
    }

    function installSelected() {
        if (DemoMode) {
            showToast("Demo games cannot be installed")
        } else if (Launcher.install(selectedInstallation.source, selectedInstallation.appId)) {
            showToast("Asking Steam to install " + selectedGame.title)
        } else {
            showToast(Launcher.lastError)
        }
    }

    function manageSelected() {
        if (Launcher.manage(selectedInstallation.source, selectedInstallation.appId,
                            selectedInstallation.flatpak || false,
                            selectedInstallation.runner || "")) {
            showToast("Opening " + selectedInstallation.source)
        } else {
            showToast(Launcher.lastError)
        }
    }

    function selectInstallation(installation) {
        selectedInstallation = installation
        if (!DemoMode && installation.source === "Steam") {
            Achievements.load(installation.appId)
            if (SteamAccount) {
                SteamAccount.refreshAchievementsIfStale(installation.appId)
            }
            if (Insights) {
                Insights.loadSteam(installation.appId)
            }
        } else {
            Achievements.load("")
            if (Insights) {
                Insights.loadSteam("")
            }
        }
    }

    function refreshSelected(source, runner, appId) {
        for (let index = 0; index < Library.rowCount(); ++index) {
            const game = Library.get(index)
            if (game.source === source && (game.runner || "") === (runner || "")
                    && game.appId === appId) {
                selectedIndex = index
                selectedGame = game
                selectedInstallations = Library.installations(index)
                selectedInstallation = selectedInstallations.length > 0
                                       ? selectedInstallations[0] : selectedGame
                return true
            }
        }
        return false
    }

    function linkCandidate(candidate) {
        const source = selectedGame.source
        const runner = selectedGame.runner || ""
        const appId = selectedGame.appId
        if (Library.linkGames(selectedIndex, candidate.source,
                              candidate.runner || "", candidate.appId)) {
            refreshSelected(source, runner, appId)
            showToast("Installations linked")
            linkDialogOpen = false
        }
    }

    FileDialog {
        id: coverDialog
        title: "Choose cover artwork"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: {
            if (Library.setCustomCover(root.selectedIndex, selectedFile)) {
                root.selectedGame = Library.get(root.selectedIndex)
                root.showToast("Cover updated")
            } else {
                root.showToast("That image could not be used")
            }
        }
    }

    visible: true
    width: 1380
    height: 880
    minimumWidth: 820
    minimumHeight: 590
    title: "Omakade"
    color: "transparent"

    font.family: Theme.fontFamily

    Shortcut {
        sequence: "Ctrl+F"
        enabled: !root.detailOpen && !root.diagnosticsOpen && !root.linkDialogOpen
                 && !root.collectionDeleteOpen
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "F11"
        onActivated: root.visibility = root.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }
    Shortcut {
        sequence: "Ctrl+M"
        onActivated: {
            Preferences.reducedMotion = !Preferences.reducedMotion
            root.showToast(Preferences.reducedMotion ? "Reduced motion enabled" : "Reduced motion disabled")
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: !root.linkDialogOpen && !root.collectionDeleteOpen
        onActivated: root.diagnosticsOpen = !root.diagnosticsOpen
    }
    Shortcut {
        sequence: "Tab"
        enabled: root.navigationContainer() !== null
        onActivated: root.focusWithin(root.navigationContainer(), true)
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.navigationContainer() !== null
        onActivated: root.focusWithin(root.navigationContainer(), false)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.linkDialogOpen) {
                root.linkDialogOpen = false
            } else if (root.collectionDeleteOpen) {
                root.collectionDeleteOpen = false
                root.pendingCollectionDelete = ""
            } else if (root.diagnosticsOpen) {
                root.diagnosticsOpen = false
            } else if (root.detailOpen) {
                root.closeDetails()
            } else if (searchField.text.length > 0) {
                searchField.clear()
                libraryView.focusGrid()
            } else if (!libraryView.gridFocused) {
                libraryView.focusGrid()
            }
        }
    }

    Binding {
        target: Controller
        property: "focusNavigation"
        value: root.detailOpen || root.diagnosticsOpen || root.linkDialogOpen
               || root.collectionDeleteOpen || !libraryView.gridFocused
    }
    Shortcut {
        sequence: "Return"
        enabled: root.navigationContainer() === null && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: root.navigationContainer() === null && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Space"
        enabled: root.navigationContainer() === null && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }

    onActiveChanged: {
        if (active && root.navigationContainer() === null && !searchField.activeFocus) {
            Qt.callLater(libraryView.focusGrid)
        }
    }
    onClosing: function(close) {
        close.accepted = true
        Qt.quit()
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha) }
            GradientStop { position: 0.48; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha * 0.88) }
            GradientStop { position: 1.0; color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha) }
        }
    }

    Rectangle {
        width: root.width * 0.52
        height: width
        radius: width / 2
        x: root.width * 0.62
        y: -height * 0.62
        color: root.alpha(Theme.accent, 0.10)
    }

    Rectangle {
        width: root.width * 0.42
        height: width
        radius: width / 2
        x: -width * 0.48
        y: root.height * 0.48
        color: root.alpha(Theme.green, 0.055)
    }

    Item {
        id: librarySurface
        anchors.fill: parent
        opacity: root.detailOpen ? 0 : 1
        scale: root.detailOpen ? 0.985 : 1
        visible: opacity > 0
        enabled: !root.detailOpen

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 150 }
        }
        Behavior on scale {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: Math.max(22, root.width * 0.032)
            anchors.rightMargin: Math.max(22, root.width * 0.032)
            anchors.topMargin: 24
            anchors.bottomMargin: 16
            spacing: 20

            RowLayout {
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

                Item { Layout.fillWidth: true }

                Row {
                    spacing: 5
                    visible: root.width >= 1040

                    GlassButton {
                        text: "ALL"
                        compact: true
                        selected: Library.mode === 0
                        onClicked: {
                            Library.mode = 0
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        text: "FAVORITES"
                        compact: true
                        selected: Library.mode === 1
                        onClicked: {
                            Library.mode = 1
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        text: "RECENT"
                        compact: true
                        selected: Library.mode === 2
                        onClicked: {
                            Library.mode = 2
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        text: "HIDDEN"
                        compact: true
                        visible: !DemoMode
                        selected: Library.mode === 3
                        onClicked: {
                            Library.mode = 3
                            libraryView.focusGrid()
                        }
                    }
                }

                TextField {
                    id: searchField
                    objectName: "searchField"
                    Layout.preferredWidth: Math.min(300, root.width * 0.26)
                    Layout.minimumWidth: 190
                    Layout.preferredHeight: 38
                    placeholderText: "Search games"
                    color: Theme.foreground
                    placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    leftPadding: 36
                    rightPadding: 12
                    selectByMouse: true
                    focus: false
                    Accessible.name: "Search games"
                    Accessible.description: "Filter the installed game library"

                    onTextChanged: {
                        Library.searchText = text
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                    Keys.onEscapePressed: function(event) {
                        if (text.length > 0) {
                            clear()
                        }
                        libraryView.focusGrid()
                        event.accepted = true
                    }
                    Keys.onDownPressed: function(event) {
                        libraryView.focusGrid()
                        event.accepted = true
                    }

                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: root.alpha(Theme.foreground, searchField.activeFocus ? 0.075 : 0.045)
                        border.width: searchField.activeFocus ? 2 : 1
                        border.color: searchField.activeFocus
                                      ? Theme.accent
                                      : root.alpha(Theme.foreground, 0.15)
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 13
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌕"
                        color: searchField.activeFocus ? Theme.accent : Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                    }
                }

                GlassButton {
                    text: "SETTINGS"
                    compact: true
                    onClicked: root.diagnosticsOpen = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.width < 1040
                spacing: 6
                GlassButton {
                    text: "ALL"
                    compact: true
                    selected: Library.mode === 0
                    onClicked: Library.mode = 0
                }
                GlassButton {
                    text: "FAVORITES"
                    compact: true
                    selected: Library.mode === 1
                    onClicked: Library.mode = 1
                }
                GlassButton {
                    text: "RECENT"
                    compact: true
                    selected: Library.mode === 2
                    onClicked: Library.mode = 2
                }
                GlassButton {
                    text: "HIDDEN"
                    compact: true
                    visible: !DemoMode
                    selected: Library.mode === 3
                    onClicked: Library.mode = 3
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Row {
                    spacing: 5
                    visible: !DemoMode
                    GlassButton {
                        text: "ALL SOURCES"
                        compact: true
                        selected: Library.sourceFilter === ""
                        onClicked: {
                            Library.sourceFilter = ""
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        text: "STEAM"
                        compact: true
                        visible: Preferences.steamEnabled
                        selected: Library.sourceFilter === "Steam"
                        onClicked: {
                            Library.sourceFilter = "Steam"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        text: "LUTRIS"
                        compact: true
                        visible: Preferences.lutrisEnabled
                        selected: Library.sourceFilter === "Lutris"
                        onClicked: {
                            Library.sourceFilter = "Lutris"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        text: "HEROIC"
                        compact: true
                        visible: Preferences.heroicEnabled
                        selected: Library.sourceFilter === "Heroic"
                        onClicked: {
                            Library.sourceFilter = "Heroic"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        text: "FAUGUS"
                        compact: true
                        visible: Preferences.faugusEnabled
                        selected: Library.sourceFilter === "Faugus"
                        onClicked: {
                            Library.sourceFilter = "Faugus"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        text: "RETROARCH"
                        compact: true
                        visible: Preferences.retroArchEnabled
                        selected: Library.sourceFilter === "RetroArch"
                        onClicked: {
                            Library.sourceFilter = "RetroArch"
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                }

                Text {
                    visible: root.width >= 1100
                    text: Library.mode === 1 ? "FAVORITES" : Library.mode === 2 ? "RECENTLY PLAYED" : Library.mode === 3 ? "HIDDEN" : "YOUR LIBRARY"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.7
                }
                Text {
                    visible: root.width >= 1100
                    text: libraryView.count + " GAMES"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                }
                Text {
                    visible: root.width >= 1100 && (SteamLibrary ? SteamLibrary.scanning : false)
                    text: "SYNCING"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true
                    text: Library.sortMode === 0 ? "SORT: TITLE" : Library.sortMode === 1 ? "SORT: RECENT" : "SORT: PLAYTIME"
                    onClicked: Library.sortMode = (Library.sortMode + 1) % 3
                }
                Text {
                    text: Controller.connected
                          ? Controller.primaryGlyph + "  OPEN   ·   " + Controller.favoriteGlyph + "  FAVORITE   ·   " + Controller.backGlyph + "  BACK"
                          : "ENTER  OPEN   ·   F  FAVORITE"
                    color: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 8
                    visible: root.width > 930
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !DemoMode
                spacing: 6

                Text {
                    text: "ORGANIZE"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                GlassButton {
                    compact: true
                    text: root.filterLabel("STATUS", Library.completionFilter)
                    selected: Library.completionFilter !== ""
                    onClicked: {
                        Library.completionFilter = root.nextFilter(
                                    Library.completionFilter,
                                    ["backlog", "playing", "completed", "abandoned"])
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                GlassButton {
                    compact: true
                    text: root.filterLabel("COLLECTION", Library.collectionFilter)
                    selected: Library.collectionFilter !== ""
                    onClicked: {
                        Library.collectionFilter = root.nextFilter(
                                    Library.collectionFilter, Library.collectionNames)
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                GlassButton {
                    compact: true
                    text: root.filterLabel("TAG", Library.tagFilter)
                    selected: Library.tagFilter !== ""
                    onClicked: {
                        Library.tagFilter = root.nextFilter(Library.tagFilter, Library.tagNames)
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                GlassButton {
                    compact: true
                    visible: Library.completionFilter !== "" || Library.collectionFilter !== ""
                             || Library.tagFilter !== ""
                    text: "CLEAR"
                    onClicked: {
                        Library.completionFilter = ""
                        Library.collectionFilter = ""
                        Library.tagFilter = ""
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                Item { Layout.fillWidth: true }
            }

            LibraryView {
                id: libraryView
                objectName: "libraryView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                libraryModel: Library
                scanning: SteamLibrary ? SteamLibrary.scanning : false
                emptyTitle: Library.sourceFilter === "Heroic" && HeroicLibrary && !HeroicLibrary.heroicDetected
                            ? "Heroic was not found"
                            : Library.sourceFilter === "Faugus" && FaugusLibrary && !FaugusLibrary.faugusDetected
                            ? "Faugus was not found"
                            : Library.sourceFilter === "RetroArch" && RetroArchLibrary && !RetroArchLibrary.retroArchDetected
                            ? "RetroArch was not found"
                            : Library.sourceFilter === "Lutris" && LutrisLibrary && !LutrisLibrary.lutrisDetected
                            ? "Lutris was not found"
                            : Library.sourceFilter === "Steam" && SteamLibrary && !SteamLibrary.steamDetected
                              ? "Steam was not found"
                              : Library.mode === 3 ? "No hidden games" : "No installed games"
                emptyMessage: Library.completionFilter !== "" || Library.collectionFilter !== "" || Library.tagFilter !== ""
                              ? "Clear or change the organization filters to see more games."
                              : Library.sourceFilter === "Faugus" && FaugusLibrary && FaugusLibrary.errorText.length > 0
                              ? FaugusLibrary.errorText
                              : Library.sourceFilter === "RetroArch" && RetroArchLibrary && RetroArchLibrary.errorText.length > 0
                              ? RetroArchLibrary.errorText
                              : Library.sourceFilter === "Heroic" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : Library.sourceFilter === "Lutris" && LutrisLibrary && LutrisLibrary.errorText.length > 0
                              ? LutrisLibrary.errorText
                              : SteamLibrary && SteamLibrary.errorText.length > 0
                                ? SteamLibrary.errorText
                                : "Install a game in Steam, Lutris, Heroic, Faugus, or RetroArch, then rescan your library."
                onGameActivated: index => root.openGame(index)
                onFavoriteToggled: index => Library.toggleFavorite(index)
                onRefreshRequested: {
                    if (SteamLibrary && Preferences.steamEnabled) {
                        SteamLibrary.refresh()
                    }
                    if (LutrisLibrary && Preferences.lutrisEnabled) {
                        LutrisLibrary.refresh()
                    }
                    if (HeroicLibrary && Preferences.heroicEnabled) {
                        HeroicLibrary.refresh()
                    }
                    if (FaugusLibrary && Preferences.faugusEnabled) {
                        FaugusLibrary.refresh()
                    }
                    if (RetroArchLibrary && Preferences.retroArchEnabled) {
                        RetroArchLibrary.refresh()
                    }
                }
            }
        }
    }

    Loader {
        id: detailsLoader
        anchors.fill: parent
        active: root.detailOpen
        opacity: root.detailOpen ? 1 : 0
        asynchronous: false

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 170 }
        }

        sourceComponent: GameDetails {
            game: root.selectedGame
            installations: root.selectedInstallations
            selectedInstallation: root.selectedInstallation
            navigationEnabled: !root.linkDialogOpen && !root.diagnosticsOpen
                               && !root.collectionDeleteOpen
            onBackRequested: root.closeDetails()
            onFavoriteRequested: {
                Library.toggleFavorite(root.selectedIndex)
                root.selectedGame = Library.get(root.selectedIndex)
            }
            onPlayRequested: root.playSelected()
            onInstallRequested: root.installSelected()
            onManageRequested: root.manageSelected()
            onInstallationSelected: installation => root.selectInstallation(installation)
            onLinkRequested: {
                linkSearch.text = root.selectedGame.title
                root.linkResults = Library.linkCandidates(root.selectedIndex, linkSearch.text)
                root.linkDialogOpen = true
            }
            onUnlinkRequested: {
                const source = root.selectedGame.source
                const runner = root.selectedGame.runner || ""
                const appId = root.selectedGame.appId
                if (Library.unlinkGames(root.selectedIndex)) {
                    if (!root.refreshSelected(source, runner, appId)) {
                        root.closeDetails()
                    }
                    root.showToast("Installations unlinked")
                }
            }
            onCoverRequested: coverDialog.open()
            onCoverResetRequested: {
                if (Library.resetCustomCover(root.selectedIndex)) {
                    root.selectedGame = Library.get(root.selectedIndex)
                    root.showToast("Original cover restored")
                }
            }
            onConnectRequested: root.diagnosticsOpen = true
            onHiddenRequested: {
                Library.toggleHidden(root.selectedIndex)
                root.closeDetails()
            }
            onCompletionStatusRequested: status => {
                if (Library.setCompletionStatus(root.selectedIndex, status)) {
                    root.refreshAfterOrganization()
                    root.showToast(status.length > 0 ? "Status updated" : "Status cleared")
                }
            }
            onTagsRequested: tags => {
                if (Library.setTags(root.selectedIndex, tags)) {
                    root.refreshAfterOrganization()
                    root.showToast("Tags updated")
                }
            }
            onCollectionToggled: function(name, included) {
                if (Library.setCollectionMembership(root.selectedIndex, name, included)) {
                    root.refreshAfterOrganization()
                    root.showToast(included ? "Added to " + name : "Removed from " + name)
                }
            }
            onCollectionCreateRequested: name => {
                if (Library.createCollection(name)
                        && Library.setCollectionMembership(root.selectedIndex, name, true)) {
                    root.refreshAfterOrganization()
                    root.showToast("Added to " + name)
                    detailsLoader.item.collectionEditorOpen = false
                } else {
                    root.showToast("That collection already exists or is invalid")
                }
            }
        }
    }

    Rectangle {
        id: linkDialogOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.linkDialogOpen
        z: 25
        color: root.alpha(Theme.darkerBackground, 0.72)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(linkSearch.forceActiveFocus)
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.linkDialogOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(620, root.width - 56)
            height: Math.min(560, root.height - 56)
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "LINK ANOTHER INSTALLATION"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true
                    text: "CLOSE"
                    onClicked: root.linkDialogOpen = false
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Choose only another installation of the same game. Omakade will keep every launch target."
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
            TextField {
                id: linkSearch
                Layout.fillWidth: true
                placeholderText: "Search installed games"
                color: Theme.foreground
                font.family: Theme.fontFamily
                onTextChanged: root.linkResults = Library.linkCandidates(root.selectedIndex, text)
                Keys.onDownPressed: function(event) {
                    if (candidateList.count > 0) {
                        candidateList.currentIndex = 0
                        const candidate = candidateList.itemAtIndex(0)
                        if (candidate) {
                            candidate.forceActiveFocus(Qt.TabFocusReason)
                        }
                        event.accepted = true
                    }
                }
                background: Rectangle {
                    radius: Math.max(5, Theme.cornerRadius)
                    color: root.alpha(Theme.foreground, 0.05)
                    border.width: linkSearch.activeFocus ? 2 : 1
                    border.color: linkSearch.activeFocus
                                  ? Theme.accent : root.alpha(Theme.foreground, 0.18)
                }
            }
            ListView {
                id: candidateList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 7
                model: root.linkResults

                delegate: Button {
                    required property var modelData
                    required property int index
                    width: candidateList.width
                    height: 58
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "Link " + modelData.title + " from " + modelData.source
                    onClicked: root.linkCandidate(modelData)
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            candidateList.currentIndex = index
                            candidateList.positionViewAtIndex(index, ListView.Contain)
                        }
                    }

                    contentItem: Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 13
                        spacing: 4
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            text: (modelData.source || "LOCAL").toUpperCase()
                                  + (modelData.runner ? "  ·  " + modelData.runner.toUpperCase() : "")
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                        }
                    }

                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: parent.down || parent.hovered || parent.activeFocus
                               ? root.alpha(Theme.foreground, 0.09)
                               : root.alpha(Theme.foreground, 0.04)
                        border.width: parent.activeFocus ? 2 : 1
                        border.color: parent.activeFocus
                                      ? Theme.accent
                                      : root.alpha(Theme.foreground, 0.14)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: candidateList.count === 0
                    text: "No matching installations"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
            }
        }
    }

    Rectangle {
        id: toast
        property string message: ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
        width: toastText.implicitWidth + 34
        height: 42
        radius: Math.max(6, Theme.cornerRadius)
        color: root.alpha(Theme.background, 0.94)
        border.color: root.alpha(Theme.accent, 0.5)
        opacity: toastTimer.running ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            enabled: !Preferences.reducedMotion
            NumberAnimation { duration: 140 }
        }

        Text {
            id: toastText
            anchors.centerIn: parent
            text: toast.message
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
    }

    Timer {
        id: toastTimer
        interval: 2400
    }

    Rectangle {
        id: settingsOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.diagnosticsOpen
        z: 20
        color: root.alpha(Theme.darkerBackground, 0.72)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() { root.focusWithin(settingsOverlay, true) })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.diagnosticsOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(610, parent.width - 48)
            height: Math.min(760, parent.height - 48)
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.2)

            MouseArea { anchors.fill: parent }

            ScrollView {
                id: settingsScroll
                anchors.fill: parent
                anchors.margins: 28
                rightPadding: 18
                contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 14

                Text {
                    text: "SETTINGS & SOURCES"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Text {
                    text: "GAME SOURCES"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: DemoMode ? [] : [
                        { name: "STEAM", enabled: Preferences.steamEnabled,
                          status: SteamLibrary ? SteamLibrary.statusText : "Unavailable",
                          error: SteamLibrary ? SteamLibrary.errorText : "",
                          paths: SteamLibrary ? SteamLibrary.detectedPaths : [],
                          lastScan: SteamLibrary ? SteamLibrary.lastScan : 0 },
                        { name: "LUTRIS", enabled: Preferences.lutrisEnabled,
                          status: LutrisLibrary ? LutrisLibrary.statusText : "Unavailable",
                          error: LutrisLibrary ? LutrisLibrary.errorText : "",
                          paths: LutrisLibrary ? LutrisLibrary.detectedPaths : [],
                          lastScan: LutrisLibrary ? LutrisLibrary.lastScan : 0 },
                        { name: "HEROIC", enabled: Preferences.heroicEnabled,
                          status: HeroicLibrary ? HeroicLibrary.statusText : "Unavailable",
                          error: HeroicLibrary ? HeroicLibrary.errorText : "",
                          paths: HeroicLibrary ? HeroicLibrary.detectedPaths : [],
                          lastScan: HeroicLibrary ? HeroicLibrary.lastScan : 0 },
                        { name: "FAUGUS", enabled: Preferences.faugusEnabled,
                          status: FaugusLibrary ? FaugusLibrary.statusText : "Unavailable",
                          error: FaugusLibrary ? FaugusLibrary.errorText : "",
                          paths: FaugusLibrary ? FaugusLibrary.detectedPaths : [],
                          lastScan: FaugusLibrary ? FaugusLibrary.lastScan : 0 },
                        { name: "RETROARCH", enabled: Preferences.retroArchEnabled,
                          status: RetroArchLibrary ? RetroArchLibrary.statusText : "Unavailable",
                          error: RetroArchLibrary ? RetroArchLibrary.errorText : "",
                          paths: RetroArchLibrary ? RetroArchLibrary.detectedPaths : [],
                          lastScan: RetroArchLibrary ? RetroArchLibrary.lastScan : 0 }
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: modelData.enabled ? Theme.accent : Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                            GlassButton {
                                compact: true
                                text: modelData.enabled ? "ENABLED" : "DISABLED"
                                selected: modelData.enabled
                                onClicked: {
                                    let nowEnabled = false
                                    if (modelData.name === "STEAM") {
                                        Preferences.steamEnabled = !Preferences.steamEnabled
                                        nowEnabled = Preferences.steamEnabled
                                        if (Preferences.steamEnabled) SteamLibrary.refresh()
                                    } else if (modelData.name === "LUTRIS") {
                                        Preferences.lutrisEnabled = !Preferences.lutrisEnabled
                                        nowEnabled = Preferences.lutrisEnabled
                                        if (Preferences.lutrisEnabled) LutrisLibrary.refresh()
                                    } else if (modelData.name === "HEROIC") {
                                        Preferences.heroicEnabled = !Preferences.heroicEnabled
                                        nowEnabled = Preferences.heroicEnabled
                                        if (Preferences.heroicEnabled) HeroicLibrary.refresh()
                                    } else if (modelData.name === "FAUGUS") {
                                        Preferences.faugusEnabled = !Preferences.faugusEnabled
                                        nowEnabled = Preferences.faugusEnabled
                                        if (Preferences.faugusEnabled) FaugusLibrary.refresh()
                                    } else {
                                        Preferences.retroArchEnabled = !Preferences.retroArchEnabled
                                        nowEnabled = Preferences.retroArchEnabled
                                        if (Preferences.retroArchEnabled) RetroArchLibrary.refresh()
                                    }
                                    if (!nowEnabled && Library.sourceFilter.toUpperCase() === modelData.name) {
                                        Library.sourceFilter = ""
                                    }
                                }
                            }
                            GlassButton {
                                compact: true
                                text: "RESCAN"
                                enabled: modelData.enabled
                                onClicked: {
                                    if (modelData.name === "STEAM") SteamLibrary.refresh()
                                    else if (modelData.name === "LUTRIS") LutrisLibrary.refresh()
                                    else if (modelData.name === "HEROIC") HeroicLibrary.refresh()
                                    else if (modelData.name === "FAUGUS") FaugusLibrary.refresh()
                                    else RetroArchLibrary.refresh()
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.status + " · " + root.scanTime(modelData.lastScan)
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.paths.length > 0
                            text: modelData.paths.join("\n")
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.enabled && modelData.error.length > 0
                            text: modelData.error
                            color: Theme.yellow
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Text {
                    visible: DemoMode
                    text: "Demo library"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
                Repeater {
                    model: [
                        { label: "LIBRARY", value: libraryView.count + " visible games" },
                        { label: "LOCAL ARTWORK", value: SteamLibrary ? SteamLibrary.artworkCount + " covers" : "Procedural demo art" },
                        { label: "CONTROLLER", value: Controller.connected ? Controller.name : "Not connected" },
                        { label: "DATABASE", value: SteamLibrary ? SteamLibrary.databasePath : "Not used in demo mode" },
                        { label: "ACHIEVEMENT ART", value: (Achievements.cacheBytes / 1048576).toFixed(1) + " MB / " + Preferences.artworkCacheLimitMb + " MB" },
                        { label: "VERSION", value: AppVersion }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.preferredWidth: 130
                            text: modelData.label
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "OPTIONAL STEAM CONNECTION"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: SteamAccount
                          ? SteamAccount.statusText
                          : "Local Steam data is used in demo mode."
                    color: SteamAccount && (SteamAccount.state === "invalid-key"
                                            || SteamAccount.state === "private"
                                            || SteamAccount.state === "rate-limited")
                           ? Theme.yellow : Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: SteamAccount !== null
                    TextField {
                        id: steamIdField
                        Layout.fillWidth: true
                        placeholderText: "Steam ID"
                        text: SteamAccount ? SteamAccount.steamId : ""
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        inputMethodHints: Qt.ImhDigitsOnly
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: steamIdField.activeFocus ? 2 : 1
                            border.color: steamIdField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE ID"
                        onClicked: SteamAccount.setSteamId(steamIdField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: SteamAccount !== null && !SteamAccount.busy
                    TextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        placeholderText: SteamAccount && SteamAccount.hasApiKey
                                         ? "API key stored securely" : "Steam Web API key"
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: apiKeyField.activeFocus ? 2 : 1
                            border.color: apiKeyField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE KEY"
                        onClicked: {
                            SteamAccount.storeApiKey(apiKeyField.text)
                            apiKeyField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: SteamAccount ? SteamAccount.hasApiKey : false
                        text: "REMOVE"
                        onClicked: SteamAccount.removeApiKey()
                    }
                }
                GlassButton {
                    compact: true
                    text: "GET A KEY FROM STEAM"
                    onClicked: Qt.openUrlExternally("https://steamcommunity.com/dev/apikey")
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "OPTIONAL GAME INSIGHTS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: Insights ? Insights.statusText : "IGDB is unavailable in demo mode."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "TWITCH SETUP · Create Application, not Extension · Redirect: http://localhost · Client type: Confidential · Manage → New Secret"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbClientIdField
                        Layout.fillWidth: true
                        placeholderText: "Twitch developer client ID"
                        text: Insights ? Insights.clientId : ""
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: igdbClientIdField.activeFocus ? 2 : 1
                            border.color: igdbClientIdField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE ID"
                        onClicked: Insights.setClientId(igdbClientIdField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbSecretField
                        Layout.fillWidth: true
                        placeholderText: Insights && Insights.hasClientSecret
                                         ? "Client secret stored securely" : "Twitch developer client secret"
                        color: Theme.foreground
                        placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: root.alpha(Theme.foreground, 0.045)
                            border.width: igdbSecretField.activeFocus ? 2 : 1
                            border.color: igdbSecretField.activeFocus
                                          ? Theme.accent
                                          : root.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE SECRET"
                        onClicked: {
                            Insights.storeClientSecret(igdbSecretField.text)
                            igdbSecretField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: Insights ? Insights.configured : false
                        text: "REMOVE"
                        onClicked: Insights.removeCredentials()
                    }
                }
                GlassButton {
                    compact: true
                    text: "OPEN TWITCH APPLICATIONS"
                    onClicked: Qt.openUrlExternally("https://dev.twitch.tv/console/apps")
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.alpha(Theme.foreground, 0.12)
                }
                Text {
                    text: "LIBRARY COLLECTIONS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: Library.collectionNames.length === 0
                    text: "Create collections from a game's details."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                }
                Repeater {
                    model: Library.collectionNames
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        GlassButton {
                            compact: true
                            text: "DELETE"
                            onClicked: {
                                root.pendingCollectionDelete = modelData
                                root.collectionDeleteOpen = true
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.topMargin: 8
                    spacing: 8
                    GlassButton {
                        compact: true
                        text: "PROJECT"
                        onClicked: Qt.openUrlExternally("https://github.com/tsouth89/omakade")
                    }
                    GlassButton {
                        compact: true
                        text: "REPORT ISSUE"
                        onClicked: Qt.openUrlExternally("https://github.com/tsouth89/omakade/issues/new/choose")
                    }
                    Item { Layout.fillWidth: true }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columnSpacing: 8
                    rowSpacing: 8
                    columns: 3
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: Preferences.reducedMotion ? "MOTION OFF" : "MOTION ON"
                        selected: Preferences.reducedMotion
                        onClicked: Preferences.reducedMotion = !Preferences.reducedMotion
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: Preferences.closeAfterLaunch ? "CLOSE AFTER PLAY" : "STAY OPEN"
                        selected: Preferences.closeAfterLaunch
                        onClicked: Preferences.closeAfterLaunch = !Preferences.closeAfterLaunch
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: Preferences.steamOwnedGames ? "OWNED STEAM GAMES" : "INSTALLED ONLY"
                        selected: Preferences.steamOwnedGames
                        onClicked: Preferences.steamOwnedGames = !Preferences.steamOwnedGames
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE -"
                        onClicked: Preferences.artworkCacheLimitMb -= 128
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE +"
                        onClicked: Preferences.artworkCacheLimitMb += 128
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CLEAR ART"
                        onClicked: Achievements.clearCache()
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        text: "CLOSE"
                        primary: true
                        onClicked: root.diagnosticsOpen = false
                    }
                }
            }
            }
        }
    }

    Rectangle {
        id: collectionDeleteOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.collectionDeleteOpen
        z: 35
        color: root.alpha(Theme.darkerBackground, 0.76)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() {
                    root.focusWithin(collectionDeleteOverlay, true, collectionCancelButton)
                })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.collectionDeleteOpen = false
                root.pendingCollectionDelete = ""
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(460, parent.width - 48)
            height: 210
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                Text {
                    text: "DELETE COLLECTION?"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: "Remove “" + root.pendingCollectionDelete
                          + "” and its game memberships? This does not remove any games."
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    GlassButton {
                        id: collectionCancelButton
                        text: "CANCEL"
                        onClicked: {
                            root.collectionDeleteOpen = false
                            root.pendingCollectionDelete = ""
                        }
                    }
                    GlassButton {
                        text: "DELETE"
                        primary: true
                        onClicked: root.confirmCollectionDelete()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        smokeReady = true
        libraryView.focusGrid()
    }

    Connections {
        target: SteamAccount
        enabled: SteamAccount !== null
        function onAchievementsUpdated(appId) {
            if (root.detailOpen && root.selectedInstallation.appId === appId) {
                root.selectedGame = Library.get(root.selectedIndex)
            }
        }
    }

    Connections {
        target: Controller
        function onFocusDirectionRequested(key) {
            const container = root.navigationContainer()
            if (!container && !libraryView.gridFocused && key === Qt.Key_Down) {
                libraryView.focusGrid()
                return
            }
            root.focusSpatial(container || librarySurface, key)
        }
        function onFavoriteRequested() {
            if (root.detailOpen && !root.diagnosticsOpen && !root.linkDialogOpen
                    && !root.collectionDeleteOpen) {
                Library.toggleFavorite(root.selectedIndex)
                root.selectedGame = Library.get(root.selectedIndex)
            } else if (!root.detailOpen && root.navigationContainer() === null
                       && libraryView.gridFocused && libraryView.currentIndex >= 0) {
                Library.toggleFavorite(libraryView.currentIndex)
            }
        }
    }
}

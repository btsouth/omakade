import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "screens"

ApplicationWindow {
    id: root

    property bool randomSelection: false
    property bool backupEditorOpen: false
    property bool bulkOrganizationOpen: false
    property bool savedFiltersOpen: false
    property bool artworkEditorOpen: false
    property bool manualEditorOpen: false
    property bool detailOpen: false
    property var selectedGame: ({})
    property var selectedInstallation: ({})
    property var selectedInstallations: []
    property var linkResults: []
    property int selectedIndex: -1
    property bool smokeReady: false
    function chooseRomFolder() { romFolderDialog.open() }
    function openGogFolderDialog() { gogFolderDialog.open() }
    Connections {
        target: Metadata
        function onEntryChanged(key) {
            if (root.detailOpen && root.selectedGame.metadataKey === key)
                root.refreshSelected(root.selectedGame.source, root.selectedGame.runner || "", root.selectedGame.appId)
        }
    }
    property bool diagnosticsOpen: false
    property bool linkDialogOpen: false
    property bool collectionDeleteOpen: false
    // The organize filters open a picker list instead of cycling through every value.
    property bool filterPickerOpen: false
    property bool couchTextEntryOpen: false
    property var couchTextEntryTarget: null
    property string couchTextEntryTitle: "ENTER TEXT"
    property bool couchTextEntryPassword: false
    property string couchTextEntryPlaceholder: "Start typing"
    property string filterPickerKind: ""
    property var filterPickerValues: []
    property string pendingCollectionDelete: ""
    property bool couchMode: CouchModeRequested
    property int romFolderSystemIndex: 0
    readonly property var romFolderSystems: [
        { id: "snes", name: "Super Nintendo" },
        { id: "nes", name: "NES" },
        { id: "genesis", name: "Sega Genesis" },
        { id: "gb", name: "Game Boy" },
        { id: "gbc", name: "Game Boy Color" },
        { id: "gba", name: "Game Boy Advance" },
        { id: "n64", name: "Nintendo 64" },
        { id: "psx", name: "PlayStation" }
    ]
    property int desktopVisibility: Window.Windowed
    readonly property bool libraryScanning: (SteamLibrary ? SteamLibrary.scanning : false)
                                            || (LutrisLibrary ? LutrisLibrary.scanning : false)
                                            || (HeroicLibrary ? HeroicLibrary.scanning : false)
                                            || (FaugusLibrary ? FaugusLibrary.scanning : false)
                                            || (RetroArchLibrary ? RetroArchLibrary.scanning : false)
                                            || (Pcsx2Library ? Pcsx2Library.scanning : false)
                                            || (RyujinxLibrary ? RyujinxLibrary.scanning : false)
                                            || (Shadps4Library ? Shadps4Library.scanning : false)
                                            || (CemuLibrary ? CemuLibrary.scanning : false)
                                            || (DolphinLibrary ? DolphinLibrary.scanning : false)
                                            || (BattleNetLibrary ? BattleNetLibrary.scanning : false)
    readonly property int ownedGameCount: SteamAccount
                                          ? SteamAccount.ownedGameCount
                                          : OwnedGameCountOverride
    // Right from the end of the source row continues along the toolbar.
    readonly property Item sourceRowNextButton:
        randomGameButton.visible && randomGameButton.enabled ? randomGameButton
      : consoleGamesButton.visible && consoleGamesButton.enabled ? consoleGamesButton : sortButton
    readonly property Item sourceRowEndButton:
        manualSourceButton.visible ? manualSourceButton
      : dolphinSourceButton.visible && dolphinSourceButton.enabled ? dolphinSourceButton
      : cemuSourceButton.visible && cemuSourceButton.enabled ? cemuSourceButton
      : shadps4SourceButton.visible && shadps4SourceButton.enabled ? shadps4SourceButton
      : ryujinxSourceButton.visible && ryujinxSourceButton.enabled ? ryujinxSourceButton
      : pcsx2SourceButton.visible && pcsx2SourceButton.enabled ? pcsx2SourceButton
      : retroArchSourceButton.visible && retroArchSourceButton.enabled ? retroArchSourceButton
      : faugusSourceButton.visible && faugusSourceButton.enabled ? faugusSourceButton
      : gogSourceButton.visible && gogSourceButton.enabled ? gogSourceButton
      : heroicSourceButton.visible && heroicSourceButton.enabled ? heroicSourceButton
      : lutrisSourceButton.visible && lutrisSourceButton.enabled ? lutrisSourceButton
      : battleNetSourceButton.visible && battleNetSourceButton.enabled ? battleNetSourceButton
      : steamSourceButton.visible && steamSourceButton.enabled ? steamSourceButton
      : allSourcesButton

    function isWithin(item, container) {
        while (item) {
            if (item === container) {
                return true
            }
            item = item.parent
        }
        return false
    }

    function openFilterPicker(kind, values) {
        filterPickerKind = kind
        filterPickerValues = values
        filterPickerOpen = true
    }

    function filterPickerCurrent() {
        return filterPickerKind === "status" ? Library.completionFilter
             : filterPickerKind === "collection" ? Library.collectionFilter
             : Library.tagFilter
    }

    function applyFilterPick(value) {
        if (filterPickerKind === "status") {
            Library.completionFilter = value
        } else if (filterPickerKind === "collection") {
            Library.collectionFilter = value
        } else {
            Library.tagFilter = value
        }
        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
        filterPickerOpen = false
    }

    function navigationContainer() {
        if (coverSizePopup.opened) return coverSizePopup.contentItem
        if (couchTextEntryOpen) {
            return null
        }
        if (backupEditorOpen) return backupEditor
        if (bulkOrganizationOpen) return bulkOrganizationEditor
        if (savedFiltersOpen) return savedFiltersEditor
        if (artworkEditorOpen) return artworkEditor
        if (manualEditorOpen) return manualEditor
        if (filterPickerOpen) {
            return filterPickerOverlay
        }
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

    function arrowNavigationEnabled() {
        const current = root.activeFocusItem
        return root.navigationContainer() !== null
                && (!current || current.controllerNavigation !== false)
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

    // Fallback for arrow keys that reach an overlay loader directly.
    function handleArrowKey(container, event) {
        if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down
                && event.key !== Qt.Key_Left && event.key !== Qt.Key_Right) {
            return
        }
        if (root.activeFocusItem
                && root.activeFocusItem.controllerNavigation === false) {
            return
        }
        root.focusSpatial(container, event.key)
        event.accepted = true
    }

    function focusSpatial(container, key) {
        if (!container) {
            return false
        }
        const current = root.activeFocusItem
        if (container === backupEditor && backupEditor.navigate(current, key)) return true
        if (container === bulkOrganizationEditor && bulkOrganizationEditor.navigate(current, key)) return true
        if (container === savedFiltersEditor && savedFiltersEditor.navigate(current, key)) return true
        if (!root.isWithin(current, container)) {
            root.focusWithin(container, true)
            return true
        }
        const targetProperty = key === Qt.Key_Up ? "controllerUpTarget"
                             : key === Qt.Key_Down ? "controllerDownTarget"
                             : key === Qt.Key_Left ? "controllerLeftTarget"
                             : "controllerRightTarget"
        // Follow the explicit chain through hidden or disabled items, so a row
        // with some sources turned off still hands focus to the next visible one.
        let explicitTarget = current[targetProperty]
        for (let hops = 0; explicitTarget && !(explicitTarget.visible && explicitTarget.enabled)
             && hops < 24; ++hops) {
            explicitTarget = explicitTarget[targetProperty]
        }
        if (explicitTarget && explicitTarget.visible && explicitTarget.enabled) {
            explicitTarget.forceActiveFocus(Qt.TabFocusReason)
            root.revealNavigationItem(container, explicitTarget)
            return true
        }
        const currentCenter = current.mapToItem(container, current.width / 2,
                                                current.height / 2)
        const currentLeft = currentCenter.x - current.width / 2
        const currentRight = currentCenter.x + current.width / 2
        const currentTop = currentCenter.y - current.height / 2
        const currentBottom = currentCenter.y + current.height / 2
        // Use rectangle edges to decide direction. Comparing centers alone treats a wider button
        // on the next row as being to the right of the current button when the two actually
        // overlap horizontally.
        let best = null
        let bestScore = Number.MAX_VALUE
        let candidate = current.nextItemInFocusChain(true)
        for (let attempts = 0; candidate && candidate !== current
             && attempts < 300; ++attempts) {
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab
                    && candidate["controllerNavigation"] !== false) {
                const center = candidate.mapToItem(container, candidate.width / 2,
                                                   candidate.height / 2)
                const dx = center.x - currentCenter.x
                const dy = center.y - currentCenter.y
                const candidateLeft = center.x - candidate.width / 2
                const candidateRight = center.x + candidate.width / 2
                const candidateTop = center.y - candidate.height / 2
                const candidateBottom = center.y + candidate.height / 2
                let primary = 0
                let cross = 0
                let crossGap = 0
                if (key === Qt.Key_Up) {
                    primary = currentTop - candidateBottom
                    cross = Math.abs(dx)
                    crossGap = Math.max(0, Math.max(currentLeft, candidateLeft)
                                           - Math.min(currentRight, candidateRight))
                } else if (key === Qt.Key_Down) {
                    primary = candidateTop - currentBottom
                    cross = Math.abs(dx)
                    crossGap = Math.max(0, Math.max(currentLeft, candidateLeft)
                                           - Math.min(currentRight, candidateRight))
                } else if (key === Qt.Key_Left) {
                    primary = currentLeft - candidateRight
                    cross = Math.abs(dy)
                    crossGap = Math.max(0, Math.max(currentTop, candidateTop)
                                           - Math.min(currentBottom, candidateBottom))
                } else if (key === Qt.Key_Right) {
                    primary = candidateLeft - currentRight
                    cross = Math.abs(dy)
                    crossGap = Math.max(0, Math.max(currentTop, candidateTop)
                                           - Math.min(currentBottom, candidateBottom))
                }
                if (primary >= -1) {
                    const score = Math.max(0, primary) + crossGap * 2.5 + cross * 0.01
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
            return true
        }
        return false
    }

    function rescanLibraries() {
        if (SteamLibrary && Preferences.steamEnabled) SteamLibrary.refresh()
        if (LutrisLibrary && Preferences.lutrisEnabled) LutrisLibrary.refresh()
        if (HeroicLibrary && (Preferences.heroicEnabled || Preferences.gogEnabled)) HeroicLibrary.refresh()
        if (FaugusLibrary && Preferences.faugusEnabled) FaugusLibrary.refresh()
        if (RetroArchLibrary && Preferences.retroArchEnabled) RetroArchLibrary.refresh()
        if (Pcsx2Library && Preferences.pcsx2Enabled) Pcsx2Library.refresh()
        if (RyujinxLibrary && Preferences.ryujinxEnabled) RyujinxLibrary.refresh()
        if (Shadps4Library && Preferences.shadps4Enabled) Shadps4Library.refresh()
        if (CemuLibrary && Preferences.cemuEnabled) CemuLibrary.refresh()
        if (DolphinLibrary && Preferences.dolphinEnabled) DolphinLibrary.refresh()
        if (BattleNetLibrary && Preferences.battleNetEnabled) BattleNetLibrary.refresh()
    }

    function focusAboveGrid() {
        if (!root.focusSpatial(librarySurface, Qt.Key_Up)) {
            sortButton.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function toggleLibraryControls() {
        if (root.navigationContainer() !== null) {
            return
        }
        if (root.couchMode) {
            couchLibraryView.toggleControls()
            return
        }
        if (libraryView.gridFocused) {
            sortButton.forceActiveFocus(Qt.TabFocusReason)
        } else {
            libraryView.focusGrid()
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
        if (container === bulkOrganizationEditor) {
            bulkOrganizationEditor.reveal(item)
        } else if (container === savedFiltersEditor) {
            savedFiltersEditor.reveal(item)
        } else if (container === settingsOverlay) {
            settingsOverlay.reveal(item)
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
            Qt.callLater(root.focusLibrary)
        }
    }

    function openCouchTextEntry(target, title, password, placeholder) {
        if (!root.couchMode || !target) {
            return
        }
        couchTextEntryTarget = target
        couchTextEntryTitle = title || "ENTER TEXT"
        couchTextEntryPassword = password || false
        couchTextEntryPlaceholder = placeholder || "Start typing"
        couchTextEntryKeyboard.value = target.text || ""
        couchTextEntryKeyboard.keyboardMode = "upper"
        couchTextEntryOpen = true
        Qt.callLater(couchTextEntryKeyboard.focusKeyboard)
    }

    function closeCouchTextEntry(accepted) {
        const target = couchTextEntryTarget
        if (accepted && target) {
            target.text = couchTextEntryKeyboard.value
        }
        couchTextEntryOpen = false
        couchTextEntryTarget = null
        if (target) {
            Qt.callLater(function() { root.restoreFocus(target) })
        }
    }

    function handleCouchTextEntry(event, target, title, password, placeholder) {
        if (!root.couchMode) {
            return
        }
        root.openCouchTextEntry(target, title, password, placeholder)
        event.accepted = true
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

    function preferredInstallation(installations, fallback) {
        const preferred = Library.preferredInstallation(root.selectedIndex)
        return preferred && preferred.appId ? preferred : fallback
    }

    function pickRandomGame() {
        const index = Library.pickRandomGame()
        if (index < 0) {
            root.showToast("No available games match these filters")
            return
        }
        root.openGame(index)
        root.randomSelection = true
    }

    function leaveConsole() {
        if (Library.consoleFilter.length > 0) {
            Library.consoleFilter = ""
            root.resetLibrarySelection()
            return true
        }
        return false
    }

    // Both views watch the same library, so a filter change has to settle the selection in
    // whichever one is on screen.
    function resetLibrarySelection() {
        const first = Library.rowCount() > 0 ? 0 : -1
        libraryView.currentIndex = first
        couchLibraryView.currentIndex = first
        couchLibraryView.refreshCurrentGame()
    }

    // Back walks out of the library the way you walked in: out of a console, then out of a
    // search, then out of a source, and finally out of anything else still narrowing the view.
    // Levels that are not active are skipped, so from RetroArch inside Nintendo 64 one press
    // reaches RetroArch and the next reaches all sources. Returns true when a level was left.
    function stepBackFilter() {
        if (root.leaveConsole()) {
            return true
        }
        if (Library.searchText.length > 0) {
            searchField.text = ""
            Library.searchText = ""
            root.resetLibrarySelection()
            return true
        }
        if (Library.sourceFilters.length > 0) {
            Library.sourceFilters = []
            root.resetLibrarySelection()
            return true
        }
        // Whatever is left narrowing the view goes together, so back always reaches the whole
        // library rather than asking for a press per filter.
        if (Library.completionFilter !== "" || Library.collectionFilter !== ""
                || Library.tagFilter !== "" || Library.mode !== 0 || Library.availability !== 0) {
            Library.completionFilter = ""
            Library.collectionFilter = ""
            Library.tagFilter = ""
            Library.mode = 0
            Library.availability = 0
            root.resetLibrarySelection()
            return true
        }
        return false
    }

    function openGame(index) {
        // Refuse a row that is not in the library rather than opening an empty page. Every way
        // in is guarded, so a stale index cannot reach here, and this is the backstop.
        if (index < 0 || index >= Library.rowCount()) {
            return
        }
        root.randomSelection = false
        selectedIndex = index
        selectedGame = Library.get(index)
        if (selectedGame.isPortal) {
            Library.consoleFilter = selectedGame.system || ""
            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
            Qt.callLater(root.focusLibrary)
            return
        }
        selectedInstallations = Library.installations(index)
        selectedInstallation = preferredInstallation(selectedInstallations, selectedGame)
        if (!DemoMode && selectedInstallation.source === "Steam") {
            Achievements.load(selectedInstallation.appId)
            if (SteamAccount) {
                SteamAccount.refreshAchievementsIfStale(selectedInstallation.appId)
            }
            if (Insights) {
                Insights.loadSteam(selectedInstallation.appId)
            }
        } else if (!DemoMode && selectedInstallation.source === "RetroArch") {
            Achievements.load(selectedInstallation.appId)
            if (RetroAchievements) {
                RetroAchievements.refreshAchievementsIfStale(selectedInstallation.appId)
            }
            if (Insights) {
                Insights.loadSteam("")
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
        Qt.callLater(root.focusLibrary)
    }

    function focusLibrary() {
        if (root.couchMode) {
            couchLibraryView.focusGrid()
        } else {
            libraryView.focusGrid()
        }
    }

    function focusCurrentSurface() {
        const container = root.navigationContainer()
        const current = root.activeFocusItem
        if (container && root.isWithin(current, container)
                && current.visible && current.enabled) {
            root.revealNavigationItem(container, current)
        } else if (container) {
            root.focusWithin(container, true)
        } else {
            root.focusLibrary()
        }
    }

    function updateCouchMode(enabled, remember) {
        if (root.couchMode === enabled) {
            return
        }
        if (!enabled) {
            if (coverSizePopup.opened) {
                coverSizePopup.close()
            } else if (root.couchTextEntryOpen) {
                root.closeCouchTextEntry(false)
            }
            if (couchLibraryView.searchOpen) {
                couchLibraryView.closeSearch(false)
            }
            if (couchLibraryView.browseOpen) {
                couchLibraryView.closeBrowse()
            }
        }
        if (enabled) {
            couchLibraryView.currentIndex = libraryView.currentIndex
            root.desktopVisibility = root.visibility
        } else {
            libraryView.currentIndex = couchLibraryView.currentIndex
        }
        root.couchMode = enabled
        if (remember) {
            Preferences.couchModeEnabled = enabled
        }
        root.visibility = enabled ? Window.FullScreen : root.desktopVisibility
        Qt.callLater(root.focusCurrentSurface)
    }

    function setCouchMode(enabled) {
        root.updateCouchMode(enabled, true)
    }

    // A Sunshine activation is session-scoped. It must not change the preferred startup
    // mode just because an already-running desktop window receives the request.
    function activateCouchMode() {
        root.updateCouchMode(true, false)
    }

    function toggleCouchMode() {
        setCouchMode(!root.couchMode)
    }

    function showToast(message) {
        toast.message = message
        toastTimer.restart()
    }

    function filterLabel(prefix, value, available) {
        if (!value || value.length === 0) {
            return available && available.length > 0 ? prefix + " (" + available.length + ")" : prefix
        }
        const shortened = value.length > 16 ? value.substring(0, 15) + "…" : value
        return prefix + ": " + shortened.toUpperCase()
    }

    readonly property bool organizationFiltersActive: Library.completionFilter !== ""
                                                      || Library.collectionFilter !== ""
                                                      || Library.tagFilter !== ""

    // Names the search or filter that produced an empty library, or returns "" when the
    // library itself is empty.
    function emptyTitleForFilters() {
        if (Library.searchText !== "") {
            return "No games match \"" + Library.searchText + "\""
        }
        const active = [Library.completionFilter, Library.collectionFilter, Library.tagFilter]
                       .filter(value => value !== "").length
        if (active > 1) {
            return "No games match these filters"
        }
        if (Library.completionFilter !== "") {
            return "No games marked " + Library.completionFilter.toUpperCase()
        }
        if (Library.collectionFilter !== "") {
            return "Nothing in " + Library.collectionFilter + " yet"
        }
        if (Library.tagFilter !== "") {
            return "No games tagged " + Library.tagFilter
        }
        return ""
    }

    function clearLibraryFilters() {
        Library.completionFilter = ""
        Library.collectionFilter = ""
        Library.tagFilter = ""
        searchField.clear()
        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
        libraryView.focusGrid()
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
        } else if (selectedInstallation.installed === false) {
            if (Launcher.install(selectedInstallation.source, selectedInstallation.appId)) {
                showToast("Opening Steam to install " + selectedGame.title)
            } else {
                showToast(Launcher.lastError)
            }
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

    function manageSelected() {
        if (Launcher.manage(selectedInstallation.source, selectedInstallation.appId,
                            selectedInstallation.flatpak || false,
                            selectedInstallation.runner || "",
                            selectedInstallation.launchTarget || "")) {
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
        } else if (!DemoMode && installation.source === "RetroArch") {
            Achievements.load(installation.appId)
            if (RetroAchievements) {
                RetroAchievements.refreshAchievementsIfStale(installation.appId)
            }
            if (Insights) {
                Insights.loadSteam("")
            }
        } else {
            Achievements.load("")
            if (Insights) {
                Insights.loadSteam("")
            }
        }
    }

    function refreshSelected(source, runner, appId) {
        const index = Library.indexOf(source, runner || "", appId)
        if (index < 0) {
            return false
        }
        selectedIndex = index
        selectedGame = Library.get(index)
        selectedInstallations = Library.installations(index)
        selectedInstallation = preferredInstallation(selectedInstallations, selectedGame)
        return true
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

    FolderDialog {
        id: romFolderDialog
        title: "Choose a ROM folder"
        onAccepted: Preferences.addRomFolder(selectedFolder, root.romFolderSystems[root.romFolderSystemIndex].id)
    }

    function openBulkOrganization() {
        Library.clearSelection()
        root.bulkOrganizationOpen = true
        Qt.callLater(bulkOrganizationEditor.focusEditor)
    }
    BulkOrganizationEditor {
        id: bulkOrganizationEditor
        objectName: "bulkOrganizationEditor"
        anchors.fill: parent
        z: 87
        visible: root.bulkOrganizationOpen
        couchMode: root.couchMode
        onDismissed: {
            Library.clearSelection()
            root.bulkOrganizationOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function openSavedFilters() {
        root.savedFiltersOpen = true
        Qt.callLater(savedFiltersEditor.focusEditor)
    }
    function applySavedFilter(id) {
        const current = Library.get(root.couchMode ? couchLibraryView.currentIndex : libraryView.currentIndex)
        if (!Library.applySavedFilter(id)) return
        searchField.text = Library.searchText
        const found = Library.indexOf(current.source || "", current.runner || "", current.appId || "")
        const index = found >= 0 ? found : Library.rowCount() > 0 ? 0 : -1
        libraryView.currentIndex = index
        couchLibraryView.currentIndex = index
        couchLibraryView.refreshCurrentGame()
        root.savedFiltersOpen = false
        if (Library.savedFilterMessage) root.showToast(Library.savedFilterMessage)
        Qt.callLater(root.focusCurrentSurface)
    }
    SavedFiltersEditor {
        id: savedFiltersEditor
        objectName: "savedFiltersEditor"
        anchors.fill: parent
        z: 86
        visible: root.savedFiltersOpen
        couchMode: root.couchMode
        onApplyRequested: id => root.applySavedFilter(id)
        onDismissed: { root.savedFiltersOpen = false; Qt.callLater(root.focusCurrentSurface) }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function openBackupEditor() {
        backupEditorOpen = true
        Qt.callLater(backupEditor.focusEditor)
    }
    function focusGogLibraryPath() { settingsOverlay.focusGogFolderField() }
    function removeGogLibraryFolder(path) {
        if (!Preferences.removeGogLibraryPath(path)) root.showToast("Could not remove that folder")
        Qt.callLater(root.focusGogLibraryPath)
    }
    BackupEditor {
        id: backupEditor
        objectName: "backupEditor"
        anchors.fill: parent
        z: 89
        visible: root.backupEditorOpen
        couchMode: root.couchMode
        onDismissed: { root.backupEditorOpen = false; Qt.callLater(root.focusCurrentSurface) }
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editArtwork() {
        artworkEditor.message = ""
        root.artworkEditorOpen = true
        Qt.callLater(artworkEditor.focusEditor)
    }
    ArtworkEditor {
        id: artworkEditor
        objectName: "artworkEditor"
        anchors.fill: parent
        z: 85
        visible: root.artworkEditorOpen
        game: root.selectedGame
        gameRow: root.selectedIndex
        couchMode: root.couchMode
        onDismissed: {
            root.artworkEditorOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onArtworkChanged: root.refreshAfterOrganization()
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editManualGame(id) {
        manualEditorOpen = true
        manualEditor.loadDraft(id ? ManualLibrary.get(id) : {})
    }

    ManualGameEditor {
        id: manualEditor
        objectName: "manualGameEditor"
        anchors.fill: parent
        z: 80
        visible: root.manualEditorOpen
        couchMode: root.couchMode
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
        onDismissed: {
            root.manualEditorOpen = false
            Qt.callLater(root.focusCurrentSurface)
        }
        onSaved: function(id) {
            root.manualEditorOpen = false
            root.diagnosticsOpen = false
            if (manualEditor.entryId === "") root.clearLibraryFilters()
            const row = Library.indexOf("Manual", "", id)
            if (row >= 0) root.openGame(row)
            else { root.detailOpen = false; Qt.callLater(root.focusLibrary) }
            root.showToast("Manual game saved")
        }
        onRemoved: {
            root.manualEditorOpen = false
            root.detailOpen = false
            Qt.callLater(root.focusCurrentSurface)
            root.showToast("Removed from Omakade. Game files were kept.")
        }
    }

    FolderDialog {
        id: gogFolderDialog
        title: "Choose a GOG library folder"
        onAccepted: {
            if (!Preferences.addGogLibraryPath(selectedFolder.toString()))
                root.showToast("Could not save that folder")
            Qt.callLater(root.focusCurrentSurface)
        }
        onRejected: Qt.callLater(root.focusCurrentSurface)
    }

    FileDialog {
        id: coverDialog
        title: "Choose cover artwork"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: {
            if (Library.setCustomCover(root.selectedIndex, selectedFile)) {
                root.refreshAfterOrganization()
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
        enabled: !root.couchMode && !root.detailOpen && !root.diagnosticsOpen
                 && !root.linkDialogOpen
                 && !root.collectionDeleteOpen
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "F11"
        onActivated: root.toggleCouchMode()
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
        sequence: "F6"
        enabled: root.navigationContainer() === null
        onActivated: root.toggleLibraryControls()
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
        sequence: "Up"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Up)
    }
    Shortcut {
        sequence: "Down"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Down)
    }
    Shortcut {
        sequence: "Left"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Left)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.arrowNavigationEnabled()
        onActivated: root.focusSpatial(root.navigationContainer(), Qt.Key_Right)
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (coverSizePopup.opened) {
                coverSizePopup.close()
            } else if (root.couchTextEntryOpen) {
                root.closeCouchTextEntry(false)
            } else if (root.backupEditorOpen) {
                backupEditor.dismiss()
            } else if (root.bulkOrganizationOpen) {
                Library.clearSelection()
                root.bulkOrganizationOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.savedFiltersOpen) {
                root.savedFiltersOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.artworkEditorOpen) {
                root.artworkEditorOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.manualEditorOpen) {
                root.manualEditorOpen = false
                Qt.callLater(root.focusCurrentSurface)
            } else if (root.filterPickerOpen) {
                root.filterPickerOpen = false
            } else if (root.couchMode && couchLibraryView.searchOpen) {
                couchLibraryView.closeSearch(false)
            } else if (root.couchMode && couchLibraryView.browseOpen) {
                couchLibraryView.closeBrowse()
            } else if (root.linkDialogOpen) {
                root.linkDialogOpen = false
            } else if (root.collectionDeleteOpen) {
                root.collectionDeleteOpen = false
                root.pendingCollectionDelete = ""
            } else if (root.diagnosticsOpen) {
                settingsOverlay.back()
            } else if (root.detailOpen && detailsLoader.item
                       && detailsLoader.item.collectionEditorOpen) {
                // The window shortcut sees Escape before the details page does.
                detailsLoader.item.closeCollectionEditor()
            } else if (root.detailOpen) {
                root.closeDetails()
            } else if (root.stepBackFilter()) {
                if (!root.couchMode) {
                    libraryView.focusGrid()
                }
            } else if (root.couchMode || !libraryView.gridFocused) {
                root.focusLibrary()
            }
        }
    }

    Binding {
        target: Controller
        property: "focusNavigation"
        value: !root.couchTextEntryOpen
               && (!root.activeFocusItem || root.activeFocusItem.controllerNavigation !== false)
               && (root.backupEditorOpen || root.bulkOrganizationOpen || root.savedFiltersOpen || root.artworkEditorOpen || root.manualEditorOpen || root.detailOpen || root.diagnosticsOpen || root.linkDialogOpen
               || root.collectionDeleteOpen
               || (!root.couchMode && !libraryView.gridFocused))
    }
    Shortcut {
        sequence: "Return"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Enter"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }
    Shortcut {
        sequence: "Space"
        enabled: !root.couchMode && root.navigationContainer() === null
                 && libraryView.gridFocused
                 && libraryView.currentIndex >= 0
        onActivated: root.openGame(libraryView.currentIndex)
    }

    onActiveChanged: {
        if (active) {
            Qt.callLater(root.focusCurrentSurface)
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
        visible: !root.couchMode && opacity > 0
        enabled: !root.couchMode && !root.detailOpen

        // Arrow keys move between the filters and toolbar controls, and Down with nothing
        // below drops back into the game grid. Controller directions take the same path.
        Keys.onPressed: function(event) {
            if (root.navigationContainer() !== null || libraryView.gridFocused) {
                return
            }
            if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down
                    && event.key !== Qt.Key_Left && event.key !== Qt.Key_Right) {
                return
            }
            if (!root.focusSpatial(librarySurface, event.key) && event.key === Qt.Key_Down) {
                libraryView.focusGrid()
            }
            event.accepted = true
        }

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
                        id: allModeButton
                        objectName: "allModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "ALL"
                        compact: true
                        selected: Library.mode === 0
                        onClicked: {
                            Library.mode = 0
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: favoritesModeButton
                        objectName: "favoritesModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "FAVORITES"
                        compact: true
                        selected: Library.mode === 1
                        onClicked: {
                            Library.mode = 1
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: recentModeButton
                        objectName: "recentModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
                        text: "RECENT"
                        compact: true
                        selected: Library.mode === 2
                        onClicked: {
                            Library.mode = 2
                            libraryView.focusGrid()
                        }
                    }
                    GlassButton {
                        id: hiddenModeButton
                        objectName: "hiddenModeButton"
                        property Item controllerDownTarget: root.sourceRowEndButton
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
                    property bool controllerNavigation: false
                    Layout.preferredWidth: root.width < 900 ? 150 : Math.min(300, root.width * 0.26)
                    Layout.minimumWidth: root.width < 900 ? 150 : 190
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
                    objectName: "bulkOrganizationButton"
                    text: "ORGANIZE"
                    compact: true
                    onClicked: root.openBulkOrganization()
                }
                GlassButton {
                    objectName: "savedFiltersButton"
                    text: "SAVED FILTERS"
                    compact: true
                    onClicked: root.openSavedFilters()
                }
                GlassButton {
                    id: settingsButton
                    objectName: "settingsButton"
                    text: "SETTINGS"
                    compact: true
                    onClicked: root.diagnosticsOpen = true
                }

                GlassButton {
                    id: couchModeButton
                    objectName: "couchModeButton"
                    text: "COUCH"
                    compact: true
                    onClicked: root.setCouchMode(true)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.width < 1040
                spacing: 6
                GlassButton {
                    id: narrowAllModeButton
                    objectName: "narrowAllModeButton"
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
                    id: narrowHiddenModeButton
                    objectName: "narrowHiddenModeButton"
                    property Item controllerDownTarget: root.sourceRowEndButton
                    text: "HIDDEN"
                    compact: true
                    visible: !DemoMode
                    selected: Library.mode === 3
                    onClicked: {
                        Library.mode = 3
                        libraryView.focusGrid()
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Flickable {
                    id: sourceFlickable
                    objectName: "sourceFlickable"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.preferredHeight: sourceButtonsRow.implicitHeight
                    visible: !DemoMode
                    clip: true
                    contentWidth: sourceButtonsRow.implicitWidth
                    contentHeight: sourceButtonsRow.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    function reveal(item) {
                        if (!item || !root.isWithin(item, sourceButtonsRow)
                                || contentWidth <= width) {
                            return
                        }
                        const position = item.mapToItem(sourceButtonsRow, 0, 0)
                        const margin = 5
                        if (position.x < contentX + margin) {
                            contentX = Math.max(0, position.x - margin)
                        } else if (position.x + item.width > contentX + width - margin) {
                            contentX = Math.min(contentWidth - width,
                                                position.x + item.width - width + margin)
                        }
                    }

                    Connections {
                        target: root
                        function onActiveFocusItemChanged() {
                            sourceFlickable.reveal(root.activeFocusItem)
                        }
                    }

                    Row {
                    id: sourceButtonsRow
                    spacing: 5
                    GlassButton {
                        id: allSourcesButton
                        objectName: "allSourcesButton"
                        property Item controllerDownTarget: root.ownedGameCount > 0
                                                            ? installedAvailabilityButton
                                                            : statusFilterButton
                        text: "ALL SOURCES"
                        compact: true
                        selected: Library.sourceFilters.length === 0
                        onClicked: {
                            Library.sourceFilters = []
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: emulatedSourcesButton
                        objectName: "emulatedSourcesButton"
                        property Item controllerLeftTarget: allSourcesButton
                        property Item controllerRightTarget: steamSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "EMULATED"
                        compact: true
                        property string sourceName: "Emulated"
                        selected: Library.emulatorSources.every(source => Library.sourceFilters.indexOf(source) >= 0)
                        onClicked: {
                            Library.sourceFilters = Library.emulatorSources
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSources(Library.emulatorSources)
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: steamSourceButton
                        objectName: "steamSourceButton"
                        text: "STEAM"
                        compact: true
                        visible: Preferences.steamEnabled
                        property string sourceName: "Steam"
                        selected: Library.sourceFilters.indexOf("Steam") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Steam"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Steam")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: battleNetSourceButton
                        objectName: "battleNetSourceButton"
                        text: "BATTLE.NET"
                        compact: true
                        visible: Preferences.battleNetEnabled
                        property string sourceName: "Battle.net"
                        selected: Library.sourceFilters.indexOf("Battle.net") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Battle.net"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Battle.net")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: lutrisSourceButton
                        objectName: "lutrisSourceButton"
                        text: "LUTRIS"
                        compact: true
                        visible: Preferences.lutrisEnabled
                        property string sourceName: "Lutris"
                        selected: Library.sourceFilters.indexOf("Lutris") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Lutris"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Lutris")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: heroicSourceButton
                        objectName: "heroicSourceButton"
                        text: "HEROIC"
                        compact: true
                        visible: Preferences.heroicEnabled
                        property string sourceName: "Heroic"
                        selected: Library.sourceFilters.indexOf("Heroic") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Heroic"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Heroic")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: gogSourceButton
                        objectName: "gogSourceButton"
                        text: "GOG"
                        compact: true
                        visible: Preferences.gogEnabled
                        property string sourceName: "GOG"
                        selected: Library.sourceFilters.indexOf("GOG") >= 0
                        onClicked: {
                            Library.sourceFilters = ["GOG"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("GOG")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: faugusSourceButton
                        objectName: "faugusSourceButton"
                        text: "FAUGUS"
                        compact: true
                        visible: Preferences.faugusEnabled
                        property string sourceName: "Faugus"
                        selected: Library.sourceFilters.indexOf("Faugus") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Faugus"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Faugus")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: retroArchSourceButton
                        objectName: "retroArchSourceButton"
                        property Item controllerRightTarget: pcsx2SourceButton
                        text: "RETROARCH"
                        compact: true
                        visible: Preferences.retroArchEnabled
                        property string sourceName: "RetroArch"
                        selected: Library.sourceFilters.indexOf("RetroArch") >= 0
                        onClicked: {
                            Library.sourceFilters = ["RetroArch"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("RetroArch")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: pcsx2SourceButton
                        objectName: "pcsx2SourceButton"
                        property Item controllerLeftTarget: retroArchSourceButton
                        property Item controllerRightTarget: ryujinxSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "PCSX2"
                        compact: true
                        visible: Preferences.pcsx2Enabled
                        property string sourceName: "PCSX2"
                        selected: Library.sourceFilters.indexOf("PCSX2") >= 0
                        onClicked: {
                            Library.sourceFilters = ["PCSX2"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("PCSX2")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: ryujinxSourceButton
                        objectName: "ryujinxSourceButton"
                        property Item controllerLeftTarget: pcsx2SourceButton
                        property Item controllerRightTarget: shadps4SourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "RYUJINX"
                        compact: true
                        visible: Preferences.ryujinxEnabled
                        property string sourceName: "Ryujinx"
                        selected: Library.sourceFilters.indexOf("Ryujinx") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Ryujinx"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Ryujinx")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: shadps4SourceButton
                        objectName: "shadps4SourceButton"
                        property Item controllerLeftTarget: ryujinxSourceButton
                        property Item controllerRightTarget: cemuSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "SHADPS4"
                        compact: true
                        visible: Preferences.shadps4Enabled
                        property string sourceName: "shadPS4"
                        selected: Library.sourceFilters.indexOf("shadPS4") >= 0
                        onClicked: {
                            Library.sourceFilters = ["shadPS4"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("shadPS4")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: cemuSourceButton
                        objectName: "cemuSourceButton"
                        property Item controllerLeftTarget: shadps4SourceButton
                        property Item controllerRightTarget: dolphinSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "CEMU"
                        compact: true
                        visible: Preferences.cemuEnabled
                        property string sourceName: "Cemu"
                        selected: Library.sourceFilters.indexOf("Cemu") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Cemu"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Cemu")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: dolphinSourceButton
                        objectName: "dolphinSourceButton"
                        property Item controllerLeftTarget: cemuSourceButton
                        property Item controllerRightTarget: manualSourceButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "DOLPHIN"
                        compact: true
                        visible: Preferences.dolphinEnabled
                        property string sourceName: "Dolphin"
                        selected: Library.sourceFilters.indexOf("Dolphin") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Dolphin"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Dolphin")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    GlassButton {
                        id: manualSourceButton
                        objectName: "manualSourceButton"
                        property Item controllerLeftTarget: dolphinSourceButton
                        property Item controllerRightTarget: root.sourceRowNextButton
                        property Item controllerDownTarget: statusFilterButton
                        text: "MANUAL"
                        compact: true
                        visible: ManualLibrary.count > 0
                        property string sourceName: "Manual"
                        selected: Library.sourceFilters.indexOf("Manual") >= 0
                        onClicked: {
                            Library.sourceFilters = ["Manual"]
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                        onSecondaryClicked: {
                            Library.toggleSource("Manual")
                            libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                        }
                    }
                    }
                }

                Text {
                    visible: root.width >= 1100
                    text: Library.consoleTitle.length > 0
                          ? "LIBRARY / " + Library.consoleTitle.toUpperCase()
                          : Library.mode === 1 ? "FAVORITES" : Library.mode === 2 ? "RECENTLY PLAYED" : Library.mode === 3 ? "HIDDEN" : "YOUR LIBRARY"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.7
                }
                Text {
                    visible: root.width >= 1100
                    text: libraryView.count
                          + (DemoMode ? " GAMES"
                             : Library.availability === 0 ? " INSTALLED"
                             : Library.availability === 2 ? " READY TO INSTALL"
                             : " GAMES")
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                }
                Text {
                    visible: root.width >= 1100 && root.libraryScanning
                    text: "SYNCING"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    id: randomGameButton
                    objectName: "randomGameButton"
                    property Item controllerLeftTarget: root.width < 1040
                                                         ? root.sourceRowEndButton
                                                         : hiddenModeButton
                    property Item controllerRightTarget: consoleGamesButton.visible && consoleGamesButton.enabled
                                                         ? consoleGamesButton : sortButton
                    compact: true
                    text: "PICK A GAME"
                    onClicked: root.pickRandomGame()
                }
                GlassButton {
                    id: consoleGamesButton
                    objectName: "consoleGamesButton"
                    // Every console system follows this view unless explicitly overridden.
                    visible: Library.hasConsoleCards || Library.expandConsoles
                    property Item controllerLeftTarget: randomGameButton
                    property Item controllerRightTarget: sortButton
                    compact: true
                    selected: Library.expandConsoles
                    text: Library.expandConsoles ? "CONSOLE VIEW: GAMES" : "CONSOLE VIEW: CONSOLES"
                    onClicked: {
                        Library.expandConsoles = !Library.expandConsoles
                        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                    }
                }
                GlassButton {
                    id: sortButton
                    objectName: "sortButton"
                    property Item controllerLeftTarget: consoleGamesButton.visible ? consoleGamesButton
                                                         : randomGameButton
                    property Item controllerRightTarget: coverSizeButton
                    compact: true
                    text: Library.sortMode === 0 ? "SORT: TITLE" : Library.sortMode === 1 ? "SORT: RECENT" : Library.sortMode === 2 ? "SORT: PLAYTIME" : Library.sortMode === 3 ? "SORT: RATING" : "SORT: POPULARITY"
                    onClicked: Library.sortMode = (Library.sortMode + 1) % 5
                }
                GlassButton {
                    id: coverSizeButton
                    property Item controllerDownTarget: statusFilterButton.visible ? statusFilterButton : libraryView.navigationTarget
                    objectName: "coverSizeButton"
                    property Item controllerLeftTarget: sortButton
                    property Item controllerRightTarget: rescanButton
                    property Item controllerUpTarget: settingsButton
                    compact: true; text: "COVER SIZE"
                    onClicked: coverSizePopup.open()
                }
                GlassButton {
                    id: rescanButton
                    objectName: "rescanButton"
                    property Item controllerLeftTarget: coverSizeButton
                    property Item controllerUpTarget: settingsButton
                    compact: true
                    text: root.libraryScanning ? "SCANNING" : "RESCAN"
                    enabled: !root.libraryScanning
                    onClicked: root.rescanLibraries()
                }
                Text {
                    readonly property bool sourceChipFocused: root.activeFocusItem
                                                               && root.activeFocusItem.sourceName !== undefined
                    text: sourceChipFocused
                          ? (Controller.connected
                             ? Controller.primaryGlyph + "  SELECT   ·   " + Controller.favoriteGlyph + "  ADD / REMOVE   ·   " + Controller.backGlyph + "  BACK"
                             : "ENTER  SELECT   ·   SHIFT+ENTER  ADD / REMOVE")
                          : Controller.connected
                          ? Controller.primaryGlyph + "  OPEN   ·   " + Controller.favoriteGlyph + "  FAVORITE   ·   " + Controller.toolbarGlyph + "  CONTROLS   ·   " + Controller.backGlyph + "  BACK"
                          : "ENTER  OPEN   ·   F  FAVORITE   ·   F6  CONTROLS"
                    color: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 8
                    // The hints are a fixed-width string; on tiled windows they
                    // starve the source chips, so they only appear with room to spare.
                    visible: root.width >= 1560
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !DemoMode && root.ownedGameCount > 0
                spacing: 6

                Text {
                    text: "AVAILABILITY"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
                GlassButton {
                    id: installedAvailabilityButton
                    objectName: "installedAvailabilityButton"
                    compact: true
                    text: "INSTALLED"
                    selected: Library.availability === 0
                    onClicked: {
                        Library.availability = 0
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    compact: true
                    text: "ALL GAMES"
                    selected: Library.availability === 1
                    onClicked: {
                        Library.availability = 1
                        libraryView.focusGrid()
                    }
                }
                GlassButton {
                    id: readyAvailabilityButton
                    objectName: "readyAvailabilityButton"
                    property Item controllerDownTarget: statusFilterButton
                    compact: true
                    text: "READY TO INSTALL"
                    selected: Library.availability === 2
                    onClicked: {
                        Library.availability = 2
                        libraryView.focusGrid()
                    }
                }
                Item { Layout.fillWidth: true }
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
                    id: statusFilterButton
                    objectName: "statusFilterButton"
                    property Item controllerDownTarget: libraryView.focusTarget
                    compact: true
                    text: root.filterLabel("STATUS", Library.completionFilter)
                    selected: Library.completionFilter !== ""
                    onClicked: root.openFilterPicker("status",
                                                     ["backlog", "playing", "completed", "abandoned"])
                }
                GlassButton {
                    id: collectionFilterButton
                    objectName: "collectionFilterButton"
                    property Item controllerDownTarget: libraryView.focusTarget
                    compact: true
                    text: root.filterLabel("COLLECTION", Library.collectionFilter,
                                           Library.collectionNames)
                    selected: Library.collectionFilter !== ""
                    onClicked: {
                        if (Library.collectionNames.length === 0) {
                            root.showToast("No collections yet. Open a game and use + New Collection.")
                            return
                        }
                        root.openFilterPicker("collection", Library.collectionNames)
                    }
                }
                GlassButton {
                    id: tagFilterButton
                    objectName: "tagFilterButton"
                    property Item controllerDownTarget: libraryView.focusTarget
                    compact: true
                    text: root.filterLabel("TAG", Library.tagFilter, Library.tagNames)
                    selected: Library.tagFilter !== ""
                    onClicked: {
                        if (Library.tagNames.length === 0) {
                            root.showToast("No tags yet. Open a game and add tags under Organize.")
                            return
                        }
                        root.openFilterPicker("tag", Library.tagNames)
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

            RowLayout {
                Layout.fillWidth: true
                visible: Library.consoleTitle.length > 0
                spacing: 8
                GlassButton {
                    objectName: "consoleBackButton"
                    compact: true
                    text: "BACK"
                    onClicked: root.leaveConsole()
                }
                Text {
                    text: Library.consoleTitle
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
            }

            LibraryView {
                id: libraryView
                objectName: "libraryView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                libraryModel: Library
                scanning: root.libraryScanning
                filtersActive: root.organizationFiltersActive || Library.searchText !== ""
                onClearFiltersRequested: root.clearLibraryFilters()
                emptyTitle: root.emptyTitleForFilters() !== "" ? root.emptyTitleForFilters()
                            : Library.sourceFilter === "GOG" && HeroicLibrary && !HeroicLibrary.gogDetected
                            ? "GOG was not found"
                            : Library.sourceFilter === "Heroic" && HeroicLibrary && !HeroicLibrary.heroicDetected
                            ? "Heroic was not found"
                            : Library.sourceFilter === "Faugus" && FaugusLibrary && !FaugusLibrary.faugusDetected
                            ? "Faugus was not found"
                            : Library.sourceFilter === "RetroArch" && RetroArchLibrary && !RetroArchLibrary.retroArchDetected
                            ? "RetroArch was not found"
                            : Library.sourceFilter === "PCSX2" && Pcsx2Library && !Pcsx2Library.pcsx2Detected
                            ? "PCSX2 was not found"
                            : Library.sourceFilter === "Ryujinx" && RyujinxLibrary && !RyujinxLibrary.ryujinxDetected
                            ? "Ryujinx was not found"
                            : Library.sourceFilter === "shadPS4" && Shadps4Library && !Shadps4Library.shadps4Detected
                            ? "shadPS4 was not found"
                            : Library.sourceFilter === "Cemu" && CemuLibrary && !CemuLibrary.cemuDetected
                            ? "Cemu was not found"
                            : Library.sourceFilter === "Dolphin" && DolphinLibrary && !DolphinLibrary.dolphinDetected
                            ? "Dolphin was not found"
                            : Library.sourceFilter === "Battle.net" && BattleNetLibrary && !BattleNetLibrary.battleNetDetected
                            ? "Battle.net was not found"
                            : Library.sourceFilter === "Lutris" && LutrisLibrary && !LutrisLibrary.lutrisDetected
                            ? "Lutris was not found"
                            : Library.sourceFilter === "Steam" && SteamLibrary && !SteamLibrary.steamDetected
                              ? "Steam was not found"
                              : Library.mode === 3 ? "No hidden games"
                              : Library.availability === 2 ? "No games ready to install"
                              : Library.availability === 1 ? "No games in this library"
                              : "No installed games"
                emptyMessage: Library.searchText !== ""
                              ? "Try a different search, or clear it to see the whole library."
                              : root.organizationFiltersActive
                              ? "This is a filter, not your library. Clear or change it to see your games."
                              : Library.sourceFilter === "Faugus" && FaugusLibrary && FaugusLibrary.errorText.length > 0
                              ? FaugusLibrary.errorText
                              : Library.sourceFilter === "RetroArch" && RetroArchLibrary && RetroArchLibrary.errorText.length > 0
                              ? RetroArchLibrary.errorText
                              : Library.sourceFilter === "PCSX2" && Pcsx2Library && Pcsx2Library.errorText.length > 0
                              ? Pcsx2Library.errorText
                              : Library.sourceFilter === "Ryujinx" && RyujinxLibrary && RyujinxLibrary.errorText.length > 0
                              ? RyujinxLibrary.errorText
                              : Library.sourceFilter === "shadPS4" && Shadps4Library && Shadps4Library.errorText.length > 0
                              ? Shadps4Library.errorText
                              : Library.sourceFilter === "Cemu" && CemuLibrary && CemuLibrary.errorText.length > 0
                              ? CemuLibrary.errorText
                              : Library.sourceFilter === "Dolphin" && DolphinLibrary && DolphinLibrary.errorText.length > 0
                              ? DolphinLibrary.errorText
                              : Library.sourceFilter === "GOG" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : Library.sourceFilter === "Heroic" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : Library.sourceFilter === "Lutris" && LutrisLibrary && LutrisLibrary.errorText.length > 0
                              ? LutrisLibrary.errorText
                              : Library.sourceFilter === "Battle.net" && BattleNetLibrary && BattleNetLibrary.errorText.length > 0
                              ? BattleNetLibrary.errorText
                              : SteamLibrary && SteamLibrary.errorText.length > 0
                                ? SteamLibrary.errorText
                                : "Install a game in Steam, GOG, Lutris, Heroic, Faugus, RetroArch, PCSX2, Ryujinx, shadPS4, Cemu, Dolphin, or Battle.net, then rescan your library."
                onGameActivated: index => root.openGame(index)
                onFavoriteToggled: index => Library.toggleFavorite(index)
                onCoverRequested: function(source, appId) {
                    if (source === "Steam" && SteamLibrary) {
                        SteamLibrary.requestCover(appId)
                    } else if (source === "Battle.net" && BattleNetLibrary) {
                        BattleNetLibrary.requestCover(appId)
                    } else if (source === "RetroArch" && RetroArchLibrary) {
                        RetroArchLibrary.requestCover(appId)
                    } else if (source === "Dolphin" && DolphinLibrary) {
                        DolphinLibrary.requestCover(appId)
                    }
                }
                onRefreshRequested: {
                    root.rescanLibraries()
                }
                onFocusAboveRequested: root.focusAboveGrid()
            }
        }
    }

    CouchLibraryView {
        id: couchLibraryView
        objectName: "couchLibrary"
        anchors.fill: parent
        visible: root.couchMode && !root.detailOpen
        enabled: visible && root.navigationContainer() === null
        libraryModel: Library
        scanning: root.libraryScanning
        viewOverride: CouchLibraryViewOverride

        onGameActivated: index => root.openGame(index)
        onFavoriteToggled: function(index) {
            Library.toggleFavorite(index)
            couchLibraryView.refreshCurrentGame()
        }
        onOrganizeRequested: root.openBulkOrganization()
        onSavedFiltersRequested: root.openSavedFilters()
        onRandomRequested: root.pickRandomGame()
        onSettingsRequested: root.diagnosticsOpen = true
        onDesktopRequested: root.setCouchMode(false)
        onCoverRequested: function(source, appId) {
            if (source === "Steam" && SteamLibrary) {
                SteamLibrary.requestCover(appId)
            } else if (source === "Battle.net" && BattleNetLibrary) {
                BattleNetLibrary.requestCover(appId)
            } else if (source === "RetroArch" && RetroArchLibrary) {
                RetroArchLibrary.requestCover(appId)
            } else if (source === "Dolphin" && DolphinLibrary) {
                DolphinLibrary.requestCover(appId)
            }
        }
    }

    Loader {
        id: detailsLoader
        anchors.fill: parent
        active: root.detailOpen
        Keys.onPressed: function(event) {
            if (item && !root.linkDialogOpen && !root.diagnosticsOpen
                    && !root.collectionDeleteOpen) {
                root.handleArrowKey(item, event)
            }
        }
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
            couchMode: root.couchMode
            navigationEnabled: !root.backupEditorOpen && !root.bulkOrganizationOpen && !root.savedFiltersOpen && !root.artworkEditorOpen && !root.manualEditorOpen && !root.linkDialogOpen && !root.diagnosticsOpen
                               && !root.collectionDeleteOpen
            onBackRequested: root.closeDetails()
            onFavoriteRequested: {
                Library.toggleFavorite(root.selectedIndex)
                // The favorite filter can drop or move the row, so find the game again by identity.
                root.refreshAfterOrganization()
            }
            onManualEditRequested: root.editManualGame(root.selectedInstallation.appId)
            onPlayRequested: root.playSelected()
            onManageRequested: root.manageSelected()
            onInstallationSelected: installation => root.selectInstallation(installation)
            onPreferredInstallationRequested: {
                const choice = root.selectedInstallation
                if (Library.setPreferredInstallation(root.selectedIndex, choice.source,
                                                     choice.runner || "", choice.appId)) {
                    root.selectedInstallations = Library.installations(root.selectedIndex)
                    root.selectInstallation(root.preferredInstallation(root.selectedInstallations,
                                                                       root.selectedGame))
                    root.showToast("Default installation saved")
                    Qt.callLater(root.focusCurrentSurface)
                } else {
                    root.showToast("Could not save the default installation")
                }
            }
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
            randomSelection: root.randomSelection
            onRandomRequested: root.pickRandomGame()
            onCoverRequested: root.editArtwork()
            onCoverResetRequested: {
                if (Library.resetCustomCover(root.selectedIndex)) {
                    root.refreshAfterOrganization()
                    root.showToast("Original cover restored")
                }
            }
            onConnectRequested: { settingsOverlay.section = 2; root.diagnosticsOpen = true }
            onHiddenRequested: {
                Library.toggleHidden(root.selectedIndex)
                root.closeDetails()
            }
            onPinRequested: {
                if (Library.setPinned(root.selectedIndex, !root.selectedGame.pinned)) {
                    root.refreshAfterOrganization()
                }
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
                    detailsLoader.item.closeCollectionEditor()
                } else {
                    root.showToast("That collection already exists or is invalid")
                }
            }
            onTextEntryRequested: function(target, title, password, placeholder) {
                root.openCouchTextEntry(target, title, password, placeholder)
            }
        }
    }

    Rectangle {
        id: linkDialogOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.linkDialogOpen
        z: 25
        Keys.onPressed: function(event) { root.handleArrowKey(linkDialogOverlay, event) }
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
                property bool controllerNavigation: root.couchMode
                Layout.fillWidth: true
                placeholderText: "Search installed games"
                Accessible.name: placeholderText
                color: Theme.foreground
                font.family: Theme.fontFamily
                onTextChanged: root.linkResults = Library.linkCandidates(root.selectedIndex, text)
                Keys.onReturnPressed: function(event) {
                    root.handleCouchTextEntry(event, linkSearch,
                                              "SEARCH INSTALLATIONS", false,
                                              linkSearch.placeholderText)
                }
                Keys.onEnterPressed: function(event) {
                    root.handleCouchTextEntry(event, linkSearch,
                                              "SEARCH INSTALLATIONS", false,
                                              linkSearch.placeholderText)
                }
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
                    id: candidateDelegate
                    required property var modelData
                    required property int index
                    width: candidateList.width
                    height: 58
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "Link " + modelData.title + " from " + modelData.source
                    onClicked: root.linkCandidate(modelData)
                    Keys.onReturnPressed: function(event) {
                        root.linkCandidate(modelData)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        root.linkCandidate(modelData)
                        event.accepted = true
                    }
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
                            textFormat: Text.PlainText
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
                        color: candidateDelegate.down || candidateDelegate.hovered
                               || candidateDelegate.activeFocus
                               ? root.alpha(Theme.foreground, 0.09)
                               : root.alpha(Theme.foreground, 0.04)
                        border.width: candidateDelegate.activeFocus ? 2 : 1
                        border.color: candidateDelegate.activeFocus
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
        id: filterPickerOverlay
        objectName: "filterPickerOverlay"
        property var previousFocus: null
        anchors.fill: parent
        visible: root.filterPickerOpen
        z: 30
        Keys.onPressed: function(event) { root.handleArrowKey(filterPickerOverlay, event) }
        color: root.alpha(Theme.darkerBackground, 0.6)
        onVisibleChanged: {
            if (visible) {
                previousFocus = root.activeFocusItem
                Qt.callLater(function() {
                    // Land on the current value so Enter keeps it and arrows move from it.
                    const current = root.filterPickerCurrent()
                    const index = current === "" ? 0 : root.filterPickerValues.indexOf(current) + 1
                    pickerList.currentIndex = Math.max(0, index)
                    pickerList.positionViewAtIndex(pickerList.currentIndex, ListView.Contain)
                    const item = pickerList.itemAtIndex(pickerList.currentIndex)
                    if (item) {
                        item.forceActiveFocus(Qt.TabFocusReason)
                    } else {
                        root.focusWithin(filterPickerOverlay, true)
                    }
                })
            } else if (previousFocus) {
                root.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.filterPickerOpen = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(380, root.width - 56)
            height: Math.min(pickerColumn.implicitHeight + 40, root.height - 56)
            radius: Math.max(8, Theme.cornerRadius)
            color: root.alpha(Theme.background, 0.98)
            border.color: root.alpha(Theme.foreground, 0.22)

            MouseArea { anchors.fill: parent }

            ColumnLayout {
                id: pickerColumn
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: root.filterPickerKind === "status" ? "FILTER BY STATUS"
                            : root.filterPickerKind === "collection" ? "FILTER BY COLLECTION"
                            : "FILTER BY TAG"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true
                        text: "CLOSE"
                        onClicked: root.filterPickerOpen = false
                    }
                }
                ListView {
                    id: pickerList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, root.height - 160)
                    implicitHeight: Layout.preferredHeight
                    clip: true
                    spacing: 6
                    // The first row clears the filter; the rest are the available values.
                    model: [""].concat(root.filterPickerValues)
                    delegate: GlassButton {
                        required property string modelData
                        required property int index
                        width: pickerList.width
                        compact: true
                        selected: modelData === root.filterPickerCurrent()
                        text: modelData === ""
                              ? (root.filterPickerKind === "status" ? "ANY STATUS"
                                 : root.filterPickerKind === "collection" ? "ALL COLLECTIONS"
                                 : "ALL TAGS")
                              : modelData.toUpperCase()
                        onClicked: root.applyFilterPick(modelData)
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
        width: Math.min(toastText.implicitWidth + 34, parent.width - 48)
        height: 42
        // Above the settings panel and dialogs so confirmations stay readable.
        z: 40
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
            width: toast.width - 34
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
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

    Popup {
        id: coverSizePopup
        objectName: "coverSizePopup"
        parent: Overlay.overlay
        width: Math.min(340, root.width - 48)
        onAboutToShow: {
            const anchor = coverSizeButton.mapToItem(Overlay.overlay, 0, coverSizeButton.height)
            x = Math.max(24, Math.min(root.width - width - 24, anchor.x))
            y = Math.max(24, Math.min(root.height - implicitHeight - 24, anchor.y + 8))
        }
        padding: 20; modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.background; radius: Math.max(8, Theme.cornerRadius); border.color: Theme.mutedText }
        contentItem: ColumnLayout {
            spacing: 12
            CoverSizeControl { id: libraryCoverSize; Layout.fillWidth: true; onEditingFinished: coverSizePopup.close() }
            Text { text: libraryView.columns + " PER ROW"; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 }
        }
        onOpened: libraryCoverSize.focusSlider()
        onClosed: coverSizeButton.forceActiveFocus(Qt.TabFocusReason)
    }

    SettingsPanel {
        id: settingsOverlay
        host: root
        libraryCount: libraryView.count
    }

    Rectangle {
        id: collectionDeleteOverlay
        property var previousFocus: null
        anchors.fill: parent
        visible: root.collectionDeleteOpen
        z: 35
        Keys.onPressed: function(event) { root.handleArrowKey(collectionDeleteOverlay, event) }
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

    CouchKeyboard {
        id: couchTextEntryKeyboard
        objectName: "couchTextEntryKeyboard"
        anchors.fill: parent
        visible: root.couchTextEntryOpen
        enabled: visible
        z: 100
        title: root.couchTextEntryTitle
        placeholder: root.couchTextEntryPlaceholder
        passwordMode: root.couchTextEntryPassword
        gridObjectName: "couchTextEntryGrid"
        onAccepted: root.closeCouchTextEntry(true)
        onCanceled: root.closeCouchTextEntry(false)
    }

    Component.onCompleted: {
        smokeReady = true
        root.focusLibrary()
    }

    Connections {
        target: SteamAccount
        enabled: SteamAccount !== null
        function onAchievementsUpdated(appId) {
            if (root.detailOpen && root.selectedInstallation.appId === appId) {
                root.selectedGame = Library.get(root.selectedIndex)
            }
        }
        function onOwnedGamesUpdated() {
            if (SteamAccount.ownedGameCount === 0) {
                Library.availability = 0
            }
            if (libraryView.currentIndex < 0 && Library.rowCount() > 0) {
                libraryView.currentIndex = 0
                couchLibraryView.currentIndex = 0
            }
            if (root.detailOpen
                    && !root.refreshSelected(root.selectedGame.source,
                                             root.selectedGame.runner || "",
                                             root.selectedGame.appId)) {
                root.closeDetails()
            }
        }
    }

    Connections {
        // Background rescans reset the library model. Re-resolve the open game by identity so
        // detail actions never land on whichever game now occupies the old row index.
        target: Library
        function onModelReset() {
            if (!root.detailOpen || !root.selectedGame || !root.selectedGame.appId) {
                return
            }
            if (!root.refreshSelected(root.selectedGame.source,
                                      root.selectedGame.runner || "",
                                      root.selectedGame.appId)) {
                root.closeDetails()
            }
        }
    }

    Connections {
        target: Controller
        function onControllerChanged() {
            if (Controller.connected && root.couchMode) {
                Qt.callLater(root.focusCurrentSurface)
            }
        }
        function onFocusDirectionRequested(key) {
            if (!Controller.inputEnabled || !root.active) return
            const container = root.navigationContainer()
            if (!root.couchMode && !container && !libraryView.gridFocused
                    && !root.focusSpatial(librarySurface, key)
                    && key === Qt.Key_Down) {
                libraryView.focusGrid()
            }
            if (container) {
                root.focusSpatial(container, key)
            }
        }
        function onToolbarRequested() {
            if (!Controller.inputEnabled || !root.active) return
            root.toggleLibraryControls()
        }
        function onFavoriteRequested() {
            if (!Controller.inputEnabled || !root.active) return
            const focused = root.activeFocusItem
            if (focused && focused.sourceName !== undefined && focused.visible) {
                // On a source chip the favorite button means "add or remove this source".
                focused.secondaryClicked()
                return
            }
            if (root.detailOpen && !root.diagnosticsOpen && !root.linkDialogOpen
                    && !root.collectionDeleteOpen) {
                Library.toggleFavorite(root.selectedIndex)
                root.refreshAfterOrganization()
            } else if (root.couchMode && !root.detailOpen
                       && root.navigationContainer() === null
                       && !couchLibraryView.searchOpen
                       && !couchLibraryView.browseOpen
                       && couchLibraryView.currentIndex >= 0) {
                Library.toggleFavorite(couchLibraryView.currentIndex)
                couchLibraryView.refreshCurrentGame()
            } else if (!root.detailOpen && root.navigationContainer() === null
                       && libraryView.gridFocused && libraryView.currentIndex >= 0) {
                Library.toggleFavorite(libraryView.currentIndex)
            }
        }
    }
}

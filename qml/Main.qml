import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import "screens"

ApplicationWindow {
    id: root

    property var activeActionMenu: null
    property bool randomSelection: false
    property bool backupEditorOpen: false
    property bool bulkOrganizationOpen: false
    property bool homeOpen: false
    property var homeLibraryState: null
    property string homeReturnIdentity: ""
    property string homeReturnAction: ""
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
    property string pendingSaveWarning: ""
    Connections {
        target: SaveBackups
        function onWarning(message) { root.pendingSaveWarning = message }
    }

    Connections {
        target: Metadata
        function onEntryChanged(key) {
            if (root.detailOpen && root.selectedGame.metadataKey === key)
                root.refreshSelected(root.selectedGame.source, root.selectedGame.runner || "", root.selectedGame.appId)
        }
    }
    Connections {
        target: SessionRecorderStatus
        function onTotalsChanged() {
            if (!root.detailOpen) return
            const chosen = root.launchIdentity(root.selectedInstallation)
            if (root.refreshSelected(root.selectedGame.source, root.selectedGame.runner || "", root.selectedGame.appId)) {
                for (const installation of root.selectedInstallations)
                    if (root.launchIdentity(installation) === chosen) root.selectedInstallation = installation
            }
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
    readonly property var activeLibraryFilters: {
        const result = []
        for (const field of [
            {key: "completionFilter", label: "Status"}, {key: "collectionFilter", label: "Collection"},
            {key: "tagFilter", label: "Tag"}, {key: "genreFilter", label: "Genre"},
            {key: "decadeFilter", label: "Decade"}, {key: "platformFilter", label: "Platform"}]) {
            const value = Library[field.key]
            if (value) result.push({key: field.key, label: field.label + ": " + value, empty: ""})
        }
        if (Library.reviewFilter) result.push({key: "reviewFilter", label: reviewFilterLabel(Library.reviewFilter), empty: ""})
        if (Library.mode === 3) result.push({key: "mode", label: "Hidden games", empty: 0})
        if (Library.availability !== 0) result.push({key: "availability", label: Library.availability === 1 ? "All owned games" : "Ready to install", empty: 0})
        return result
    }
    function clearContextFilters() {
        if (Library.mode === 3) Library.mode = 0
        Library.completionFilter = ""; Library.collectionFilter = ""; Library.tagFilter = ""
        Library.genreFilter = ""; Library.decadeFilter = ""; Library.platformFilter = ""; Library.reviewFilter = ""
        Library.availability = 0
    }

    function isWithin(item, container) {
        while (item) {
            if (item === container) {
                return true
            }
            item = item.parent
        }
        return false
    }

    property bool returnToFilters: false
    function openFilterPicker(kind, values) {
        returnToFilters = libraryFilters.opened
        if (libraryFilters.opened) libraryFilters.close()
        filterPickerKind = kind
        filterPickerValues = values
        filterPickerOpen = true
    }

    function reviewFilterLabel(value) {
        return value === "identification" ? "Needs identification"
             : value === "artwork" ? "Missing artwork"
             : value === "either" ? "Needs identification or artwork" : "Any review status"
    }

    function filterPickerCurrent() {
        return filterPickerKind === "status" ? Library.completionFilter
             : filterPickerKind === "collection" ? Library.collectionFilter
             : filterPickerKind === "genre" ? Library.genreFilter
             : filterPickerKind === "decade" ? Library.decadeFilter
             : filterPickerKind === "platform" ? Library.platformFilter
             : filterPickerKind === "review" ? Library.reviewFilter
             : Library.tagFilter
    }

    function applyFilterPick(value) {
        if (filterPickerKind === "status") {
            Library.completionFilter = value
        } else if (filterPickerKind === "collection") {
            Library.collectionFilter = value
        } else if (filterPickerKind === "genre") {
            Library.genreFilter = value
        } else if (filterPickerKind === "decade") {
            Library.decadeFilter = value
        } else if (filterPickerKind === "review") {
            Library.reviewFilter = value
        } else if (filterPickerKind === "platform") {
            Library.platformFilter = value
        } else {
            Library.tagFilter = value
        }
        libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
        filterPickerOpen = false
    }

    function navigationContainer() {
        if (activeActionMenu && activeActionMenu.opened) return activeActionMenu.contentItem
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
        if (homeOpen) return homeScreen
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
        if (preferred && root.isWithin(preferred, container)
                && preferred.visible && preferred.enabled) {
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
        if (container === homeScreen && homeScreen.navigate(current, key)) return true
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
        if (explicitTarget && root.isWithin(explicitTarget, container)
                && explicitTarget.visible && explicitTarget.enabled) {
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
        // Two tiers. A candidate that overlaps the current item across the direction of travel
        // is a real neighbour and always wins; one that sits off to the side is only taken when
        // nothing overlaps and it is still roughly in line. Without the second rule a button at
        // the left edge of a column handed focus to the sidebar four hundred pixels higher up,
        // because any candidate in the half plane beat having no candidate at all.
        let best = null
        let bestScore = Number.MAX_VALUE
        let aside = null
        let asideScore = Number.MAX_VALUE
        // How far out of line a candidate may sit when nothing overlaps. Scaled by the current
        // item so a tall card tolerates more than a compact button, and floored so small
        // controls in a row can still reach each other.
        const vertical = key === Qt.Key_Up || key === Qt.Key_Down
        const sideways = Math.max(64, current.width * 0.75)
        let candidate = current.nextItemInFocusChain(true)
        for (let attempts = 0; candidate && candidate !== current
             && attempts < 300; ++attempts) {
            // An ancestor is not a neighbour. The details page keeps its content in a Flickable
            // that takes focus itself, and it sat directly below and to the right of everything
            // inside it, so down and right kept landing on the scroll view.
            if (root.isWithin(candidate, container) && candidate.visible
                    && candidate.enabled && candidate.activeFocusOnTab
                    && !root.isWithin(current, candidate)
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
                    if (crossGap <= 0) {
                        if (score < bestScore) {
                            best = candidate
                            bestScore = score
                        }
                    } else if (vertical && crossGap <= sideways && score < asideScore) {
                        // Only up and down settle for a candidate that is out of line, because
                        // columns rarely line up exactly. Left and right crossing into another
                        // row is never what is meant by pressing left or right.
                        aside = candidate
                        asideScore = score
                    }
                }
            }
            candidate = candidate.nextItemInFocusChain(true)
        }
        // Staying put is the right answer when nothing is really in that direction. Moving
        // somewhere far away because it was the only thing in the half plane is what made this
        // feel random.
        const chosen = best !== null ? best : aside
        if (chosen) {
            chosen.forceActiveFocus(Qt.TabFocusReason)
            root.revealNavigationItem(container, chosen)
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

    function openLibrarySearch() {
        if (root.activeActionMenu && root.activeActionMenu.opened) root.activeActionMenu.close()
        root.homeOpen = false
        if (root.couchMode) couchLibraryView.openSearch()
        else Qt.callLater(searchField.forceActiveFocus)
    }

    function toggleLibraryControls() {
        if (root.couchTextEntryOpen || couchLibraryView.searchOpen) return
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
        if (root.activeActionMenu && container === root.activeActionMenu.contentItem) {
            const scroll = container.navigationScrollView || container
            if (root.isWithin(item, scroll)) root.revealInScrollView(scroll, item)
        } else if (container === bulkOrganizationEditor) {
            bulkOrganizationEditor.reveal(item)
        } else if (container === homeScreen) {
            homeScreen.reveal(item)
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

    // Whether typing needs help from the app rather than a keyboard on the desk. Couch mode
    // always does; on a desktop it depends on whether the controller is the thing being used,
    // so a pad plugged in for gaming never makes this appear on a mouse click.
    readonly property bool textEntryNeedsKeyboard: TextEntry.keyboardNeeded
    // The singleton is what every field reads; the window is what knows the mode.
    Binding { target: TextEntry; property: "couchMode"; value: root.couchMode }

    function openCouchTextEntry(target, title, password, placeholder) {
        if (!root.textEntryNeedsKeyboard || !target) {
            return
        }
        couchTextEntryTarget = target
        couchTextEntryTitle = title || "ENTER TEXT"
        couchTextEntryPassword = password || false
        couchTextEntryPlaceholder = placeholder || "Start typing"
        couchTextEntryKeyboard.maximumLength = target.maximumLength !== undefined ? target.maximumLength : 128
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
        if (!root.textEntryNeedsKeyboard) {
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
        if (homeLibraryState !== null) { Library.applyFilterState(homeLibraryState); homeLibraryState = null }
        Qt.callLater(root.focusLibrary)
    }

    function focusLibrary() {
        if (root.homeOpen) { homeScreen.restoreIdentity(root.homeReturnIdentity, root.homeReturnAction); return }
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
        root.returnToViewMenu = false
        if (coverSizePopup.opened) coverSizePopup.close()
        if (activeActionMenu && activeActionMenu.opened) activeActionMenu.close()
        if (!enabled) {
            if (root.couchTextEntryOpen) {
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

    Connections {
        target: Preferences
        function onSaveFailed(message) { root.showToast(message) }
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
                                                      || Library.genreFilter !== ""
                                                      || Library.decadeFilter !== ""
                                                      || Library.platformFilter !== "" || Library.reviewFilter !== ""

    // Names the search or filter that produced an empty library, or returns "" when the
    // library itself is empty.
    readonly property string emptySourceFilter: Library.sourceFilters.length === 1 ? Library.sourceFilters[0] : ""

    function emptyTitleForFilters() {
        if (Library.searchText !== "") {
            return "No games match \"" + Library.searchText + "\""
        }
        const active = [Library.completionFilter, Library.collectionFilter, Library.tagFilter, Library.genreFilter, Library.decadeFilter, Library.platformFilter, Library.reviewFilter]
                       .filter(value => value !== "").length
        if (active > 1) {
            return "No games match these filters"
        }
        if (Library.reviewFilter === "identification") return "No games need identification in this view"
        if (Library.reviewFilter === "artwork") return "No games are missing artwork in this view"
        if (Library.reviewFilter === "either") return "No games need review in this view"
        if (Library.genreFilter || Library.decadeFilter || Library.platformFilter) {
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
        Library.genreFilter = ""
        Library.decadeFilter = ""
        Library.platformFilter = ""
        Library.reviewFilter = ""
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

    readonly property bool launchMatchesSelection: launchFeedback.request.gameKey === launchIdentity(selectedGame)
        && launchIdentity(launchFeedback.request.installation || {}) === launchIdentity(selectedInstallation)

    function launchIdentity(game) {
        return JSON.stringify([game.source || "", game.runner || "", game.appId || ""])
    }

    LaunchFeedback {
        id: launchFeedback
        objectName: "launchFeedback"
        onDispatchRequested: request => root.dispatchLaunch(request)
    }

    function playSelected() {
        if (launchFeedback.pending) { showToast(launchFeedback.message); return }
        launchFeedback.begin({gameKey: launchIdentity(selectedGame),
            title: selectedGame.title, installation: selectedInstallation})
    }

    function dispatchLaunch(request) {
        pendingSaveWarning = ""
        const choice = request.installation
        const installing = choice.installed === false
        let okay = false
        if (!DemoMode) {
            okay = installing ? Launcher.install(choice.source, choice.appId)
                : Launcher.launch(choice.source, choice.appId, choice.flatpak || false,
                                  choice.runner || "", choice.installPath || "", choice.launchTarget || "", choice.system || "")
        }
        const message = okay
            ? (installing ? "Opening Steam to install " : "Opening ") + request.title
                + (installing ? "" : " in " + choice.source)
            : (DemoMode ? "Demo games cannot be launched" : Launcher.lastError || "Could not open this game. Try again.")
        launchFeedback.finish(okay, pendingSaveWarning ? message + ". " + pendingSaveWarning : message)
        showToast(pendingSaveWarning || message)
        if (okay && !installing) {
            // Filters or selection may have changed during the feedback frame.
            Library.recordLaunchByIdentity(choice.source, choice.runner || "", choice.appId)
            if (Preferences.closeAfterLaunch && !pendingSaveWarning) Qt.callLater(Qt.quit)
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

    property var libraryEditorInvoker: null
    function dismissLibraryEditor(kind) {
        if (kind === "bulk") {
            Library.clearSelection()
            root.bulkOrganizationOpen = false
        } else root.savedFiltersOpen = false
        const invoker = root.libraryEditorInvoker
        root.libraryEditorInvoker = null
        if (invoker && invoker.visible && invoker.enabled) root.restoreFocus(invoker)
        else Qt.callLater(root.focusCurrentSurface)
    }

    function openBulkOrganization() {
        root.libraryEditorInvoker = root.activeFocusItem
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
        onDismissed: root.dismissLibraryEditor("bulk")
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function openSavedFilters() {
        root.libraryEditorInvoker = root.activeFocusItem
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
        root.libraryEditorInvoker = null
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
        onDismissed: root.dismissLibraryEditor("saved")
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    property var editorInvokers: ({})
    function rememberEditor(kind) { editorInvokers[kind] = root.activeFocusItem }
    function dismissEditor(kind) {
        root[kind + "EditorOpen"] = false
        const invoker = editorInvokers[kind]
        delete editorInvokers[kind]
        if (invoker && invoker.visible && invoker.enabled) root.restoreFocus(invoker)
        else Qt.callLater(root.focusCurrentSurface)
    }
    function openBackupEditor() {
        rememberEditor("backup")
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
        onDismissed: root.dismissEditor("backup")
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editArtwork() {
        rememberEditor("artwork")
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
        onDismissed: root.dismissEditor("artwork")
        onArtworkChanged: root.refreshAfterOrganization()
        onTextEntryRequested: (target, title) => root.openCouchTextEntry(target, title, false, "")
    }

    function editManualGame(id) {
        rememberEditor("manual")
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
        onDismissed: root.dismissEditor("manual")
        onSaved: function(id) {
            root.manualEditorOpen = false
            root.diagnosticsOpen = false
            if (manualEditor.entryId === "") root.clearLibraryFilters()
            const row = Library.indexOf("Manual", "", id)
            if (row >= 0) root.openGame(row)
            else root.closeDetails()
            root.showToast("Manual game saved")
        }
        onRemoved: {
            root.manualEditorOpen = false
            root.closeDetails()
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
        enabled: !root.couchTextEntryOpen && !root.detailOpen
                 && (root.navigationContainer() === null || root.navigationContainer() === homeScreen
                     || (root.activeActionMenu && root.activeActionMenu.opened))
        onActivated: root.openLibrarySearch()
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
        enabled: !root.couchTextEntryOpen && !couchLibraryView.searchOpen && !root.linkDialogOpen && !root.collectionDeleteOpen
                 && !root.backupEditorOpen && !root.manualEditorOpen && !root.artworkEditorOpen && !root.bulkOrganizationOpen && !root.savedFiltersOpen
        onActivated: {
            if (root.activeActionMenu && root.activeActionMenu.opened) root.activeActionMenu.close()
            root.diagnosticsOpen = !root.diagnosticsOpen
        }
    }
    Shortcut {
        sequence: "F6"
        enabled: root.navigationContainer() === null
        onActivated: root.toggleLibraryControls()
    }
    Shortcut {
        objectName: "navigationTabForward"
        sequence: "Tab"
        enabled: root.navigationContainer() !== null
        onActivated: root.focusWithin(root.navigationContainer(), true)
    }
    Shortcut {
        objectName: "navigationTabBackward"
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
            if (activeActionMenu && activeActionMenu.opened) {
                activeActionMenu.close()
            } else if (coverSizePopup.opened) {
                coverSizePopup.close()
            } else if (root.couchTextEntryOpen) {
                root.closeCouchTextEntry(false)
            } else if (root.backupEditorOpen) {
                backupEditor.dismiss()
            } else if (root.bulkOrganizationOpen) {
                root.dismissLibraryEditor("bulk")
            } else if (root.savedFiltersOpen) {
                root.dismissLibraryEditor("saved")
            } else if (root.artworkEditorOpen) {
                root.dismissEditor("artwork")
            } else if (root.manualEditorOpen) {
                root.dismissEditor("manual")
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
            } else if (root.homeOpen) {
                root.homeOpen = false
                Qt.callLater(root.focusLibrary)
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
        visible: !root.homeOpen && !root.couchMode && opacity > 0
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

                GlassButton {
                    objectName: "openHomeButton"
                    text: "HOME"; compact: true
                    onClicked: { root.homeOpen = true; Qt.callLater(homeScreen.focusHome) }
                }
                GlassButton {
                    objectName: "libraryDestinationButton"
                    text: "LIBRARY"; compact: true; selected: true
                    onClicked: libraryView.focusGrid()
                }
                Item { Layout.fillWidth: true }

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

            GridLayout {
                objectName: "libraryQueryBar"
                Layout.fillWidth: true
                columns: root.width < 720 ? 1 : 2
                columnSpacing: 12
                rowSpacing: 8
                Row {
                    spacing: 5
                    visible: root.width >= 1040

                    GlassButton {
                        id: allModeButton
                        objectName: "allModeButton"
                        property Item controllerDownTarget: sourcesMenuButton
                        text: "ALL"
                        compact: true
                        selected: Library.mode === 0
                        onClicked: {
                            Library.mode = 0
                        }
                    }
                    GlassButton {
                        id: favoritesModeButton
                        objectName: "favoritesModeButton"
                        property Item controllerDownTarget: sourcesMenuButton
                        text: "FAVORITES"
                        compact: true
                        selected: Library.mode === 1
                        onClicked: {
                            Library.mode = 1
                        }
                    }
                    GlassButton {
                        id: recentModeButton
                        objectName: "recentModeButton"
                        property Item controllerDownTarget: sourcesMenuButton
                        text: "RECENT"
                        compact: true
                        selected: Library.mode === 2
                        onClicked: {
                            Library.mode = 2
                        }
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
                        }
                    }
                    GlassButton {
                        text: "FAVORITES"
                        compact: true
                        selected: Library.mode === 1
                        onClicked: {
                            Library.mode = 1
                        }
                    }
                    GlassButton {
                        text: "RECENT"
                        compact: true
                        selected: Library.mode === 2
                        onClicked: {
                            Library.mode = 2
                        }
                    }


                }
                TextField {
                    id: searchField
                    objectName: "searchField"
                    property bool controllerNavigation: TextEntry.keyboardNeeded
                    Layout.fillWidth: true
                    Layout.preferredWidth: 220
                    Layout.minimumWidth: 140
                    Layout.preferredHeight: 38
                    placeholderText: "Search games"
                    color: Theme.foreground
                    placeholderTextColor: root.alpha(Theme.foreground, 0.42)
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    leftPadding: 36
                    rightPadding: searchFieldClear.visible ? searchFieldClear.reservedWidth : 12
                    selectByMouse: true
                    focus: false
                    property Item controllerUpTarget: root.width < 720 ? narrowAllModeButton : null
                    property Item controllerRightTarget: searchFieldClear.visible ? searchFieldClear : null
                    FieldClearButton { id: searchFieldClear; field: searchField }
                    Keys.onReturnPressed: event => root.handleCouchTextEntry(event, searchField, "SEARCH GAMES", false, "Search games")
                    Keys.onEnterPressed: event => root.handleCouchTextEntry(event, searchField, "SEARCH GAMES", false, "Search games")
                    Accessible.name: "Search games"
                    Accessible.description: "Search the current game library"

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
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                GlassButton {
                    id: sourcesMenuButton; objectName: "sourcesMenuButton"
                    compact: true; text: Library.sourceFilters.length ? "SOURCES (" + Library.sourceFilters.length + ")" : "SOURCES"
                    selected: Library.sourceFilters.length > 0
                    onClicked: librarySources.open()
                }
                GlassButton {
                    id: filtersMenuButton; objectName: "filtersMenuButton"
                    compact: true; text: root.activeLibraryFilters.length ? "FILTERS (" + root.activeLibraryFilters.length + ")" : "FILTERS"
                    selected: root.activeLibraryFilters.length > 0
                    onClicked: libraryFilters.open()
                }
                GlassButton {
                    id: sortButton
                    property Item controllerLeftTarget: filtersMenuButton
                    property Item controllerRightTarget: viewMenuButton
                    objectName: "sortButton"
                    compact: true
                    text: Library.sortMode === 0 ? "SORT: TITLE" : Library.sortMode === 1 ? "SORT: RECENT" : Library.sortMode === 2 ? "SORT: PLAYTIME" : Library.sortMode === 3 ? "SORT: RATING" : "SORT: POPULARITY"
                    onClicked: librarySort.open()
                }
                GlassButton {
                    id: viewMenuButton; objectName: "viewMenuButton"
                    text: "VIEW"; compact: true
                    onClicked: libraryViewMenu.open()
                }
                GlassButton {
                    id: libraryMoreButton
                    property Item controllerLeftTarget: viewMenuButton
                    property Item controllerUpTarget: settingsButton
                    objectName: "libraryMoreButton"
                    text: "MORE"; compact: true
                    onClicked: libraryActions.open()
                }
                Text {
                    text: root.libraryScanning ? "SCANNING…" : libraryView.count + " GAMES"
                    color: Theme.mutedText; font.family: Theme.fontFamily
                    font.pixelSize: 11
                    height: 34; verticalAlignment: Text.AlignVCenter
                }

            }

            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: root.activeLibraryFilters.length > 0
                Repeater {
                    model: root.activeLibraryFilters
                    GlassButton {
                        required property var modelData
                        compact: true
                        maximumLabelWidth: Math.max(80, librarySurface.width - 100)
                        text: modelData.label + " ×"
                        Accessible.name: "Remove " + modelData.label + " filter"
                        onClicked: { Library[modelData.key] = modelData.empty; Qt.callLater(filtersMenuButton.forceActiveFocus) }
                    }
                }
                GlassButton {
                    text: "CLEAR FILTERS"; compact: true
                    onClicked: { root.clearContextFilters(); filtersMenuButton.forceActiveFocus() }
                }
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
                            : root.emptySourceFilter === "GOG" && HeroicLibrary && !HeroicLibrary.gogDetected
                            ? "GOG was not found"
                            : root.emptySourceFilter === "Heroic" && HeroicLibrary && !HeroicLibrary.heroicDetected
                            ? "Heroic was not found"
                            : root.emptySourceFilter === "Faugus" && FaugusLibrary && !FaugusLibrary.faugusDetected
                            ? "Faugus was not found"
                            : root.emptySourceFilter === "RetroArch" && RetroArchLibrary && !RetroArchLibrary.retroArchDetected
                            ? "RetroArch was not found"
                            : root.emptySourceFilter === "PCSX2" && Pcsx2Library && !Pcsx2Library.pcsx2Detected
                            ? "PCSX2 was not found"
                            : root.emptySourceFilter === "Ryujinx" && RyujinxLibrary && !RyujinxLibrary.ryujinxDetected
                            ? "Ryujinx was not found"
                            : root.emptySourceFilter === "shadPS4" && Shadps4Library && !Shadps4Library.shadps4Detected
                            ? "shadPS4 was not found"
                            : root.emptySourceFilter === "Cemu" && CemuLibrary && !CemuLibrary.cemuDetected
                            ? "Cemu was not found"
                            : root.emptySourceFilter === "Dolphin" && DolphinLibrary && !DolphinLibrary.dolphinDetected
                            ? "Dolphin was not found"
                            : root.emptySourceFilter === "Battle.net" && BattleNetLibrary && !BattleNetLibrary.battleNetDetected
                            ? "Battle.net was not found"
                            : root.emptySourceFilter === "Lutris" && LutrisLibrary && !LutrisLibrary.lutrisDetected
                            ? "Lutris was not found"
                            : root.emptySourceFilter === "Steam" && SteamLibrary && !SteamLibrary.steamDetected
                              ? "Steam was not found"
                              : Library.mode === 1 ? "No favorites in this view"
                              : Library.mode === 2 ? "No recently played games in this view"
                              : Library.mode === 3 ? "No hidden games"
                              : Library.availability === 2 ? "No games ready to install"
                              : Library.availability === 1 ? "No games in this library"
                              : "No installed games"
                emptyMessage: Library.searchText !== ""
                              ? "Try a different search, or clear it to see the whole library."
                              : root.organizationFiltersActive
                              ? "Clear or change these filters to see more games."
                              : root.emptySourceFilter === "Faugus" && FaugusLibrary && FaugusLibrary.errorText.length > 0
                              ? FaugusLibrary.errorText
                              : root.emptySourceFilter === "RetroArch" && RetroArchLibrary && RetroArchLibrary.errorText.length > 0
                              ? RetroArchLibrary.errorText
                              : root.emptySourceFilter === "PCSX2" && Pcsx2Library && Pcsx2Library.errorText.length > 0
                              ? Pcsx2Library.errorText
                              : root.emptySourceFilter === "Ryujinx" && RyujinxLibrary && RyujinxLibrary.errorText.length > 0
                              ? RyujinxLibrary.errorText
                              : root.emptySourceFilter === "shadPS4" && Shadps4Library && Shadps4Library.errorText.length > 0
                              ? Shadps4Library.errorText
                              : root.emptySourceFilter === "Cemu" && CemuLibrary && CemuLibrary.errorText.length > 0
                              ? CemuLibrary.errorText
                              : root.emptySourceFilter === "Dolphin" && DolphinLibrary && DolphinLibrary.errorText.length > 0
                              ? DolphinLibrary.errorText
                              : root.emptySourceFilter === "GOG" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : root.emptySourceFilter === "Heroic" && HeroicLibrary && HeroicLibrary.errorText.length > 0
                              ? HeroicLibrary.errorText
                              : root.emptySourceFilter === "Lutris" && LutrisLibrary && LutrisLibrary.errorText.length > 0
                              ? LutrisLibrary.errorText
                              : root.emptySourceFilter === "Battle.net" && BattleNetLibrary && BattleNetLibrary.errorText.length > 0
                              ? BattleNetLibrary.errorText
                              : (Library.sourceFilters.length === 0 || Library.sourceFilters.indexOf("Steam") >= 0)
                                && SteamLibrary && SteamLibrary.errorText.length > 0
                                ? SteamLibrary.errorText
                                : Library.mode === 1 ? "Mark games as favorites from their details, or change this view to see more games."
                                : Library.mode === 2 ? "Games you play appear here when they match this view."
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

    Binding { target: Home; property: "active"; value: root.homeOpen }
    HomeScreen {
        id: homeScreen
        objectName: "homeScreen"
        launchBusy: launchFeedback.pending && launchFeedback.request.gameKey === root.launchIdentity(homeScreen.featured)
        anchors.fill: parent
        visible: root.homeOpen && !root.detailOpen
        couchMode: root.couchMode
        onLibraryRequested: { root.homeOpen = false; Qt.callLater(root.focusLibrary) }
        onBrowseRequested: (kind, value) => {
            if (kind === "saved") {
                if (Library.applySavedFilter(value)) {
                    root.homeOpen = false
                    Qt.callLater(root.focusLibrary)
                } else root.showToast(Library.savedFilterMessage || "Could not open saved view")
                return
            }
            root.clearLibraryFilters()
            Library.searchText = ""
            Library.sourceFilters = kind === "source" ? [value] : []
            Library.consoleFilter = kind === "console" ? value : ""
            Library.showHidden = false
            Library.mode = kind === "favorites" ? 1 : kind === "recent" ? 2 : 0
            if (kind === "recent") Library.sortMode = 1
            Library.availability = 0
            Library.completionFilter = kind === "backlog" ? "backlog" : ""
            Library.collectionFilter = kind === "collection" ? value : ""
            root.homeOpen = false
            Qt.callLater(root.focusLibrary)
        }
        function selectHomeGame(game, action) {
            root.homeReturnAction = action || "tile"
            root.homeReturnIdentity = homeScreen.focusKey(game)
            root.homeLibraryState = Library.filterState()
            const row = Library.revealGame(game.source, game.runner || "", game.appId)
            if (row >= 0) {
                root.openGame(row)
                return true
            }
            Library.applyFilterState(root.homeLibraryState)
            root.homeLibraryState = null
            root.showToast("This game is no longer available")
            return false
        }
        onGameRequested: (game, action) => selectHomeGame(game, action)
        onPlayRequested: game => {
            if (launchFeedback.pending) root.showToast(launchFeedback.message)
            else if (selectHomeGame(game, "play")) root.playSelected()
        }
    }

    CouchLibraryView {
        id: couchLibraryView
        objectName: "couchLibrary"
        anchors.fill: parent
        visible: !root.homeOpen && root.couchMode && !root.detailOpen
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
        onHomeRequested: { root.homeOpen = true; Qt.callLater(homeScreen.focusHome) }
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
        onLoaded: Qt.callLater(function() {
            if (root.detailOpen && detailsLoader.item) detailsLoader.item.focusPrimary()
        })
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
            launchBusy: launchFeedback.pending && root.launchMatchesSelection
            launchMessage: root.launchMatchesSelection ? launchFeedback.message : ""
            launchFailed: launchFeedback.failed
            installations: root.selectedInstallations
            selectedInstallation: root.selectedInstallation
            couchMode: root.couchMode
            navigationEnabled: !root.activeActionMenu && !root.backupEditorOpen && !root.bulkOrganizationOpen && !root.savedFiltersOpen && !root.artworkEditorOpen && !root.manualEditorOpen && !root.linkDialogOpen && !root.diagnosticsOpen
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
                property bool controllerNavigation: root.couchMode || (Controller !== null && Controller.driving)
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
            } else if (root.returnToFilters) {
                root.returnToFilters = false
                previousFocus = null
                Qt.callLater(libraryFilters.open)
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
                            : "FILTER BY " + root.filterPickerKind.toUpperCase()
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
                Text {
                    Layout.fillWidth: true
                    visible: root.filterPickerKind === "genre" || root.filterPickerKind === "decade"
                    text: "Uses available game metadata. Games without a matching value are excluded."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
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
                                 : root.filterPickerKind === "review" ? "ANY REVIEW STATUS"
                                 : "ANY " + root.filterPickerKind.toUpperCase())
                              : root.filterPickerKind === "review" ? root.reviewFilterLabel(modelData).toUpperCase() : modelData.toUpperCase()
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

    ActionMenu {
        id: librarySources
        objectName: "librarySources"
        host: root
        anchorItem: sourcesMenuButton
        title: "SOURCES"
        width: Math.min(540, root.width - 48)
        initialFocus: allSourcesButton
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Select a source. Shift+Enter or the controller favorite button adds or removes a source."
            color: Theme.mutedText
            font.family: Theme.fontFamily
        }
        Flow {
            id: sourceButtonsRow
            Layout.fillWidth: true
            spacing: 6
            GlassButton {
                id: allSourcesButton
                objectName: "allSourcesButton"
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
    ActionMenu {
        id: libraryFilters
        objectName: "libraryFilters"
        host: root
        anchorItem: filtersMenuButton
        title: "FILTER LIBRARY"
        width: Math.min(540, root.width - 48)
        initialFocus: !DemoMode && root.ownedGameCount > 0 ? installedAvailabilityButton : statusFilterButton
        MenuAction {
            id: hiddenModeButton
            objectName: "hiddenModeButton"
            visible: !DemoMode
            compact: true
            text: "HIDDEN GAMES"
            selected: Library.mode === 3
            onClicked: Library.mode = Library.mode === 3 ? 0 : 3
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
                }
            }
            GlassButton {
                compact: true
                text: "ALL GAMES"
                selected: Library.availability === 1
                onClicked: {
                    Library.availability = 1
                }
            }
            GlassButton {
                id: readyAvailabilityButton
                objectName: "readyAvailabilityButton"
                compact: true
                text: "READY TO INSTALL"
                selected: Library.availability === 2
                onClicked: {
                    Library.availability = 2
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6

            GlassButton {
                id: statusFilterButton
                visible: !DemoMode
                objectName: "statusFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: root.filterLabel("STATUS", Library.completionFilter)
                selected: Library.completionFilter !== ""
                onClicked: root.openFilterPicker("status", ["backlog", "playing", "completed", "abandoned"])
            }
            GlassButton {
                id: collectionFilterButton
                visible: !DemoMode
                objectName: "collectionFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: root.filterLabel("COLLECTION", Library.collectionFilter, Library.collectionNames)
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
                visible: !DemoMode
                objectName: "tagFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
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
                visible: Library.completionFilter !== "" || Library.collectionFilter !== "" || Library.tagFilter !== ""
                text: "CLEAR"
                onClicked: {
                    Library.completionFilter = ""
                    Library.collectionFilter = ""
                    Library.tagFilter = ""
                    libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
                }
            }

            GlassButton {
                objectName: "genreFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: root.filterLabel("GENRE", Library.genreFilter)
                selected: Library.genreFilter !== ""
                onClicked: root.openFilterPicker("genre", Library.genreNames)
            }
            GlassButton {
                objectName: "decadeFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: root.filterLabel("DECADE", Library.decadeFilter)
                selected: Library.decadeFilter !== ""
                onClicked: root.openFilterPicker("decade", Library.decadeNames)
            }
            GlassButton {
                objectName: "platformFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: root.filterLabel("PLATFORM", Library.platformFilter)
                selected: Library.platformFilter !== ""
                onClicked: root.openFilterPicker("platform", Library.platformNames)
            }
            GlassButton {
                objectName: "reviewFilterButton"
                maximumLabelWidth: Math.max(80, libraryFilters.width - 80)
                compact: true
                text: Library.reviewFilter ? root.reviewFilterLabel(Library.reviewFilter).toUpperCase() : "NEEDS REVIEW"
                selected: Library.reviewFilter !== ""
                onClicked: root.openFilterPicker("review", ["identification", "artwork", "either"])
            }
            GlassButton {
                compact: true
                visible: Library.genreFilter !== "" || Library.decadeFilter !== "" || Library.platformFilter !== "" || Library.reviewFilter !== ""
                text: "CLEAR METADATA FILTERS"
                onClicked: {
                    Library.genreFilter = ""
                    Library.decadeFilter = ""
                    Library.platformFilter = ""
                    Library.reviewFilter = ""
                }
            }
        }
    }
    ActionMenu {
        id: librarySort
        objectName: "librarySort"
        host: root
        anchorItem: sortButton
        title: "SORT GAMES"
        Repeater {
            model: ["TITLE", "RECENTLY PLAYED", "PLAYTIME", "RATING", "POPULARITY"]
            MenuAction {
                required property int index
                required property string modelData
                Layout.fillWidth: true
                compact: true
                text: modelData
                selected: Library.sortMode === index
                onClicked: {
                    Library.sortMode = index
                    librarySort.close()
                }
            }
        }
    }
    ActionMenu {
        id: libraryViewMenu
        objectName: "libraryViewMenu"
        host: root
        anchorItem: viewMenuButton
        title: "LIBRARY VIEW"
        MenuAction {
            id: consoleGamesButton
            objectName: "consoleGamesButton"
            // Every console system follows this view unless explicitly overridden.
            visible: Library.hasConsoleCards || Library.expandConsoles
            compact: true
            selected: Library.expandConsoles
            text: Library.expandConsoles ? "CONSOLE VIEW: GAMES" : "CONSOLE VIEW: CONSOLES"
            onClicked: {
                Library.expandConsoles = !Library.expandConsoles
                libraryView.currentIndex = Library.rowCount() > 0 ? 0 : -1
            }
        }
        MenuAction {
            id: coverSizeButton
            objectName: "coverSizeButton"
            compact: true
            text: "COVER SIZE"
            onClicked: {
                root.returnToViewMenu = true
                libraryViewMenu.invoke(coverSizePopup.open)
            }
        }
    }
    ActionMenu {
        id: libraryActions
        objectName: "libraryActions"
        host: root
        anchorItem: libraryMoreButton
        title: "LIBRARY ACTIONS"
        initialFocus: randomGameButton
        MenuAction {
            objectName: "libraryAddGameButton"
            visible: !DemoMode
            Layout.fillWidth: true
            compact: true
            text: "ADD A GAME"
            onClicked: libraryActions.invoke(function () {
                root.editManualGame("")
            })
        }
        MenuAction {
            objectName: "libraryCollectionsButton"
            visible: !DemoMode
            Layout.fillWidth: true
            compact: true
            text: "MANAGE COLLECTIONS"
            onClicked: libraryActions.invoke(function () {
                root.diagnosticsOpen = true
                settingsOverlay.focusCollections()
            })
        }

        MenuAction {
            id: randomGameButton
            objectName: "randomGameButton"
            compact: true
            text: "PICK A GAME"
            onClicked: libraryActions.invoke(root.pickRandomGame)
        }
        MenuAction {
            objectName: "bulkOrganizationButton"
            text: "ORGANIZE"
            Layout.fillWidth: true
            compact: true
            onClicked: libraryActions.invoke(root.openBulkOrganization)
        }
        MenuAction {
            objectName: "savedFiltersButton"
            text: "SAVED FILTERS"
            Layout.fillWidth: true
            compact: true
            onClicked: libraryActions.invoke(root.openSavedFilters)
        }
        MenuAction {
            id: rescanButton
            objectName: "rescanButton"
            Layout.fillWidth: true
            compact: true
            text: root.libraryScanning ? "SCANNING" : "RESCAN"
            enabled: !root.libraryScanning
            onClicked: libraryActions.invoke(root.rescanLibraries)
        }
    }

    property bool returnToViewMenu: false
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
        onClosed: {
            if (root.returnToViewMenu && !root.couchMode) Qt.callLater(libraryViewMenu.open)
            root.returnToViewMenu = false
        }
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
        function onStartRequested() {
            if (!Controller.inputEnabled || !root.active) return
            if (root.couchTextEntryOpen) root.closeCouchTextEntry(true)
            else if (root.couchMode && couchLibraryView.searchOpen) couchLibraryView.closeSearch(true)
            else root.toggleCouchMode()
        }
        function onToolbarRequested() {
            if (!Controller.inputEnabled || !root.active) return
            const keyboard = root.couchTextEntryOpen ? couchTextEntryKeyboard
                           : couchLibraryView.searchOpen ? couchLibraryView.searchKeyboard : null
            if (keyboard) { keyboard.appendText(" "); return }
            root.toggleLibraryControls()
        }
        function onFavoriteRequested() {
            if (!Controller.inputEnabled || !root.active) return
            const keyboard = root.couchTextEntryOpen ? couchTextEntryKeyboard
                           : couchLibraryView.searchOpen ? couchLibraryView.searchKeyboard : null
            if (keyboard) { keyboard.activateKey(40); return }
            const focused = root.activeFocusItem
            if (focused && focused.sourceName !== undefined && focused.visible) {
                // On a source chip the favorite button means "add or remove this source".
                focused.secondaryClicked()
                return
            }
            if (root.activeActionMenu && root.activeActionMenu.opened) return
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

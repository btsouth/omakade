import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Rectangle {
    id: root
    objectName: "libraryRepairPanel"
    color: Theme.background
    signal dismissed()
    signal openGame(string kind)
    signal editManualRequested(string appId)
    signal textEntryRequested(var target, string title, bool password, string placeholder)
    property var service: typeof LibraryRepair !== "undefined" ? LibraryRepair : null
    readonly property bool couchMode: root.Window.window ? root.Window.window.couchMode : false
    readonly property real uiScale: root.couchMode ? 1.35 : 1
    property bool relocationOpen: false
    readonly property Item relocationNavigationItem: relocationOverlay
    property string relocationKey: ""
    property string relocationPath: ""
    property var relocationPreview: ({})
    readonly property var entries: root.service ? root.service.entries : []
    readonly property var reasonCounts: {
        const counts = ({})
        for (const entry of root.entries)
            for (const reason of entry.reasons || []) counts[reason] = (counts[reason] || 0) + 1
        return counts
    }
    readonly property string reasonSummary: {
        const labels = {"identification": "need identity", "artwork": "need artwork",
                        "missing-file": "missing file", "missing-storage": "storage disconnected",
                        "runtime": "runtime unavailable", "source-error": "scan error",
                        "unavailable": "unavailable", "duplicates": "possible duplicate"}
        const parts = []
        for (const key of Object.keys(labels))
            if (root.reasonCounts[key] > 0) parts.push(root.reasonCounts[key] + " " + labels[key])
        return parts.join("  ·  ")
    }
    property var game: root.service ? root.service.current : ({})
    readonly property int currentPosition: {
        if (!root.game.metadataKey) return 0
        for (let index = 0; index < root.entries.length; ++index)
            if (root.entries[index].metadataKey === root.game.metadataKey) return index + 1
        return 0
    }
    readonly property bool metadataRetryAvailable: {
        for (const entry of root.entries)
            if ((entry.reasons || []).includes("identification") || (entry.reasons || []).includes("artwork"))
                return true
        return false
    }
    function reveal(item) {
        let candidate = item
        while (candidate && candidate !== reviewScroll && candidate !== reviewScroll.contentItem)
            candidate = candidate.parent
        if (candidate) root.Window.window.revealInScrollView(reviewScroll, item)
    }
    function focusEditor() { closeButton.forceActiveFocus() }
    function refreshRelocationPreview() {
        relocationPreview = root.service
            ? root.service.previewRelocation(relocationKey, relocationPath) : ({})
    }
    function openRelocation(key, initialPath) {
        relocationKey = key
        relocationPath = initialPath || ""
        relocationOpen = true
        refreshRelocationPreview()
        Qt.callLater(function() { relocationPathField.forceActiveFocus() })
    }
    function closeRelocation() {
        relocationOpen = false
        relocationKey = ""
        relocationPath = ""
        relocationPreview = ({})
        Qt.callLater(root.focusEditor)
    }
    function confirmRelocation() {
        if (!root.service || !relocationPreview.ok) return
        if (root.service.relocate(relocationKey, relocationPath)) closeRelocation()
        else refreshRelocationPreview()
    }
    component RepairButton: GlassButton {
        id: repairButton
        displayScale: root.uiScale
        onActiveFocusChanged: if (activeFocus) root.reveal(repairButton)
    }
    function actionForReasonRow(start, direction) {
        for (let index = start; index >= 0 && index < reasonItems.count; index += direction) {
            const row = reasonItems.itemAt(index)
            const target = row ? direction < 0 ? row.lastAction : row.firstAction : null
            if (target) return target
        }
        return null
    }
    MouseArea { anchors.fill: parent }
    ScrollView {
        id: reviewScroll
        enabled: !root.relocationOpen
        anchors.centerIn: parent
        width: Math.max(1, Math.min(parent.width - 64, 1100 * root.uiScale))
        height: parent.height - 64
        contentWidth: availableWidth
        ColumnLayout {
            width: reviewScroll.availableWidth
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5 * root.uiScale
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "libraryRepairTitle"
                        text: "REPAIR LIBRARY"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 26 * root.uiScale
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    RepairButton {
                        id: closeButton
                        objectName: "libraryRepairCloseButton"
                        text: "CLOSE"
                        displayScale: root.uiScale
                        property Item controllerDownTarget: sourceFilter
                        onClicked: root.dismissed()
                    }
                }
                Text {
                    objectName: "libraryRepairProgress"
                    text: root.currentPosition + " OF " + root.entries.length
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                }
            }
            Text {
                objectName: "libraryRepairReasonSummary"
                Layout.fillWidth: true
                text: (root.Window.window && root.Window.window.libraryScanning
                       ? "Checking sources; counts may change. " : "")
                      + root.entries.length + " games in this review view"
                      + (root.reasonSummary ? "  ·  " + root.reasonSummary : "")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.body * root.uiScale
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                ThemedComboBox {
                    id: sourceFilter
                    objectName: "libraryRepairSourceFilter"
                    uiScale: root.uiScale
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    model: ["All sources"].concat(root.service ? root.service.sources : [])
                    currentIndex: Math.max(0, model.indexOf(root.service && root.service.source
                                                               ? root.service.source : "All sources"))
                    Accessible.name: "Filter repair games by source"
                    onActiveFocusChanged: if (activeFocus) root.reveal(sourceFilter)
                    property Item controllerUpTarget: closeButton
                    property Item controllerRightTarget: reasonFilter
                    property Item controllerDownTarget: root.actionForReasonRow(0, 1) || previousButton
                    onActivated: root.service.source = currentIndex ? currentText : ""
                }
                ThemedComboBox {
                    id: reasonFilter
                    objectName: "libraryRepairReasonFilter"
                    uiScale: root.uiScale
                    font.pixelSize: 13 * root.uiScale
                    implicitHeight: 40 * root.uiScale
                    Layout.fillWidth: true
                    property var values: ["", "identification", "artwork", "missing-file",
                                          "missing-storage", "runtime", "source-error",
                                          "unavailable", "duplicates"]
                    model: ["All reasons", "Needs identification", "Missing artwork",
                            "Game file moved or missing", "Drive or folder disconnected",
                            "Emulator or core unavailable", "Source scan failed", "Unavailable",
                            "Duplicate suggestions"]
                    currentIndex: Math.max(0, values.indexOf(root.service ? root.service.reason : ""))
                    Accessible.name: "Filter repair games by reason"
                    onActiveFocusChanged: if (activeFocus) root.reveal(reasonFilter)
                    property Item controllerLeftTarget: sourceFilter
                    property Item controllerUpTarget: closeButton
                    property Item controllerDownTarget: root.actionForReasonRow(0, 1) || previousButton
                    onActivated: root.service.reason = values[currentIndex]
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 18 * root.uiScale
                Rectangle {
                    Layout.preferredWidth: 96 * root.uiScale
                    Layout.preferredHeight: 144 * root.uiScale
                    radius: 8 * root.uiScale
                    color: Theme.darkerBackground
                    border.color: Qt.alpha(Theme.foreground, 0.14)
                    CoverArtwork {
                        anchors.fill: parent
                        anchors.margins: 3 * root.uiScale
                        source: root.game.coverPath || ""
                        visible: source.toString().length > 0
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !root.game.coverPath
                        text: "NO COVER"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.uiScale
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12 * root.uiScale
                    Text {
                        objectName: "libraryRepairGameTitle"
                        Layout.fillWidth: true
                        text: root.game.title || "Nothing left to review in these filters"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 24 * root.uiScale
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!root.game.title
                        text: (root.game.source || "Unknown source") + "  ·  "
                              + (root.game.system || "Unknown platform")
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.uiScale
                        wrapMode: Text.Wrap
                    }
                    ColumnLayout {
                        id: reasonRows
                        objectName: "libraryRepairReasonRows"
                        Layout.fillWidth: true
                        spacing: 10 * root.uiScale
                        Repeater {
                            id: reasonItems
                            model: root.game.reasonDetails || []
                            FocusScope {
                                id: reasonRow
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: reasonRowLayout.implicitHeight
                                readonly property string reasonKey: modelData.key || ""
                                readonly property bool manualSource: root.game.source === "Manual"
                                readonly property bool emulatorSource:
                                    typeof Launcher !== "undefined" && Launcher
                                    && Launcher.isEmulatorSource(root.game.source || "")
                                readonly property Item firstAction:
                                    reasonKey === "identification" ? correctIdentityButton
                                    : reasonKey === "artwork" ? chooseArtworkButton
                                    : reasonKey === "duplicates" ? linkButton
                                    : reasonKey === "missing-file" && manualSource ? editManualButton
                                    : reasonKey === "missing-file" && emulatorSource ? locateFileButton
                                    : reasonKey === "missing-file" ? recheckMissingButton
                                    : reasonKey === "missing-storage" && manualSource ? editManualButton
                                    : reasonKey === "missing-storage" && emulatorSource ? recheckButton
                                    : reasonKey === "runtime" && manualSource ? editManualButton
                                    : reasonKey === "runtime" ? launchSetupButton
                                    : reasonKey === "source-error" ? retrySourceButton : null
                                readonly property Item lastAction:
                                    reasonKey === "identification" && undoIdentityButton.visible ? undoIdentityButton
                                    : reasonKey === "artwork" && undoArtworkButton.visible ? undoArtworkButton
                                    : (reasonKey === "missing-file" || reasonKey === "missing-storage")
                                      && undoRelocationReasonButton.visible ? undoRelocationReasonButton
                                    : reasonKey === "missing-storage" && locateFileButton.visible ? locateFileButton
                                    : firstAction
                                ColumnLayout {
                                    id: reasonRowLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    spacing: 5 * root.uiScale
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3 * root.uiScale
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.label || reasonRow.reasonKey
                                            color: Theme.brightForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: UiMetrics.body * root.uiScale
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.Wrap
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.detail || ""
                                            color: Theme.mutedText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: UiMetrics.supporting * root.uiScale
                                            wrapMode: Text.WrapAnywhere
                                            visible: text.length > 0
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: implicitHeight
                                        spacing: 8 * root.uiScale
                                        RepairButton {
                                            id: correctIdentityButton
                                            objectName: visible ? "libraryRepairCorrectIdentityButton" : ""
                                            visible: reasonRow.reasonKey === "identification"
                                            text: "CORRECT IDENTITY"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title && !Metadata.busy
                                            property Item controllerRightTarget:
                                                undoIdentityButton.visible ? undoIdentityButton : null
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.right: controllerRightTarget
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("identity")
                                        }
                                        RepairButton {
                                            id: undoIdentityButton
                                            objectName: visible ? "libraryRepairUndoIdentityButton" : ""
                                            visible: reasonRow.reasonKey === "identification" && !!root.game.undoIdentity
                                            text: "UNDO IDENTITY"
                                            displayScale: root.uiScale
                                            property Item controllerLeftTarget: correctIdentityButton
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.left: correctIdentityButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.undo("identity")
                                        }
                                        RepairButton {
                                            id: chooseArtworkButton
                                            objectName: visible ? "libraryRepairChooseArtworkButton" : ""
                                            visible: reasonRow.reasonKey === "artwork"
                                            text: "CHOOSE ARTWORK"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title && !Metadata.busy
                                            property Item controllerRightTarget:
                                                undoArtworkButton.visible ? undoArtworkButton : null
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.right: controllerRightTarget
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("artwork")
                                        }
                                        RepairButton {
                                            id: undoArtworkButton
                                            objectName: visible ? "libraryRepairUndoArtworkButton" : ""
                                            visible: reasonRow.reasonKey === "artwork" && !!root.game.undoArtwork
                                            text: "UNDO ARTWORK"
                                            displayScale: root.uiScale
                                            property Item controllerLeftTarget: chooseArtworkButton
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.left: chooseArtworkButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.undo("artwork")
                                        }
                                        RepairButton {
                                            id: editManualButton
                                            objectName: visible ? "libraryRepairEditManualButton" : ""
                                            visible: reasonRow.manualSource &&
                                                     (reasonRow.reasonKey === "missing-file" ||
                                                      reasonRow.reasonKey === "missing-storage" ||
                                                      reasonRow.reasonKey === "runtime")
                                            text: "EDIT GAME"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.editManualRequested(root.game.appId || "")
                                        }
                                        RepairButton {
                                            id: recheckMissingButton
                                            objectName: visible ? "libraryRepairRecheckMissingButton" : ""
                                            visible: !reasonRow.manualSource && !reasonRow.emulatorSource &&
                                                     (reasonRow.reasonKey === "missing-file" ||
                                                      reasonRow.reasonKey === "missing-storage")
                                            text: "RECHECK"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.recheck()
                                        }
                                        RepairButton {
                                            id: locateFileButton
                                            objectName: visible ? "libraryRepairLocateFileButton" : ""
                                            visible: reasonRow.emulatorSource &&
                                                     (reasonRow.reasonKey === "missing-file" ||
                                                      reasonRow.reasonKey === "missing-storage")
                                            text: "LOCATE FILE"
                                            displayScale: root.uiScale
                                            property Item controllerRightTarget:
                                                undoRelocationReasonButton.visible ? undoRelocationReasonButton : null
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.right: controllerRightTarget
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openRelocation(root.game.metadataKey, "")
                                        }
                                        RepairButton {
                                            id: undoRelocationReasonButton
                                            objectName: visible ? "libraryRepairUndoRelocationReasonButton" : ""
                                            visible: reasonRow.emulatorSource &&
                                                     (reasonRow.reasonKey === "missing-file" ||
                                                      reasonRow.reasonKey === "missing-storage") &&
                                                     !!root.game.undoRelocation
                                            text: "UNDO RELOCATION"
                                            displayScale: root.uiScale
                                            property Item controllerLeftTarget: locateFileButton
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.left: locateFileButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.undoRelocation(root.game.metadataKey)
                                        }
                                        RepairButton {
                                            id: linkButton
                                            objectName: visible ? "libraryRepairLinkButton" : ""
                                            visible: reasonRow.reasonKey === "duplicates"
                                            text: "LAUNCH SETUP / LINK"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("")
                                        }
                                        RepairButton {
                                            id: recheckButton
                                            objectName: visible ? "libraryRepairRecheckButton" : ""
                                            visible: reasonRow.reasonKey === "missing-storage" &&
                                                     reasonRow.emulatorSource
                                            text: "RECHECK"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.recheck()
                                        }
                                        RepairButton {
                                            id: launchSetupButton
                                            objectName: visible ? "libraryRepairLaunchSetupButton" : ""
                                            visible: reasonRow.reasonKey === "runtime" && !reasonRow.manualSource
                                            text: "LAUNCH SETUP"
                                            displayScale: root.uiScale
                                            enabled: !!root.game.title
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.openGame("")
                                        }
                                        RepairButton {
                                            id: retrySourceButton
                                            objectName: visible ? "libraryRepairRetrySourceButton" : ""
                                            visible: reasonRow.reasonKey === "source-error"
                                            text: "RETRY SOURCE"
                                            displayScale: root.uiScale
                                            property Item controllerUpTarget:
                                                root.actionForReasonRow(reasonRow.index - 1, -1) || reasonFilter
                                            property Item controllerDownTarget:
                                                root.actionForReasonRow(reasonRow.index + 1, 1) || previousButton
                                            KeyNavigation.up: controllerUpTarget
                                            KeyNavigation.down: controllerDownTarget
                                            onClicked: root.service.retrySource()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!root.game.undoRelocation
                RepairButton {
                    id: undoRelocationButton
                    objectName: "libraryRepairUndoRelocationButton"
                    text: "UNDO RELOCATION"
                    displayScale: root.uiScale
                    property Item controllerUpTarget:
                        root.actionForReasonRow(reasonItems.count - 1, -1) || reasonFilter
                    property Item controllerDownTarget: previousButton
                    KeyNavigation.up: controllerUpTarget
                    KeyNavigation.down: previousButton
                    onClicked: root.service.undoRelocation(root.game.metadataKey)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                RepairButton {
                    id: previousButton
                    objectName: "libraryRepairPreviousButton"
                    text: "PREVIOUS"
                    displayScale: root.uiScale
                    enabled: !!root.game.title
                    property Item controllerRightTarget: nextButton
                    property Item controllerUpTarget:
                        undoRelocationButton.visible ? undoRelocationButton
                                                    : root.actionForReasonRow(reasonItems.count - 1, -1) || reasonFilter
                    property Item controllerDownTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    KeyNavigation.right: nextButton
                    KeyNavigation.up: controllerUpTarget
                    KeyNavigation.down: controllerDownTarget
                    onClicked: root.service.move(-1)
                }
                RepairButton {
                    id: nextButton
                    objectName: "libraryRepairNextButton"
                    text: "NEXT"
                    displayScale: root.uiScale
                    enabled: !!root.game.title
                    property Item controllerLeftTarget: previousButton
                    property Item controllerRightTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    property Item controllerUpTarget:
                        undoRelocationButton.visible ? undoRelocationButton
                                                    : root.actionForReasonRow(reasonItems.count - 1, -1) || reasonFilter
                    property Item controllerDownTarget:
                        root.metadataRetryAvailable ? retryThisGameButton : closeButton
                    KeyNavigation.left: previousButton
                    KeyNavigation.up: controllerUpTarget
                    KeyNavigation.down: controllerDownTarget
                    onClicked: root.service.move(1)
                }
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: 8 * root.uiScale
                visible: root.metadataRetryAvailable
                RepairButton {
                    id: retryThisGameButton
                    objectName: "libraryRepairRetryThisGameButton"
                    text: "RETRY THIS GAME"
                    displayScale: root.uiScale
                    enabled: !!root.game.title && (root.game.reasons || []).some(reason =>
                        reason === "identification" || reason === "artwork") && !Metadata.busy
                    property Item controllerRightTarget: selectForRetryButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.right: selectForRetryButton
                    KeyNavigation.up: nextButton
                    onClicked: root.service.retry(false)
                }
                RepairButton {
                    id: selectForRetryButton
                    objectName: "libraryRepairSelectForRetryButton"
                    text: root.game.selected ? "REMOVE FROM RETRY" : "SELECT FOR RETRY"
                    displayScale: root.uiScale
                    enabled: !!root.game.title && (root.game.reasons || []).some(reason =>
                        reason === "identification" || reason === "artwork")
                    property Item controllerLeftTarget: retryThisGameButton
                    property Item controllerRightTarget: retrySelectedButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: retryThisGameButton
                    KeyNavigation.right: retrySelectedButton
                    KeyNavigation.up: nextButton
                    onClicked: root.service.toggleSelected()
                }
                RepairButton {
                    id: retrySelectedButton
                    objectName: "libraryRepairRetrySelectedButton"
                    text: "RETRY SELECTED (" + (root.service ? root.service.selectedTitles.length : 0) + ")"
                    displayScale: root.uiScale
                    enabled: root.service && root.service.selectedTitles.length > 0 && !Metadata.busy
                    property Item controllerLeftTarget: selectForRetryButton
                    property Item controllerRightTarget: stopRetryButton.enabled ? stopRetryButton : null
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: selectForRetryButton
                    KeyNavigation.right: controllerRightTarget
                    KeyNavigation.up: nextButton
                    onClicked: root.service.retrySelected()
                }
                RepairButton {
                    id: stopRetryButton
                    objectName: "libraryRepairStopRetryButton"
                    text: "STOP RETRY"
                    displayScale: root.uiScale
                    enabled: Metadata.busy
                    property Item controllerLeftTarget: retrySelectedButton
                    property Item controllerUpTarget: nextButton
                    KeyNavigation.left: retrySelectedButton
                    KeyNavigation.up: nextButton
                    onClicked: Metadata.cancel()
                }
            }
            Text {
                objectName: "libraryRepairSelectionStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12 * root.uiScale
                color: Theme.mutedText
                visible: root.service && root.service.selectedTitles.length > 0
                text: visible ? "Selected for retry: " + root.service.selectedTitles.join(", ") : ""
            }
            Text {
                objectName: "libraryRepairMetadataStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: UiMetrics.body * root.uiScale
                color: Theme.foreground
                text: root.service && root.service.message ? root.service.message
                      : Metadata.status + (Metadata.busy ? " • " + Metadata.pending + " pending" : "")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }
    FileDialog {
        id: relocationFileDialog
        title: "Locate the game file"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Game files (*)"]
        onAccepted: {
            root.relocationPath = selectedFile.toLocalFile()
            root.refreshRelocationPreview()
        }
    }
    Rectangle {
        id: relocationOverlay
        objectName: "libraryRepairRelocationPreview"
        anchors.fill: parent
        visible: root.relocationOpen
        z: 100
        color: Theme.background
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent
            width: Math.max(1, Math.min(parent.width - 48, 840 * root.uiScale))
            height: Math.max(1, Math.min(parent.height - 48, relocationLayout.implicitHeight + 48 * root.uiScale))
            radius: 12 * root.uiScale
            color: Theme.background
            border.color: Theme.mutedText
            ColumnLayout {
                id: relocationLayout
                anchors.fill: parent
                anchors.margins: 24 * root.uiScale
                spacing: 14 * root.uiScale
                Text {
                    Layout.fillWidth: true
                    text: "LOCATE GAME FILE"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 22 * root.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    objectName: "libraryRepairRelocationPaths"
                    Layout.fillWidth: true
                    text: "Current file: " + (root.relocationPreview.oldPath || "Unknown")
                          + "\nNew file: " + (root.relocationPath || "Choose a file")
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: UiMetrics.supporting * root.uiScale
                    wrapMode: Text.WrapAnywhere
                }
                RowLayout {
                    Layout.fillWidth: true
                    ThemedTextField {
                        id: relocationPathField
                        objectName: "libraryRepairRelocationPath"
                        Layout.fillWidth: true
                        text: root.relocationPath
                        placeholderText: "Absolute path to the new game file"
                        Accessible.name: placeholderText
                        property bool controllerNavigation: true
                        property Item controllerRightTarget:
                            relocationBrowseButton.visible ? relocationBrowseButton : relocationTextEntryButton
                        property Item controllerDownTarget: relocationCancelButton
                        KeyNavigation.right: controllerRightTarget
                        KeyNavigation.down: relocationCancelButton

                        function acceptInput(event) {
                            if (TextEntry.keyboardNeeded) {
                                root.textEntryRequested(relocationPathField, "NEW GAME PATH", false,
                                                        placeholderText)
                                event.accepted = true
                            }
                        }
                        function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                        Keys.onReturnPressed: event => acceptInput(event)
                        Keys.onEnterPressed: function(event) {
                            if (TextEntry.keyboardNeeded) {
                                root.textEntryRequested(relocationPathField, "NEW GAME PATH", false,
                                                        placeholderText)
                                event.accepted = true
                            }
                        }
                        onTextChanged: {
                            if (text !== root.relocationPath) root.relocationPath = text
                            relocationPreviewTimer.restart()
                        }
                    }
                    RepairButton {
                        id: relocationBrowseButton
                        objectName: "libraryRepairRelocationBrowseButton"
                        visible: !root.couchMode
                        text: "BROWSE"
                        displayScale: root.uiScale
                        property Item controllerLeftTarget: relocationPathField
                        property Item controllerRightTarget:
                            relocationTextEntryButton.visible ? relocationTextEntryButton : null
                        property Item controllerDownTarget: relocationCancelButton
                        KeyNavigation.left: relocationPathField
                        KeyNavigation.right: controllerRightTarget
                        KeyNavigation.down: relocationCancelButton
                        onClicked: relocationFileDialog.open()
                    }
                    RepairButton {
                        id: relocationTextEntryButton
                        objectName: "libraryRepairRelocationTextEntryButton"
                        visible: root.couchMode || TextEntry.keyboardNeeded
                        text: "ENTER PATH"
                        displayScale: root.uiScale
                        property Item controllerLeftTarget:
                            relocationBrowseButton.visible ? relocationBrowseButton : relocationPathField
                        property Item controllerDownTarget: relocationCancelButton
                        KeyNavigation.left: controllerLeftTarget
                        KeyNavigation.down: relocationCancelButton
                        onClicked: root.textEntryRequested(relocationPathField, "NEW GAME PATH", false,
                                                           relocationPathField.placeholderText)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "Identity, organization, and play history follow this game. Game files stay where they are. Eligible save backups are copied, with originals kept. Undo restores the previous launch setup and keeps both backup copies."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * root.uiScale
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: root.relocationPreview.backupMessages || []
                    Text {
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.uiScale
                        wrapMode: Text.Wrap
                    }
                }
                Text {
                    objectName: "libraryRepairRelocationRefusal"
                    Layout.fillWidth: true
                    visible: !!root.relocationPreview.refusal
                    text: root.relocationPreview.refusal || ""
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * root.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    RepairButton {
                        id: relocationCancelButton
                        objectName: "libraryRepairRelocationCancelButton"
                        text: "CANCEL"
                        displayScale: root.uiScale
                        property Item controllerRightTarget: relocationConfirmButton
                        property Item controllerUpTarget: relocationPathField
                        KeyNavigation.right: relocationConfirmButton
                        KeyNavigation.up: relocationPathField
                        onClicked: root.closeRelocation()
                    }
                    RepairButton {
                        id: relocationConfirmButton
                        objectName: "libraryRepairRelocationConfirmButton"
                        text: "CONFIRM"
                        displayScale: root.uiScale
                        enabled: !!root.relocationPreview.ok
                        property Item controllerLeftTarget: relocationCancelButton
                        property Item controllerUpTarget: relocationPathField
                        KeyNavigation.left: relocationCancelButton
                        KeyNavigation.up: relocationPathField
                        onClicked: root.confirmRelocation()
                    }
                }
            }
            Timer {
                id: relocationPreviewTimer
                interval: 250
                repeat: false
                onTriggered: root.refreshRelocationPreview()
            }
        }
    }
}

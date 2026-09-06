import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

    Rectangle {
        id: settingsOverlay
        objectName: "settingsOverlay"
        function reveal(item) { if (host.isWithin(item, settingsScroll)) host.revealInScrollView(settingsScroll, item) }
        // Every connection row reports the same three states from the service that owns it.
        // Credentials are stored in the keyring, so "connected" means Omakade holds what the
        // provider needs, and a provider that answered with a problem says so instead.
        readonly property var connectionProblems: ["invalid-key", "private", "rate-limited",
                                                   "unsupported", "error"]
        function connectionLabel(name, ready, state) {
            if (!ready) return name + " · NOT CONNECTED"
            if (state && settingsOverlay.connectionProblems.indexOf(state) >= 0)
                return name + " · CHECK SETTINGS"
            return name + " · CONNECTED"
        }
        // Main.qml owns the GOG folder actions, but the field lives here.
        function focusGogFolderField() {
            settingsOverlay.section = 0
            settingsOverlay.sourceDetail = ""
            gogLibraryPathField.forceActiveFocus()
            settingsOverlay.reveal(gogLibraryPathField)
        }
        required property var host
        property int libraryCount: 0
        property int section: 0
        property int connection: -1
        property bool availableSources: false
        property string sourceSearch: ""
        property string sourceDetail: ""
        readonly property var sections: ["Sources", "Library", "Connections", "Controls & streaming", "About & storage"]
        function pageChanged() {
            Qt.callLater(function() {
                settingsScroll.contentItem.contentY = 0
                host.focusWithin(settingsScroll, true)
            })
        }
        function back() {
            if (section === 0 && sourceDetail) { sourceDetail = ""; pageChanged(); return }
            if (section === 2 && connection >= 0) { connection = -1; pageChanged(); return }
            host.diagnosticsOpen = false
        }
        onSectionChanged: pageChanged()
        property var previousFocus: null
        anchors.fill: parent
        visible: host.diagnosticsOpen
        z: 20
        Keys.onPressed: function(event) { host.handleArrowKey(settingsOverlay, event) }
        color: host.alpha(Theme.darkerBackground, 0.72)
        onVisibleChanged: {
            if (visible) {
                previousFocus = host.activeFocusItem
                Qt.callLater(function() { host.focusWithin(settingsOverlay, true) })
            } else if (previousFocus) {
                host.restoreFocus(previousFocus)
                previousFocus = null
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: host.diagnosticsOpen = false
        }

        Rectangle {
            id: settingsPanel
            anchors.centerIn: parent
            readonly property real layoutScale: host.couchMode
                                                    ? Math.max(1, Math.min(2,
                                                                          host.height / 1080))
                                                    : 1
            readonly property real uiScale: host.couchMode ? 1.25 * layoutScale : 1
            width: Math.min(host.couchMode ? 1280 * layoutScale : 1120,
                            parent.width - (host.couchMode ? 96 : 48))
            height: Math.min(host.couchMode ? 900 * layoutScale : 760,
                             parent.height - (host.couchMode ? 72 : 48))
            radius: Math.max(host.couchMode ? 14 * layoutScale : 8, Theme.cornerRadius)
            color: host.alpha(Theme.background, 0.98)
            border.color: host.alpha(Theme.foreground, 0.2)

            MouseArea { anchors.fill: parent }

            RowLayout {
                id: settingsHeader
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 24
                Text { Layout.fillWidth: true; text: "SETTINGS"; color: Theme.brightForeground; font.family: Theme.fontFamily; font.pixelSize: 20 * settingsPanel.uiScale }
                GlassButton { id: closeSettings; objectName: "closeSettings"; compact: true; text: "CLOSE"; onClicked: host.diagnosticsOpen = false }
            }
            ColumnLayout {
                id: sectionNavigation
                visible: settingsPanel.width >= 850
                anchors.left: parent.left; anchors.top: settingsHeader.bottom; anchors.margins: 24
                width: 190 * settingsPanel.layoutScale
                spacing: 8
                Repeater {
                    model: settingsOverlay.sections
                    GlassButton {
                        required property int index
                        required property string modelData
                        objectName: "settingsSection" + index
                        Layout.fillWidth: true; compact: true
                        text: modelData.toUpperCase(); selected: settingsOverlay.section === index
                        onClicked: settingsOverlay.section = index
                        property Item controllerRightTarget: null
                        Keys.onRightPressed: event => { host.focusWithin(settingsScroll, true); event.accepted = true }
                        property Item controllerUpTarget: index === 0 ? closeSettings : null
                    }
                }
            }
            Flow {
                id: compactSections
                visible: !sectionNavigation.visible
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: settingsHeader.bottom; anchors.margins: 20
                spacing: 6
                Repeater {
                    model: settingsOverlay.sections
                    GlassButton {
                        required property int index
                        required property string modelData
                        compact: true; text: modelData.toUpperCase(); selected: settingsOverlay.section === index
                        onClicked: settingsOverlay.section = index
                        Accessible.name: modelData + " settings"
                    }
                }
            }
            ScrollView {
                id: settingsScroll
                objectName: "settingsScroll"
                readonly property real navigationContentY: contentItem ? contentItem.contentY : 0
                anchors.left: sectionNavigation.visible ? sectionNavigation.right : parent.left
                anchors.right: parent.right
                anchors.top: sectionNavigation.visible ? settingsHeader.bottom : compactSections.bottom
                anchors.bottom: parent.bottom
                anchors.margins: host.couchMode ? 42 * settingsPanel.layoutScale : 28
                anchors.bottomMargin: host.couchMode ? 70 * settingsPanel.layoutScale : 28
                rightPadding: 18
                contentWidth: availableWidth

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: settingsOverlay.section === 0
                RowLayout {
                    Layout.fillWidth: true
                    GlassButton { id: sourceBack; visible: settingsOverlay.sourceDetail !== ""; text: "BACK TO SOURCES"; compact: true; onClicked: { settingsOverlay.sourceDetail = ""; settingsOverlay.pageChanged() } }
                    GlassButton { visible: settingsOverlay.sourceDetail === ""; text: "IN USE"; selected: !settingsOverlay.availableSources; compact: true; onClicked: settingsOverlay.availableSources = false }
                    GlassButton { visible: settingsOverlay.sourceDetail === ""; text: "AVAILABLE"; selected: settingsOverlay.availableSources; compact: true; onClicked: settingsOverlay.availableSources = true }
                }
                GlassButton {
                    Layout.fillWidth: true; compact: true
                    visible: settingsOverlay.sourceDetail === "" && !DemoMode
                    text: "RESCAN ENABLED SOURCES"
                    onClicked: host.rescanLibraries()
                }
                TextField {
                    id: sourceSearchField
                    Layout.fillWidth: true; visible: settingsOverlay.sourceDetail === ""
                    placeholderText: "Search all sources"; Accessible.name: "Search sources"
                    color: Theme.foreground; font.family: Theme.fontFamily
                    onTextChanged: settingsOverlay.sourceSearch = text
                    placeholderTextColor: Theme.mutedText
                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                        border.width: sourceSearchField.activeFocus ? 2 : 1
                        border.color: sourceSearchField.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                    }
                    property bool controllerNavigation: host.couchMode
                    Keys.onReturnPressed: event => host.handleCouchTextEntry(event, sourceSearchField, "SEARCH SOURCES", false, placeholderText)
                }
                Repeater {
                    model: [
                        { name: "STEAM", enabled: Preferences.steamEnabled,
                          status: SteamLibrary ? SteamLibrary.statusText : "Unavailable",
                          error: SteamLibrary ? SteamLibrary.errorText : "",
                          paths: SteamLibrary ? SteamLibrary.detectedPaths : [],
                          lastScan: SteamLibrary ? SteamLibrary.lastScan : 0 },
                        { name: "BATTLE.NET", enabled: Preferences.battleNetEnabled,
                          status: BattleNetLibrary ? BattleNetLibrary.statusText : "Unavailable",
                          error: BattleNetLibrary ? BattleNetLibrary.errorText : "",
                          paths: BattleNetLibrary ? BattleNetLibrary.detectedPaths : [],
                          lastScan: BattleNetLibrary ? BattleNetLibrary.lastScan : 0 },
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
                        { name: "GOG", enabled: Preferences.gogEnabled,
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
                          lastScan: RetroArchLibrary ? RetroArchLibrary.lastScan : 0 },
                        { name: "PCSX2", enabled: Preferences.pcsx2Enabled,
                          status: Pcsx2Library ? Pcsx2Library.statusText : "Unavailable",
                          error: Pcsx2Library ? Pcsx2Library.errorText : "",
                          paths: Pcsx2Library ? Pcsx2Library.detectedPaths : [],
                          lastScan: Pcsx2Library ? Pcsx2Library.lastScan : 0 },
                        { name: "RYUJINX", enabled: Preferences.ryujinxEnabled,
                          status: RyujinxLibrary ? RyujinxLibrary.statusText : "Unavailable",
                          error: RyujinxLibrary ? RyujinxLibrary.errorText : "",
                          paths: RyujinxLibrary ? RyujinxLibrary.detectedPaths : [],
                          lastScan: RyujinxLibrary ? RyujinxLibrary.lastScan : 0 },
                        { name: "SHADPS4", enabled: Preferences.shadps4Enabled,
                          status: Shadps4Library ? Shadps4Library.statusText : "Unavailable",
                          error: Shadps4Library ? Shadps4Library.errorText : "",
                          paths: Shadps4Library ? Shadps4Library.detectedPaths : [],
                          lastScan: Shadps4Library ? Shadps4Library.lastScan : 0 },
                        { name: "CEMU", enabled: Preferences.cemuEnabled,
                          status: CemuLibrary ? CemuLibrary.statusText : "Unavailable",
                          error: CemuLibrary ? CemuLibrary.errorText : "",
                          paths: CemuLibrary ? CemuLibrary.detectedPaths : [],
                          lastScan: CemuLibrary ? CemuLibrary.lastScan : 0 },
                        { name: "DOLPHIN", enabled: Preferences.dolphinEnabled,
                          status: DolphinLibrary ? DolphinLibrary.statusText : "Unavailable",
                          error: DolphinLibrary ? DolphinLibrary.errorText : "",
                          paths: DolphinLibrary ? DolphinLibrary.detectedPaths : [],
                          lastScan: DolphinLibrary ? DolphinLibrary.lastScan : 0 }
                    ]
                    ColumnLayout {
                        required property var modelData
                        enabled: !DemoMode
                        readonly property bool detail: settingsOverlay.sourceDetail === modelData.name
                        visible: settingsOverlay.sourceDetail ? detail
                                 : (settingsOverlay.sourceSearch === "" ? modelData.enabled !== settingsOverlay.availableSources : true)
                                   && modelData.name.toLowerCase().includes(settingsOverlay.sourceSearch.toLowerCase())
                        Layout.fillWidth: true
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                Layout.preferredWidth: 130
                                color: modelData.enabled ? Theme.accent : Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 11 * settingsPanel.uiScale
                                font.weight: Font.Bold
                            }
                            GlassButton {
                                compact: true
                                text: modelData.enabled ? "ON" : "OFF"
                                selected: modelData.enabled
                                onClicked: {
                                    let nowEnabled = false
                                    if (modelData.name === "STEAM") {
                                        Preferences.steamEnabled = !Preferences.steamEnabled
                                        nowEnabled = Preferences.steamEnabled
                                        if (Preferences.steamEnabled) SteamLibrary.refresh()
                                    } else if (modelData.name === "BATTLE.NET") {
                                        Preferences.battleNetEnabled = !Preferences.battleNetEnabled
                                        nowEnabled = Preferences.battleNetEnabled
                                        if (Preferences.battleNetEnabled && BattleNetLibrary) BattleNetLibrary.refresh()
                                    } else if (modelData.name === "LUTRIS") {
                                        Preferences.lutrisEnabled = !Preferences.lutrisEnabled
                                        nowEnabled = Preferences.lutrisEnabled
                                        if (Preferences.lutrisEnabled) LutrisLibrary.refresh()
                                    } else if (modelData.name === "HEROIC") {
                                        Preferences.heroicEnabled = !Preferences.heroicEnabled
                                        nowEnabled = Preferences.heroicEnabled
                                        if (Preferences.heroicEnabled) HeroicLibrary.refresh()
                                    } else if (modelData.name === "GOG") {
                                        Preferences.gogEnabled = !Preferences.gogEnabled
                                        nowEnabled = Preferences.gogEnabled
                                        if (Preferences.gogEnabled) HeroicLibrary.refresh()
                                    } else if (modelData.name === "FAUGUS") {
                                        Preferences.faugusEnabled = !Preferences.faugusEnabled
                                        nowEnabled = Preferences.faugusEnabled
                                        if (Preferences.faugusEnabled) FaugusLibrary.refresh()
                                    } else if (modelData.name === "PCSX2") {
                                        Preferences.pcsx2Enabled = !Preferences.pcsx2Enabled
                                        nowEnabled = Preferences.pcsx2Enabled
                                        if (Preferences.pcsx2Enabled) Pcsx2Library.refresh()
                                    } else if (modelData.name === "RYUJINX") {
                                        Preferences.ryujinxEnabled = !Preferences.ryujinxEnabled
                                        nowEnabled = Preferences.ryujinxEnabled
                                        if (Preferences.ryujinxEnabled) RyujinxLibrary.refresh()
                                    } else if (modelData.name === "SHADPS4") {
                                        Preferences.shadps4Enabled = !Preferences.shadps4Enabled
                                        nowEnabled = Preferences.shadps4Enabled
                                        if (Preferences.shadps4Enabled) Shadps4Library.refresh()
                                    } else if (modelData.name === "CEMU") {
                                        Preferences.cemuEnabled = !Preferences.cemuEnabled
                                        nowEnabled = Preferences.cemuEnabled
                                        if (Preferences.cemuEnabled) CemuLibrary.refresh()
                                    } else if (modelData.name === "DOLPHIN") {
                                        Preferences.dolphinEnabled = !Preferences.dolphinEnabled
                                        nowEnabled = Preferences.dolphinEnabled
                                        if (Preferences.dolphinEnabled) DolphinLibrary.refresh()
                                    } else {
                                        Preferences.retroArchEnabled = !Preferences.retroArchEnabled
                                        nowEnabled = Preferences.retroArchEnabled
                                        if (Preferences.retroArchEnabled) RetroArchLibrary.refresh()
                                    }
                                    if (!nowEnabled && Library.sourceSelected(modelData.name)) {
                                        Library.toggleSource(modelData.name)
                                    }
                                }
                            }
                        GlassButton {
                            visible: !detail
                            compact: true
                            text: "DETAILS"
                            Accessible.name: modelData.name + " details"
                            onClicked: { settingsOverlay.sourceDetail = modelData.name; settingsOverlay.pageChanged() }
                        }
                            GlassButton {
                                compact: true
                                text: "RESCAN SOURCE"
                                visible: detail
                                enabled: modelData.enabled
                                onClicked: {
                                    if (modelData.name === "STEAM") SteamLibrary.refresh()
                                    else if (modelData.name === "BATTLE.NET" && BattleNetLibrary) BattleNetLibrary.refresh()
                                    else if (modelData.name === "LUTRIS") LutrisLibrary.refresh()
                                    else if (modelData.name === "HEROIC") HeroicLibrary.refresh()
                                    else if (modelData.name === "GOG") HeroicLibrary.refresh()
                                    else if (modelData.name === "FAUGUS") FaugusLibrary.refresh()
                                    else if (modelData.name === "PCSX2") Pcsx2Library.refresh()
                                    else if (modelData.name === "RYUJINX") RyujinxLibrary.refresh()
                                    else if (modelData.name === "SHADPS4") Shadps4Library.refresh()
                                    else if (modelData.name === "CEMU") CemuLibrary.refresh()
                                    else if (modelData.name === "DOLPHIN") DolphinLibrary.refresh()
                                    else RetroArchLibrary.refresh()
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.status + (detail ? " · " + host.scanTime(modelData.lastScan) : "")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 10 * settingsPanel.uiScale
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: detail && modelData.paths.length > 0
                            text: modelData.paths.join("\n")
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9 * settingsPanel.uiScale
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: detail && modelData.error.length > 0
                            text: modelData.error
                            color: Theme.yellow
                            font.family: Theme.fontFamily
                            font.pixelSize: 9 * settingsPanel.uiScale
                            wrapMode: Text.Wrap
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !DemoMode && settingsOverlay.sourceDetail === ""
                    spacing: 8
                    Text {
                        text: "GAMES YOU ADD YOURSELF"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Add a native game or a desktop entry that no launcher reports. Removing one from Omakade never deletes its files."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton {
                            objectName: "addManualGameButton"
                            compact: true
                            text: "ADD A GAME"
                            onClicked: host.editManualGame("")
                        }
                        GlassButton {
                            compact: true
                            visible: ManualLibrary.count > 0
                            text: "MANUAL GAMES · " + ManualLibrary.count
                            onClicked: {
                                Library.sourceFilters = ["Manual"]
                                host.diagnosticsOpen = false
                                host.focusLibrary()
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !DemoMode
                    spacing: 8
                    Text {
                        text: "ROM FOLDERS"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Scan a folder of dumps without a RetroArch playlist. EmuDeck folders under ~/Emulation/roms are detected automatically. Switch, Wii U, PS2, and PS4 stay with their dedicated emulators."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9 * settingsPanel.uiScale
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: Preferences.romFolders
                        RowLayout {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: {
                                    const split = modelData.lastIndexOf("|")
                                    const path = split >= 0 ? modelData.slice(0, split) : modelData
                                    const system = split >= 0 ? modelData.slice(split + 1) : ""
                                    return (system ? system.toUpperCase() + " · " : "") + path
                                }
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: 10 * settingsPanel.uiScale
                                elide: Text.ElideMiddle
                            }
                            GlassButton {
                                compact: true
                                text: "REMOVE"
                                onClicked: Preferences.removeRomFolderAt(index)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        GlassButton {
                            compact: true
                            text: host.romFolderSystems[host.romFolderSystemIndex].name.toUpperCase()
                            onClicked: host.romFolderSystemIndex =
                                       (host.romFolderSystemIndex + 1) % host.romFolderSystems.length
                        }
                        GlassButton {
                            compact: true
                            text: "ADD FOLDER"
                            onClicked: host.chooseRomFolder()
                        }
                    }
                }
                ColumnLayout {
                    objectName: "gogFoldersSection"
                    Layout.fillWidth: true
                    visible: (!DemoMode || GogSettingsFixture) && settingsOverlay.sourceDetail === ""
                    spacing: 8
                    Text {
                        text: "EXTRA GOG FOLDERS"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Standard folders are discovered automatically. Add a folder containing GOG installations. Removing it here never deletes game files."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: Preferences.gogLibraryPaths
                        ColumnLayout {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11 * settingsPanel.uiScale
                                    wrapMode: Text.WrapAnywhere
                                }
                                GlassButton {
                                    objectName: "gogRemoveFolder_" + index
                                    compact: true
                                    text: "REMOVE"
                                    Accessible.name: "Remove GOG folder " + modelData
                                    onClicked: host.removeGogLibraryFolder(modelData)
                                }
                            }
                            Text {
                                objectName: "gogFolderStatus_" + index
                                Layout.fillWidth: true
                                readonly property string scanState: HeroicLibrary
                                    ? HeroicLibrary.statusText + HeroicLibrary.errorText : ""
                                text: { scanState; return Preferences.gogLibraryPathStatus(modelData) }
                                color: Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 10 * settingsPanel.uiScale
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    TextField {
                        id: gogLibraryPathField
                        objectName: "gogLibraryPathField"
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        placeholderText: "/path/to/GOG games"
                        Accessible.name: "GOG library folder path"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        font.pixelSize: 13 * settingsPanel.uiScale
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, gogLibraryPathField, "GOG FOLDER", false,
                                                      gogLibraryPathField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, gogLibraryPathField, "GOG FOLDER", false,
                                                      gogLibraryPathField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: gogLibraryPathField.activeFocus ? 2 : 1
                            border.color: gogLibraryPathField.activeFocus ? Theme.accent : host.alpha(Theme.foreground, 0.15)
                        }
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton {
                            objectName: "gogAddFolderButton"
                            compact: true
                            text: "ADD FOLDER"
                            onClicked: {
                                if (Preferences.addGogLibraryPath(gogLibraryPathField.text)) {
                                    gogLibraryPathField.clear()
                                    host.showToast("GOG folder saved")
                                } else {
                                    host.showToast("Enter an absolute folder path. Up to 64 extra folders can be saved.")
                                }
                            }
                        }
                        GlassButton {
                            compact: true
                            visible: !host.couchMode
                            text: "BROWSE"
                            onClicked: host.openGogFolderDialog()
                        }
                    }
                }
                Text {
                    visible: DemoMode
                    text: "Demo library"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 12 * settingsPanel.uiScale
                }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: settingsOverlay.section === 1
                CoverSizeControl { Layout.fillWidth: true; uiScale: settingsPanel.uiScale }
                CoverSizeControl { Layout.fillWidth: true; couch: true; uiScale: settingsPanel.uiScale }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "CONSOLE VIEW"; color: Theme.foreground; font.family: Theme.fontFamily; Layout.fillWidth: true }
                    GlassButton { compact: true; text: Preferences.expandConsoles ? "GAMES" : "CONSOLES"; onClicked: Preferences.expandConsoles = !Preferences.expandConsoles }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: true
                    spacing: 6
                    Text {
                        text: "CONSOLE LAYOUT"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * settingsPanel.uiScale
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Every console follows the library view unless you choose an override below. Pinned games can still appear beside their console card."
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9 * settingsPanel.uiScale
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: Preferences.consoleSystems
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: 10 * settingsPanel.uiScale
                                elide: Text.ElideRight
                            }
                            GlassButton {
                                compact: true
                                text: modelData.layout === "card" ? "ALWAYS CONSOLE" : modelData.layout === "library" ? "ALWAYS GAMES" : "FOLLOW VIEW"
                                selected: modelData.layout === "card"
                                onClicked: Preferences.setConsoleLayout(modelData.id,
                                                                         modelData.layout === "follow" ? "card" : modelData.layout === "card" ? "library" : "follow")
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: !DemoMode
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "STANDALONE EMULATORS"
                            color: Preferences.preferStandaloneEmulators ? Theme.accent : Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * settingsPanel.uiScale
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "When a ROM has no RetroArch core, prefer Snes9x, Nestopia, and other standalone emulators over a RetroArch fallback."
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 9 * settingsPanel.uiScale
                            wrapMode: Text.Wrap
                        }
                    }
                    GlassButton {
                        compact: true
                        text: Preferences.preferStandaloneEmulators ? "PREFERRED" : "RETROARCH FIRST"
                        selected: Preferences.preferStandaloneEmulators
                        onClicked: Preferences.preferStandaloneEmulators = !Preferences.preferStandaloneEmulators
                    }
                }
                Text {
                    text: "LIBRARY COLLECTIONS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: Library.collectionNames.length === 0
                    text: "Create collections from a game's details."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
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
                            font.pixelSize: 11 * settingsPanel.uiScale
                            elide: Text.ElideRight
                        }
                        GlassButton {
                            compact: true
                            text: "DELETE"
                            onClicked: {
                                host.pendingCollectionDelete = modelData
                                host.collectionDeleteOpen = true
                            }
                        }
                    }
                }
                Flow { Layout.fillWidth: true; spacing: 8
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: Preferences.reducedMotion ? "MOTION OFF" : "MOTION ON"
                        selected: Preferences.reducedMotion
                        onClicked: Preferences.reducedMotion = !Preferences.reducedMotion
                    }                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "AUTO-CLOSE: " + (Preferences.closeAfterLaunch ? "ON" : "OFF")
                        selected: Preferences.closeAfterLaunch
                        onClicked: Preferences.closeAfterLaunch = !Preferences.closeAfterLaunch
                    }                }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: settingsOverlay.section === 2
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: "Optional connections add ratings, portraits, and achievements. Local games work without them."
                    color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * settingsPanel.uiScale
                }
                GlassButton {
                    Layout.fillWidth: true; compact: true
                    text: Metadata && Metadata.busy ? "UPDATING · " + Metadata.pending + " REMAINING" : "UPDATE RATINGS & PORTRAITS"
                    enabled: Metadata && !Metadata.busy
                    onClicked: Metadata.refreshLibrary()
                }
                GlassButton { compact: true; text: "STOP UPDATE"; visible: Metadata && Metadata.busy; onClicked: Metadata.cancel() }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Metadata ? Metadata.status : ""; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 10 * settingsPanel.uiScale }
                GlassButton { Layout.fillWidth: true; compact: true; text: settingsOverlay.connectionLabel("STEAMGRIDDB PORTRAIT COVERS", Metadata && Metadata.hasGridKey, ""); selected: settingsOverlay.connection === 3; onClicked: { settingsOverlay.connection = settingsOverlay.connection === 3 ? -1 : 3; settingsOverlay.pageChanged() } }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 10; visible: settingsOverlay.connection === 3
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Add a SteamGridDB API key for portrait covers. You can choose a different portrait in each game's details."; color: Theme.mutedText; font.family: Theme.fontFamily; font.pixelSize: 11 * settingsPanel.uiScale }
                    TextField {
                        id: gridKeyField; Layout.fillWidth: true
                        placeholderText: Metadata && Metadata.hasGridKey ? "API key saved securely" : "SteamGridDB API key"
                        echoMode: TextInput.Password; color: Theme.foreground; font.family: Theme.fontFamily
                        Accessible.name: "SteamGridDB API key"
                        placeholderTextColor: Theme.mutedText
                    background: Rectangle {
                        radius: Math.max(5, Theme.cornerRadius)
                        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.045)
                        border.width: gridKeyField.activeFocus ? 2 : 1
                        border.color: gridKeyField.activeFocus ? Theme.accent : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.12)
                    }
                    property bool controllerNavigation: host.couchMode
                        Keys.onReturnPressed: event => host.handleCouchTextEntry(event, gridKeyField, "STEAMGRIDDB KEY", true, placeholderText)
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        GlassButton { compact: true; text: "SAVE KEY"; enabled: Metadata && !Metadata.busy && gridKeyField.text.length > 0; onClicked: { Metadata.storeGridKey(gridKeyField.text); gridKeyField.clear() } }
                        GlassButton { compact: true; text: "DISCONNECT"; enabled: Metadata && Metadata.hasGridKey && !Metadata.busy; onClicked: Metadata.removeGridKey() }
                        GlassButton { compact: true; text: "TEST CONNECTION"; enabled: Metadata && Metadata.hasGridKey && !Metadata.busy; onClicked: Metadata.testGridConnection() }
                        GlassButton { compact: true; text: "GET AN API KEY"; onClicked: Qt.openUrlExternally("https://www.steamgriddb.com/profile/preferences/api") }
                    }
                }
                GlassButton { Layout.fillWidth: true; compact: true; text: settingsOverlay.connectionLabel("STEAM LIBRARY INFORMATION", SteamAccount && SteamAccount.hasApiKey && SteamAccount.steamId.length > 0, SteamAccount ? SteamAccount.state : ""); selected: settingsOverlay.connection === 0; onClicked: { settingsOverlay.connection = settingsOverlay.connection === 0 ? -1 : 0; settingsOverlay.pageChanged() } }
                ColumnLayout { Layout.fillWidth: true; spacing: 12; visible: settingsOverlay.connection === 0
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
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: SteamAccount !== null
                    TextField {
                        id: steamIdField
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        placeholderText: "Steam ID (17 digits, starts with 7656119)"
                        Accessible.name: "Steam ID"
                        // Copy the saved value in instead of binding so a keyring lookup
                        // finishing mid-edit cannot overwrite what is being typed.
                        readonly property string savedText: SteamAccount ? SteamAccount.steamId : ""
                        onSavedTextChanged: if (!activeFocus) text = savedText
                        Component.onCompleted: text = savedText
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        inputMethodHints: Qt.ImhDigitsOnly
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, steamIdField, "STEAM ID", false,
                                                      steamIdField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, steamIdField, "STEAM ID", false,
                                                      steamIdField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: steamIdField.activeFocus ? 2 : 1
                            border.color: steamIdField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
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
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        Accessible.name: "Steam Web API key"
                        placeholderText: SteamAccount && SteamAccount.hasApiKey
                                         ? "API key stored securely" : "Steam Web API key"
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, apiKeyField, "STEAM WEB API KEY", true,
                                                      apiKeyField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, apiKeyField, "STEAM WEB API KEY", true,
                                                      apiKeyField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: apiKeyField.activeFocus ? 2 : 1
                            border.color: apiKeyField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
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
                RowLayout {
                    Layout.fillWidth: true
                    GlassButton {
                        compact: true
                        enabled: SteamAccount !== null && !SteamAccount.busy
                                 && SteamAccount.hasApiKey
                                 && SteamAccount.steamId.length > 0
                        text: SteamAccount && SteamAccount.busy
                              ? "SYNCING STEAM LIBRARY" : "SYNC OWNED STEAM LIBRARY"
                        onClicked: SteamAccount.refreshOwnedGames()
                    }
                    Text {
                        visible: SteamAccount && SteamAccount.ownedGameCount > 0
                        text: SteamAccount
                              ? SteamAccount.ownedGameCount + " OWNED GAMES CACHED" : ""
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: 9 * settingsPanel.uiScale
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    text: "OWNED LIBRARY SYNC REQUIRES PUBLIC STEAM GAME DETAILS"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 8 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                }
                GlassButton { Layout.fillWidth: true; compact: true; text: settingsOverlay.connectionLabel("RETROACHIEVEMENTS", RetroAchievements && RetroAchievements.hasApiKey && RetroAchievements.username.length > 0, RetroAchievements ? RetroAchievements.state : ""); selected: settingsOverlay.connection === 1; onClicked: { settingsOverlay.connection = settingsOverlay.connection === 1 ? -1 : 1; settingsOverlay.pageChanged() } }
                ColumnLayout { Layout.fillWidth: true; spacing: 12; visible: settingsOverlay.connection === 1
                Text {
                    Layout.fillWidth: true
                    text: RetroAchievements
                          ? RetroAchievements.statusText
                          : "RetroAchievements is unavailable in demo mode."
                    color: RetroAchievements && (RetroAchievements.state === "invalid-key"
                                                 || RetroAchievements.state === "unsupported"
                                                 || RetroAchievements.state === "rate-limited")
                           ? Theme.yellow : Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: RetroAchievements !== null && !RetroAchievements.busy
                    TextField {
                        id: retroAchievementsUsernameField
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        placeholderText: "RetroAchievements username"
                        text: RetroAchievements ? RetroAchievements.username : ""
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, retroAchievementsUsernameField,
                                                      "RETROACHIEVEMENTS USERNAME", false,
                                                      retroAchievementsUsernameField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, retroAchievementsUsernameField,
                                                      "RETROACHIEVEMENTS USERNAME", false,
                                                      retroAchievementsUsernameField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: retroAchievementsUsernameField.activeFocus ? 2 : 1
                            border.color: retroAchievementsUsernameField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE USERNAME"
                        onClicked: RetroAchievements.setUsername(retroAchievementsUsernameField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: RetroAchievements !== null && !RetroAchievements.busy
                    TextField {
                        id: retroAchievementsKeyField
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        placeholderText: RetroAchievements && RetroAchievements.hasApiKey
                                         ? "API key stored securely" : "RetroAchievements Web API key"
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, retroAchievementsKeyField,
                                                      "RETROACHIEVEMENTS API KEY", true,
                                                      retroAchievementsKeyField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, retroAchievementsKeyField,
                                                      "RETROACHIEVEMENTS API KEY", true,
                                                      retroAchievementsKeyField.placeholderText)
                        }
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: retroAchievementsKeyField.activeFocus ? 2 : 1
                            border.color: retroAchievementsKeyField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE KEY"
                        onClicked: {
                            RetroAchievements.storeApiKey(retroAchievementsKeyField.text)
                            retroAchievementsKeyField.clear()
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: RetroAchievements ? RetroAchievements.hasApiKey : false
                        text: "REMOVE"
                        onClicked: RetroAchievements.removeApiKey()
                    }
                }
                GlassButton {
                    compact: true
                    text: "GET A KEY FROM RETROACHIEVEMENTS"
                    onClicked: Qt.openUrlExternally("https://retroachievements.org/settings")
                }
                Text {
                    Layout.fillWidth: true
                    text: "SUPPORTS NES, SNES, GENESIS, GAME BOY AND OTHER CARTRIDGE SYSTEMS FIRST; DISC-BASED SYSTEMS ARE NOT MATCHED YET"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 8 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                }
                GlassButton { Layout.fillWidth: true; compact: true; text: settingsOverlay.connectionLabel("RATINGS AND GAME INFORMATION · IGDB", Insights && Insights.configured, ""); selected: settingsOverlay.connection === 2; onClicked: { settingsOverlay.connection = settingsOverlay.connection === 2 ? -1 : 2; settingsOverlay.pageChanged() } }
                ColumnLayout { Layout.fillWidth: true; spacing: 12; visible: settingsOverlay.connection === 2
                Text {
                    Layout.fillWidth: true
                    text: Insights ? Insights.statusText : "IGDB is unavailable in demo mode."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "TWITCH SETUP · Create Application, not Extension · Redirect: http://localhost · Client type: Confidential · Manage → New Secret"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbClientIdField
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        placeholderText: "Twitch developer client ID"
                        Accessible.name: placeholderText
                        readonly property string savedText: Insights ? Insights.clientId : ""
                        onSavedTextChanged: if (!activeFocus) text = savedText
                        Component.onCompleted: text = savedText
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        font.family: Theme.fontFamily
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, igdbClientIdField,
                                                      "TWITCH CLIENT ID", false,
                                                      igdbClientIdField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, igdbClientIdField,
                                                      "TWITCH CLIENT ID", false,
                                                      igdbClientIdField.placeholderText)
                        }
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: igdbClientIdField.activeFocus ? 2 : 1
                            border.color: igdbClientIdField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Insights !== null && !Insights.busy
                    TextField {
                        id: igdbSecretField
                        property bool controllerNavigation: host.couchMode
                        Layout.fillWidth: true
                        Accessible.name: "Twitch developer client secret"
                        placeholderText: Insights && Insights.hasClientSecret
                                         ? "Client secret stored securely" : "Twitch developer client secret"
                        color: Theme.foreground
                        placeholderTextColor: host.alpha(Theme.foreground, 0.42)
                        echoMode: TextInput.Password
                        Keys.onReturnPressed: function(event) {
                            host.handleCouchTextEntry(event, igdbSecretField,
                                                      "TWITCH CLIENT SECRET", true,
                                                      igdbSecretField.placeholderText)
                        }
                        Keys.onEnterPressed: function(event) {
                            host.handleCouchTextEntry(event, igdbSecretField,
                                                      "TWITCH CLIENT SECRET", true,
                                                      igdbSecretField.placeholderText)
                        }
                        font.family: Theme.fontFamily
                        background: Rectangle {
                            radius: Math.max(5, Theme.cornerRadius)
                            color: host.alpha(Theme.foreground, 0.045)
                            border.width: igdbSecretField.activeFocus ? 2 : 1
                            border.color: igdbSecretField.activeFocus
                                          ? Theme.accent
                                          : host.alpha(Theme.foreground, 0.15)
                        }
                    }
                    GlassButton {
                        compact: true
                        text: "SAVE CONNECTION"
                        onClicked: {
                            Insights.saveCredentials(igdbClientIdField.text, igdbSecretField.text)
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
                    compact: true; text: "TEST CONNECTION"
                    enabled: Insights && Insights.configured && !Insights.busy && Metadata && !Metadata.busy
                    onClicked: Insights.testConnection()
                }
                GlassButton {
                    compact: true
                    text: "OPEN TWITCH APPLICATIONS"
                    onClicked: Qt.openUrlExternally("https://dev.twitch.tv/console/apps")
                }
                }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: settingsOverlay.section === 3
                Text { Layout.fillWidth: true; text: Controller.connected ? "CONTROLLER · " + Controller.name : "CONTROLLER · NOT CONNECTED"; color: Theme.foreground; font.family: Theme.fontFamily; font.pixelSize: 12 * settingsPanel.uiScale }
                GlassButton { compact: true; text: host.couchMode ? "SWITCH TO DESKTOP" : "SWITCH TO COUCH MODE"; onClicked: host.setCouchMode(!host.couchMode) }
                Text {
                    text: "STREAM WITH SUNSHINE AND MOONLIGHT"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * settingsPanel.uiScale
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: Sunshine ? Sunshine.statusText : "Sunshine export is unavailable in demo mode."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 10 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "Moonlight shows Sunshine's app list. Omakade can add itself next to Steam Big Picture and one app per installed game with its cover. Sunshine reads the list when it starts, so restart it after changes."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 9 * settingsPanel.uiScale
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Sunshine !== null && Sunshine.detected
                    Text {
                        Layout.fillWidth: true
                        text: "OMAKADE IN MOONLIGHT"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * settingsPanel.uiScale
                    }
                    GlassButton {
                        objectName: "sunshineOmakadeButton"
                        compact: true
                        text: Preferences.sunshineOmakadeApp ? "ENABLED" : "DISABLED"
                        onClicked: Preferences.sunshineOmakadeApp = !Preferences.sunshineOmakadeApp
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: Sunshine !== null && Sunshine.detected
                    Text {
                        Layout.fillWidth: true
                        text: Sunshine && Sunshine.exportedGames > 0
                              ? "ONE APP PER INSTALLED GAME · " + Sunshine.exportedGames + " EXPORTED"
                              : "ONE APP PER INSTALLED GAME"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 10 * settingsPanel.uiScale
                    }
                    GlassButton {
                        objectName: "sunshineGamesButton"
                        compact: true
                        text: Preferences.sunshineGameApps ? "ENABLED" : "DISABLED"
                        onClicked: Preferences.sunshineGameApps = !Preferences.sunshineGameApps
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Sunshine !== null && Sunshine.detected
                    GlassButton {
                        compact: true
                        text: "UPDATE APP LIST"
                        enabled: Sunshine && !Sunshine.busy
                        onClicked: Sunshine.sync()
                    }
                    GlassButton {
                        compact: true
                        visible: Sunshine && Sunshine.restartNeeded && !Sunshine.streaming
                        enabled: Sunshine && !Sunshine.busy
                        text: "RESTART SUNSHINE"
                        onClicked: Sunshine.restartSunshine()
                    }
                }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: settingsOverlay.section === 4
                GlassButton { compact: true; text: "CLEAR DOWNLOADED PORTRAITS"; enabled: Metadata && !Metadata.busy; onClicked: Metadata.clearPortraitCache() }
                GlassButton {
                    objectName: "backupSettingsButton"
                    compact: true
                    text: "BACKUP & RESTORE"
                    enabled: Backups.available
                    onClicked: host.openBackupEditor()
                }
                Repeater {
                    model: [
                        { label: "LIBRARY", value: settingsOverlay.libraryCount + " visible games" },
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
                            font.pixelSize: 10 * settingsPanel.uiScale
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * settingsPanel.uiScale
                            elide: Text.ElideMiddle
                        }
                    }
                }
                Flow { Layout.fillWidth: true; spacing: 8
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE -"
                        onClicked: Preferences.artworkCacheLimitMb -= 128
                    }                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CACHE +"
                        onClicked: Preferences.artworkCacheLimitMb += 128
                    }                    GlassButton {
                        Layout.fillWidth: true
                        compact: true
                        text: "CLEAR ART"
                        onClicked: Achievements.clearCache()
                    }                }
                RowLayout {
                    Layout.topMargin: 8
                    spacing: 8
                    GlassButton {
                        compact: true
                        text: "PROJECT"
                        onClicked: Qt.openUrlExternally("https://github.com/btsouth/omakade")
                    }
                    GlassButton {
                        compact: true
                        text: "REPORT ISSUE"
                        onClicked: Qt.openUrlExternally("https://github.com/btsouth/omakade/issues/new/choose")
                    }
                    Item { Layout.fillWidth: true }
                }
                }
            }
            }

            Text {
                visible: host.couchMode
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 42
                anchors.bottomMargin: 24
                text: Controller.primaryGlyph + "  SELECT     "
                      + Controller.backGlyph + (settingsOverlay.sourceDetail || settingsOverlay.connection >= 0 ? "  BACK" : "  CLOSE")
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 12 * settingsPanel.uiScale
                font.weight: Font.DemiBold
            }
        }
    }

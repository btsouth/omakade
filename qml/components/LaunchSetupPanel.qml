import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    objectName: "launchSetupPanel"
    property var installation: ({})
    property int revision: 0
    property bool expanded: false
    readonly property Item firstControl: plan.supported === true ? emulator : copyLaunchDetailsButton
    readonly property real uiScale: root.Window.window && root.Window.window.couchMode ? 1.35 : 1
    readonly property var plan: { const update=revision; return Launcher.inspect(installation) }
    signal textEntryRequested(var target,string title,bool password,string placeholder)
    signal locateMissingContentRequested(string path)
    signal undoRelocationRequested()
    property bool undoRelocationAvailable: false
    spacing: 8
    function populate() {
        emulator.currentIndex=Math.max(0,plan.options ? plan.options.indexOf(plan.mode || "Automatic") : 0)
        core.text=plan.core || ""
        location.text=plan.path || ""
        flatpak.checked=plan.flatpak !== undefined ? plan.flatpak : (installation.flatpak || false)
    }
    onInstallationChanged: populate()
    Component.onCompleted: populate()
    Connections { target: Launcher; function onSetupChanged() { root.revision++; root.populate() } }
    ColumnLayout {
        Layout.fillWidth: true; visible: root.expanded; spacing: 8
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.plan.summary || ""; color: Theme.foreground }
        RowLayout {
            Layout.fillWidth: true
            visible: (root.plan.error || "").length > 0 || root.undoRelocationAvailable
            Text {
                objectName: "launchSetupPlanErrorText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.plan.error || ""
                visible: text.length > 0
                color: Theme.mutedText
            }
            GlassButton {
                objectName: "launchSetupLocateMissingContentButton"
                compact: true
                text: "LOCATE FILE"
                visible: root.plan.errorCategory === "content"
                onClicked: root.locateMissingContentRequested(root.plan.gamePath ||
                                                               root.installation.installPath || "")
            }
            GlassButton {
                objectName: "launchSetupUndoRelocationButton"
                compact: true
                text: "UNDO RELOCATION"
                visible: root.undoRelocationAvailable
                onClicked: root.undoRelocationRequested()
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; visible: root.plan.supported === true
            ThemedComboBox { id: emulator; objectName: "launchEmulatorChoice"; uiScale: root.uiScale; Layout.fillWidth: true; model: root.plan.options || []; Accessible.name: "Emulator choice" }
            ThemedCheckBox { id: flatpak; uiScale: root.uiScale; text: "Use Flatpak"; Accessible.name: text }
            RowLayout {
                Layout.fillWidth: true
                ThemedTextField { id: core; objectName: "launchCorePath"; Layout.fillWidth: true; placeholderText: "Optional libretro core path"; Accessible.name: placeholderText
                    property bool controllerNavigation: TextEntry.keyboardNeeded

                    function acceptInput(event) { if(TextEntry.keyboardNeeded){root.textEntryRequested(core,"CORE PATH",false,placeholderText);event.accepted=true} }
                    function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                    Keys.onReturnPressed: event => acceptInput(event)
                    Keys.onEnterPressed: event => { if(TextEntry.keyboardNeeded){root.textEntryRequested(core,"CORE PATH",false,placeholderText);event.accepted=true} }
                }
                GlassButton { text: "CORE"; compact: true; onClicked: { filePicker.forCore=true;filePicker.open() } }
            }
            RowLayout {
                Layout.fillWidth: true
                ThemedTextField { id: location; objectName: "launchGamePath"; Layout.fillWidth: true; placeholderText: root.installation.installPath || "Game location override"; Accessible.name: "Game file location"
                    property bool controllerNavigation: TextEntry.keyboardNeeded

                    function acceptInput(event) { if(TextEntry.keyboardNeeded){root.textEntryRequested(location,"GAME PATH",false,placeholderText);event.accepted=true} }
                    function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                    Keys.onReturnPressed: event => acceptInput(event)
                    Keys.onEnterPressed: event => { if(TextEntry.keyboardNeeded){root.textEntryRequested(location,"GAME PATH",false,placeholderText);event.accepted=true} }
                }
                GlassButton { text: "LOCATE"; compact: true; onClicked: {filePicker.forCore=false;filePicker.open()} }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Blank paths keep the discovered setup. Explicit choices stay with this installation. Linked installation preference is managed separately."; color: Theme.mutedText }
            Flow { Layout.fillWidth: true; spacing: 8
                GlassButton { objectName: "saveLaunchSetup"; text: "SAVE SETUP"; onClicked: Launcher.saveSetup(root.installation,emulator.currentText,core.text,flatpak.checked,location.text) }
                GlassButton { text: "RESET TO AUTOMATIC"; onClicked: Launcher.resetSetup(root.installation) }
            }
        }
        GlassButton { id: copyLaunchDetailsButton; objectName: "copyLaunchDetailsButton"; text: "COPY LAUNCH DETAILS"; compact: true; onClicked: Launcher.copyLaunchDetails(root.installation) }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: Launcher.lastError; visible: text.length>0; color: Theme.mutedText }
    }
    FileDialog { id: filePicker; property bool forCore: false; title: forCore ? "Choose a libretro core" : "Locate the game"; nameFilters: forCore ? ["Libretro cores (*_libretro.so)"] : ["Game files (*)"]; onAccepted: {const path=decodeURIComponent(selectedFile.toString().replace(/^file:\/\//,""));if(forCore)core.text=path;else location.text=path} }
}

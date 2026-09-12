import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    objectName: "rommSettingsPanel"
    property var host: null
    property bool expanded: false
    readonly property var service: typeof RommLibrary !== "undefined" ? RommLibrary : null
    spacing: 10
    GlassButton {
        objectName: "rommConnectionToggle"
        Layout.fillWidth: true
        text: "ROMM LIBRARY: " + (Preferences.rommEnabled ? "ON" : "OFF")
        selected: Preferences.rommEnabled
        onClicked: root.expanded = !root.expanded
    }
    ColumnLayout {
        id: configuration
        Layout.fillWidth: true
        visible: root.expanded
        spacing: 10
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.mutedText; text: "Browse RomM games from a locally mounted library. Use a Client API Token with roms.read access. Omakade never downloads or changes server files." }
        TextField {
            id: server; objectName: "rommServerField"; Layout.fillWidth: true
            text: Preferences.rommUrl; placeholderText: "https://romm.example"; Accessible.name: "RomM server address"
            property bool controllerNavigation: root.host && root.host.couchMode
            Keys.onReturnPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,server,"ROMM SERVER",false,placeholderText) }
            Keys.onEnterPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,server,"ROMM SERVER",false,placeholderText) }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: folder; objectName: "rommFolderField"; Layout.fillWidth: true
                text: Preferences.rommLibraryRoot; placeholderText: "Mounted library folder"; Accessible.name: "RomM mounted library folder"
                property bool controllerNavigation: root.host && root.host.couchMode
                Keys.onReturnPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,folder,"LIBRARY FOLDER",false,placeholderText) }
                Keys.onEnterPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,folder,"LIBRARY FOLDER",false,placeholderText) }
            }
            GlassButton { text: "BROWSE"; compact: true; onClicked: folderPicker.open() }
        }
        TextField {
            id: token; objectName: "rommTokenField"; Layout.fillWidth: true
            echoMode: TextInput.Password; placeholderText: root.service && root.service.hasToken ? "Token saved securely; leave blank to reuse" : "Client API Token"; Accessible.name: "RomM Client API Token"
            property bool controllerNavigation: root.host && root.host.couchMode
            Keys.onReturnPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,token,"ROMM TOKEN",true,placeholderText) }
            Keys.onEnterPressed: event => { if(root.host) root.host.handleCouchTextEntry(event,token,"ROMM TOKEN",true,placeholderText) }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            GlassButton { objectName: "rommConnect"; text: "SAVE AND CONNECT"; enabled: root.service && !root.service.scanning; onClicked: { if(root.service.connectServer(server.text,folder.text,token.text)) token.clear() } }
            GlassButton { text: "TEST / REFRESH"; enabled: root.service && Preferences.rommEnabled && !root.service.scanning; onClicked: root.service.refresh() }
            GlassButton { text: "DISCONNECT"; enabled: root.service && Preferences.rommEnabled; onClicked: root.service.disconnectServer(false) }
            GlassButton { text: "FORGET TOKEN"; enabled: root.service && !root.service.scanning; onClicked: { root.service.disconnectServer(true); token.clear() } }
        }
        Text { objectName: "rommConnectionStatus"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.foreground; text: root.service ? root.service.statusText : "Unavailable in demo mode" }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.mutedText; text: root.service ? root.service.errorText : ""; visible: text.length > 0 }
    }
    FolderDialog { id: folderPicker; title: "Select the mounted RomM library"; onAccepted: folder.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//,"")) }
}

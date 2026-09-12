import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    objectName: "saveProtectionPanel"
    property var host: null
    property bool expanded: false
    property var selected: ({})
    property var preview: ({})
    property bool confirmCleanup: false
    property int page: 0
    readonly property var entries: SaveProtection.entries.filter(entry => !search.text || (entry.title || "").toLowerCase().indexOf(search.text.toLowerCase())>=0)
    Connections { target: SaveProtection; function onChanged() {
        if (SaveProtection.busy || !root.selected.key) return
        const updated = SaveProtection.entries.find(entry => entry.key === root.selected.key)
        root.selected = updated || ({})
    } }
    spacing: 8
    GlassButton { objectName: "saveOverviewToggle"; text: "SAVE PROTECTION OVERVIEW"; onClicked: {root.expanded=!root.expanded;if(root.expanded)SaveProtection.refresh()} }
    ColumnLayout {
        Layout.fillWidth: true; visible: root.expanded; spacing: 10
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: SaveProtection.busy ? "Checking save coverage…" : root.entries.length+" installations · "+(SaveProtection.storageBytes/(1024*1024)).toFixed(1)+" MiB in backups (shared sets counted once)"; color: Theme.foreground }
        TextField { id: search; Layout.fillWidth: true; placeholderText: "Find a game"; Accessible.name: placeholderText; onTextChanged: root.page=0
            property bool controllerNavigation: TextEntry.keyboardNeeded
                    Keys.onReturnPressed: event => {if(root.host)root.host.handleCouchTextEntry(event,search,"FIND GAME",false,placeholderText)}
            Keys.onEnterPressed: event => {if(root.host)root.host.handleCouchTextEntry(event,search,"FIND GAME",false,placeholderText)}
        }
        Repeater {
            model: root.entries.slice(root.page*15,(root.page+1)*15)
            GlassButton {
                required property var modelData
                Layout.fillWidth: true; compact: true
                text: modelData.title+" · "+modelData.status+(modelData.shared?" · SHARED":"")
                onClicked: {root.selected=modelData;root.preview=({});customFiles.clear();root.confirmCleanup=false;SaveProtection.select(modelData.key)}
            }
        }
        Flow { Layout.fillWidth: true; spacing: 8
            GlassButton { text: "PREVIOUS"; enabled: root.page>0; onClicked:root.page-- }
            GlassButton { text: "NEXT"; enabled: (root.page+1)*15<root.entries.length; onClicked:root.page++ }
            GlassButton { text: "REFRESH COVERAGE"; enabled: !SaveProtection.busy; onClicked: {root.page=0;SaveProtection.refresh()} }
        }
        ColumnLayout {
            Layout.fillWidth: true; visible: !!root.selected.key; spacing: 8
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.selected.title || ""; color: Theme.foreground; font.bold:true }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: (root.selected.description || "")+"\n"+(root.selected.error || "")+"\n"+(root.selected.latestVerified ? "Latest verified capture: "+root.selected.latestVerified : "No capture-verification record yet; backups are validated before restore."); color:Theme.mutedText }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Included files:\n"+(root.selected.files || []).join("\n")+"\nIncluded folders:\n"+(root.selected.trees || []).join("\n");color:Theme.mutedText }
            GlassButton { id: backupsButton; text:"BACKUPS / RESTORE / UNDO"; onClicked: {if(SaveProtection.select(root.selected.key))backupMenu.open()} }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Custom layout: choose explicit save files. Preview the selection before applying. No game saves are changed by configuration.";color:Theme.mutedText }
            TextArea { id: customFiles; objectName: "customSaveFiles"; Layout.fillWidth:true; implicitHeight:80; placeholderText:"One save-file path per line"; Accessible.name:placeholderText; onTextChanged:root.preview=({})
                property bool controllerNavigation: TextEntry.keyboardNeeded
                    Keys.onReturnPressed: event => {if(TextEntry.keyboardNeeded && root.host)root.host.handleCouchTextEntry(event,customFiles,"SAVE FILES",false,placeholderText)}
                Keys.onEnterPressed: event => {if(TextEntry.keyboardNeeded && root.host)root.host.handleCouchTextEntry(event,customFiles,"SAVE FILES",false,placeholderText)}
            }
            CheckBox { id: shared; palette.windowText: Theme.foreground; text:"These files are shared by multiple games"; onCheckedChanged:root.preview=({}) }
            Flow { Layout.fillWidth:true;spacing:8
                GlassButton {text:"CHOOSE FILES";onClicked:filesDialog.open()}
                GlassButton {text:"PREVIEW LAYOUT";onClicked:root.preview=SaveBackups.previewCustomFiles(root.selected.context,customFiles.text.split("\n").filter(path=>path.trim().length>0),shared.checked)}
                GlassButton {text:"APPLY LAYOUT";enabled:root.preview.valid===true;onClicked:{SaveBackups.setCustomFiles(root.selected.context,root.preview.files,shared.checked);root.preview=({});SaveProtection.refresh()}}
                GlassButton {text:"USE AUTOMATIC LAYOUT";onClicked:{SaveBackups.resetCustomFiles(root.selected.context);SaveProtection.refresh()}}
            }
            Text {Layout.fillWidth:true;wrapMode:Text.Wrap;color:Theme.foreground;text:root.preview.valid ? root.preview.files.length+" files · "+(root.preview.bytes/1024).toFixed(1)+" KiB" : (root.preview.error || "")}
        }
        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"Retention and storage";color:Theme.foreground;font.bold:true}
        RowLayout {
            Layout.fillWidth:true
            Text {text:"Versions";color:Theme.mutedText}
            SpinBox {id:retention;from:2;to:50;value:SaveBackups.retention;Accessible.name:"Backup versions to retain"}
            Text {text:"MiB limit";color:Theme.mutedText}
            SpinBox {id:budget;from:256;to:8192;stepSize:256;value:SaveBackups.storageLimitMiB;Accessible.name:"Backup storage limit in MiB"}
        }
        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:"Default legacy SRAM snapshots retain their 256 MiB cap until limits are saved. Lowering limits keeps existing backups. Review the proposed older versions below before deleting anything. A full store refuses new backups until space is available.";color:Theme.mutedText}
        Flow {Layout.fillWidth:true;spacing:8
            GlassButton {text:"SAVE LIMITS";onClicked:{root.confirmCleanup=false;SaveBackups.setPolicy(retention.value,budget.value)}}
            GlassButton {text:"REVIEW CLEANUP";enabled:!SaveProtection.busy;onClicked:{SaveProtection.previewCleanup();root.confirmCleanup=true}}
        }
        ColumnLayout {
            Layout.fillWidth:true;visible:root.confirmCleanup
            Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:SaveProtection.cleanup.length+" older backups selected. Deletion cannot be undone; live saves are unchanged.";color:Theme.foreground}
            Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:SaveProtection.cleanup.map(item=>item.title+" · "+item.createdAt+" · "+(item.bytes/1024).toFixed(1)+" KiB").join("\n");color:Theme.mutedText}
            Flow {Layout.fillWidth:true;spacing:8
                GlassButton {text:"DELETE LISTED BACKUPS";enabled:SaveProtection.cleanup.length>0;onClicked:{SaveProtection.applyCleanup();root.confirmCleanup=false}}
                GlassButton {text:"CANCEL";onClicked:root.confirmCleanup=false}
            }
        }
        Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:SaveBackups.message;color:Theme.foreground}
    }
    SaveBackupMenu {id:backupMenu;namePrefix:"overview";host:root.Window.window;anchorItem:backupsButton;onClosed:SaveProtection.refresh()}
    FileDialog {id:filesDialog;title:"Choose explicit save files";fileMode:FileDialog.OpenFiles;onAccepted:customFiles.text=selectedFiles.map(url=>decodeURIComponent(url.toString().replace(/^file:\/\//,""))).join("\n")}
}

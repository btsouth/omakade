import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UserDates.js" as UserDates

ActionMenu {
        id: saveBackupsMenu
        property string namePrefix: ""
        property string gameTitle: ""
        property string sourceLabel: ""
        objectName: namePrefix + "saveBackupsMenu"
        function storageSizeText(value) { return value < 1024*1024 ? (value/1024).toFixed(1)+" KiB" : (value/(1024*1024)).toFixed(1)+" MiB" }
        showCloseButton: false
        title: "SAVE BACKUPS"
        fixedHeader: true
        preferredWidth: 460
        property string pendingVersion: ""
        property bool pendingShared: false
        property bool pendingDelete: false
        property string pendingLabel: ""
        onClosed: {
            pendingVersion = ""
            pendingDelete = false
            pendingLabel = ""
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: saveBackupsMenu.gameTitle.length > 0
            text: "Saves for " + saveBackupsMenu.gameTitle
                  + (saveBackupsMenu.sourceLabel ? " · " + saveBackupsMenu.sourceLabel : "")
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: UiMetrics.body
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: "Backup destination: " + SaveBackups.backupRoot
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: UiMetrics.supporting
            wrapMode: Text.WrapAnywhere
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            lineHeight: 1.2
            text: "Automatic snapshots made before launch. Keep up to " + SaveBackups.retention + " versions. Lower limits require reviewed cleanup. Save states are not included."
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            text: typeof SaveBackups !== "undefined" ? SaveBackups.message : ""
        }
        Text {
            Layout.fillWidth: true
            visible: typeof SaveBackups !== "undefined" && SaveBackups.versions.length > 0
            wrapMode: Text.Wrap
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            text: SaveBackups.versions.length + (SaveBackups.versions.length === 1 ? " backup" : " backups")
                  + "  ·  " + saveBackupsMenu.storageSizeText(SaveBackups.storageBytes)
        }
        Text {
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion !== "" && saveBackupsMenu.pendingShared
            wrapMode: Text.Wrap
            color: Theme.yellow
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            text: "SHARED STORAGE · This may also change saves for other games or profiles."
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion !== ""
            wrapMode: Text.Wrap
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: 12
            lineHeight: 1.2
            text: (saveBackupsMenu.pendingDelete ? "Delete backup from " : "Restore backup from ")
                  + saveBackupsMenu.pendingLabel + " for "
                  + (saveBackupsMenu.gameTitle || "this game") + "? "
                  + (saveBackupsMenu.pendingDelete
                     ? "This cannot be undone. Your current save will not change."
                     : "Close the emulator first. Your current saves will be backed up before anything changes.")
        }
        MenuAction {
            id: cancelSaveRestore
            objectName: saveBackupsMenu.namePrefix + "cancelSaveRestore"
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion !== ""
            text: saveBackupsMenu.pendingDelete ? "BACK" : "CANCEL"
            onClicked: {
                if (saveBackupsMenu.pendingDelete) {
                    saveBackupsMenu.pendingDelete = false
                    Qt.callLater(cancelSaveRestore.forceActiveFocus)
                } else {
                    saveBackupsMenu.pendingVersion = ""
                    saveBackupsMenu.doneControl.forceActiveFocus()
                }
            }
        }
        MenuAction {
            objectName: saveBackupsMenu.namePrefix + "deleteSaveBackup"
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion !== "" && !saveBackupsMenu.pendingDelete
            text: "DELETE BACKUP…"
            onClicked: {
                saveBackupsMenu.pendingDelete = true
                Qt.callLater(cancelSaveRestore.forceActiveFocus)
            }
        }
        MenuAction {
            objectName: saveBackupsMenu.namePrefix + "confirmSaveRestore"
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion !== ""
            text: saveBackupsMenu.pendingDelete ? "DELETE BACKUP" : "RESTORE THIS SAVE"
            onClicked: {
                if (saveBackupsMenu.pendingDelete)
                    SaveBackups.deleteVersion(saveBackupsMenu.pendingVersion)
                else
                    SaveBackups.restore(saveBackupsMenu.pendingVersion)
                saveBackupsMenu.pendingVersion = ""
                saveBackupsMenu.pendingDelete = false
                saveBackupsMenu.doneControl.forceActiveFocus()
            }
        }
        MenuAction {
            objectName: saveBackupsMenu.namePrefix + "createSaveBackup"
            Layout.fillWidth: true
            visible: saveBackupsMenu.pendingVersion === ""
                     && typeof SaveBackups !== "undefined" && SaveBackups.canSnapshot
            text: "BACK UP NOW"
            onClicked: {
                SaveBackups.snapshotSelected()
                saveBackupsMenu.doneControl.forceActiveFocus()
            }
        }
        MenuAction {
            Layout.fillWidth: true
            visible: typeof SaveBackups !== "undefined" && SaveBackups.recoveryPending
            text: "RETRY SAVE RECOVERY"
            onClicked: SaveBackups.retryRecovery()
        }
        Repeater {
            model: typeof SaveBackups !== "undefined" ? SaveBackups.versions : []
            MenuAction {
                required property var modelData
                required property int index
                objectName: saveBackupsMenu.namePrefix + "saveBackupVersion_" + index
                Layout.fillWidth: true
                visible: saveBackupsMenu.pendingVersion === ""
                text: UserDates.format(new Date(modelData.createdAt), "datetime")
                      + "  ·  " + saveBackupsMenu.storageSizeText(modelData.bytes)
                      + (modelData.shared === true ? "  ·  SHARED" : "")
                onClicked: {
                    saveBackupsMenu.pendingShared = modelData.shared === true
                    saveBackupsMenu.pendingDelete = false
                    saveBackupsMenu.pendingVersion = modelData.id
                    saveBackupsMenu.pendingLabel = UserDates.format(new Date(modelData.createdAt), "datetime")
                    Qt.callLater(cancelSaveRestore.forceActiveFocus)
                }
            }
        }
    }

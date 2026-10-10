import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Rectangle {
    id: editor
    required property var game
    required property int gameRow
    property bool couchMode: false
    property string selectedKind: "cover"
    property string message: ""
    readonly property real uiScale: couchMode ? Math.max(1.2, Math.min(2.4, height / 600)) : 1
    signal dismissed()
    signal artworkChanged()
    signal textEntryRequested(var target, string title)
    color: Theme.background

    function apply(kind, path) {
        if (Library.setCustomArtwork(gameRow, kind, path)) {
            message = "Artwork updated"
            artworkChanged()
        } else message = "Choose a readable PNG, JPEG, or WebP image up to 32 MB."
    }
    function reset(kind) {
        if (Library.resetCustomArtwork(gameRow, kind)) {
            message = "Original artwork restored"
            artworkChanged()
        } else message = "Could not reset that artwork"
    }
    function focusEditor() { doneButton.forceActiveFocus() }
    FileDialog {
        id: artworkDialog
        property var selectedField: null
        title: "Choose " + editor.selectedKind + " artwork"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: {
            if (selectedField) selectedField.text = selectedFile.toString()
            editor.apply(editor.selectedKind, selectedFile)
            selectedField = null
        }
        onRejected: selectedField = null
    }
    MouseArea { anchors.fill: parent }
    ScrollView {
        id: scroll
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 740 * editor.uiScale)
        height: parent.height - 48
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12 * editor.uiScale
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "CUSTOM IMAGES"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 24 * editor.uiScale
                }
                GlassButton {
                    id: doneButton
                    objectName: "artworkDoneButton"
                    text: "DONE"
                    displayScale: editor.uiScale
                    onClicked: editor.dismissed()
                }
            }
            Text {
                Layout.fillWidth: true
                text: editor.game.title || "Game"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 14 * editor.uiScale
            }
            Repeater {
                model: [
                    { kind: "cover", title: "COVER", note: "Portrait artwork for the library", flag: "customCover" },
                    { kind: "hero", title: "BACKGROUND", note: "Wide background for game details", flag: "customHero" }
                ].concat(editor.couchMode || editor.game.customLogo
                    ? [{ kind: "logo", title: "COUCH LOGO", note: "Optional title artwork in the Couch library", flag: "customLogo" }]
                    : [])
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6 * editor.uiScale
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: modelData.title + " · " + modelData.note
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * editor.uiScale
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: scroll.availableWidth < 540 * editor.uiScale ? 1 : 2
                        columnSpacing: 12 * editor.uiScale
                        rowSpacing: 12 * editor.uiScale
                        Item {
                            id: previewFrame
                            objectName: "artworkPreview_" + modelData.kind
                            readonly property string effectivePath: editor.game[modelData.kind + "Path"] || ""
                            readonly property bool hasTypedPath: pathField.text.trim().length > 0
                            readonly property real previewWidth: modelData.kind === "cover" ? 88 * editor.uiScale : 150 * editor.uiScale
                            readonly property real previewHeight: modelData.kind === "cover" ? 132 * editor.uiScale
                                                                                          : modelData.kind === "hero" ? 84 * editor.uiScale
                                                                                                                        : 64 * editor.uiScale
                            Layout.preferredWidth: previewWidth
                            Layout.preferredHeight: previewHeight
                            Rectangle {
                                anchors.fill: parent
                                radius: 5
                                color: modelData.kind === "logo"
                                       ? Qt.alpha(Theme.foreground, 0.08) : Theme.darkerBackground
                                border.color: Qt.alpha(Theme.foreground, 0.15)
                            }
                            Image {
                                id: effectivePreview
                                objectName: "artworkEffectivePreview_" + modelData.kind
                                anchors.fill: parent
                                anchors.margins: 4 * editor.uiScale
                                source: previewFrame.effectivePath
                                visible: !previewFrame.hasTypedPath || pathPreview.status !== Image.Ready
                                autoTransform: true
                                asynchronous: true
                                cache: false
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: 400
                                sourceSize.height: 320
                            }
                            Image {
                                id: pathPreview
                                objectName: "artworkPathPreview_" + modelData.kind
                                anchors.fill: parent
                                anchors.margins: 4 * editor.uiScale
                                source: previewFrame.hasTypedPath ? pathField.text.trim() : ""
                                visible: previewFrame.hasTypedPath && status === Image.Ready
                                autoTransform: true
                                asynchronous: true
                                cache: false
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: 400
                                sourceSize.height: 320
                            }
                            Text {
                                objectName: "artworkPreviewMessage_" + modelData.kind
                                anchors.fill: parent
                                anchors.margins: 5 * editor.uiScale
                                visible: text !== ""
                                text: previewFrame.hasTypedPath && pathPreview.status === Image.Error
                                      ? "CAN'T LOAD THIS FILE"
                                      : !previewFrame.hasTypedPath && previewFrame.effectivePath.length === 0
                                        ? "NO IMAGE"
                                        : !previewFrame.hasTypedPath && effectivePreview.status === Image.Error
                                          ? "CAN'T LOAD THIS FILE" : ""
                                color: Theme.mutedText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.Wrap
                                font.family: Theme.fontFamily
                                font.pixelSize: UiMetrics.supporting * editor.uiScale
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: pathField
                                property Item controllerRightTarget: pathFieldClear.visible ? pathFieldClear : null
                                rightPadding: pathFieldClear.reservedWidth
                                FieldClearButton { id: pathFieldClear; field: pathField }
                                objectName: "artworkPath_" + modelData.kind
                                property bool controllerNavigation: editor.couchMode || (Controller !== null && Controller.driving)
                                Layout.fillWidth: true
                                placeholderText: "/path/to/image.png"
                                Accessible.name: modelData.title + " image path"
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: UiMetrics.body * editor.uiScale
                                placeholderTextColor: Theme.mutedText

                                function acceptInput(event) {
                                    if (TextEntry.keyboardNeeded) { editor.textEntryRequested(pathField, modelData.title + " IMAGE PATH"); event.accepted = true }
                                }
                                function controllerAccept() { acceptInput({ modifiers: Qt.NoModifier, accepted: false }) }
                                Keys.onReturnPressed: event => acceptInput(event)
                                Keys.onEnterPressed: function(event) {
                                    if (TextEntry.keyboardNeeded) { editor.textEntryRequested(pathField, modelData.title + " IMAGE PATH"); event.accepted = true }
                                }
                                background: Rectangle {
                                    color: Theme.background
                                    radius: 5
                                    border.width: pathField.activeFocus ? 2 : 1
                                    border.color: pathField.activeFocus ? Theme.accent : Theme.mutedText
                                }
                            }
                            RowLayout {
                                GlassButton {
                                    objectName: "artworkApply_" + modelData.kind
                                    text: "APPLY"
                                    compact: true
                                    displayScale: editor.uiScale
                                    onClicked: editor.apply(modelData.kind, pathField.text)
                                }
                                GlassButton {
                                    text: "BROWSE"
                                    compact: true
                                    visible: !editor.couchMode
                                    displayScale: editor.uiScale
                                    onClicked: {
                                        editor.selectedKind = modelData.kind
                                        artworkDialog.selectedField = pathField
                                        artworkDialog.open()
                                    }
                                }
                                GlassButton {
                                    objectName: "artworkAutomatic_" + modelData.kind
                                    text: "USE AUTOMATIC"
                                    compact: true
                                    enabled: editor.game[modelData.flag] || false
                                    displayScale: editor.uiScale
                                    onClicked: {
                                        editor.reset(modelData.kind)
                                        pathField.text = ""
                                        pathField.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: editor.message
                wrapMode: Text.Wrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.body * editor.uiScale
            }
        }
    }
}

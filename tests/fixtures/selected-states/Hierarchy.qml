import QtQuick

// Copied next to GlassButton.qml by SelectedStateUiTests, so GlassButton resolves from the
// same folder.
Rectangle {
    width: 700
    height: 380
    color: Theme.darkerBackground

    Column {
        x: 16
        y: 16
        spacing: 12
        GlassButton {
            objectName: "wide"
            width: 320
            text: "PLAY"
            iconText: "▶"
        }
        GlassButton {
            objectName: "leftAligned"
            width: 320
            text: "MENU ROW"
            labelAlignment: Qt.AlignLeft
        }
        Row {
            spacing: 12
            GlassButton {
                objectName: "primary"
                text: "PLAY"
                primary: true
            }
            GlassButton {
                objectName: "primarySelected"
                text: "PLAY"
                primary: true
                selected: true
            }
            GlassButton {
                objectName: "selected"
                text: "FAVORITED"
                selected: true
            }
            GlassButton {
                objectName: "secondary"
                text: "DETAILS"
            }
            GlassButton {
                objectName: "destructive"
                text: "DELETE"
                destructive: true
            }
        }
        GlassButton {
            objectName: "narrow"
            width: 110
            text: "A LABEL MUCH WIDER THAN ITS BUTTON"
        }
        // Sized to their labels at the Game Mode library's 1080p scale.
        Row {
            objectName: "fittedChips"
            spacing: 7
            Repeater {
                model: ["SHOW: ALL", "SOURCE: ALL", "SORT: TITLE", "CONSOLES"]
                GlassButton {
                    required property string modelData
                    text: modelData
                    compact: true
                    displayScale: 1.18
                    selected: modelData === "CONSOLES"
                }
            }
        }
    }
}

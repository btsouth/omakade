import QtQuick

// Copied next to GlassButton.qml by SelectedStateUiTests, so GlassButton resolves from the
// same folder.
Rectangle {
    id: page
    width: 800
    height: 120
    color: Theme.darkerBackground
    property string current: "first"
    property bool toggleOn: false

    Row {
        x: 16
        y: 16
        spacing: 12
        GlassButton {
            objectName: "firstTab"
            text: "FIRST"
            selectable: true
            selected: page.current === "first"
            onClicked: page.current = "first"
        }
        GlassButton {
            objectName: "secondTab"
            text: "SECOND"
            selectable: true
            selected: page.current === "second"
            onClicked: page.current = "second"
        }
        GlassButton {
            objectName: "toggle"
            text: page.toggleOn ? "ON" : "OFF"
            selectable: true
            selected: page.toggleOn
            onClicked: page.toggleOn = !page.toggleOn
        }
        GlassButton {
            objectName: "plain"
            text: "PLAIN"
        }
        GlassButton {
            objectName: "highlight"
            text: "HIGHLIGHT"
            selected: true
        }
    }
}

import QtQuick

// Copied next to GlassButton.qml by SelectedStateUiTests, so GlassButton resolves from the
// same folder.
Rectangle {
    id: page
    width: 1000
    height: 120
    color: Theme.darkerBackground
    property string current: "first"
    property bool toggleOn: false
    property bool favorite: false

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
            id: favoriteToggle
            objectName: "favoriteToggle"
            text: page.favorite ? "FAVORITED" : "ADD FAVORITE"
            selectable: true
            selected: page.favorite
            iconDelegate: HeartIcon {
                objectName: "favoriteHeart"
                filled: favoriteToggle.selected
                color: filled ? Theme.accent : favoriteToggle.iconColor
                size: 24
            }
            onClicked: page.favorite = !page.favorite
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

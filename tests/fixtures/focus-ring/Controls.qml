import QtQuick

// Copied next to the components by FocusRingUiTests, so they resolve from the same folder.
// Buttons, a field and two library cards, the first of them the retained current card.
Rectangle {
    width: 760
    height: 440
    color: Theme.darkerBackground

    Row {
        x: 24
        y: 24
        spacing: 24
        GlassButton {
            objectName: "button"
            text: "PLAY"
            hoverEnabled: true
        }
        GlassButton {
            objectName: "other"
            text: "OPTIONS"
            hoverEnabled: true
        }
        ThemedTextField {
            objectName: "field"
            width: 240
            placeholderText: "Search"
        }
    }

    Row {
        x: 24
        y: 110
        spacing: 24
        Repeater {
            model: 2
            GameCard {
                required property int index
                objectName: index === 0 ? "currentCard" : "otherCard"
                current: index === 0
                width: 150
                height: Math.round(width * 1.5) + 50
                title: index === 0 ? "Portal 2" : "Celeste"
                subtitle: "Steam"
                hours: 12
                rating: -1
                progress: 0
                favorite: false
                completionStatus: ""
                accentStart: "#17445c"
                accentEnd: "#253248"
                coverMark: index === 0 ? "P2" : "C"
                coverPath: ""
            }
        }
    }
}

import QtQuick
import "../../../qml/components"

Row {
    width: 800
    height: 400
    spacing: 20
    property int activations: 0

    // The game details page's achievement cards, fed the same roles as the Achievements model:
    // a hidden achievement before and after it is unlocked.
    Column {
        spacing: 10
        Repeater {
            model: ListModel {
                ListElement {
                    title: "Secret Ending"; description: "Beat the final boss"; iconPath: ""
                    unlocked: false; unlockTime: 0; rarity: 3.25; hidden: true
                }
                ListElement {
                    title: "Secret Ending"; description: "Beat the final boss"; iconPath: ""
                    unlocked: true; unlockTime: 1794139200; rarity: 3.25; hidden: true
                }
            }
            AchievementCard {
                width: 260
                height: 82
            }
        }
    }

    GameCard {
        objectName: "favoriteCard"
        width: 220
        height: 380
        title: "Portal 2"
        subtitle: "Steam"
        hours: 12
        rating: 91
        progress: 0
        favorite: true
        completionStatus: "playing"
        installed: false
        accentStart: "#17445c"
        accentEnd: "#253248"
        coverMark: "P2"
        coverPath: ""
        onActivated: parent.activations++
    }
    GameCard {
        objectName: "plainCard"
        width: 220
        height: 380
        title: "Celeste"
        subtitle: "Heroic"
        hours: 0
        rating: -1
        progress: 0
        favorite: false
        completionStatus: ""
        accentStart: "#17445c"
        accentEnd: "#253248"
        coverMark: "C"
        coverPath: ""
    }
}

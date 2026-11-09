import QtQuick
import "../../../qml/components"
import "../../../qml/components/GameAccessibility.js" as GameAccessibility

Row {
    width: 520
    height: 400
    spacing: 20
    property int activations: 0

    // What a screen reader hears for a hidden achievement before and after it is unlocked.
    readonly property var hiddenAchievement: ({ title: "Secret Ending", description: "Beat the final boss",
                                                hidden: true, unlocked: false, rarity: 3.25 })
    readonly property var unlockedAchievement: ({ title: "Secret Ending", description: "Beat the final boss",
                                                  hidden: true, unlocked: true, rarity: 3.25 })
    readonly property string hiddenName: GameAccessibility.achievementName(hiddenAchievement)
    readonly property string hiddenDescription: GameAccessibility.achievementDescription(hiddenAchievement, "")
    readonly property string unlockedName: GameAccessibility.achievementName(unlockedAchievement)
    readonly property string unlockedDescription:
        GameAccessibility.achievementDescription(unlockedAchievement, "Nov 8, 2026")

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

import QtQuick
import "../../../qml/components"
Rectangle {
    width: 660
    height: 465
    color: Theme.darkerBackground
    Row {
        anchors.centerIn: parent
        spacing: 24
        Repeater {
            model: [128, 180, 240]
            GameCard {
                required property int modelData
                width: modelData
                height: Math.round((width + 16) * 1.5) + 50
                title: "Portal 2"
                subtitle: "Steam"
                gameSource: "Steam"
                appId: "620"
                hours: 999
                rating: 95
                progress: 0
                favorite: false
                completionStatus: ""
                accentStart: "#17445c"
                accentEnd: "#253248"
                coverMark: "P2"
                coverPath: ""
            }
        }
    }
}

import QtQuick
import "../../../qml/components"
Rectangle {
    width: 1180
    height: 820
    color: Theme.darkerBackground
    Grid {
        anchors.centerIn: parent
        columns: 5
        spacing: 16
        Repeater {
            // The smallest library card (cover size 60) and a wide one, each with every
            // status, plus favorites so the pill has to stay clear of the heart.
            model: [
                { width: 110, status: "completed", favorite: true },
                { width: 110, status: "abandoned", favorite: true },
                { width: 110, status: "playing", favorite: true },
                { width: 110, status: "backlog", favorite: true },
                { width: 110, status: "completed", favorite: false },
                { width: 220, status: "completed", favorite: true },
                { width: 220, status: "abandoned", favorite: true },
                { width: 220, status: "playing", favorite: false },
                { width: 220, status: "backlog", favorite: false },
                { width: 220, status: "abandoned", favorite: false }
            ]
            GameCard {
                required property var modelData
                width: modelData.width
                height: Math.round(width * 1.5) + 50
                title: "Portal 2"
                subtitle: "Steam"
                hours: 12
                rating: -1
                progress: 0
                favorite: modelData.favorite
                completionStatus: modelData.status
                accentStart: "#17445c"
                accentEnd: "#253248"
                coverMark: "P2"
                coverPath: ""
            }
        }
    }
}

import QtQuick
import QtQuick.Layouts
import "UserDates.js" as UserDates
import "GameAccessibility.js" as GameAccessibility

// One achievement on the game details page. A hidden one keeps its name and description
// secret until it is unlocked, on screen and for screen readers.
Rectangle {
    id: card
    required property int index
    required property string title
    required property string description
    required property string iconPath
    required property bool unlocked
    required property double unlockTime
    required property real rarity
    required property bool hidden
    // Shown in place of the rarity when the source does not publish one.
    property string sourceLabel: "STEAM"
    readonly property string unlockedOn: unlocked && unlockTime > 0
                                         ? UserDates.format(new Date(unlockTime * 1000), "day") : ""

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }

    objectName: "achievementCard" + index
    activeFocusOnTab: true
    Accessible.name: GameAccessibility.achievementName(card)
    Accessible.description: GameAccessibility.achievementDescription(card, unlockedOn)
    Accessible.role: Accessible.ListItem
    Accessible.focused: activeFocus
    Layout.minimumWidth: 260
    Layout.preferredHeight: UiMetrics.scaled(82)
    radius: Math.max(6, Theme.cornerRadius)
    color: alpha(Theme.foreground, unlocked ? 0.075 : 0.035)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? Theme.accent
                  : unlocked
                    ? alpha(Theme.accent, 0.34)
                    : alpha(Theme.foreground, 0.10)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 11
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 54
            Layout.preferredHeight: 54
            radius: 5
            color: card.alpha(Theme.darkerBackground, 0.54)
            border.color: card.alpha(Theme.foreground, 0.12)
            clip: true

            Image {
                anchors.fill: parent
                source: card.iconPath
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                opacity: card.unlocked ? 1 : 0.42
            }
            Text {
                visible: card.iconPath.length === 0
                anchors.centerIn: parent
                text: card.unlocked ? "◆" : "◇"
                color: card.unlocked ? Theme.accent : Theme.mutedText
                font.pixelSize: 19
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: card.hidden && !card.unlocked ? "Hidden achievement" : card.title
                textFormat: Text.PlainText
                color: card.unlocked ? Theme.brightForeground : Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.label
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: card.hidden && !card.unlocked ? "Unlock to reveal details" : card.description
                textFormat: Text.PlainText
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.supporting
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: (card.unlockedOn.length > 0
                       ? "UNLOCKED " + card.unlockedOn.toUpperCase() + "  ·  " : "")
                      + (card.rarity > 0 ? card.rarity.toFixed(1) + "% OF PLAYERS" : card.sourceLabel)
                color: card.unlocked ? Theme.accent : card.alpha(Theme.foreground, 0.45)
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.caption
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }
}

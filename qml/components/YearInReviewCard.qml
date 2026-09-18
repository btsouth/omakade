import QtQuick
import QtQuick.Layouts

// The shareable card: one fixed-proportion scene so the exported image is exactly what the
// preview shows.
//
// Every figure here is the figure the Stats screen shows, and the same rules apply. The period
// total is recorded play with the window it covers named under it, launcher totals are labelled
// as launcher totals, and nothing is added together that should not be. A card is the one place
// these numbers travel without the screen explaining them, so the labels travel with them.
Item {
    id: card
    objectName: "yearInReviewCard"
    property real cardWidth: 1000
    property real cardHeight: 1500
    readonly property var headline: Stats.headline
    readonly property var byHour: Stats.byHour
    readonly property var byWeekday: Stats.byWeekday
    readonly property var bySystem: Stats.bySystem
    readonly property var achievements: Stats.achievements
    readonly property var streaks: Stats.streaks
    readonly property string periodLabel: Stats.periodLabel
    readonly property string windowNote: Stats.windowNote
    readonly property bool hasRecordedPlay: Number(headline.recordedSeconds || 0) > 0
    readonly property real unit: cardWidth / 1000
    readonly property real peakHourSeconds: {
        let peak = 0
        for (const entry of card.byHour)
            peak = Math.max(peak, Number(entry.seconds) || 0)
        return peak
    }
    readonly property var topSystems: {
        const picked = []
        for (let index = 0; index < card.bySystem.length && index < 4; ++index)
            picked.push(card.bySystem[index])
        return picked
    }

    implicitWidth: cardWidth
    implicitHeight: cardHeight

    function durationText(seconds) {
        const value = Math.max(0, Number(seconds) || 0)
        if (value < 60) return value > 0 ? "<1m" : "0m"
        const minutes = Math.floor(value / 60)
        if (minutes < 60) return minutes + "m"
        const hours = Math.floor(minutes / 60)
        const rest = minutes % 60
        if (hours < 100) return rest ? hours + "h " + rest + "m" : hours + "h"
        return Math.round(hours) + "h"
    }
    function percentText(share) {
        const value = Math.max(0, Math.min(1, Number(share) || 0)) * 100
        if (value > 0 && value < 1) return "<1%"
        return Math.round(value) + "%"
    }
    function countText(value, singular, plural) {
        const count = Number(value) || 0
        return count + " " + (count === 1 ? singular : (plural || singular + "s"))
    }
    function busiestHourText() {
        let best = -1
        let bestSeconds = 0
        for (const entry of card.byHour) {
            const seconds = Number(entry.seconds) || 0
            if (seconds > bestSeconds) {
                bestSeconds = seconds
                best = Number(entry.hour)
            }
        }
        return best < 0 ? "" : (best < 10 ? "0" : "") + best + ":00"
    }
    function busiestWeekdayText() {
        const names = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
        let best = -1
        let bestSeconds = 0
        for (const entry of card.byWeekday) {
            const seconds = Number(entry.seconds) || 0
            if (seconds > bestSeconds) {
                bestSeconds = seconds
                best = Number(entry.weekday)
            }
        }
        return best < 0 || bestSeconds <= 0 ? "" : names[best]
    }
    function lateNightShare() {
        let night = 0
        for (const entry of card.byHour) {
            const hour = Number(entry.hour) || 0
            if (hour >= 23 || hour <= 4) night += Number(entry.seconds) || 0
        }
        return Number(headline.recordedSeconds) > 0
                ? night / Number(headline.recordedSeconds) : 0
    }
    // The most played games after the first, which is already the hero figure above.
    function rankedGames(count) {
        const picked = []
        const list = Stats.topGames || []
        for (let index = 1; index < list.length && picked.length < count; ++index)
            picked.push(list[index])
        return picked
    }
    readonly property bool rarestIsRarity: String(card.achievements.rarest
                                                  && card.achievements.rarest.basis
                                                  ? card.achievements.rarest.basis : "")
                                           === "rarity"
    function rarestText() {
        const rarest = card.achievements.rarest
        if (!rarest || !rarest.title) return ""
        return Number(rarest.rarity) > 0
                ? rarest.title + "  ·  " + Number(rarest.rarity).toFixed(1) + "% of players"
                : rarest.title
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkerBackground

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 64 * card.unit
            spacing: 26 * card.unit

            // Mark and year.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2 * card.unit
                Text {
                    text: "OMAKADE"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 20 * card.unit
                    font.letterSpacing: 5 * card.unit
                    font.weight: Font.DemiBold
                }
                Text {
                    text: card.periodLabel
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 76 * card.unit
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "YOUR PLAY IN REVIEW"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 20 * card.unit
                    font.letterSpacing: 3 * card.unit
                }
            }

            // A thin rule with an accent lead, the same shape the rest of the app uses.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 2 * card.unit
                color: Qt.rgba(1, 1, 1, 0.20)
                Rectangle {
                    width: parent.width * 0.22
                    height: parent.height
                    color: Theme.accent
                }
            }

            // The one figure the whole card is about, with the window it covers.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4 * card.unit
                Text {
                    text: card.durationText(card.headline.recordedSeconds)
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 96 * card.unit
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "recorded play"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 20 * card.unit
                    font.letterSpacing: 2 * card.unit
                }
                Text {
                    Layout.fillWidth: true
                    visible: card.windowNote.length > 0
                    wrapMode: Text.Wrap
                    text: card.windowNote
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 17 * card.unit
                }
            }

            // Four figures across.
            RowLayout {
                Layout.fillWidth: true
                spacing: 18 * card.unit
                Repeater {
                    model: [
                        {label: "SESSIONS", value: String(card.headline.recordedSessions || 0)},
                        {label: "DAYS PLAYED", value: String(card.headline.daysPlayed || 0)},
                        {label: "GAMES PLAYED", value: String(card.headline.gamesPlayed || 0)},
                        {label: "LONGEST RUN",
                         value: card.countText(card.streaks.longestRun || 0, "day")}
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2 * card.unit
                        Text {
                            text: modelData.value
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 40 * card.unit
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: modelData.label
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 15 * card.unit
                            font.letterSpacing: 1.6 * card.unit
                        }
                    }
                }
            }

            // Most played.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8 * card.unit
                visible: (card.headline.topGameTitle || "").length > 0
                Text {
                    text: "MOST PLAYED"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 15 * card.unit
                    font.letterSpacing: 1.6 * card.unit
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: card.headline.topGameTitle || ""
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 42 * card.unit
                    font.weight: Font.DemiBold
                }
                Text {
                    text: card.durationText(card.headline.topGameSeconds)
                          + "  ·  " + card.percentText(card.headline.topGameShare)
                          + " of recorded play"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 20 * card.unit
                }
                // The rest of the ranking, which is what makes the card read like a recap rather
                // than a single number.
                Repeater {
                    model: card.rankedGames(4)
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4 * card.unit
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10 * card.unit
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: modelData.title || ""
                                color: Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: 24 * card.unit
                            }
                            Text {
                                text: card.durationText(modelData.seconds)
                                      + "  ·  " + card.percentText(modelData.share)
                                color: Theme.mutedText
                                font.family: Theme.fontFamily
                                font.pixelSize: 20 * card.unit
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 8 * card.unit
                            radius: 4 * card.unit
                            color: Qt.rgba(1, 1, 1, 0.14)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, Number(modelData.share) || 0))
                                height: parent.height
                                radius: 4 * card.unit
                                color: Theme.accent
                            }
                        }
                    }
                }
            }

            // When the play happened.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10 * card.unit
                visible: card.hasRecordedPlay
                Text {
                    text: "WHEN YOU PLAY"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 15 * card.unit
                    font.letterSpacing: 1.6 * card.unit
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 3 * card.unit
                    Repeater {
                        model: card.byHour
                        Item {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 84 * card.unit
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: Math.max(0, parent.height
                                                 * (card.peakHourSeconds > 0
                                                    ? (Number(modelData.seconds) || 0)
                                                      / card.peakHourSeconds : 0))
                                radius: 2 * card.unit
                                color: (Number(modelData.seconds) || 0) >= card.peakHourSeconds
                                       && card.peakHourSeconds > 0
                                       ? Theme.accent : Qt.rgba(1, 1, 1, 0.28)
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 19 * card.unit
                    text: {
                        const sentences = []
                        if (card.busiestHourText().length > 0)
                            sentences.push("Most of it around " + card.busiestHourText())
                        if (card.busiestWeekdayText().length > 0)
                            sentences.push(card.busiestWeekdayText() + " was your busiest day")
                        if (card.lateNightShare() > 0)
                            sentences.push(card.percentText(card.lateNightShare())
                                           + " after 23:00 and before 05:00")
                        return sentences.join(". ") + (sentences.length > 0 ? "." : "")
                    }
                }
            }

            // Where it went, by machine.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8 * card.unit
                visible: card.topSystems.length > 0
                Text {
                    text: "WHERE IT WENT"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 15 * card.unit
                    font.letterSpacing: 1.6 * card.unit
                }
                Repeater {
                    model: card.topSystems
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 3 * card.unit
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: modelData.name
                                color: Theme.brightForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: 22 * card.unit
                            }
                            Text {
                                text: card.percentText(modelData.share)
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: 22 * card.unit
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 8 * card.unit
                            radius: 4 * card.unit
                            color: Qt.rgba(1, 1, 1, 0.12)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, Number(modelData.share) || 0))
                                height: parent.height
                                radius: 4 * card.unit
                                color: Theme.accent
                            }
                        }
                    }
                }
                // The rows above are the top few, so say how many were left out: without this the
                // percentages visibly stop short of 100 and the card looks like it lost a slice.
                Text {
                    Layout.fillWidth: true
                    visible: card.bySystem.length > card.topSystems.length
                    text: "and " + card.countText(card.bySystem.length - card.topSystems.length, "more")
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 17 * card.unit
                }
            }

            // Unlocked.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6 * card.unit
                visible: Number(card.achievements.unlockedInPeriod || 0) > 0
                Text {
                    text: "UNLOCKED"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 15 * card.unit
                    font.letterSpacing: 1.6 * card.unit
                }
                Text {
                    text: card.countText(card.achievements.unlockedInPeriod, "achievement")
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 34 * card.unit
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    visible: card.rarestText().length > 0
                    wrapMode: Text.Wrap
                    text: (card.rarestIsRarity ? "Rarest: " : "Latest: ") + card.rarestText()
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 19 * card.unit
                }
            }

            Item { Layout.fillHeight: true }

            // What the launchers and emulators report for the whole library, labelled as theirs.
            // The card is the one place these figures travel without the screen beside them, and
            // the note under it says plainly that they are not part of the recorded totals above.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4 * card.unit
                visible: Number(card.headline.librarySeconds || 0) > 0
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1 * card.unit
                    color: Qt.rgba(1, 1, 1, 0.12)
                }
                Text {
                    text: "YOUR LIBRARY, ALL TIME"
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 15 * card.unit
                    font.letterSpacing: 1.6 * card.unit
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: card.durationText(card.headline.librarySeconds) + "  ·  "
                          + card.countText(card.headline.libraryGames, "game") + "  ·  all time"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 24 * card.unit
                }
            }

            // The honest line, on the image itself: this is the one place the figures travel
            // without the screen beside them.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3 * card.unit
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1 * card.unit
                    color: Qt.rgba(1, 1, 1, 0.12)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    // Short on purpose: the card is a fixed height and a longer sentence pushes the
                    // footer off its bottom edge.
                    text: "Recorded by Omakade from the day it first saw a game run. The library "
                          + "figure above is all time, and where an emulator keeps no counter it is "
                          + "that same time."
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 14 * card.unit
                }
            }
        }
    }
}

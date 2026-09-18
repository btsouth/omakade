import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// What you played, and when.
//
// Everything here comes from Stats, which reads the library's own numbers for anything that
// describes the library and the recorder's sessions for anything dated, and never adds the two
// together: a launcher's total cannot be split by period, so it is shown as a library figure
// while period figures come from recording and name the window they really cover.
FocusScope {
    id: root
    objectName: "statsScreen"
    property bool couchMode: false
    readonly property real scaleFactor: couchMode ? 1.25 : 1
    // Signals the window closes this view and returns to the library.
    signal libraryRequested()

    readonly property var headline: Stats.headline
    readonly property var bySource: Stats.bySource
    readonly property var bySystem: Stats.bySystem
    readonly property var byHour: Stats.byHour
    readonly property var byWeekday: Stats.byWeekday
    readonly property var sessionShape: Stats.sessionShape
    readonly property var streaks: Stats.streaks
    readonly property var libraryStats: Stats.library
    readonly property bool hasRecordedPlay: Number(headline.recordedSeconds || 0) > 0
    // The tallest hour is the scale for the strip, so the busiest time of day always fills it.
    readonly property real peakHourSeconds: {
        let peak = 0
        for (const entry of root.byHour)
            peak = Math.max(peak, Number(entry.seconds) || 0)
        return peak
    }
    readonly property real peakWeekdaySeconds: {
        let peak = 0
        for (const entry of root.byWeekday)
            peak = Math.max(peak, Number(entry.seconds) || 0)
        return peak
    }

    function alpha(color, value) {
        return Qt.rgba(color.r, color.g, color.b, value)
    }
    // The same shape the library uses for a game's playtime, so a figure here reads like the
    // figure on a card: minutes under an hour, then hours and minutes.
    function durationText(seconds) {
        const value = Math.max(0, Number(seconds) || 0)
        if (value < 60) return value > 0 ? "<1m" : "0m"
        const minutes = Math.floor(value / 60)
        if (minutes < 60) return minutes + "m"
        const hours = Math.floor(minutes / 60)
        const rest = minutes % 60
        if (hours >= 1000) return Math.round(hours / 100) * 100 + "h"
        return rest > 0 ? hours + "h " + rest + "m" : hours + "h"
    }
    function countText(value, singular, plural) {
        const count = Number(value) || 0
        return count + " " + (count === 1 ? singular : (plural || singular + "s"))
    }
    function percentText(share) {
        const value = Math.max(0, Math.min(1, Number(share) || 0)) * 100
        if (value > 0 && value < 1) return "<1%"
        return Math.round(value) + "%"
    }
    // The screen is opened from the navigation bar, so it owns the focus when it appears.
    function focusStats() {
        if (!visible) return
        root.forceActiveFocus(Qt.TabFocusReason)
        periodRow.focusCurrent()
    }
    function close() {
        root.libraryRequested()
    }
    function barRatio(seconds, peak) {
        return peak > 0 ? Math.max(0, Math.min(1, (Number(seconds) || 0) / peak)) : 0
    }
    // The hour and the weekday the most recorded time landed in, named rather than drawn, so the
    // strip has a sentence next to it.
    function busiestHourText() {
        let best = -1
        let bestSeconds = 0
        for (const entry of root.byHour) {
            const seconds = Number(entry.seconds) || 0
            if (seconds > bestSeconds) {
                bestSeconds = seconds
                best = Number(entry.hour)
            }
        }
        if (best < 0) return ""
        return (best < 10 ? "0" : "") + best + ":00"
    }
    function busiestWeekdayText() {
        const names = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
        let best = -1
        let bestSeconds = 0
        for (const entry of root.byWeekday) {
            const seconds = Number(entry.seconds) || 0
            if (seconds > bestSeconds) {
                bestSeconds = seconds
                best = Number(entry.weekday)
            }
        }
        return best < 0 || bestSeconds <= 0 ? "" : names[best]
    }
    function weekdayShortName(day) {
        return ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"][Number(day) || 0]
    }
    // Night means after 23:00 and before 05:00, which is the window a person recognises as late
    // rather than a cut chosen to make a number look interesting.
    function lateNightSeconds() {
        let total = 0
        for (const entry of root.byHour) {
            const hour = Number(entry.hour) || 0
            if (hour >= 23 || hour <= 4) total += Number(entry.seconds) || 0
        }
        return total
    }
    function bucketShare(count) {
        const total = Number(root.sessionShape.count) || 0
        return total > 0 ? (Number(count) || 0) / total : 0
    }

    Keys.onEscapePressed: function(event) {
        root.close()
        event.accepted = true
    }

    // One headline figure: what it is, the number, and any qualifier it needs.
    component Stat: ColumnLayout {
        id: stat
        property string label: ""
        property string value: ""
        property string detail: ""
        spacing: 2 * root.scaleFactor
        Text {
            text: stat.label.toUpperCase()
            color: Theme.mutedText
            font.family: Theme.fontFamily
            font.pixelSize: 9 * root.scaleFactor
            font.letterSpacing: 0.7
        }
        Text {
            text: stat.value
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: 24 * root.scaleFactor
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            visible: stat.detail.length > 0
            text: stat.detail
            color: Theme.mutedText
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: 10 * root.scaleFactor
        }
    }

    // A share of the recorded time, drawn as well as written: the bar makes the comparison
    // readable at a glance and the number keeps it honest.
    component ShareRow: ColumnLayout {
        id: row
        property string label: ""
        property real share: 0
        property string detail: ""
        spacing: 3 * root.scaleFactor
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: row.label
                color: Theme.foreground
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 12 * root.scaleFactor
            }
            Text {
                text: row.detail
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: 11 * root.scaleFactor
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 4 * root.scaleFactor
            radius: implicitHeight / 2
            color: root.alpha(Theme.foreground, 0.10)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, row.share))
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: 6 * root.scaleFactor
        color: Theme.mutedText
        font.family: Theme.fontFamily
        font.pixelSize: 10 * root.scaleFactor
        font.letterSpacing: 1.0
    }

    component PeriodRow: RowLayout {
        id: periodRow
        spacing: 8
        function focusCurrent() {
            if (Stats.period === "all") allTimeButton.forceActiveFocus(Qt.TabFocusReason)
            else thisYearButton.forceActiveFocus(Qt.TabFocusReason)
        }
        GlassButton {
            id: thisYearButton
            objectName: "statsThisYearButton"
            text: "THIS YEAR"
            compact: true
            selected: Stats.period !== "all"
            onClicked: Stats.period = "year"
        }
        GlassButton {
            id: allTimeButton
            objectName: "statsAllTimeButton"
            text: "ALL TIME"
            compact: true
            selected: Stats.period === "all"
            onClicked: Stats.period = "all"
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.alpha(Theme.darkerBackground, Theme.surfaceAlpha)

        Flickable {
            id: scroller
            anchors.fill: parent
            anchors.margins: 18 * root.scaleFactor
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: scroller.width
                spacing: 14 * root.scaleFactor

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "STATS"
                            color: Theme.brightForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: 20 * root.scaleFactor
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.0
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Stats.periodLabel
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 11 * root.scaleFactor
                        }
                    }
                    PeriodRow { id: periodRow }
                    GlassButton {
                        objectName: "statsBackButton"
                        text: "BACK"
                        compact: true
                        onClicked: root.close()
                    }
                }

                // The honest window line, repeated on the card. It is not decoration: without it
                // a partial first month reads as a whole year of play.
                Text {
                    Layout.fillWidth: true
                    visible: Stats.windowNote.length > 0
                    text: Stats.windowNote
                    color: Theme.mutedText
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * root.scaleFactor
                }
                Text {
                    Layout.fillWidth: true
                    visible: Stats.error.length > 0
                    text: Stats.error
                    color: Theme.red
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily
                    font.pixelSize: 11 * root.scaleFactor
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 720 * root.scaleFactor ? 4 : 2
                    columnSpacing: 18 * root.scaleFactor
                    rowSpacing: 12 * root.scaleFactor
                    Stat {
                        objectName: "statsRecordedTime"
                        label: "Recorded play"
                        value: root.durationText(headline.recordedSeconds)
                        detail: "Across " + root.countText(headline.recordedSessions, "session")
                    }
                    Stat {
                        objectName: "statsDaysPlayed"
                        label: "Days played"
                        value: String(headline.daysPlayed || 0)
                    }
                    Stat {
                        objectName: "statsGamesPlayed"
                        label: "Games played"
                        value: String(headline.gamesPlayed || 0)
                    }
                    Stat {
                        objectName: "statsLibraryTotal"
                        label: "Library total"
                        value: root.durationText(headline.librarySeconds)
                        detail: "What your launchers and emulators report, all time"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6 * root.scaleFactor
                    visible: root.hasRecordedPlay
                    SectionTitle { text: "MOST PLAYED" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: headline.topGameTitle || ""
                            color: Theme.brightForeground
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 16 * root.scaleFactor
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.durationText(headline.topGameSeconds) + "  ·  "
                                  + root.percentText(headline.topGameShare) + " of recorded play"
                            color: Theme.mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: 12 * root.scaleFactor
                        }
                    }
                }

                // The shape of the play itself: only recorded sessions can answer this, so the
                // sections are absent rather than zeroed until something has been recorded.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.hasRecordedPlay
                    SectionTitle { text: "WHEN YOU PLAY" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3 * root.scaleFactor
                        Repeater {
                            model: root.byHour
                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2 * root.scaleFactor
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 46 * root.scaleFactor
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Math.max(1, parent.height
                                                         * root.barRatio(modelData.seconds,
                                                                         root.peakHourSeconds))
                                        radius: 1.5 * root.scaleFactor
                                        color: (Number(modelData.seconds) || 0) >= root.peakHourSeconds
                                               && root.peakHourSeconds > 0
                                               ? Theme.accent
                                               : root.alpha(Theme.foreground, 0.30)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: (Number(modelData.hour) % 6) === 0 ? String(modelData.hour) : ""
                                    color: Theme.mutedText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 8 * root.scaleFactor
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "statsWhenYouPlay"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 11 * root.scaleFactor
                        text: {
                            const sentences = []
                            if (root.busiestHourText().length > 0)
                                sentences.push("You play most around " + root.busiestHourText())
                            if (root.busiestWeekdayText().length > 0)
                                sentences.push(root.busiestWeekdayText() + " is your busiest day")
                            const night = root.lateNightSeconds()
                            if (night > 0)
                                sentences.push(root.percentText(night / Math.max(1, Number(headline.recordedSeconds)))
                                               + " of it happens after 23:00")
                            return sentences.length > 0 ? sentences.join(". ") + "." : ""
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6 * root.scaleFactor
                        Repeater {
                            model: root.byWeekday
                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2 * root.scaleFactor
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 30 * root.scaleFactor
                                    color: root.alpha(Theme.foreground, 0.07)
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Math.max(1, parent.height
                                                         * root.barRatio(modelData.seconds,
                                                                         root.peakWeekdaySeconds))
                                        radius: 1.5 * root.scaleFactor
                                        color: root.alpha(Theme.accent, 0.85)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.weekdayShortName(modelData.weekday)
                                    color: Theme.mutedText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 8 * root.scaleFactor
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.hasRecordedPlay
                    SectionTitle { text: "SESSION SHAPE" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 720 * root.scaleFactor ? 3 : 1
                        columnSpacing: 18 * root.scaleFactor
                        rowSpacing: 10 * root.scaleFactor
                        Stat {
                            label: "Average session"
                            value: root.durationText(root.sessionShape.averageSeconds)
                        }
                        Stat {
                            label: "Longest session"
                            value: root.durationText(root.sessionShape.longestSeconds)
                            detail: root.sessionShape.longestTitle || ""
                        }
                        Stat {
                            label: "Over two hours"
                            value: String(root.sessionShape.overTwoHours || 0)
                        }
                    }
                    Repeater {
                        model: root.sessionShape.buckets
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: modelData.label
                            share: root.bucketShare(modelData.count)
                            detail: root.countText(modelData.count, "session")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.hasRecordedPlay
                    SectionTitle { text: "STREAKS" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 720 * root.scaleFactor ? 3 : 1
                        columnSpacing: 18 * root.scaleFactor
                        rowSpacing: 10 * root.scaleFactor
                        Stat {
                            objectName: "statsLongestRun"
                            label: "Longest run"
                            value: root.countText(root.streaks.longestRun, "day")
                            detail: "In a row"
                        }
                        Stat {
                            label: "Current run"
                            value: root.countText(root.streaks.currentRun, "day")
                        }
                        Stat {
                            label: "Days off"
                            value: String(root.streaks.daysOff || 0)
                            detail: "Days you did not play"
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.hasRecordedPlay
                    SectionTitle { text: "WHERE THE TIME WENT" }
                    Repeater {
                        model: root.bySystem
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: modelData.name
                            share: modelData.share
                            detail: root.durationText(modelData.seconds) + "  ·  "
                                    + root.percentText(modelData.share)
                        }
                    }
                    SectionTitle { text: "BY LAUNCHER AND EMULATOR" }
                    Repeater {
                        model: root.bySource
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: modelData.name
                            share: modelData.share
                            detail: root.durationText(modelData.seconds) + "  ·  "
                                    + root.countText(modelData.games, "game")
                        }
                    }
                }

                // Nothing recorded yet is stated plainly, with what recording does, rather than
                // a row of zeroes that reads like a fact about the games.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6 * root.scaleFactor
                    visible: !root.hasRecordedPlay
                    SectionTitle { text: "NO RECORDED PLAY YET" }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: 12 * root.scaleFactor
                        text: "The recorder starts counting from the first time Omakade sees a game run, "
                              + "so the hours of day, session lengths and streaks appear here once you play "
                              + "something. Your library's own totals are shown above and are unaffected."
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    SectionTitle { text: "THE LIBRARY" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18 * root.scaleFactor
                        Text {
                            text: root.countText(libraryStats.games, "game")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 12 * root.scaleFactor
                        }
                        Text {
                            text: root.countText(libraryStats.systems, "system")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: 12 * root.scaleFactor
                        }
                    }
                }

                Item { Layout.preferredHeight: 6 * root.scaleFactor }
            }
        }
    }
}

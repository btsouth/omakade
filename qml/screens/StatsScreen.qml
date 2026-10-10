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
    // A television is read from the couch, so the couch treatment is a much larger scale rather
    // than the same layout at the same size: the screen scrolls, so the cost is more scrolling.
    readonly property real scaleFactor: couchMode ? 1.7 : 1
    // Period changes dated figures. The Library view always describes current and lifetime data.
    property int currentView: 0 // Overview, Play patterns, Library snapshot
    // The header and the view tabs do not line up, so Down and Up link them directly.
    readonly property Item currentViewButton: currentView === 1 ? patternsButton
                                            : currentView === 2 ? librarySnapshotButton : overviewButton
    readonly property Item headerReturnButton: couchMode ? periodRow.firstButton : desktopPeriodRow.firstButton
    property bool showAllGames: false
    property bool showAllGenres: false
    property bool showHourValues: false
    property int selectedHour: -1
    // Signals the window closes this view and returns to the library.
    signal homeRequested()
    signal libraryRequested()
    signal statsRequested()
    signal settingsRequested()
    signal couchRequested()

    // The card preview, exposed so the window can route focus and Escape into it while it is open.
    readonly property alias cardPreviewItem: cardPreview
    readonly property bool cardPreviewOpen: cardPreview.visible
    function closeCardPreview() {
        cardPreview.close()
    }

    // Reveal a real control when keyboard or controller focus enters the scrolling content.
    function revealItem(item) {
        if (!item || !scroller) return
        const position = item.mapToItem(scroller.contentItem, 0, 0)
        const margin = 24 * scaleFactor
        if (position.y < scroller.contentY + margin) {
            scroller.contentY = Math.max(0, position.y - margin)
        } else if (position.y + item.height > scroller.contentY + scroller.height - margin) {
            scroller.contentY = Math.min(
                Math.max(0, scroller.contentHeight - scroller.height),
                position.y + item.height - scroller.height + margin)
        }
    }

    readonly property var headline: Stats.headline
    readonly property var bySource: Stats.bySource
    readonly property var bySystem: Stats.bySystem
    readonly property var byHour: Stats.byHour
    readonly property var byWeekday: Stats.byWeekday
    readonly property var topGames: Stats.topGames
    // Everything after the first: the first one is already the hero figure above it.
    readonly property var rankedGames: root.firstEntries(root.topGames, 5)
    readonly property var visibleGames: root.showAllGames ? root.topGames : root.firstEntries(root.topGames, 4)
    readonly property var sessionShape: Stats.sessionShape
    readonly property var streaks: Stats.streaks
    readonly property var achievements: Stats.achievements
    readonly property var backlog: Stats.backlog
    readonly property var libraryStats: Stats.library
    // The screen exists in every window, open or not, and the model does no work until the view is
    // asked for it. So every map key below is a key that is absent most of the time, and reading a
    // list off it unguarded is a TypeError in every other render in the suite, not just this one.
    readonly property var achievementInfo: root.achievements || ({})
    readonly property var backlogInfo: root.backlog || ({})
    readonly property var completions: root.libraryStats.completions || []
    readonly property var genreRows: root.libraryStats.genres || []
    readonly property var topRatedRows: root.libraryStats.topRated || []
    readonly property var sessionBuckets: root.sessionShape.buckets || []
    readonly property var backlogReturns: root.backlogInfo.returnsList || []
    // The lists are trimmed here rather than in the data layer: the screen decides how many of
    // them are worth reading, the figures stay complete for the card and any later view.
    readonly property var topGenres: root.showAllGenres ? root.genreRows : root.firstEntries(root.genreRows, 6)
    readonly property var topRatedGames: root.firstEntries(root.topRatedRows, 3)
    readonly property var completionRows: root.firstEntries(root.completions, 4)
    readonly property int markedGames: {
        let total = 0
        for (const row of root.completions)
            total += Number(row.count) || 0
        return total
    }
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
        if (root.couchMode) periodRow.focusCurrent()
        else desktopPeriodRow.firstButton.forceActiveFocus(Qt.TabFocusReason)
    }
    function selectView(index) {
        currentView = index
        scroller.contentY = 0
        Qt.callLater(function() {
            if (index === 1) patternsButton.forceActiveFocus(Qt.TabFocusReason)
            else if (index === 2) librarySnapshotButton.forceActiveFocus(Qt.TabFocusReason)
            else overviewButton.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    function hourText(hour) {
        const value = Number(hour)
        return (value < 10 ? "0" : "") + value + ":00"
    }
    function selectedHourText() {
        if (selectedHour < 0 || selectedHour >= byHour.length) return "Select an hour to read its recorded time."
        const row = byHour[selectedHour]
        return hourText(row.hour) + " to " + hourText((Number(row.hour) + 1) % 24)
               + ": " + durationText(row.seconds) + " recorded"
    }
    function openCardPreview() {
        cardPreview.open()
    }
    // Used by the headless export: opens the card and writes it to the given path.
    function exportCard(path) {
        cardPreview.open()
        cardPreview.saveTo(path)
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
    function firstEntries(list, count) {
        const trimmed = []
        const source = list || []
        for (let index = 0; index < source.length && index < count; ++index)
            trimmed.push(source[index])
        return trimmed
    }
    // Achievement rarity arrives as the share of players who have it, already in percent, so it
    // is printed as it comes rather than run through the share formatter.
    function rarityText(rarity) {
        const value = Number(rarity) || 0
        return value > 0 ? value.toFixed(1) + "%" : ""
    }
    readonly property bool rarestIsRarity: String(root.achievementInfo.rarest
                                                  && root.achievementInfo.rarest.basis
                                                  ? root.achievementInfo.rarest.basis : "")
                                           === "rarity"
    function rarestDetailText() {
        const rarest = root.achievementInfo.rarest
        if (!rarest || !rarest.title) return ""
        const parts = []
        if (root.rarestIsRarity && Number(rarest.rarity) > 0)
            parts.push(root.rarityText(rarest.rarity) + " of players have it")
        else if (rarest.source === "retroachievements")
            parts.push("RetroAchievements")
        if (rarest.gameTitle && rarest.gameTitle.length > 0) parts.push("in " + rarest.gameTitle)
        return parts.join("  ·  ")
    }
    function completionLabel(status) {
        const labels = {backlog: "Backlog", playing: "Playing", completed: "Finished",
                        abandoned: "Abandoned"}
        const key = String(status || "")
        return labels[key] ? labels[key] : (key.length > 0 ? key : "Not set")
    }
    function completionShare(count) {
        // Scaled against the games that carry a mark, not the whole library: one finished game in
        // a hundred drew a sliver that told the reader nothing.
        const marked = root.markedGames
        return marked > 0 ? (Number(count) || 0) / marked : 0
    }

    Keys.onEscapePressed: function(event) {
        root.close()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        const page = Math.max(120, scroller.height * 0.8)
        if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height,
                scroller.contentY + (event.key === Qt.Key_PageDown ? page : -page)))
            event.accepted = true
        } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            scroller.contentY = event.key === Qt.Key_Home ? 0
                              : Math.max(0, scroller.contentHeight - scroller.height)
            event.accepted = true
        }
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
            font.pixelSize: UiMetrics.label * root.scaleFactor
            font.letterSpacing: 0.7
        }
        Text {
            text: stat.value
            color: Theme.brightForeground
            font.family: Theme.fontFamily
            font.pixelSize: UiMetrics.heading * root.scaleFactor
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            // Kept in the layout even when empty so a row of figures shares one baseline: hiding it
            // moved some labels a line above their neighbours.
            text: stat.detail
            color: Theme.mutedText
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: UiMetrics.supporting * root.scaleFactor
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
                wrapMode: Text.Wrap
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.body * root.scaleFactor
            }
            Text {
                text: row.detail
                color: Theme.mutedText
                font.family: Theme.fontFamily
                font.pixelSize: UiMetrics.supporting * root.scaleFactor
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
        color: Theme.brightForeground
        font.family: Theme.fontFamily
        font.pixelSize: UiMetrics.section * root.scaleFactor
        font.weight: Font.DemiBold
    }

    component PeriodRow: RowLayout {
        id: periodRow
        property string objectNamePrefix: ""
        spacing: 8
        // The last button in the chip chain, so the control beside this component can link to it:
        // an id declared inside an inline component is not visible from the file around it.
        readonly property alias firstButton: thisYearButton
        readonly property alias lastButton: makeCardButton
        function focusCurrent() {
            if (Stats.period === "all") allTimeButton.forceActiveFocus(Qt.TabFocusReason)
            else thisYearButton.forceActiveFocus(Qt.TabFocusReason)
        }
        GlassButton {
            id: thisYearButton
            property Item controllerDownTarget: root.currentViewButton
            objectName: periodRow.objectNamePrefix === "" ? "statsThisYearButton"
                                                           : periodRow.objectNamePrefix + "ThisYearButton"
            property Item controllerUpTarget: root.couchMode ? null : statsAppHeader.statsButton
            text: "THIS YEAR"
            compact: true
            selected: Stats.period !== "all"
            onClicked: Stats.period = "year"
            KeyNavigation.right: allTimeButton
            KeyNavigation.up: root.couchMode ? null : statsAppHeader.statsButton
        }
        GlassButton {
            id: allTimeButton
            property Item controllerDownTarget: root.currentViewButton
            objectName: periodRow.objectNamePrefix === "" ? "statsAllTimeButton"
                                                           : periodRow.objectNamePrefix + "AllTimeButton"
            property Item controllerUpTarget: root.couchMode ? null : statsAppHeader.statsButton
            text: "ALL TIME"
            compact: true
            selected: Stats.period === "all"
            onClicked: Stats.period = "all"
            KeyNavigation.left: thisYearButton
            KeyNavigation.right: makeCardButton
            KeyNavigation.up: root.couchMode ? null : statsAppHeader.statsButton
        }
        GlassButton {
            id: makeCardButton
            property Item controllerDownTarget: root.currentViewButton
            objectName: "statsMakeCardButton"
            property Item controllerUpTarget: root.couchMode ? null : statsAppHeader.statsButton
            property Item controllerRightTarget: root.couchMode ? statsBackButton : statsAppHeader.settingsButton
            text: "MAKE A CARD"
            compact: true
            onClicked: root.openCardPreview()
            KeyNavigation.left: allTimeButton
            KeyNavigation.right: root.couchMode ? statsBackButton : statsAppHeader.settingsButton
            KeyNavigation.up: root.couchMode ? null : statsAppHeader.statsButton
        }
    }

    Rectangle {
        id: statsSurface
        anchors.fill: parent
        readonly property real desktopMargin: Math.max(22, width * 0.032)
        color: root.couchMode ? Theme.darkerBackground
                              : root.alpha(Theme.darkerBackground, Theme.surfaceAlpha)

        AppHeader {
            id: statsAppHeader
            objectName: "statsAppHeader"
            objectNamePrefix: "stats"
            current: "stats"
            contentFocusTarget: root.couchMode ? periodRow.firstButton : desktopPeriodRow.firstButton
            visible: !root.couchMode
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 24
            anchors.leftMargin: statsSurface.desktopMargin
            anchors.rightMargin: statsSurface.desktopMargin
            height: implicitHeight
            onHomeRequested: root.homeRequested()
            onLibraryRequested: root.libraryRequested()
            onStatsRequested: root.statsRequested()
            onSettingsRequested: root.settingsRequested()
            onCouchRequested: root.couchRequested()
        }
        RowLayout {
            id: desktopStatsHeading
            objectName: "desktopStatsHeading"
            visible: !root.couchMode
            anchors.top: statsAppHeader.bottom
            anchors.topMargin: 20
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(1200, parent.width - 2 * statsSurface.desktopMargin)
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "STATS"
                    color: Theme.brightForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.0
                }
                Text {
                    Layout.fillWidth: true
                    text: Stats.periodLabel
                    color: Theme.mutedText
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillWidth: true }
            PeriodRow { id: desktopPeriodRow }
        }

        Flickable {
            id: scroller
            objectName: "statsScroller"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.top: root.couchMode ? parent.top : desktopStatsHeading.bottom
            anchors.leftMargin: root.couchMode ? 18 * root.scaleFactor : 0
            anchors.rightMargin: root.couchMode ? 18 * root.scaleFactor : 0
            anchors.topMargin: root.couchMode ? 18 * root.scaleFactor : 14
            anchors.bottomMargin: root.couchMode ? 18 * root.scaleFactor : 16
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                objectName: "statsPageContent"
                width: Math.min(scroller.width - 32 * root.scaleFactor, 1200)
                x: (scroller.width - width) / 2
                spacing: UiMetrics.sectionGap * root.scaleFactor

                RowLayout {
                    visible: root.couchMode
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
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
                            font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        }
                    }
                    PeriodRow { id: periodRow; objectNamePrefix: "couch" }
                    GlassButton {
                        id: statsBackButton
                        objectName: "statsBackButton"
                        property Item controllerDownTarget: root.currentViewButton
                        text: "BACK"
                        compact: true
                        onClicked: root.close()
                        KeyNavigation.left: periodRow.lastButton
                    }
                }

                // A read that failed is stated plainly rather than shown as zeroes, which would
                // read as a fact about the library.
                Text {
                    Layout.fillWidth: true
                    visible: Stats.error.length > 0
                    wrapMode: Text.Wrap
                    text: Stats.error
                    color: Theme.red
                    font.family: Theme.fontFamily
                    font.pixelSize: UiMetrics.body * root.scaleFactor
                }

                RowLayout {
                    objectName: "statsViewSelectors"
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    GlassButton {
                        id: overviewButton
                        property Item controllerUpTarget: root.headerReturnButton
                        objectName: "statsOverviewButton"
                        text: "OVERVIEW"
                        compact: true
                        selected: root.currentView === 0
                        Accessible.name: "Stats overview"
                        onClicked: root.selectView(0)
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                    GlassButton {
                        id: patternsButton
                        property Item controllerUpTarget: root.headerReturnButton
                        objectName: "statsPatternsButton"
                        text: "PLAY PATTERNS"
                        compact: true
                        selected: root.currentView === 1
                        Accessible.name: "Play patterns"
                        onClicked: root.selectView(1)
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                    GlassButton {
                        id: librarySnapshotButton
                        property Item controllerUpTarget: root.headerReturnButton
                        objectName: "statsLibrarySnapshotButton"
                        text: "LIBRARY SNAPSHOT"
                        compact: true
                        selected: root.currentView === 2
                        Accessible.name: "Library snapshot, current and lifetime totals"
                        onClicked: root.selectView(2)
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                }

                // Coverage describes dated figures. It is never used to date library totals.
                Text {
                    Layout.fillWidth: true
                    visible: root.currentView !== 2 && Stats.windowNote.length > 0
                    text: Stats.windowNote
                    color: Theme.mutedText
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily
                    font.pixelSize: UiMetrics.supporting * root.scaleFactor
                }
                GridLayout {
                    visible: root.currentView === 0 && Stats.error.length === 0
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
                        label: "Sessions"
                        value: String(headline.recordedSessions || 0)
                        detail: "Recorded in this period"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6 * root.scaleFactor
                    visible: root.currentView === 0 && root.hasRecordedPlay && Stats.error.length === 0
                    SectionTitle { text: "MOST PLAYED" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3 * root.scaleFactor
                        Text {
                            Layout.fillWidth: true
                            text: headline.topGameTitle || ""
                            color: Theme.brightForeground
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.section * root.scaleFactor
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.durationText(headline.topGameSeconds) + "  ·  "
                                  + root.percentText(headline.topGameShare) + " of recorded play"
                            color: Theme.mutedText
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        }
                    }
                    // The rest of your most played, so the section is a ranking rather than a
                    // single line. The first one is already the hero figure above.
                    Repeater {
                        model: root.visibleGames.slice(1)
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: modelData.title || ""
                            share: Number(modelData.share) || 0
                            detail: root.durationText(modelData.seconds)
                        }
                    }
                    GlassButton {
                        visible: root.topGames.length > 4
                        compact: true
                        text: root.showAllGames ? "SHOW LESS" : "SEE ALL GAMES"
                        Accessible.name: text
                        onClicked: root.showAllGames = !root.showAllGames
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.currentView === 0 && root.hasRecordedPlay && Stats.error.length === 0
                    spacing: 6 * root.scaleFactor
                    SectionTitle { text: "WEEKLY PATTERN" }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.busiestWeekdayText().length > 0
                              ? root.busiestWeekdayText() + " has the most recorded play. Explore Play patterns for each day and hour."
                              : "No weekday pattern is available yet."
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * root.scaleFactor
                    }
                    GlassButton {
                        compact: true
                        text: "PLAY PATTERNS"
                        onClicked: root.selectView(1)
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                }

                // The shape of the play itself: only recorded sessions can answer this, so the
                // sections are absent rather than zeroed until something has been recorded.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.currentView === 1 && root.hasRecordedPlay && Stats.error.length === 0
                    SectionTitle { text: "WHEN YOU PLAY" }
                    Text {
                        Layout.fillWidth: true
                        text: "Recorded time by hour of day"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * root.scaleFactor
                    }
                    RowLayout {
                        objectName: "statsHourChart"
                        uniformCellSizes: true
                        Layout.fillWidth: true
                        spacing: 3 * root.scaleFactor
                        Repeater {
                            model: root.byHour
                            ColumnLayout {
                                required property var modelData
                                objectName: "statsHourBin"
                                Accessible.name: root.hourText(modelData.hour) + ", "
                                                 + root.durationText(modelData.seconds) + " recorded"
                                Accessible.role: Accessible.StaticText
                                Layout.fillWidth: true
                                spacing: 2 * root.scaleFactor
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 64 * root.scaleFactor
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
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.selectedHour = Number(modelData.hour)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: (Number(modelData.hour) % 6) === 0 ? String(modelData.hour) : ""
                                    color: Theme.mutedText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: UiMetrics.label * root.scaleFactor
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedHourText()
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * root.scaleFactor
                    }
                    GlassButton {
                        compact: true
                        text: root.showHourValues ? "HIDE HOUR VALUES" : "SHOW HOUR VALUES"
                        Accessible.name: text
                        onClicked: root.showHourValues = !root.showHourValues
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        visible: root.showHourValues
                        columns: root.width < 720 * root.scaleFactor ? 2 : 4
                        columnSpacing: 16 * root.scaleFactor
                        rowSpacing: 6 * root.scaleFactor
                        Repeater {
                            model: root.byHour
                            Text {
                                required property var modelData
                                text: root.hourText(modelData.hour) + "  " + root.durationText(modelData.seconds)
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: UiMetrics.supporting * root.scaleFactor
                            }
                        }
                    }
                    Text {
                        objectName: "statsWhenYouPlay"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * root.scaleFactor
                        text: {
                            const sentences = []
                            if (root.busiestHourText().length > 0)
                                sentences.push("You play most around " + root.busiestHourText())
                            if (root.busiestWeekdayText().length > 0)
                                sentences.push(root.busiestWeekdayText() + " is your busiest day")
                            const night = root.lateNightSeconds()
                            if (night > 0)
                                sentences.push(root.percentText(night / Math.max(1, Number(headline.recordedSeconds)))
                                               + " of it happens after 23:00 and before 05:00")
                            return sentences.length > 0 ? sentences.join(". ") + "." : ""
                        }
                    }
                    RowLayout {
                        objectName: "statsWeekdayChart"
                        uniformCellSizes: true
                        Layout.fillWidth: true
                        spacing: 6 * root.scaleFactor
                        Repeater {
                            model: root.byWeekday
                            ColumnLayout {
                                required property var modelData
                                objectName: "statsWeekdayBin"
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
                                    font.pixelSize: UiMetrics.label * root.scaleFactor
                                }
                            }
                        }
                    }
                    Repeater {
                        model: root.byWeekday
                        Text {
                            required property var modelData
                            visible: root.showHourValues
                            text: root.weekdayShortName(modelData.weekday) + "  "
                                  + root.durationText(modelData.seconds)
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.currentView === 1 && root.hasRecordedPlay && Stats.error.length === 0
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
                        model: root.sessionBuckets
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
                    visible: root.currentView === 1 && root.hasRecordedPlay && Stats.error.length === 0
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
                            detail: "Days off while the recorder was running"
                        }
                    }
                }

                // Unlocks carry dates. Completion marks do not and appear in Library snapshot.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.currentView === 0 && Stats.error.length === 0
                             && Number(achievementInfo.unlockedTotal || 0) > 0
                    SectionTitle { text: "ACHIEVEMENTS IN THIS PERIOD" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 720 * root.scaleFactor ? 3 : 1
                        columnSpacing: 18 * root.scaleFactor
                        rowSpacing: 10 * root.scaleFactor
                        Stat {
                            objectName: "statsUnlockedInPeriod"
                            label: "Unlocked in this period"
                            value: String(achievementInfo.unlockedInPeriod || 0)
                            detail: String(achievementInfo.unlockedTotal || 0) + " unlocked in all"
                        }
                        Stat {
                            label: "Unlock rate (all time)"
                            value: root.percentText(achievementInfo.rate || 0)
                            detail: root.countText(achievementInfo.known, "achievement") + " known"
                        }
                        Stat {
                            objectName: "statsRarest"
                            // "Rarest" only where the source publishes a rarity. RetroAchievements
                            // rows carry none, so a period with unlocks would otherwise read "None
                            // yet"; those fall back to the newest unlock, labelled as what it is.
                            label: root.rarestIsRarity ? "Rarest in this period"
                                                       : "Latest in this period"
                            value: (achievementInfo.rarest && achievementInfo.rarest.title)
                                   ? achievementInfo.rarest.title : "None yet"
                            detail: root.rarestDetailText()
                        }
                    }
                }

                // Habits read against the whole history, which is what makes "one and done" and
                // "you came back after a gap" answerable at all.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.currentView === 1 && root.hasRecordedPlay && Stats.error.length === 0
                    SectionTitle { text: "HABITS" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 720 * root.scaleFactor ? 3 : 1
                        columnSpacing: 18 * root.scaleFactor
                        rowSpacing: 10 * root.scaleFactor
                        Stat {
                            objectName: "statsFirstTimePlays"
                            label: "First-time plays"
                            value: String(backlogInfo.firstTimeGames || 0)
                            detail: "Games you started for the first time"
                        }
                        Stat {
                            label: "One and done"
                            value: String(backlogInfo.oneAndDone || 0)
                            detail: "Played once and never again"
                        }
                        Stat {
                            label: "Returns"
                            value: root.countText(backlogInfo.returns, "comeback")
                            detail: "After 30 days or more"
                        }
                    }
                    Repeater {
                        model: root.backlogReturns
                        Text {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.body * root.scaleFactor
                            text: "You came back to " + modelData.title + " after "
                                  + root.countText(modelData.gapDays, "day") + " away."
                        }
                    }
                }

                // Nothing recorded yet is stated plainly, with what recording does, rather than
                // a row of zeroes that reads like a fact about the games.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6 * root.scaleFactor
                    visible: root.currentView !== 2 && !root.hasRecordedPlay && Stats.error.length === 0
                    SectionTitle { text: "NO RECORDED PLAY YET" }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.body * root.scaleFactor
                        text: "The recorder starts counting from the first time Omakade sees a game run, "
                              + "so the hours of day, session lengths and streaks appear here once you play "
                              + "something. Library totals remain available in Library snapshot."
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8 * root.scaleFactor
                    visible: root.currentView === 2 && Stats.error.length === 0
                    SectionTitle { text: "LIBRARY NOW" }
                    Text {
                        Layout.fillWidth: true
                        text: "Current collection and lifetime totals stay fixed when the period changes. "
                              + "System, source and genre rows below describe recorded play in the selected period."
                        color: Theme.mutedText
                        wrapMode: Text.Wrap
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                    }
                    Stat {
                        objectName: "statsLibraryTotal"
                        label: "Lifetime library total"
                        value: root.durationText(headline.librarySeconds)
                        detail: "Reconciled launcher and recorded totals; no period is inferred for imported time."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18 * root.scaleFactor
                        Text {
                            text: root.countText(libraryStats.games, "game")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.body * root.scaleFactor
                        }
                        Text {
                            text: root.countText(libraryStats.systems, "console")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.body * root.scaleFactor
                        }
                    }
                    SectionTitle { text: "CURRENT COMPLETION MARKS" }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        text: root.completionRows.length === 0
                              ? "No games are marked finished, playing or abandoned yet."
                              : "Share of " + root.countText(root.markedGames, "marked game")
                                + ". These marks have no completion date."
                    }
                    Repeater {
                        model: root.completionRows
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: root.completionLabel(modelData.status)
                            share: root.completionShare(modelData.count)
                            detail: root.countText(modelData.count, "game")
                        }
                    }
                    Text {
                        objectName: "statsSystemScope"
                        Layout.fillWidth: true
                        visible: root.bySystem.length > 0
                        text: "SYSTEMS BY RECORDED PLAY IN " + Stats.periodLabel.toUpperCase()
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        wrapMode: Text.Wrap
                    }
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
                    Text {
                        objectName: "statsSourceScope"
                        Layout.fillWidth: true
                        visible: root.bySource.length > 0
                        text: "SOURCES BY RECORDED PLAY IN " + Stats.periodLabel.toUpperCase()
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        wrapMode: Text.Wrap
                    }
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
                    // Genres arrive through the metadata layer and only for games whose identity is
                    // confirmed, so an unidentified game contributes no genre rather than a guess.
                    Text {
                        objectName: "statsGenreScope"
                        Layout.fillWidth: true
                        visible: root.topGenres.length > 0
                        text: "GENRES BY RECORDED PLAY IN " + Stats.periodLabel.toUpperCase()
                              + "  ·  GENRES CAN OVERLAP"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        wrapMode: Text.Wrap
                    }
                    GlassButton {
                        visible: root.genreRows.length > 6
                        compact: true
                        text: root.showAllGenres ? "SHOW FEWER GENRES" : "SEE ALL GENRES"
                        onClicked: root.showAllGenres = !root.showAllGenres
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                    Repeater {
                        model: root.topGenres
                        ShareRow {
                            required property var modelData
                            Layout.fillWidth: true
                            label: modelData.name
                            share: Number(modelData.share) || 0
                            detail: root.durationText(modelData.seconds)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.topRatedGames.length > 0
                        text: "TOP RATED  ·  SCORES FROM IGDB"
                        color: Theme.mutedText
                        font.family: Theme.fontFamily
                        font.pixelSize: UiMetrics.supporting * root.scaleFactor
                        font.letterSpacing: 0.6
                    }
                    Repeater {
                        model: root.topRatedGames
                        Text {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: UiMetrics.body * root.scaleFactor
                            text: modelData.title + "  ·  " + Number(modelData.rating).toFixed(0)
                                  + (Number(modelData.ratingCount) > 0
                                     ? "  ·  " + root.countText(modelData.ratingCount, "rating")
                                     : "")
                        }
                    }
                }

                Item { Layout.preferredHeight: 6 * root.scaleFactor }
            }
        }
    }

    // The card preview sits above the screen and owns the focus while it is open, so Escape and
    // the controller reach its buttons rather than the ones behind it.
    YearInReviewPreview {
        id: cardPreview
        objectName: "yearInReviewPreviewHost"
        anchors.fill: parent
        visible: false
        couchMode: root.couchMode
    }
}

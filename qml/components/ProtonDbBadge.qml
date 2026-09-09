import QtQuick
import QtQuick.Controls

Text {
    id: root
    property string gameSource: ""
    property string appId: ""
    property bool compact: false
    property bool fetchEnabled: parent ? parent.visible : false
    readonly property var entry: {
        if (typeof ProtonDB === "undefined" || !ProtonDB) return ({})
        const revision = ProtonDB.revision
        return ProtonDB.summary(gameSource, appId)
    }
    readonly property string tier: entry.tier || ""
    readonly property string tierLabel: tier === "pending" ? (entry.total > 0 ? "Pending" : "No reports")
        : tier.length > 0 ? tier.charAt(0).toUpperCase() + tier.slice(1) : ""
    text: (compact ? "" : "ProtonDB · ") + tierLabel + (entry.stale ? " *" : "")
    textFormat: Text.PlainText
    visible: tier.length > 0
    color: Theme.mutedText
    font.family: Theme.fontFamily
    font.pixelSize: compact ? 10 : 12
    Accessible.name: "ProtonDB " + tierLabel
    Accessible.description: details
    readonly property string details: "ProtonDB community reports, not a compatibility guarantee. "
        + (entry.total || 0) + " reports. Cached " + (entry.updated || "")
        + (entry.stale ? ". Older than seven days; refresh unavailable or pending." : ".")
    ToolTip.visible: hover.hovered && visible
    ToolTip.text: details
    ToolTip.delay: 500
    HoverHandler { id: hover }
    function refresh() {
        if (fetchEnabled && typeof ProtonDB !== "undefined" && ProtonDB)
            ProtonDB.request(gameSource, appId)
    }
    onGameSourceChanged: refresh()
    onAppIdChanged: refresh()
    onFetchEnabledChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: typeof ProtonDB !== "undefined" ? ProtonDB : null
        function onChanged() { root.refresh() }
    }
    Timer {
        interval: 30000
        repeat: true
        running: root.fetchEnabled && typeof ProtonDB !== "undefined" && ProtonDB && ProtonDB.enabled
        onTriggered: root.refresh()
    }
}

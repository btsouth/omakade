import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property real availableHeight: 0
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var perf: d.performance || {}
  readonly property bool hooked: !!perf.mangohud
  property var rows: hooked ? [[hud], [limit], [profile]] : [[setup], [profile]]

  spacing: g.rhythm(root, 24)

  // Frame rate and frame times, when MangoHud is in the game.
  Column {
    visible: root.hooked
    width: parent.width
    spacing: root.g.s(6)
    Row {
      spacing: root.g.s(10)
      Label { g: root.g; role: "hero"; font.pixelSize: root.g.f(64); text: String(root.perf.fps || 0) }
      Column {
        anchors.bottom: parent.bottom; anchors.bottomMargin: root.g.s(8)
        Label { g: root.g; role: "caps"; text: "fps" }
        Label { g: root.g; role: "small"; text: (root.perf.frametime || 0).toFixed(1) + " ms per frame" }
      }
    }
    Sparkline {
      g: root.g
      width: parent.width; height: root.g.s(180)
      values: root.perf.frametimes || []
      ceiling: 33.3
    }
    Row {
      width: parent.width
      Label { g: root.g; role: "caption"; text: "Last 10 seconds"; width: parent.width / 2 }
      Label { g: root.g; role: "caption"; text: "1% low " + (root.perf.low1 || "-") + " fps"; width: parent.width / 2; horizontalAlignment: Text.AlignRight }
    }
  }

  // Without MangoHud there is no frame rate to show: say how to get it.
  Action {
    id: setup
    visible: !root.hooked
    g: root.g; width: parent.width
    variant: "tile"; icon: root.g.icon.gauge
    title: "Frame rate needs MangoHud"
    detail: root.perf.setupHint || "Omakade can add it the next time this game starts"
    onTriggered: root.act("enable-mangohud", null)
  }

  Grid {
    width: parent.width
    columns: 2
    columnSpacing: root.g.s(18)
    rowSpacing: root.g.s(14)
    Repeater {
      model: root.perf.stats || []
      delegate: Meter {
        required property var modelData
        g: root.g
        width: (root.width - root.g.s(18)) / 2
        label: modelData.label
        value: modelData.value
        progress: modelData.progress
        fill: modelData.progress > 0.9 ? root.g.urgent : root.g.accent
      }
    }
  }

  Column {
    width: parent.width
    spacing: root.g.s(12)
    ChoiceRow {
      id: hud
      visible: root.hooked
      g: root.g; width: parent.width
      label: "On-screen overlay"
      options: [{ value: "off", label: "Off" }, { value: "fps", label: "FPS" }, { value: "frametime", label: "+ Time" }, { value: "full", label: "Full" }]
      value: root.perf.hud || "off"
      onChosen: function(v) { root.act("hud", v) }
    }
    ChoiceRow {
      id: limit
      visible: root.hooked
      g: root.g; width: parent.width
      label: "Frame limit"
      detail: root.perf.refresh ? "Display " + root.perf.refresh + " Hz" : ""
      options: [{ value: "0", label: "Off" }, { value: "30", label: "30" }, { value: "40", label: "40" }, { value: "60", label: "60" }, { value: "120", label: "120" }]
      value: String(root.perf.limit || 0)
      onChosen: function(v) { root.act("limit", Number(v)) }
    }
    ChoiceRow {
      id: profile
      g: root.g; width: parent.width
      label: "Power profile"
      options: [{ value: "power-saver", label: "Saver" }, { value: "balanced", label: "Balanced" }, { value: "performance", label: "Performance" }]
      value: root.perf.profile || "balanced"
      onChosen: function(v) { root.act("profile", v) }
    }
  }
}

import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var perf: d.performance || {}
  readonly property bool hooked: !!perf.mangohud
  property var rows: hooked ? [[hud], [limit], [profile]] : [[setup], [profile]]

  readonly property var stats: perf.stats || []
  readonly property int statRows: Math.ceil(stats.length / 2)
  readonly property real tileHeight: g.s(hooked ? 84 : 112)
  hero: hooked ? frameHero : null
  heroMinimum: hooked && root.perf.fps !== undefined ? fpsRow.height + g.s(180) + graphCaption.height + g.s(12) : 0
  heroMaximum: heroMinimum + g.s(72)

  // Frame rate and frame times, when MangoHud is in the game.
  Column {
    id: frameHero
    visible: root.hooked && root.perf.fps !== undefined
    width: parent.width
    spacing: root.g.s(6)
    Row {
      id: fpsRow
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
      width: parent.width; height: root.g.s(180) + root.heroExtra
      values: root.perf.frametimes || []
      ceiling: 33.3
    }
    Row {
      id: graphCaption
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

  // Live system load as large tiles; without MangoHud they are the page.
  Flow {
    id: statGrid
    width: parent.width
    spacing: root.g.s(10)
    Repeater {
      model: root.stats
      delegate: Rectangle {
        required property var modelData
        required property int index
        // Temperatures arrive without a fraction: read the bar from the degrees.
        readonly property real fraction: modelData.progress > 0 ? modelData.progress
          : /°C$/.test(modelData.value || "") ? Math.min(1, parseFloat(modelData.value) / 100) : 0
        readonly property bool hot: fraction > 0.9
        // An odd last tile spans the row instead of leaving a hole.
        width: index === root.stats.length - 1 && root.stats.length % 2 ? root.width : (root.width - root.g.s(10)) / 2
        height: root.tileHeight
        radius: root.g.radius; color: root.g.well
        border.width: 1; border.color: root.g.line
        Column {
          anchors.fill: parent; anchors.margins: root.g.s(14)
          spacing: root.g.s(4)
          Label { g: root.g; role: "caps"; text: modelData.label.replace("temperature", "temp"); width: parent.width; elide: Text.ElideRight }
          // "20.2 / 31.1 GB" reads as a big "20.2 GB" over "of 31.1 GB".
          readonly property var parts: String(modelData.value || "").split(" / ")
          Label { g: root.g; role: "hero"; font.pixelSize: root.g.f(root.hooked ? 22 : 30); text: parent.parts.length === 2 ? parent.parts[0] + " " + parent.parts[1].replace(/^[\d.]+\s*/, "") : modelData.value; color: hot ? root.g.urgent : root.g.foreground; width: parent.width; elide: Text.ElideRight }
          Label { g: root.g; role: "caption"; visible: parent.parts.length === 2; text: visible ? "of " + parent.parts[1] : ""; width: parent.width }
        }
        Rectangle {
          anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
          anchors.margins: root.g.s(14)
          height: Math.max(3, root.g.s(4)); radius: height / 2; color: root.g.track
          Rectangle { height: parent.height; radius: parent.radius; color: hot ? root.g.urgent : root.g.accent; width: Math.max(height, parent.width * parent.parent.fraction)
            Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } } }
        }
      }
    }
  }

  Column {
    width: parent.width
    spacing: root.g.s(16)
    ChoiceRow {
      id: hud
      visible: root.hooked
      g: root.g; width: parent.width
      label: "On-screen overlay"
      detail: root.perf.nextLaunch ? "Visibility changes now. Detail applies next launch." : ""
      options: [{ value: "off", label: "Off" }, { value: "fps", label: "FPS" }, { value: "frametime", label: "+ Time" }, { value: "full", label: "Full" }]
      value: root.perf.hud || "off"
      onChosen: function(v) { root.act("hud", v) }
    }
    ChoiceRow {
      id: limit
      visible: root.hooked
      g: root.g; width: parent.width
      label: "Frame limit"
      detail: root.perf.nextLaunch ? "Applies next launch" : ""
      options: [{ value: "0", label: "Off" }, { value: "30", label: "30" }, { value: "40", label: "40" }, { value: "60", label: "60" }, { value: "120", label: "120" }, {value: String(root.perf.refresh || 60), label: "Display"}]
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

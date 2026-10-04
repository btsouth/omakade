import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property real availableHeight: 0
  property string family: "xbox"
  signal act(string name, var arg)
  signal requestedFocus(var item)

  readonly property var sys: d.system || {}
  readonly property var profiles: ["power-saver", "balanced", "performance"]
  readonly property string profile: (d.performance || {}).profile || "balanced"
  property var rows: [[wifi, bluetooth], [dnd, power], [light, suspend], [brightness], [library], [desktop]]
  spacing: g.rhythm(root, 20)

  Grid {
    width: parent.width; columns: 2
    columnSpacing: root.g.s(12); rowSpacing: root.g.s(12)
    Action {
      id: wifi
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.wifi; title: "Wi-Fi"
      detail: root.sys.wifi ? (root.sys.ssid || "Connected") : "Off"
      selected: !!root.sys.wifi
      onTriggered: root.act("wifi", !root.sys.wifi)
    }
    Action {
      id: bluetooth
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.bluetooth; title: "Bluetooth"
      detail: root.sys.bluetooth ? "On · Controller & headset" : "Off"
      selected: !!root.sys.bluetooth
      onTriggered: root.act("bluetooth", !root.sys.bluetooth)
    }
    Action {
      id: dnd
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.dnd; title: "Do not disturb"
      detail: root.sys.dnd ? "On · Notifications held" : "Off"
      selected: !!root.sys.dnd
      onTriggered: root.act("dnd", !root.sys.dnd)
    }
    Action {
      id: power
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.gauge; title: "Power profile"
      detail: root.profile === "power-saver" ? "Saver" : root.profile === "performance" ? "Performance" : "Balanced"
      onTriggered: root.act("profile", root.profiles[(root.profiles.indexOf(root.profile) + 1) % 3])
    }
    Action {
      id: light
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.brightness; title: "Brightness"
      detail: Math.round((root.sys.brightness || 0) * 100) + "%"
      onTriggered: root.requestedFocus(brightness)
    }
    Action {
      id: suspend
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.sleep; title: "Suspend"
      detail: "Wake where you left off"
      onTriggered: root.act("suspend", null)
    }
  }

  SliderRow {
    id: brightness
    g: root.g; width: parent.width
    icon: root.g.icon.brightness; title: "Display brightness"
    value: root.sys.brightness || 0
    onMoved: function(v) { root.act("brightness", v) }
  }
  Section { g: root.g; text: "Leave the game"; width: parent.width }
  Action {
    id: library
    g: root.g; width: parent.width; height: root.g.s(58)
    icon: root.g.icon.library; title: "Game library"; detail: "The game keeps running"; chevron: true
    onTriggered: root.act("library", null)
  }
  Action {
    id: desktop
    g: root.g; width: parent.width; height: root.g.s(58)
    icon: root.g.icon.desktop; title: "Return to desktop"; detail: "Come back with the Guide button"; chevron: true
    onTriggered: root.act("desktop", null)
  }
}

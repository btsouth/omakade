import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)
  signal requestedFocus(var item)

  readonly property var sys: d.system || {}
  readonly property var profiles: ["power-saver", "balanced", "performance"]
  readonly property string profile: (d.performance || {}).profile || ""
  // brightness is null or undefined without a backlight (desktops): no control then.
  readonly property bool backlight: sys.brightness !== undefined && sys.brightness !== null
  property var rows: [[wifi, bluetooth], [dnd, power], [brightness], [couch], [library], [desktop], [suspend]]

  Grid {
    width: parent.width; columns: 2
    columnSpacing: root.g.s(12); rowSpacing: root.g.s(12)
    Action {
      id: wifi
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.wifi; title: "Wi-Fi"
      detail: root.sys.wifi === undefined ? "Unavailable" : root.sys.wifi ? (root.sys.ssid || "Connected") : "Off"
      selected: !!root.sys.wifi
      onTriggered: root.act("wifi", !root.sys.wifi)
    }
    Action {
      id: bluetooth
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.bluetooth; title: "Bluetooth"
      detail: root.sys.bluetooth === undefined ? "Unavailable" : root.sys.bluetooth ? "On" : "Off"
      selected: !!root.sys.bluetooth
      onTriggered: root.act("bluetooth", !root.sys.bluetooth)
    }
    Action {
      id: dnd
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.dnd; title: "Do not disturb"
      detail: root.sys.dnd === undefined ? "Unavailable" : root.sys.dnd ? "On" : "Off"
      selected: !!root.sys.dnd
      onTriggered: root.act("dnd", !root.sys.dnd)
    }
    Action {
      id: power
      g: root.g; width: (root.width - root.g.s(12)) / 2
      variant: "tile"; icon: root.g.icon.gauge; title: "Power profile"
      detail: root.profile === "" ? "Unavailable" : root.profile === "power-saver" ? "Saver" : root.profile === "performance" ? "Performance" : "Balanced"
      onTriggered: root.act("profile", root.profiles[(root.profiles.indexOf(root.profile) + 1) % 3])
    }
  }

  SliderRow {
    id: brightness
    g: root.g; width: parent.width
    visible: root.backlight
    icon: root.g.icon.brightness; title: "Display brightness"
    value: root.sys.brightness || 0
    onMoved: function(v) { root.act("brightness", v) }
  }
  ChoiceRow {
    id: couch
    g: root.g; width: parent.width; label: "Couch scale"
    options: [{value: "auto", label: "Auto"}, {value: "1", label: "1x"}, {value: "1.25", label: "1.25x"}, {value: "1.5", label: "1.5x"}, {value: "2", label: "2x"}]
    value: root.sys.couch || "auto"
    onChosen: v => root.act("couch-scale", v)
  }
  bottomBlock: Column {
    width: parent.width; spacing: root.g.s(8)
    Section { g: root.g; text: "Leave the game"; width: parent.width }
    Column {
      width: parent.width; spacing: root.g.s(6)
      Action {
        id: library
        visible: root.d.libraryAvailable !== false
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
      Action {
        id: suspend
        g: root.g; width: parent.width; height: root.g.s(58)
        icon: root.g.icon.sleep; title: "Suspend"; detail: "Sleep now"; chevron: true
        onTriggered: root.act("suspend", null)
      }
    }
  }
}

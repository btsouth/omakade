import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var sys: d.system || {}
  property var rows: [[brightness], [wifi], [bluetooth], [dnd], [library], [desktop], [suspend]]

  spacing: g.s(2)

  SliderRow {
    id: brightness
    g: root.g; width: parent.width
    icon: root.g.icon.brightness
    title: "Brightness"
    value: root.sys.brightness || 0
    onMoved: function(v) { root.act("brightness", v) }
  }
  ToggleRow {
    id: wifi
    g: root.g; width: parent.width
    icon: root.g.icon.wifi; title: "Wi-Fi"
    detail: root.sys.wifi ? (root.sys.ssid || "Connected") : "Off"
    checked: !!root.sys.wifi
    onToggled: function(v) { root.act("wifi", v) }
  }
  ToggleRow {
    id: bluetooth
    g: root.g; width: parent.width
    icon: root.g.icon.bluetooth; title: "Bluetooth"
    detail: root.sys.bluetooth ? (root.sys.bluetoothDetail || "On") : "Off"
    checked: !!root.sys.bluetooth
    onToggled: function(v) { root.act("bluetooth", v) }
  }
  ToggleRow {
    id: dnd
    g: root.g; width: parent.width
    icon: root.sys.dnd ? root.g.icon.dnd : root.g.icon.bell; title: "Do not disturb"
    detail: root.sys.dnd ? (root.sys.dndDetail || "Notifications wait until you are done") : "Notifications can appear over the game"
    checked: !!root.sys.dnd
    onToggled: function(v) { root.act("dnd", v) }
  }

  Section { g: root.g; text: "Leave the game"; width: parent.width }

  Action {
    id: library
    g: root.g; width: parent.width
    icon: root.g.icon.library; title: "Game library"; detail: "The game keeps running"
    chevron: true
    onTriggered: root.act("library", null)
  }
  Action {
    id: desktop
    g: root.g; width: parent.width
    icon: root.g.icon.desktop; title: "Return to desktop"; detail: "Come back with the Guide button"
    chevron: true
    onTriggered: root.act("desktop", null)
  }
  Action {
    id: suspend
    g: root.g; width: parent.width
    icon: root.g.icon.sleep; title: "Suspend"; detail: "Pauses everything, wakes where you left off"
    onTriggered: root.act("suspend", null)
  }
}

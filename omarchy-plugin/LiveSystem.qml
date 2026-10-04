import QtQuick
import Quickshell.Io

// Generic guide controls work even without Omakade. Failed reads stay unknown.
Item {
  id: root
  signal failure(string title)
  property bool active: false
  property var system: ({})
  property string profile: ""
  readonly property var status: ({wifi: root.system.wifi, bluetooth: root.system.bluetooth, dnd: root.system.dnd})

  function run(command, done) {
    var process = runner.createObject(root, {command: command})
    process.result = done || function() {}
    process.running = true
  }
  Component {
    id: runner
    Process {
      id: process
      property var result: function() {}
      property string output: ""
      stdout: StdioCollector { onStreamFinished: process.output = text }
      onExited: function(code, status) { result(code === 0 && status === 0, output.trim()); destroy() }
    }
  }
  function set(key, value) {
    var copy = Object.assign({}, root.system); copy[key] = value; root.system = copy
  }
  function refresh() {
    run(["nmcli", "radio", "wifi"], function(ok, value) { if (ok) set("wifi", value === "enabled") })
    run(["bluetoothctl", "show"], function(ok, value) { if (ok) set("bluetooth", /Powered: yes/.test(value)) })
    run(["omarchy-shell", "notifications", "dndState"], function(ok, value) { if (ok) set("dnd", value === "on") })
    run(["powerprofilesctl", "get"], function(ok, value) { if (ok) root.profile = value })
    run(["brightnessctl", "-m"], function(ok, value) {
      if (!ok) return
      var match = value.match(/,(\d+)%,/)
      if (match) set("brightness", Number(match[1]) / 100)
    })
  }
  onActiveChanged: if (active) refresh()
  Timer { interval: 30000; running: root.active; repeat: true; onTriggered: root.refresh() }
  function act(name, value) {
    var command
    switch (name) {
    case "wifi": command = ["nmcli", "radio", "wifi", value ? "on" : "off"]; break
    case "bluetooth": command = ["bluetoothctl", "power", value ? "on" : "off"]; break
    case "dnd": command = ["omarchy-shell", "notifications", "setDnd", value ? "true" : "false"]; break
    case "profile":
      if (["power-saver", "balanced", "performance"].indexOf(value) < 0) return false
      command = ["powerprofilesctl", "set", value]; break
    case "brightness": command = ["brightnessctl", "set", Math.round(Math.max(0, Math.min(1, Number(value))) * 100) + "%"]; break
    case "suspend": command = ["systemctl", "suspend"]; break
    default: return false
    }
    run(command, function(ok) { if (!ok) root.failure("The setting could not be changed"); root.refresh() })
    return true
  }
}

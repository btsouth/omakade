import QtQuick
import Quickshell.Io
import Quickshell.Services.Pipewire

// Output volume, on the sink the volume keys and Omarchy's audio panel use:
// omarchy-audio-output-sink resolves the default output through a DSP sink to
// the physical one, so the slider moves what the speakers actually play.
Item {
  id: root

  property bool active: false
  property string sinkName: ""
  readonly property var defaultSink: Pipewire.defaultAudioSink
  readonly property var sink: {
    if (sinkName === "" || !defaultSink || sinkName === String(defaultSink.name)) return defaultSink
    var nodes = Pipewire.nodes ? Pipewire.nodes.values : []
    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i]
      if (n && n.isSink && !n.isStream && n.audio && String(n.name) === sinkName) return n
    }
    return defaultSink
  }
  readonly property bool available: !!(sink && sink.audio)
  readonly property real volume: available ? sink.audio.volume : 0
  readonly property bool muted: available ? sink.audio.muted : false

  // Output devices to switch between, in PipeWire's order, by their own names.
  readonly property var sinks: (Pipewire.nodes ? Pipewire.nodes.values : [])
    .filter(function(n) { return n && n.audio && n.isSink && !n.isStream })
  readonly property var outputs: root.sinks.map(function(n) {
    return {name: String(n.description || n.nickname || n.name), current: n === root.defaultSink}
  })

  PwObjectTracker { objects: root.sinks }

  function cycleOutput(step) {
    var list = root.sinks
    if (list.length < 2) return
    var i = list.indexOf(root.defaultSink)
    Pipewire.preferredDefaultAudioSink = list[((i < 0 ? 0 : i) + step + list.length) % list.length]
  }

  function setVolume(value) {
    if (!available) return
    sink.audio.volume = Math.max(0, Math.min(1, value))
    if (sink.audio.muted && value > 0) sink.audio.muted = false
  }

  function toggleMute() {
    if (available) sink.audio.muted = !sink.audio.muted
  }

  onActiveChanged: if (active && !resolver.running) resolver.running = true
  onDefaultSinkChanged: if (active && !resolver.running) resolver.running = true

  Process {
    id: resolver
    command: ["omarchy-audio-output-sink"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.sinkName = String(text).trim()
    }
  }
}

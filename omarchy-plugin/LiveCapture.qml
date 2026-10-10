import QtQuick
import Quickshell
import Quickshell.Io

// Readings for the perf line and the capture rows, polled only while the card is
// open. Clips use the helper's own recorder, so a screen recording started from
// Omarchy is neither shown here nor stopped by the guide, and the reverse.
Item {
  id: root

  property bool active: false
  property string output: ""
  // {cpu, cpuTemp, gpu, gpuTemp, recording: {pid, started}, replay: {pid, seconds}}
  property var status: ({})
  property bool busy: false
  readonly property string helper: String(Qt.resolvedUrl("guide-files.py")).replace("file://", "")

  readonly property var recording: status.recording || null
  readonly property var replay: status.replay || null

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
      onExited: function(code, status) { result(code === 0 && status === 0, process.output.trim()); destroy() }
    }
  }

  function refresh() {
    if (root.busy) return
    root.busy = true
    run(["python3", "-I", root.helper, "status"], function(ok, text) {
      root.busy = false
      if (!ok) return
      try { root.status = JSON.parse(text) } catch (e) {}
    })
  }

  onActiveChanged: if (active) refresh()
  Timer { interval: 2000; running: root.active; repeat: true; onTriggered: root.refresh() }

  // `done(ok)` runs once the file exists, or once it is known not to.
  function screenshot(done) {
    run(["python3", "-I", root.helper, "screenshot", root.output], function(ok) { if (done) done(ok) })
  }

  function toggleRecording() {
    if (root.recording) {
      run(["python3", "-I", root.helper, "record-stop", String(root.recording.pid)], root.refresh)
      root.status = Object.assign({}, root.status, {recording: null})
    } else {
      Quickshell.execDetached(["python3", "-I", root.helper, "record", root.output])
    }
  }

  function saveReplay() {
    if (root.replay) Quickshell.execDetached(["python3", "-I", root.helper, "replay-save", String(root.replay.pid)])
  }
}

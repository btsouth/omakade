import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  property var runner
  property string output: ""
  property bool active: false
  property var settings: ({replaySeconds: 30, sound: "game"})
  property var files: ({})
  property string recordingFile: ""
  property string pendingSave: ""
  property string recordError: ""
  property string replayError: ""
  signal toast(var data)
  readonly property var captureData: ({replayOn: replay.running, replaySeconds: settings.replaySeconds, sound: settings.sound,
    recording: recording.running, folder: files.videos || "", recent: files.recent || []})
  readonly property var status: ({recording: recording.running, replay: {on: replay.running, seconds: settings.replaySeconds}})
  readonly property string helper: String(Qt.resolvedUrl("guide-files.py")).replace("file://", "")
  function refresh() {
    runner.run(["python3", helper, "scan"], function(ok, value) { if (ok) { try { root.files = JSON.parse(value) } catch(e) {} } })
  }
  Component.onCompleted: refresh()
  onActiveChanged: if (active) refresh()
  Timer { interval: 3000; running: root.active || replay.running || recording.running; repeat: true; onTriggered: root.refresh() }
  function command(replayMode) {
    var args = ["gpu-screen-recorder", "-w", output, "-k", "auto", "-f", "60", "-fallback-cpu-encoding", "yes"]
    if (settings.sound !== "none") args = args.concat(["-a", settings.sound === "game-mic" ? "default_output|default_input" : "default_output", "-ac", "aac"])
    if (replayMode) args = args.concat(["-c", "mp4", "-r", String(settings.replaySeconds), "-o", files.videos])
    else args = args.concat(["-o", recordingFile])
    return args
  }
  Process {
    id: recording
    stderr: StdioCollector { onStreamFinished: root.recordError = text.slice(-700) }
    onExited: function(code, status) {
      var path = root.recordingFile
      if (status !== 0 || code !== 0) root.toast({title: "Recording failed", detail: root.recordError || "The capture backend is unavailable"})
      else runner.run(["test", "-s", path], function(ok) { root.toast({title: ok ? "Recording saved" : "Recording produced no file"}) })
      root.refresh()
    }
  }
  Process {
    id: replay
    stderr: StdioCollector { onStreamFinished: root.replayError = text.slice(-700) }
    stdout: SplitParser {
      onRead: function(line) {
        if (!line.trim()) return
        root.refresh()
        root.toast({title: "Replay saved", detail: line.trim()})
      }
    }
    onExited: function(code, status) { if (code !== 0 || status !== 0) root.toast({title: "Replay unavailable", detail: root.replayError || "The capture backend is unavailable"}) }
  }
  function act(name, value) {
    if (name === "record") {
      if (recording.running) recording.signal(2)
      else if (output && files.videos) {
        recordingFile = files.videos + "/screenrecording-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd_HH-mm-ss-zzz") + ".mp4"
        recordError = ""; recording.command = command(false); recording.running = true
      }
      return true
    }
    if (name === "replay-buffer") {
      if (value && !replay.running && output && files.videos) { replayError = ""; replay.command = command(true); replay.running = true }
      else if (!value && replay.running) replay.signal(2)
      return true
    }
    if (name === "save-replay") {
      if (replay.running) replay.signal(10)
      else root.toast({title: "Replay buffer is off", detail: "Turn it on before saving a replay"})
      return true
    }
    if (name === "open-capture") { if (value && value.path) runner.run(["xdg-open", value.path]); return true }
    if (name === "open-folder") { runner.run(["xdg-open", files.videos]); return true }
    return false
  }
  Component.onDestruction: {
    if (recording.running) recording.signal(2)
    if (replay.running) replay.signal(2)
  }
}

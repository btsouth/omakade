import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var cap: d.capture || {}
  readonly property var recent: (cap.recent || []).slice(0, 3)
  property var rows: [[shot, clip], [record], [buffer], [length], [sound], [thumb0, thumb1, thumb2], [folder]]

  spacing: g.s(10)

  Row {
    width: parent.width
    spacing: root.g.s(10)
    Action {
      id: shot
      g: root.g; width: (parent.width - parent.spacing) / 2
      variant: "tile"; icon: root.g.icon.capture
      title: "Screenshot"; detail: "Without the guide"
      hintFamily: root.family; hintButton: "y"
      onTriggered: root.act("screenshot", null)
    }
    Action {
      id: clip
      g: root.g; width: (parent.width - parent.spacing) / 2
      variant: "tile"; icon: root.g.icon.replay
      title: "Save replay"
      detail: root.cap.replayOn ? "Last " + root.cap.replaySeconds + " seconds" : "Replay buffer is off"
      hintFamily: root.family; hintButton: "x"
      onTriggered: root.act("save-replay", null)
    }
  }

  Column {
    width: parent.width
    spacing: 0
    Action {
      id: record
      g: root.g; width: parent.width
      icon: root.g.icon.record
      title: root.cap.recording ? "Stop recording" : "Record a clip"
      detail: root.cap.recording ? "Recording for " + root.cap.recordingTime : "Game and sound, until you stop"
      trailing: root.cap.recording ? "" : "Off"
      onTriggered: root.act("record", null)
    }
    ToggleRow {
      id: buffer
      g: root.g; width: parent.width
      icon: root.g.icon.timer
      title: "Replay buffer"
      detail: "Always keeps the latest moments"
      checked: !!root.cap.replayOn
      onToggled: function(v) { root.act("replay-buffer", v) }
    }
    ChoiceRow {
      id: length
      g: root.g; width: parent.width
      label: "Replay length"
      options: [{ value: "15", label: "15 s" }, { value: "30", label: "30 s" }, { value: "60", label: "1 min" }, { value: "120", label: "2 min" }]
      value: String(root.cap.replaySeconds || 30)
      onChosen: function(v) { root.act("replay-length", Number(v)) }
    }
    ChoiceRow {
      id: sound
      g: root.g; width: parent.width
      label: "Sound in captures"
      options: [{ value: "game", label: "Game" }, { value: "game-mic", label: "Game + mic" }, { value: "none", label: "None" }]
      value: root.cap.sound || "game"
      onChosen: function(v) { root.act("capture-sound", v) }
    }
  }

  Section { g: root.g; text: "Recent"; width: parent.width }

  Row {
    width: parent.width
    spacing: root.g.s(10)
    Repeater {
      id: thumbs
      model: 3
      delegate: Focusable {
        required property int index
        readonly property var item: root.recent[index] || null
        width: (root.width - root.g.s(20)) / 3
        height: pic.height + root.g.s(26)
        onTriggered: root.act("open-capture", item)
        Picture { id: pic; g: root.g; width: parent.width; height: Math.round(width * 9 / 16); source: parent.item ? parent.item.thumb : "" }
        Rectangle {
          visible: !!(parent.item && parent.item.kind === "Clip")
          anchors.right: pic.right; anchors.bottom: pic.bottom; anchors.margins: root.g.s(6)
          width: lengthText.implicitWidth + root.g.s(10); height: lengthText.implicitHeight + root.g.s(4)
          radius: root.g.innerRadius
          color: root.g.background
          Label { id: lengthText; g: root.g; role: "caption"; color: root.g.foreground; anchors.centerIn: parent; text: parent.parent.item ? parent.parent.item.length || "" : "" }
        }
        Label {
          g: root.g; role: "small"
          anchors.top: pic.bottom; anchors.topMargin: root.g.s(6)
          width: parent.width
          text: parent.item ? parent.item.kind + " · " + parent.item.age : ""
        }
      }
    }
  }
  property Item thumb0: thumbs.count > 0 ? thumbs.itemAt(0) : null
  property Item thumb1: thumbs.count > 1 ? thumbs.itemAt(1) : null
  property Item thumb2: thumbs.count > 2 ? thumbs.itemAt(2) : null

  Action {
    id: folder
    g: root.g; width: parent.width
    icon: root.g.icon.folder
    title: "All captures"
    detail: root.cap.folder || "~/Pictures"
    chevron: true
    onTriggered: root.act("open-folder", null)
  }
}

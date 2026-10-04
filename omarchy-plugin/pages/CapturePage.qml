import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var cap: d.capture || {}
  readonly property var recent: (cap.recent || []).slice(0, 3)
  readonly property var session: (cap.recent || []).filter(function(c) { return c.session })
  property var captureItems: []
  property var rows: [[shot, clip], [record], [buffer], [length], [sound], captureItems, [folder]]

  hero: captureHero
  heroMinimum: g.s(148) + captureCaption.height + g.s(8)
  heroMaximum: heroMinimum + g.s(32)

  Column {
    id: captureHero
    width: parent.width; spacing: root.g.s(8)
    Picture { g: root.g; width: parent.width; height: root.g.s(148) + root.heroExtra; source: root.cap.lastShot || (root.recent[0] || {}).thumb || "" }
    Label { id: captureCaption; g: root.g; role: "caption"; text: "Latest capture · " + ((root.recent[0] || {}).age || "This session"); width: parent.width }
  }

  Row {
    width: parent.width
    spacing: root.g.s(10)
    Action {
      id: shot
      g: root.g; width: (parent.width - parent.spacing) / 2
      variant: "tile"; height: root.g.s(90); icon: root.g.icon.capture
      title: "Screenshot"; detail: "Without the guide"
      hintFamily: root.family; hintButton: "y"
      onTriggered: root.act("screenshot", null)
    }
    Action {
      id: clip
      g: root.g; width: (parent.width - parent.spacing) / 2
      variant: "tile"; height: root.g.s(90); icon: root.g.icon.replay
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
      detail: root.cap.recording ? "Recording until you stop" : "Game and sound, until you stop"
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
      options: [{ value: "30", label: "30 s" }, { value: "60", label: "1 min" }, { value: "120", label: "2 min" }]
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

  Column {
    width: parent.width; spacing: root.g.s(8)
    Section { g: root.g; text: root.session.length ? "Recent · " + root.session.length + " this session" : "Recent"; width: parent.width }

    Row {
      width: parent.width
      spacing: root.g.s(10)
      Repeater {
        id: thumbs
        onItemAdded: (i, item) => root.captureItems = root.g.withItem(root.captureItems, i, item)
        onItemRemoved: (i, item) => root.captureItems = root.g.withItem(root.captureItems, i, null)
        model: root.recent.length
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
            text: parent.item ? (parent.item.kind === "Screenshot" ? "Shot" : parent.item.kind) + " · " + parent.item.age : ""
          }
        }
      }
    }

  }

  bottomBlock: Action {
    id: folder
    g: root.g; width: parent.width
    icon: root.g.icon.folder
    title: "Recording folder"
    detail: root.cap.folder || "~/Pictures"
    chevron: true
    onTriggered: root.act("open-folder", null)
  }
}

import QtQuick
import "../components"

PageRhythm {
  id: root

  // A round play mark over clip thumbnails.
  component PlayBadge: Rectangle {
    property real size: 32
    width: size; height: size; radius: size / 2
    color: Qt.rgba(0, 0, 0, 0.55)
    border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.5)
    Glyph { g: root.g; anchors.centerIn: parent; anchors.horizontalCenterOffset: parent.size * 0.04; name: root.g.icon.play; size: parent.size * 0.45; color: "white" }
  }
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
    Item {
      readonly property var latest: root.recent[0] || null
      readonly property string art: (latest || {}).thumb || root.cap.lastShot || ""
      width: parent.width; height: root.g.s(148) + root.heroExtra
      Picture { id: latestArt; g: root.g; anchors.fill: parent; radius: root.g.radius; source: parent.art; visible: parent.art !== "" }
      Rectangle {
        visible: parent.art === ""
        anchors.fill: parent; radius: root.g.radius; color: "transparent"
        border.width: 1; border.color: root.g.line
        Column {
          anchors.centerIn: parent; spacing: root.g.s(8)
          Glyph { g: root.g; name: root.recent.length && root.recent[0].kind === "Clip" ? root.g.icon.replay : root.g.icon.capture; size: root.g.f(28); color: root.g.dim; anchors.horizontalCenter: parent.horizontalCenter }
          Label { g: root.g; role: "small"; visible: !root.recent.length; text: "Screenshots and clips show up here"; anchors.horizontalCenter: parent.horizontalCenter }
        }
      }
      PlayBadge { visible: !!parent.latest && parent.latest.kind === "Clip" && parent.art === parent.latest.thumb && parent.art !== ""; anchors.centerIn: parent; size: root.g.s(44) }
    }
    Label { id: captureCaption; g: root.g; role: "caption"; text: root.recent.length ? "Latest " + ((root.recent[0].kind === "Clip") ? "clip" : "screenshot") + " · " + root.recent[0].age : "Nothing captured yet"; width: parent.width }
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
    spacing: root.g.s(8)
    Action {
      id: record
      g: root.g; width: parent.width
      icon: root.g.icon.record
      title: root.cap.recording ? "Stop recording" : "Record a clip"
      detail: root.cap.recording ? "Recording until you stop" : "Game and sound, until you stop"
      trailing: root.cap.recording ? "Stop" : "Start"
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
          Glyph {
            visible: !!(parent.item && !parent.item.thumb)
            g: root.g; anchors.centerIn: pic
            name: parent.item && parent.item.kind === "Clip" ? root.g.icon.replay : root.g.icon.capture
            size: root.g.f(20); color: root.g.dim
          }
          PlayBadge { visible: !!(parent.item && parent.item.kind === "Clip" && parent.item.thumb); anchors.centerIn: pic; size: root.g.s(28) }
          Rectangle {
            visible: !!(parent.item && parent.item.kind === "Clip" && (parent.item.duration || parent.item.length))
            anchors.right: pic.right; anchors.bottom: pic.bottom; anchors.margins: root.g.s(6)
            width: lengthText.implicitWidth + root.g.s(10); height: lengthText.implicitHeight + root.g.s(4)
            radius: root.g.innerRadius
            color: root.g.background
            Label { id: lengthText; g: root.g; role: "caption"; color: root.g.foreground; anchors.centerIn: parent; text: parent.parent.item ? parent.parent.item.duration || parent.parent.item.length || "" : "" }
          }
          Label {
            g: root.g; role: "small"
            anchors.top: pic.bottom; anchors.topMargin: root.g.s(6)
            width: parent.width
            text: parent.item ? parent.item.age : ""
            elide: Text.ElideRight
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

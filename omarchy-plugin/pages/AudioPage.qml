import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var audio: d.audio || {}
  readonly property var media: audio.nowPlaying || null
  readonly property var outputs: audio.outputs || []
  property var transportItems: []
  property var outputItems: []
  property var inputItems: []
  property var rows: {
    var r = []
    if (media) r.push(transportItems)
    r.push([volume])
    for (var i = 0; i < outputItems.length; i++) r.push([outputItems[i]])
    inputItems.forEach(i => r.push([i]))
    r.push([mic])
    return r
  }

  hero: mediaHero
  heroMinimum: g.s(84) + progress.implicitHeight + transport.height + g.s(52)
  heroMaximum: heroMinimum + g.s(44)

  // Now playing: whatever MPRIS player is running.
  Rectangle {
    id: mediaHero
    visible: !!root.media
    width: parent.width
    height: mediaColumn.height + root.g.s(28)
    radius: root.g.radius
    color: root.g.well
    border.width: Math.max(1, root.g.s(1)); border.color: root.g.line

    Column {
      id: mediaColumn
      x: root.g.s(14); y: root.g.s(14)
      width: parent.width - root.g.s(28)
      spacing: root.g.s(12)
      Row {
        width: parent.width
        spacing: root.g.s(14)
        Item {
          id: art
          width: root.g.s(84) + root.heroExtra; height: width
          Picture { g: root.g; anchors.fill: parent; source: root.media ? root.media.art || "" : ""; visible: !!(root.media && root.media.art) }
          Rectangle {
            visible: !(root.media && root.media.art)
            anchors.fill: parent; radius: root.g.innerRadius; color: root.g.track
            Glyph { g: root.g; anchors.centerIn: parent; name: root.g.icon.music; size: parent.width * 0.4; color: root.g.dim }
          }
        }
        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - art.width - parent.spacing
          spacing: root.g.s(2)
          Label { g: root.g; role: "caps"; text: root.media ? root.media.player : "" }
          Label { g: root.g; role: "heading"; text: root.media ? root.media.title : ""; width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
          Label { g: root.g; role: "small"; text: root.media ? root.media.artist || "" : ""; width: parent.width; elide: Text.ElideRight; visible: text !== "" }
        }
      }
      Meter {
        id: progress
        g: root.g; width: parent.width
        label: root.media ? root.media.position : ""
        value: root.media ? root.media.length : ""
        progress: root.media ? root.media.progress : 0
        fill: root.g.foreground
      }
      Row {
        id: transport
        width: childrenRect.width
        spacing: root.g.s(16); anchors.horizontalCenter: parent.horizontalCenter
        Repeater {
          id: transportRepeater
          onItemAdded: (i, item) => root.transportItems = root.g.withItem(root.transportItems, i, item)
          onItemRemoved: (i, item) => root.transportItems = root.g.withItem(root.transportItems, i, null)
          model: ["previous", "play", "next"]
          delegate: Focusable {
            required property string modelData
            width: root.g.s(52); height: width
            onTriggered: root.act("media", modelData)
            Rectangle { anchors.fill: parent; radius: root.g.innerRadius; color: root.g.well; border.width: 1; border.color: root.g.line }
            Glyph {
              g: root.g; anchors.centerIn: parent; size: root.g.f(24)
              name: modelData === "previous" ? root.g.icon.previous : modelData === "next" ? root.g.icon.next
                : (root.media && root.media.playing ? root.g.icon.pause : root.g.icon.play)
            }
          }
        }
      }
    }
  }

  SliderRow {
    id: volume
    g: root.g; width: parent.width
    icon: root.audio.muted ? root.g.icon.micOff : root.g.icon.audio
    title: "Volume"
    value: root.audio.volume || 0
    onMoved: function(v) { root.act("volume", v) }
  }

  Column {
    visible: root.outputs.length > 0
    width: parent.width; spacing: root.g.s(8)
    Section { g: root.g; text: "Output"; width: parent.width }
    Column {
      width: parent.width
      spacing: root.g.s(6)
      Repeater {
        id: outputRepeater
        onItemAdded: (i, item) => root.outputItems = root.g.withItem(root.outputItems, i, item)
        onItemRemoved: (i, item) => root.outputItems = root.g.withItem(root.outputItems, i, null)
        model: root.outputs
        delegate: Action {
          required property var modelData
          g: root.g; width: root.width
          icon: modelData.kind === "tv" ? root.g.icon.tv : modelData.kind === "headset" || /headset/i.test(modelData.name) ? root.g.icon.headset : root.g.icon.speaker
          height: root.g.s(58)
          selected: !!modelData.current
          title: modelData.name
          detail: modelData.detail || ""
          trailing: modelData.current ? "\u{f012c}" : ""
          onTriggered: root.act("output", modelData.id === undefined ? modelData.name : modelData.id)
        }
      }
    }
  }

  Column {
    width: parent.width
    spacing: root.g.s(6)
    visible: (root.audio.inputs || []).length > 0
    Section { g: root.g; text: "Input"; width: parent.width }
    Repeater {
      model: root.audio.inputs || []
      onItemAdded: (i, item) => root.inputItems = root.g.withItem(root.inputItems, i, item)
      onItemRemoved: (i, item) => root.inputItems = root.g.withItem(root.inputItems, i, null)
      delegate: Action {
        required property var modelData
        g: root.g; width: root.width; height: root.g.s(58); title: modelData.name; selected: !!modelData.current
        icon: /headset/i.test(modelData.name) ? root.g.icon.headset : root.g.icon.mic
        trailing: modelData.current ? "\u{f012c}" : ""
        onTriggered: root.act("input-device", modelData.id)
      }
    }
  }
  bottomBlock: ToggleRow {
    id: mic
    g: root.g; width: parent.width
    // micMuted is undefined when there is no microphone at all.
    readonly property bool present: root.audio.micMuted !== undefined
    icon: present && !root.audio.micMuted ? root.g.icon.mic : root.g.icon.micOff
    title: "Microphone"
    detail: !present ? "No microphone" : (root.audio.micMuted ? "Muted · " : "On · ") + (root.audio.micName || "")
    checked: present && !root.audio.micMuted
    onToggled: function(v) { if (present) root.act("mic", v) }
  }
}

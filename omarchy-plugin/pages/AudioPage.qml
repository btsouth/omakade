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
  property var rows: {
    var r = []
    if (media) r.push(transportItems)
    r.push([volume])
    for (var i = 0; i < outputItems.length; i++) r.push([outputItems[i]])
    r.push([mic])
    return r
  }

  hero: mediaHero
  heroMinimum: g.s(128) + progress.implicitHeight + transport.height + g.s(52)
  heroMaximum: heroMinimum + g.s(72)

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
        Picture { id: art; g: root.g; width: root.g.s(128) + root.heroExtra; height: width; source: root.media ? root.media.art || "" : "" }
        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - art.width - parent.spacing
          spacing: root.g.s(2)
          Label { g: root.g; role: "caps"; text: root.media ? root.media.player : "" }
          Label { g: root.g; role: "heading"; text: root.media ? root.media.title : ""; width: parent.width }
          Label { g: root.g; role: "small"; text: root.media ? root.media.artist : ""; width: parent.width }
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
          onTriggered: root.act("output", modelData.name)
        }
      }
    }
  }

  bottomBlock: ToggleRow {
    id: mic
    g: root.g; width: parent.width
    icon: root.audio.micMuted ? root.g.icon.micOff : root.g.icon.mic
    title: "Microphone"
    detail: (root.audio.micMuted ? "Muted · " : "On · ") + (root.audio.micName || "")
    checked: !root.audio.micMuted
    onToggled: function(v) { root.act("mic", v) }
  }
}

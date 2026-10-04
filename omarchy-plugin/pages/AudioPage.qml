import QtQuick
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var audio: d.audio || {}
  readonly property var media: audio.nowPlaying || null
  readonly property var outputs: audio.outputs || []
  property var rows: {
    var r = []
    if (media) r.push([prev, playPause, next])
    r.push([volume])
    for (var i = 0; i < outputRepeater.count; i++) r.push([outputRepeater.itemAt(i)])
    r.push([mic])
    return r
  }

  spacing: g.s(10)

  // Now playing: whatever MPRIS player is running.
  Rectangle {
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
        Picture { id: art; g: root.g; width: root.g.s(64); height: width; source: root.media ? root.media.art || "" : "" }
        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - art.width - parent.spacing
          spacing: root.g.s(2)
          Label { g: root.g; role: "caps"; text: root.media ? root.media.player : "" }
          Label { g: root.g; role: "title"; text: root.media ? root.media.title : ""; width: parent.width }
          Label { g: root.g; role: "small"; text: root.media ? root.media.artist : ""; width: parent.width }
        }
      }
      Item {
        width: parent.width; height: root.g.s(38)
        Row {
          id: transport
          spacing: root.g.s(6)
          anchors.verticalCenter: parent.verticalCenter
          Repeater {
            id: transportRepeater
            model: ["previous", "play", "next"]
            delegate: Focusable {
              required property string modelData
              width: root.g.s(38); height: width
              onTriggered: root.act("media", modelData)
              Rectangle {
                anchors.fill: parent; radius: width / 2
                color: modelData === "play" ? root.g.foreground : "transparent"
                border.width: modelData === "play" ? 0 : Math.max(1, root.g.s(1)); border.color: root.g.line
              }
              Glyph {
                g: root.g; anchors.centerIn: parent; size: root.g.f(18)
                color: modelData === "play" ? root.g.background : root.g.foreground
                name: modelData === "previous" ? root.g.icon.previous : modelData === "next" ? root.g.icon.next
                  : (root.media && root.media.playing ? root.g.icon.pause : root.g.icon.play)
              }
            }
          }
        }
        Meter {
          id: progress
          g: root.g
          anchors.left: transport.right; anchors.leftMargin: root.g.s(16)
          anchors.right: parent.right
          anchors.verticalCenter: parent.verticalCenter
          label: root.media ? root.media.position : ""
          value: root.media ? root.media.length : ""
          progress: root.media ? root.media.progress : 0
          fill: root.g.foreground
        }
      }
    }
  }
  property Item prev: transportRepeater.count > 0 ? transportRepeater.itemAt(0) : null
  property Item playPause: transportRepeater.count > 1 ? transportRepeater.itemAt(1) : null
  property Item next: transportRepeater.count > 2 ? transportRepeater.itemAt(2) : null

  SliderRow {
    id: volume
    g: root.g; width: parent.width
    icon: root.audio.muted ? root.g.icon.micOff : root.g.icon.audio
    title: "Volume"
    value: root.audio.volume || 0
    onMoved: function(v) { root.act("volume", v) }
  }

  Section { g: root.g; text: "Output"; width: parent.width }
  Column {
    width: parent.width
    Repeater {
      id: outputRepeater
      model: root.outputs
      delegate: Action {
        required property var modelData
        g: root.g; width: root.width
        icon: modelData.kind === "tv" ? root.g.icon.tv : root.g.icon.speaker
        title: modelData.name
        detail: modelData.detail || ""
        trailing: modelData.current ? "\u{f012c}" : ""
        onTriggered: root.act("output", modelData.name)
      }
    }
  }

  ToggleRow {
    id: mic
    g: root.g; width: parent.width
    icon: root.audio.micMuted ? root.g.icon.micOff : root.g.icon.mic
    title: "Microphone"
    detail: (root.audio.micMuted ? "Muted · " : "On · ") + (root.audio.micName || "")
    checked: !root.audio.micMuted
    onToggled: function(v) { root.act("mic", v) }
  }
}

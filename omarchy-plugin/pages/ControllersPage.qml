import QtQuick
import "../components"

PageRhythm {
  id: root
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var pads: d.controllers || []
  property var padItems: []
  property var rows: {
    var r = []
    for (var i = 0; i < padItems.length; i++) if (root.pads[i] && root.pads[i].identifiable) r.push([padItems[i]])
    r.push([pair])
    r.push([prompts])
    return r
  }

  hero: padSection
  heroMinimum: root.pads.length ? g.s(164) + Math.max(0, root.pads.length - 1) * (g.s(114) + g.s(16)) : 0
  heroMaximum: heroMinimum + g.s(72)

  Column {
    id: padSection
    width: parent.width; spacing: root.g.s(16)
    Repeater {
      id: padRepeater
      onItemAdded: (i, item) => root.padItems = root.g.withItem(root.padItems, i, item)
      onItemRemoved: (i, item) => root.padItems = root.g.withItem(root.padItems, i, null)
      model: root.pads
      delegate: Focusable {
        id: pad
        required property var modelData
        required property int index
        readonly property bool low: modelData.battery !== undefined && modelData.battery <= 20
        width: root.width
        height: root.g.s(index === 0 ? 164 : 114) + (index === 0 ? root.heroExtra : 0)
        onTriggered: if (modelData.identifiable) root.act("identify", modelData.name)

        Rectangle {
          anchors.fill: parent; radius: root.g.radius
          color: pad.low ? root.g.background : root.g.well
          border.width: Math.max(1, root.g.s(1)); border.color: root.g.line
        }
        Glyph {
          id: padIcon
          g: root.g
          x: root.g.s(16); anchors.verticalCenter: parent.verticalCenter
          width: root.g.s(pad.index === 0 ? 86 : 46)
          size: root.g.f(pad.index === 0 ? 76 : 34)
          name: pad.modelData.family === "playstation" ? root.g.icon.playstation
            : pad.modelData.family === "nintendo" ? root.g.icon.nintendo : root.g.icon.controllers
        }
        Column {
          anchors.left: padIcon.right; anchors.leftMargin: root.g.s(14)
          anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
          anchors.verticalCenter: parent.verticalCenter
          spacing: root.g.s(4)
          Label { g: root.g; role: "caps"; width: parent.width; text: "Player " + (pad.index + 1) }
          Label { g: root.g; role: "title"; text: pad.modelData.name; width: parent.width }
          Label { g: root.g; role: "small"; text: pad.modelData.connection || ""; width: parent.width }
          Row {
            width: parent.width; spacing: root.g.s(8)
            Glyph {
              g: root.g; name: root.g.icon.battery; size: root.g.f(18)
              anchors.verticalCenter: parent.verticalCenter
              color: pad.low ? root.g.urgent : root.g.foreground
            }
            Meter {
              g: root.g; width: parent.width - root.g.s(26)
              label: "Battery"
              value: pad.modelData.battery === undefined ? "Wired" : pad.modelData.battery + "%"
              progress: pad.modelData.battery === undefined ? 1 : pad.modelData.battery / 100
              fill: pad.low ? root.g.urgent : root.g.foreground
              valueColor: pad.low ? root.g.urgent : root.g.foreground
            }
          }
        }
      }
    }
  }

  Label {
    visible: root.pads.some(p => p.identifiable)
    g: root.g; role: "small"
    leftPadding: root.g.s(10)
    width: parent.width; wrapMode: Text.WordWrap
    text: "Press A on a controller to make it rumble."
  }

  bottomBlock: Column {
    width: parent.width; spacing: root.g.s(16)
    Action {
      id: pair
      g: root.g; width: parent.width
      icon: root.g.icon.bluetooth
      title: "Pair a controller"
      detail: "Hold the pairing button"
      chevron: true
      onTriggered: root.act("pair", null)
    }

    ChoiceRow {
      id: prompts
      g: root.g; width: parent.width
      label: "Button prompts"
      options: [{ value: "auto", label: "Auto" }, { value: "xbox", label: "Xbox" }, { value: "playstation", label: "PlayStation" }, { value: "nintendo", label: "Nintendo" }]
      value: root.d.prompts || "auto"
      onChosen: function(v) { root.act("prompts", v) }
    }
  }
}

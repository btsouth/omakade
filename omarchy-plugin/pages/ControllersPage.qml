import QtQuick
import qs.Commons
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property real availableHeight: 0
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var pads: d.controllers || []
  property var padItems: []
  property var rows: {
    var r = []
    for (var i = 0; i < padItems.length; i++) r.push([padItems[i]])
    r.push([pair])
    r.push([prompts])
    return r
  }

  spacing: g.rhythm(root, 22)

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
      width: root.width; height: root.g.s(index === 0 ? 252 : 100)
      onTriggered: root.act("identify", modelData.name)

      Rectangle {
        anchors.fill: parent; radius: root.g.radius
        color: root.g.well
        border.width: Math.max(1, root.g.s(1)); border.color: root.g.line
      }
      Glyph {
        id: padIcon
        g: root.g
        x: pad.index === 0 ? (parent.width - width) / 2 : root.g.s(16)
        y: pad.index === 0 ? root.g.s(24) : (parent.height - height) / 2
        size: root.g.f(pad.index === 0 ? 84 : 34)
        name: pad.modelData.family === "playstation" ? root.g.icon.playstation
          : pad.modelData.family === "nintendo" ? root.g.icon.nintendo : root.g.icon.controllers
      }
      Column {
        anchors.left: pad.index === 0 ? parent.left : padIcon.right; anchors.leftMargin: root.g.s(16)
        anchors.right: pad.index === 0 ? parent.right : battery.left; anchors.rightMargin: root.g.s(16)
        y: pad.index === 0 ? root.g.s(134) : (parent.height - height) / 2
        spacing: root.g.s(2)
        Label { g: root.g; role: "caps"; width: parent.width; horizontalAlignment: pad.index === 0 ? Text.AlignHCenter : Text.AlignLeft; text: "Player " + (pad.index + 1) }
        Label { g: root.g; role: pad.index === 0 ? "heading" : "title"; text: pad.modelData.name; width: parent.width; horizontalAlignment: pad.index === 0 ? Text.AlignHCenter : Text.AlignLeft }
        Label { g: root.g; role: "small"; text: pad.modelData.connection || ""; width: parent.width; horizontalAlignment: pad.index === 0 ? Text.AlignHCenter : Text.AlignLeft }
      }
      Column {
        id: battery
        anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
        y: pad.index === 0 ? parent.height - height - root.g.s(20) : (parent.height - height) / 2
        width: pad.index === 0 ? parent.width - root.g.s(32) : root.g.s(84)
        spacing: root.g.s(6)
        Label {
          g: root.g; role: "body"
          anchors.right: parent.right
          text: pad.modelData.battery === undefined ? "Wired" : pad.modelData.battery + "%"
          color: pad.low ? root.g.urgent : root.g.foreground
        }
        Rectangle {
          visible: pad.modelData.battery !== undefined
          width: parent.width; height: Math.max(3, root.g.s(4)); radius: height / 2
          color: root.g.track
          Rectangle {
            width: parent.width * (pad.modelData.battery || 0) / 100; height: parent.height; radius: parent.radius
            color: pad.low ? root.g.urgent : root.g.foreground
          }
        }
      }
    }
  }

  Label {
    visible: root.pads.length > 0
    g: root.g; role: "small"
    leftPadding: root.g.s(10)
    width: parent.width; wrapMode: Text.WordWrap
    text: "Press A on a controller to make it rumble."
  }

  Action {
    id: pair
    g: root.g; width: parent.width
    icon: root.g.icon.bluetooth
    title: "Pair a controller"
    detail: "Hold its pairing button, then choose it here"
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

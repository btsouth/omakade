import QtQuick
import qs.Commons
import "../components"

Column {
  id: root
  required property var g
  required property var d
  property string family: "xbox"
  signal act(string name, var arg)

  readonly property var pads: d.controllers || []
  property var rows: {
    var r = []
    for (var i = 0; i < padRepeater.count; i++) r.push([padRepeater.itemAt(i)])
    r.push([pair])
    r.push([prompts])
    return r
  }

  spacing: g.s(10)

  Repeater {
    id: padRepeater
    model: root.pads
    delegate: Focusable {
      id: pad
      required property var modelData
      required property int index
      readonly property bool low: modelData.battery !== undefined && modelData.battery <= 20
      width: root.width; height: root.g.s(84)
      onTriggered: root.act("identify", modelData.name)

      Rectangle {
        anchors.fill: parent; radius: root.g.radius
        color: root.g.well
        border.width: Math.max(1, root.g.s(1)); border.color: root.g.line
      }
      Glyph {
        id: padIcon
        g: root.g
        x: root.g.s(16); anchors.verticalCenter: parent.verticalCenter
        size: root.g.f(34)
        name: pad.modelData.family === "playstation" ? root.g.icon.playstation
          : pad.modelData.family === "nintendo" ? root.g.icon.nintendo : root.g.icon.controllers
      }
      Column {
        anchors.left: padIcon.right; anchors.leftMargin: root.g.s(16)
        anchors.right: battery.left; anchors.rightMargin: root.g.s(16)
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.g.s(2)
        Label { g: root.g; role: "caps"; text: "Player " + (pad.index + 1) }
        Label { g: root.g; role: "title"; text: pad.modelData.name; width: parent.width }
        Label { g: root.g; role: "small"; text: pad.modelData.connection || ""; width: parent.width }
      }
      Column {
        id: battery
        anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
        anchors.verticalCenter: parent.verticalCenter
        width: root.g.s(84)
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

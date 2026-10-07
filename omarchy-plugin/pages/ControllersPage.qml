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
    for (var i = 0; i < padItems.length; i++) if (root.pads[i] && root.pads[i].identifiable !== false) r.push([padItems[i]])
    r.push([pair])
    r.push([prompts])
    return r
  }

  Column {
    id: padSection
    width: parent.width; spacing: root.g.s(10)
    Repeater {
      id: padRepeater
      onItemAdded: (i, item) => root.padItems = root.g.withItem(root.padItems, i, item)
      onItemRemoved: (i, item) => root.padItems = root.g.withItem(root.padItems, i, null)
      model: root.pads
      delegate: Focusable {
        id: pad
        required property var modelData
        required property int index
        readonly property bool hasBattery: modelData.battery !== undefined && modelData.battery !== null
        readonly property bool low: hasBattery && modelData.battery <= 20
        readonly property string link: modelData.connection
          || (/bluetooth|:0005:/i.test(modelData.id || "") ? "Bluetooth" : /\/usb\d/.test(modelData.id || "") ? "Wired" : "")
        width: root.width
        height: root.g.s(hasBattery ? 104 : 88)
        onTriggered: if (modelData.identifiable !== false) root.act("identify", modelData.node || modelData.name)

        Rectangle {
          anchors.fill: parent; radius: root.g.radius
          color: root.g.well
          border.width: Math.max(1, root.g.s(1)); border.color: pad.low ? root.g.urgent : root.g.line
        }
        Rectangle {
          id: padIcon
          x: root.g.s(14); anchors.verticalCenter: parent.verticalCenter
          width: root.g.s(56); height: width; radius: width / 2
          color: root.g.track
          Glyph {
            g: root.g; anchors.centerIn: parent
            size: root.g.f(30)
            name: pad.modelData.family === "playstation" ? root.g.icon.playstation
              : pad.modelData.family === "nintendo" ? root.g.icon.nintendo : root.g.icon.controllers
          }
        }
        Column {
          anchors.left: padIcon.right; anchors.leftMargin: root.g.s(14)
          anchors.right: identify.visible ? identify.left : parent.right; anchors.rightMargin: root.g.s(14)
          anchors.verticalCenter: parent.verticalCenter
          spacing: root.g.s(4)
          Label { g: root.g; role: "caps"; width: parent.width; text: ["Player " + (pad.index + 1), pad.link].filter(function(x) { return x !== "" }).join("  ·  ") }
          Label { g: root.g; role: "body"; font.bold: true; text: pad.modelData.name; width: parent.width; elide: Text.ElideRight }
          Item {
            visible: pad.hasBattery
            width: parent.width; height: root.g.s(18)
            Glyph { id: batteryIcon; g: root.g; name: root.g.icon.battery; size: root.g.f(15); color: pad.low ? root.g.urgent : root.g.dim; anchors.verticalCenter: parent.verticalCenter }
            Rectangle {
              anchors.left: batteryIcon.right; anchors.leftMargin: root.g.s(8)
              anchors.right: batteryText.left; anchors.rightMargin: root.g.s(10)
              anchors.verticalCenter: parent.verticalCenter
              height: Math.max(3, root.g.s(4)); radius: height / 2; color: root.g.track
              Rectangle { height: parent.height; radius: parent.radius; color: pad.low ? root.g.urgent : root.g.foreground; width: Math.max(height, parent.width * (pad.hasBattery ? pad.modelData.battery / 100 : 0)) }
            }
            Label { id: batteryText; g: root.g; role: "small"; text: pad.hasBattery ? pad.modelData.battery + "%" : ""; color: pad.low ? root.g.urgent : root.g.foreground; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter }
          }
        }
        Row {
          id: identify
          visible: pad.modelData.identifiable !== false
          anchors.right: parent.right; anchors.rightMargin: root.g.s(16)
          anchors.verticalCenter: parent.verticalCenter
          spacing: root.g.s(8)
          PadGlyph { g: root.g; family: root.family; button: "a"; size: root.g.f(18); anchors.verticalCenter: parent.verticalCenter }
          Label { g: root.g; role: "small"; text: "Rumble"; anchors.verticalCenter: parent.verticalCenter }
        }
      }
    }
    Rectangle {
      visible: root.pads.length === 0
      width: parent.width; height: root.g.s(120)
      radius: root.g.radius; color: "transparent"
      border.width: 1; border.color: root.g.line
      Column {
        anchors.centerIn: parent; spacing: root.g.s(8)
        Glyph { g: root.g; name: root.g.icon.controllers; size: root.g.f(30); color: root.g.dim; anchors.horizontalCenter: parent.horizontalCenter }
        Label { g: root.g; role: "small"; text: "No controllers connected"; anchors.horizontalCenter: parent.horizontalCenter }
      }
    }
  }

  Column {
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

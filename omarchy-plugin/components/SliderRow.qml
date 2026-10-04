import QtQuick
import qs.Ui

// A level with the shell's slider. Left and right move it in steps.
Item {
  id: root
  required property var g
  property string icon: ""
  property string title: ""
  property real value: 0
  property real stepSize: 0.05
  property string valueText: Math.round(value * 100) + "%"
  property bool cursor: false
  signal moved(real value)

  function step(d) { moved(Math.max(0, Math.min(1, Math.round((value + d * stepSize) * 100) / 100))); return true }
  function activate() {}

  implicitHeight: g.s(62)

  Row {
    id: top
    anchors.left: parent.left; anchors.leftMargin: root.g.s(10)
    anchors.right: parent.right; anchors.rightMargin: root.g.s(10)
    y: root.g.s(8)
    spacing: root.g.s(12)
    Glyph { g: root.g; name: root.icon; size: root.g.f(17); width: root.g.s(22); anchors.verticalCenter: parent.verticalCenter }
    Label { g: root.g; role: "body"; text: root.title; anchors.verticalCenter: parent.verticalCenter }
  }
  Label { g: root.g; role: "body"; text: root.valueText; anchors.right: parent.right; anchors.rightMargin: root.g.s(10); anchors.verticalCenter: top.verticalCenter }
  PanelSlider {
    anchors.left: parent.left; anchors.leftMargin: root.g.s(44)
    anchors.right: parent.right; anchors.rightMargin: root.g.s(10)
    anchors.top: top.bottom; anchors.topMargin: root.g.s(2)
    value: root.value
    trackColor: root.g.track
    fillColor: root.g.foreground
    knobColor: root.g.foreground
    onMoved: function(v) { root.moved(v) }
  }
}

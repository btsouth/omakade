import QtQuick
import qs.Ui

// A short list of choices in the shell's own chip group. Left and right change
// the value straight away, as on a handheld's quick menu.
Item {
  id: root
  required property var g
  property string label: ""
  property string detail: ""
  property var options: []
  property string value: ""
  property bool cursor: false
  signal chosen(string value)

  function indexOf(v) {
    for (var i = 0; i < options.length; i++) if (String(options[i].value) === v) return i
    return -1
  }
  function step(d) {
    var i = Math.max(0, Math.min(options.length - 1, indexOf(value) + d))
    if (String(options[i].value) !== value) chosen(String(options[i].value))
    return true
  }
  function activate() { chosen(String(options[(indexOf(value) + 1) % options.length].value)) }

  implicitHeight: head.height + g.s(8) + group.height + g.s(16)

  Label { id: head; g: root.g; role: "caps"; text: root.label; anchors.left: parent.left; anchors.leftMargin: root.g.s(10); y: root.g.s(8) }
  Label { g: root.g; role: "small"; text: root.detail; anchors.right: parent.right; anchors.rightMargin: root.g.s(10); anchors.verticalCenter: head.verticalCenter }
  ButtonGroup {
    id: group
    anchors.left: parent.left; anchors.leftMargin: root.g.s(10)
    anchors.top: head.bottom; anchors.topMargin: root.g.s(8)
    options: root.options
    value: root.value
    focusable: false
    fontSize: root.g.f(12)
    cursorIndex: -1
    onChanged: function(v) { root.chosen(v) }
  }
}

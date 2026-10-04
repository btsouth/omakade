import QtQuick
import qs.Commons

// A setting that is on or off, drawn with the guide surface tokens.
Item {
  id: root
  required property var g
  property string icon: ""
  property string title: ""
  property string detail: ""
  property bool checked: false
  property bool cursor: false
  signal toggled(bool value)

  function activate() { toggled(!checked) }
  function step(d) { if ((d > 0) !== checked) toggled(d > 0); return true }

  implicitHeight: g.s(48)

  Row {
    anchors.left: parent.left; anchors.leftMargin: root.g.s(10)
    anchors.right: toggle.left; anchors.rightMargin: root.g.s(10)
    anchors.verticalCenter: parent.verticalCenter
    spacing: root.g.s(12)
    Glyph { g: root.g; name: root.icon; size: root.g.f(17); width: root.g.s(22); anchors.verticalCenter: parent.verticalCenter }
    Column {
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width - x
      spacing: root.g.s(2)
      Label { g: root.g; role: "body"; text: root.title; width: parent.width }
      Label { g: root.g; role: "small"; text: root.detail; visible: text !== ""; width: parent.width }
    }
  }
  GuideSwitch {
    id: toggle
    g: root.g
    anchors.right: parent.right; anchors.rightMargin: root.g.s(6)
    anchors.verticalCenter: parent.verticalCenter
    checked: root.checked
  }
  MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.activate() }
}

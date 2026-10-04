import QtQuick
import QtQuick.Effects
import qs.Commons
import qs.Ui

// "Screenshot saved" and friends: a small card in the bottom-right corner with
// the shell's popup chrome and an optional thumbnail.
BorderSurface {
  id: root
  required property var g
  property string icon: ""
  property string title: ""
  property string detail: ""
  property string image: ""

  width: g.s(360)
  height: g.s(72)
  radius: g.radius
  color: Color.popups.background
  borderSpec: Border.surfaceSpec("popups", "border", Color.popups.border, Math.max(1, Style.space(2)))

  Row {
    anchors.fill: parent
    anchors.margins: root.g.s(10)
    spacing: root.g.s(12)
    Item {
      width: root.image !== "" ? root.g.s(92) : root.g.s(34)
      height: parent.height
      Image {
        id: thumb
        visible: false
        anchors.fill: parent
        source: root.image
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
      }
      Rectangle { id: thumbMask; anchors.fill: parent; radius: root.g.innerRadius; visible: false; layer.enabled: true }
      MultiEffect {
        visible: root.image !== ""
        anchors.fill: parent
        source: thumb
        maskEnabled: true
        maskSource: thumbMask
      }
      Glyph { visible: root.image === ""; g: root.g; anchors.centerIn: parent; name: root.icon; size: root.g.f(22); color: root.g.accent }
    }
    Column {
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width - x
      spacing: root.g.s(2)
      Label { g: root.g; role: "title"; text: root.title; width: parent.width }
      Label { g: root.g; role: "small"; text: root.detail; width: parent.width }
    }
  }
}

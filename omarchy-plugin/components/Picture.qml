import QtQuick
import QtQuick.Effects

// An image cropped to the theme's corner radius.
Item {
  id: root
  required property var g
  property string source: ""
  property real radius: g.innerRadius

  Image {
    id: image
    anchors.fill: parent
    source: root.source
    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    sourceSize.width: width * 2
    visible: false
  }
  Rectangle { id: mask; anchors.fill: parent; radius: root.radius; visible: false; layer.enabled: true }
  MultiEffect {
    anchors.fill: parent
    source: image
    maskEnabled: true
    maskSource: mask
    maskThresholdMin: 0.5
    maskSpreadAtMin: 1.0
  }
  Rectangle {
    anchors.fill: parent
    radius: root.radius
    color: "transparent"
    border.width: Math.max(1, root.g.s(1))
    border.color: root.g.line
  }
}

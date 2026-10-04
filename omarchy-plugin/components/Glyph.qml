import QtQuick

// One Nerd Font icon.
Text {
  required property var g
  property string name: ""
  property real size: g.f(18)
  text: name
  color: g.foreground
  font.family: g.font
  font.pixelSize: size
  verticalAlignment: Text.AlignVCenter
  horizontalAlignment: Text.AlignHCenter
  textFormat: Text.PlainText
}

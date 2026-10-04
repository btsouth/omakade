import QtQuick

// A button glyph and what it does.
Row {
  id: root
  required property var g
  property string family: "xbox"
  property string button: "a"
  property string label: ""
  spacing: g.s(6)

  PadGlyph { g: root.g; family: root.family; button: root.button; size: root.g.f(19); anchors.verticalCenter: parent.verticalCenter }
  Label { g: root.g; role: "small"; text: root.label; color: root.g.foreground; anchors.verticalCenter: parent.verticalCenter }
}

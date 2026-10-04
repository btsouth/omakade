import QtQuick
import qs.Commons

// Something the guide can do. "primary" is the one big action on a page, "tile"
// sits in a grid with its icon on top, "row" is a quiet list entry.
Rectangle {
  id: root
  required property var g
  property string variant: "row"
  property string icon: ""
  property string title: ""
  property string detail: ""
  property string trailing: ""
  property bool chevron: false
  property bool danger: false
  property string hintFamily: ""
  property string hintButton: ""
  property bool cursor: false
  signal triggered()

  function activate() { triggered() }
  function step(d) { return false }

  readonly property color iconColor: danger ? g.urgent : (variant === "primary" ? g.accent : g.foreground)

  implicitHeight: variant === "primary" ? g.s(64) : variant === "tile" ? g.s(104) : g.s(44)
  radius: g.radius
  color: variant === "row" ? "transparent" : g.well
  border.width: variant === "row" ? 0 : Math.max(1, g.s(1))
  border.color: g.line

  // Primary: a play disc, title and detail, then the button that triggers it.
  Row {
    visible: root.variant !== "tile"
    anchors.left: parent.left
    anchors.leftMargin: root.variant === "row" ? root.g.s(10) : root.g.s(14)
    anchors.right: trailingItem.left
    anchors.rightMargin: root.g.s(10)
    anchors.verticalCenter: parent.verticalCenter
    spacing: root.variant === "primary" ? root.g.s(14) : root.g.s(12)

    Rectangle {
      visible: root.variant === "primary"
      width: root.g.s(38); height: width; radius: width / 2
      color: root.g.accent
      anchors.verticalCenter: parent.verticalCenter
      Glyph { g: root.g; anchors.centerIn: parent; anchors.horizontalCenterOffset: root.g.s(1); name: root.icon; size: root.g.f(20); color: root.g.background }
    }
    Glyph {
      visible: root.variant === "row"
      g: root.g; name: root.icon; size: root.g.f(17); color: root.iconColor
      width: root.g.s(22)
      anchors.verticalCenter: parent.verticalCenter
    }
    Column {
      anchors.verticalCenter: parent.verticalCenter
      spacing: root.g.s(2)
      width: parent.width - x
      Label { g: root.g; role: root.variant === "primary" ? "heading" : "body"; text: root.title; width: parent.width; color: root.danger ? root.g.urgent : root.g.foreground }
      Label { g: root.g; role: "small"; text: root.detail; visible: text !== ""; width: parent.width }
    }
  }

  // Tile: icon top-left, hint top-right, text at the bottom.
  Item {
    visible: root.variant === "tile"
    anchors.fill: parent
    anchors.margins: root.g.s(14)
    Glyph { g: root.g; name: root.icon; size: root.g.f(22); color: root.iconColor; anchors.left: parent.left; anchors.top: parent.top }
    Column {
      anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
      spacing: root.g.s(2)
      Label { g: root.g; role: "title"; text: root.title; width: parent.width }
      Label { g: root.g; role: "small"; text: root.detail; visible: text !== ""; width: parent.width }
    }
  }

  Row {
    id: trailingItem
    anchors.right: parent.right
    anchors.rightMargin: root.variant === "row" ? root.g.s(10) : root.g.s(14)
    anchors.top: root.variant === "tile" ? parent.top : undefined
    anchors.topMargin: root.g.s(14)
    anchors.verticalCenter: root.variant === "tile" ? undefined : parent.verticalCenter
    spacing: root.g.s(8)
    Label { g: root.g; role: "small"; text: root.trailing; visible: text !== ""; anchors.verticalCenter: parent.verticalCenter }
    PadGlyph { visible: root.hintButton !== ""; g: root.g; family: root.hintFamily; button: root.hintButton || "a"; size: root.g.f(18); anchors.verticalCenter: parent.verticalCenter }
    Glyph { visible: root.chevron; g: root.g; name: root.g.icon.chevron; size: root.g.f(16); color: root.g.dim; anchors.verticalCenter: parent.verticalCenter }
  }

  MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.triggered() }
}

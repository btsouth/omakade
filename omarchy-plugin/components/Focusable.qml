import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A cursor target drawn the Omarchy menu's way: the menu's selected fill, its
// selected border (or the card's focus hairline where a theme's fill is too
// faint), the selected text colour for everything inside. At rest it is clear,
// or carries a control's normal border when `bordered`, as ButtonGroup chips do.
BorderSurface {
  id: root

  property var g
  property string key: ""
  property bool current: g && g.cursor === key && g.view === "main"
  property bool urgent: false
  property bool bordered: false
  readonly property color ink: root.current ? (root.urgent ? g.urgentInk : g.selectedInk) : g.text

  signal activated()

  radius: g.sized(Style.cornerRadius)
  // As Omarchy's ConfirmDialog draws its choices: the menu's selected fill and
  // a border in the selected colour (urgent for a destructive choice); at rest
  // a bordered chip keeps a faint border, a row none.
  readonly property int edgeWidth: Math.max(1, Style.normalBorderWidth)
  color: root.current ? Commons.Color.menu.selectedBackground : "transparent"
  borderSpec: root.current ? Border.flat(root.ink, root.edgeWidth)
    : root.bordered ? Border.flat(Util.alpha(g.text, 0.38), root.edgeWidth) : Border.none()

  MouseArea {
    id: pointer
    anchors.fill: parent
    z: -1
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onEntered: if (root.key) root.g.hover(root.key, pointer, {x: pointer.mouseX, y: pointer.mouseY})
    onPositionChanged: function(mouse) { if (root.key) root.g.hover(root.key, pointer, mouse) }
    onClicked: { if (root.key) { root.g.cursor = root.key; root.g.activate(root.key) } root.activated() }
  }
}

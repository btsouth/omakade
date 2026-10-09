import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A cursor target drawn as Omarchy's ConfirmDialog draws its choices: the
// menu's selected fill and a border in the selected colour (the urgent colour
// for a destructive choice). At rest a bordered chip keeps a faint border, as
// ButtonGroup options do; a row has none.
BorderSurface {
  id: root

  property var g
  // The guide's name for this control: the cursor key, and its name in the
  // card's geometry. `name` alone registers without taking the cursor.
  property string key: ""
  property string name: key
  property bool current: g.cursor === key && g.view === "main"
  property bool urgent: false
  property bool bordered: false
  readonly property color ink: root.current ? (root.urgent ? g.urgentInk : g.selectedInk) : g.text
  readonly property int edgeWidth: Math.max(1, Style.normalBorderWidth)
  // Set by each kind of control, for layout checks.
  property Item iconItem: null
  property bool truncated: false

  signal activated()
  signal hovered(Item source, var mouse)

  radius: g.sized(Style.cornerRadius)
  color: root.current ? Commons.Color.menu.selectedBackground : "transparent"
  borderSpec: root.current ? Border.flat(root.ink, root.edgeWidth)
    : root.bordered ? Border.flat(Util.alpha(g.text, 0.38), root.edgeWidth) : Border.none()

  Component.onCompleted: if (root.name) root.g.register(root.name, root)
  Component.onDestruction: if (root.name) root.g.unregister(root.name, root)

  onHovered: (source, mouse) => { if (root.key) root.g.hover(root.key, source, mouse) }

  MouseArea {
    id: pointer
    anchors.fill: parent
    z: -1
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onEntered: root.hovered(pointer, {x: pointer.mouseX, y: pointer.mouseY})
    onPositionChanged: function(mouse) { root.hovered(pointer, mouse) }
    onClicked: {
      if (root.key) { root.g.cursor = root.key; root.g.activate(root.key) }
      root.activated()
    }
  }
}

import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A cursor target. At rest a button keeps a faint fill and edge in the text
// colour; under the cursor it takes a tint and a firm ring in the theme's
// selected colour, or in the urgent colour for a destructive choice. `tone`
// says how it rests:
//   "plain"   faint fill and edge (buttons and tiles)
//   "outline" no fill, an edge in the urgent colour (Quit)
//   "tint"    a wash and an edge in the urgent colour (a clip running)
//   "row"     nothing (the sound rows); its ring reaches `outsetX` past it
Item {
  id: root

  property var g
  // The guide's name for this control: the cursor key, and its name in the
  // card's geometry. `name` alone registers without taking the cursor.
  property string key: ""
  property string name: key
  property bool current: g.cursor === key && g.view === "main"
  property bool urgent: false
  property string tone: "plain"
  property int outsetX: 0
  property int outsetY: 0
  readonly property color ink: root.urgent ? g.urgentInk : g.text
  readonly property color ring: root.urgent ? g.urgentInk : g.accentInk
  // Set by each kind of control, for layout checks.
  property Item iconItem: null
  property bool truncated: false

  signal activated()
  signal hovered(Item source, var mouse)

  BorderSurface {
    anchors.fill: parent
    anchors.leftMargin: -root.outsetX
    anchors.rightMargin: -root.outsetX
    anchors.topMargin: -root.outsetY
    anchors.bottomMargin: -root.outsetY
    radius: root.g.sized(Style.cornerRadius)
    color: root.current ? Util.alpha(root.ring, 0.12)
      : root.tone === "plain" ? root.g.fill
      : root.tone === "tint" ? Util.alpha(root.g.urgentInk, 0.08) : "transparent"
    borderSpec: root.current ? Border.flat(Util.alpha(root.ring, 0.85), Math.max(2, root.g.sized(Style.space(2))))
      : root.tone === "plain" ? Border.flat(root.g.edge, Math.max(1, root.g.sized(1)))
      : root.tone === "outline" ? Border.flat(Util.alpha(root.g.urgentInk, 0.35), Math.max(1, root.g.sized(1)))
      : root.tone === "tint" ? Border.flat(Util.alpha(root.g.urgentInk, 0.5), Math.max(1, root.g.sized(1)))
      : Border.none()
  }

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

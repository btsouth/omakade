import QtQuick
import qs.Commons
import qs.Ui

// The menu's section divider: a hairline in a band of its own height.
Item {
  id: root

  property color color: Color.menu.text

  // Couch scale: one multiplier on the card's Omarchy tokens.
  property real zoom: 1
  function sized(v) { return v > 0 ? Math.max(1, Math.round(v * root.zoom)) : 0 }

  width: parent ? parent.width : 0
  height: root.sized(Style.space(17))

  PanelSeparator {
    x: root.sized(Style.space(4))
    width: root.width - root.sized(Style.space(8))
    anchors.verticalCenter: parent.verticalCenter
    foreground: root.color
  }
}

import QtQuick
import qs.Commons
import qs.Ui

// PanelSeparator across the whole card, out to its edges: `bleed` is the
// card's padding on each side.
Item {
  id: sep
  property var g
  property int bleed: 0
  x: -sep.bleed
  width: (parent ? parent.width : 0) + sep.bleed * 2
  height: 1
  PanelSeparator {
    width: parent.width
    foreground: sep.g.text
    strength: 0.1
  }
}

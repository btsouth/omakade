import QtQuick
import qs.Commons
import qs.Ui

// PanelSeparator with the card's gap above and below it.
Item {
  id: sep
  property var g
  property int gap: g.sized(Style.space(14))
  width: parent ? parent.width : 0
  height: sep.gap * 2 + 1
  PanelSeparator {
    width: parent.width
    y: sep.gap
    foreground: sep.g.text
  }
}

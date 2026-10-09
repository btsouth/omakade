import QtQuick
import qs.Commons
import qs.Ui

// The menu's section divider: a hairline in a band of its own height.
Item {
  id: root

  property color color: Color.menu.text

  width: parent ? parent.width : 0
  height: Style.space(17)

  PanelSeparator {
    x: Style.space(4)
    width: root.width - Style.space(8)
    anchors.verticalCenter: parent.verticalCenter
    foreground: root.color
  }
}

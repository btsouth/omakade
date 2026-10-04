import QtQuick

// Sections keep their own row spacing. Spare height first widens the section
// gaps, then grows the hero, then stays above the bottom-anchored block.
Item {
  id: root
  required property var g
  property real availableHeight: 0
  property Item hero: null
  property real heroMinimum: 0
  property real heroMaximum: heroMinimum
  default property alias sections: stack.data
  property alias bottomBlock: bottom.data

  readonly property var metrics: {
    var count = 0, total = 0
    for (var i = 0; i < stack.children.length; i++) {
      var item = stack.children[i]
      if (!item.visible) continue
      var natural = item === hero ? heroMinimum : item.height
      if (natural <= 0) continue
      count++
      total += natural
    }
    if (bottom.height > 0) { count++; total += bottom.height }
    return { height: total, gaps: Math.max(0, count - 1) }
  }
  readonly property real sectionGap: Math.max(g.s(16), Math.min(g.s(28),
    (availableHeight - metrics.height) / Math.max(1, metrics.gaps)))
  readonly property real heroExtra: hero && hero.visible ? Math.min(Math.max(0, heroMaximum - heroMinimum),
    Math.max(0, availableHeight - metrics.height - metrics.gaps * sectionGap)) : 0
  implicitHeight: Math.max(availableHeight, metrics.height + heroExtra + metrics.gaps * sectionGap)
  height: implicitHeight

  Column { id: stack; width: parent.width; spacing: root.sectionGap }
  Column { id: bottom; width: parent.width; anchors.bottom: parent.bottom }
}

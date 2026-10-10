import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A section's name in spaced capitals: Omarchy's PanelSectionHeader at the
// card's scale, in the card's quiet colour.
PanelSectionHeader {
  property var g
  fontFamily: g.fontFamily
  fontSize: g.sized(Style.font.caption)
  color: g.quiet
  font.letterSpacing: Math.max(1, Math.round(g.sized(Style.font.caption) * 0.14))
  font.bold: false
  topPadding: 0
}

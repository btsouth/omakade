import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A section's name in capitals: Omarchy's PanelSectionHeader at the card's
// scale, in the card's quiet colour.
PanelSectionHeader {
  property var g
  fontFamily: g.fontFamily
  fontSize: g.sized(Style.font.bodySmall)
  color: g.quiet
  font.letterSpacing: 1
  topPadding: 0
}

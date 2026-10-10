import QtQuick
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// A button with its icon and name side by side: Resume, Desktop, the RetroArch
// menu, Quit and the quit question's two choices. Left-aligned unless
// `centered`; a pad button's glyph (`hint`) may sit at its right end.
Focusable {
  id: button

  property string icon: ""
  property real iconSize: g.sized(Style.font.iconLarge)
  property string label: ""
  property int labelSize: g.sized(Style.font.title)
  property bool strong: false
  property bool centered: false
  property string hint: ""
  readonly property int padX: g.sized(Style.space(18))

  iconItem: glyph
  truncated: labelText.truncated

  Row {
    id: content
    x: button.centered ? Math.round((button.width - width) / 2) : button.padX
    height: button.height
    spacing: button.g.sized(Style.space(12))

    InkGlyph {
      id: glyph
      visible: button.icon !== ""
      width: Math.round(button.iconSize * 1.2)
      height: button.height
      text: button.icon
      color: button.ink
      size: button.iconSize
      fontFamily: button.g.fontFamily
    }

    Text {
      id: labelText
      textFormat: Text.PlainText
      anchors.verticalCenter: parent.verticalCenter
      width: Math.min(implicitWidth, button.width - button.padX * 2 - (glyph.visible ? glyph.width + content.spacing : 0)
                      - (hintGlyph.visible ? hintGlyph.width + content.spacing : 0))
      text: button.label
      color: button.ink
      font.family: button.g.fontFamily
      font.pixelSize: button.labelSize
      font.weight: button.strong ? Font.DemiBold : Font.Normal
      elide: Text.ElideRight
    }
  }

  ButtonGlyph {
    id: hintGlyph
    g: button.g
    visible: button.hint !== ""
    text: button.hint
    anchors.right: parent.right
    anchors.rightMargin: button.padX
    anchors.verticalCenter: parent.verticalCenter
  }
}

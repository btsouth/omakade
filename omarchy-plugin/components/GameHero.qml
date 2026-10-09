import QtQuick
import QtQuick.Effects
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// The card's hero, built as Omarchy's PanelHero: a picture on the left, the
// title over a line of quiet capitals, and the clock pinned to the trailing
// edge. The picture is the game's cover, small and in the theme's radius, or
// a controller glyph when there is no cover or `glyphOnly` is set.
Item {
  id: hero
  property var g
  property bool glyphOnly: false
  property int coverWidth: g.sized(Style.space(42))
  readonly property string cover: (!glyphOnly && g.game && g.game.cover) ? String(g.game.cover) : ""
  readonly property bool hasCover: cover !== ""
  readonly property bool truncated: titleText.truncated

  implicitHeight: Math.max(picture.height, labels.implicitHeight)

  Item {
    id: picture
    width: hero.hasCover ? hero.coverWidth : hero.g.sized(Style.space(34))
    height: hero.hasCover ? Math.round(hero.coverWidth * 4 / 3) : hero.g.sized(Style.space(34))
    anchors.verticalCenter: parent.verticalCenter

    Image {
      id: art
      anchors.fill: parent
      visible: false
      source: hero.cover
      fillMode: Image.PreserveAspectCrop
      sourceSize.width: parent.width * 2
      smooth: true
      mipmap: true
    }

    // The theme's radius, at most a small one: a cover is a picture, not a chip.
    Rectangle {
      id: mask
      anchors.fill: parent
      visible: false
      layer.enabled: true
      radius: Math.min(hero.g.sized(Style.cornerRadius), hero.g.sized(Style.space(6)))
    }

    MultiEffect {
      anchors.fill: parent
      visible: art.status === Image.Ready
      source: art
      maskEnabled: mask.radius > 0
      maskSource: mask
      maskThresholdMin: 0.5
      maskSpreadAtMin: 1.0
    }

    InkGlyph {
      visible: art.status !== Image.Ready
      anchors.fill: parent
      text: hero.g.icons.gamepad
      color: hero.g.text
      size: hero.g.sized(Style.font.display)
      fontFamily: hero.g.fontFamily
    }
  }

  Column {
    id: labels
    anchors.left: picture.right
    anchors.leftMargin: hero.g.sized(Style.space(14))
    anchors.right: clock.left
    anchors.rightMargin: hero.g.sized(Style.space(12))
    anchors.verticalCenter: parent.verticalCenter
    spacing: hero.g.sized(Style.space(3))

    Text {
      id: titleText
      width: parent.width
      textFormat: Text.PlainText
      text: hero.g.game ? hero.g.game.title : "No game running"
      color: hero.g.text
      font.family: hero.g.fontFamily
      font.pixelSize: hero.g.sized(Style.font.heading)
      font.bold: true
      wrapMode: Text.Wrap
      maximumLineCount: 3
      elide: Text.ElideRight
    }

    Caps {
      g: hero.g
      width: parent.width
      visible: text !== ""
      text: hero.g.sessionText().toUpperCase()
      wrapMode: Text.Wrap
    }
  }

  Text {
    id: clock
    textFormat: Text.PlainText
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    text: hero.g.clock
    color: hero.g.quiet
    font.family: hero.g.fontFamily
    font.pixelSize: hero.g.sized(Style.font.body)
  }
}
